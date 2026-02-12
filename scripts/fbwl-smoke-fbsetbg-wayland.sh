#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout
need_cmd python3

need_exe ./fluxbox-wayland
need_exe ./fbwl-remote
need_exe ./fbwl-screencopy-client

[[ -r ./util/fbsetbg ]] || { echo "missing required script: ./util/fbsetbg" >&2; exit 1; }

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-fbsetbg-$UID-$$.log}"
WALLPAPER_PNG="${WALLPAPER_PNG:-/tmp/fbwl-fbsetbg-$UID-$$.png}"
TILE_PNG="${TILE_PNG:-/tmp/fbwl-fbsetbg-tile-$UID-$$.png}"
SC_LOG="${SC_LOG:-/tmp/fbwl-fbsetbg-screencopy-$UID-$$.log}"

cleanup() {
  rm -f "$WALLPAPER_PNG" "$TILE_PNG" "$SC_LOG" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
: >"$SC_LOG"

python3 - "$WALLPAPER_PNG" <<'PY'
import struct, sys, zlib

path = sys.argv[1]
w, h = 1000, 100
a = 255

def pixel(x: int):
    # Left: red, middle: green, right: blue.
    if x < 400:
        return (255, 0, 0, a)
    if x < 600:
        return (0, 255, 0, a)
    return (0, 0, 255, a)

def chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag)
    crc = zlib.crc32(data, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

png = bytearray(b"\x89PNG\r\n\x1a\n")
png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))  # RGBA
row = bytearray([0])  # filter=0
for x in range(w):
    row += bytes(pixel(x))
raw = bytes(row) * h
png += chunk(b"IDAT", zlib.compress(raw))
png += chunk(b"IEND", b"")

with open(path, "wb") as f:
    f.write(png)
PY

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

WH=$(python3 - "$LOG" <<'PY'
import re, sys

txt = open(sys.argv[1], "r", encoding="utf-8", errors="ignore").read()
m = re.search(r"OutputLayout:.*\bw=(\d+)\b.*\bh=(\d+)\b", txt)
if not m:
    raise SystemExit(2)
print(m.group(1), m.group(2))
PY
)
W="${WH%% *}"
H="${WH#* }"
[[ "$W" =~ ^[0-9]+$ ]] || { echo "failed to parse output width from log" >&2; exit 1; }
[[ "$H" =~ ^[0-9]+$ ]] || { echo "failed to parse output height from log" >&2; exit 1; }
SAMPLE_LX=5
SAMPLE_RX=$((W - 5))
SAMPLE_Y=$((H / 2))
if (( SAMPLE_RX <= SAMPLE_LX )); then
  echo "invalid sample coords: W=$W H=$H" >&2
  exit 1
fi

TILE_W=64
MAX_TILE_W=$(( (W - 4) / 2 ))
if (( MAX_TILE_W < 8 )); then
  echo "output too small for tile test: W=$W" >&2
  exit 1
fi
if (( TILE_W > MAX_TILE_W )); then
  TILE_W=$MAX_TILE_W
fi
TILE_W=$(( TILE_W - (TILE_W % 4) ))
(( TILE_W >= 8 )) || { echo "tile width too small after rounding: TILE_W=$TILE_W" >&2; exit 1; }
TILE_H=32

python3 - "$TILE_PNG" "$TILE_W" "$TILE_H" <<'PY'
import struct, sys, zlib

path = sys.argv[1]
w = int(sys.argv[2])
h = int(sys.argv[3])
a = 255

def pixel(x: int):
    # Left half: red, right half: blue.
    if x < w // 2:
        return (255, 0, 0, a)
    return (0, 0, 255, a)

def chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag)
    crc = zlib.crc32(data, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

png = bytearray(b"\x89PNG\r\n\x1a\n")
png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))  # RGBA
row = bytearray([0])  # filter=0
for x in range(w):
    row += bytes(pixel(x))
raw = bytes(row) * h
png += chunk(b"IDAT", zlib.compress(raw))
png += chunk(b"IEND", b"")

with open(path, "wb") as f:
    f.write(png)
PY

WAYLAND_DISPLAY="$SOCKET" sh ./util/fbsetbg -f "$WALLPAPER_PNG"
timeout 5 bash -c "until rg -q 'Background: wallpaper set .*mode=stretch' '$LOG'; do sleep 0.05; done"

if ! ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#ff0000' --sample-x "$SAMPLE_LX" --sample-y "$SAMPLE_Y" >"$SC_LOG" 2>&1; then
  echo "fbwl-screencopy-client failed (stretch-left):" >&2
  cat "$SC_LOG" >&2 || true
  echo "fluxbox-wayland log tail:" >&2
  tail -n 200 "$LOG" >&2 || true
  exit 1
fi
rg -q '^ok screencopy$' "$SC_LOG"

if ! ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#0000ff' --sample-x "$SAMPLE_RX" --sample-y "$SAMPLE_Y" >"$SC_LOG" 2>&1; then
  echo "fbwl-screencopy-client failed (stretch-right):" >&2
  cat "$SC_LOG" >&2 || true
  echo "fluxbox-wayland log tail:" >&2
  tail -n 200 "$LOG" >&2 || true
  exit 1
fi
rg -q '^ok screencopy$' "$SC_LOG"

WAYLAND_DISPLAY="$SOCKET" sh ./util/fbsetbg -a "$WALLPAPER_PNG"
timeout 5 bash -c "until rg -q 'Background: wallpaper set .*mode=fill' '$LOG'; do sleep 0.05; done"

if ! ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#00ff00' --sample-x "$SAMPLE_LX" --sample-y "$SAMPLE_Y" >"$SC_LOG" 2>&1; then
  echo "fbwl-screencopy-client failed (fill-left):" >&2
  cat "$SC_LOG" >&2 || true
  echo "fluxbox-wayland log tail:" >&2
  tail -n 200 "$LOG" >&2 || true
  exit 1
fi
rg -q '^ok screencopy$' "$SC_LOG"

if ! ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#00ff00' --sample-x "$SAMPLE_RX" --sample-y "$SAMPLE_Y" >"$SC_LOG" 2>&1; then
  echo "fbwl-screencopy-client failed (fill-right):" >&2
  cat "$SC_LOG" >&2 || true
  echo "fluxbox-wayland log tail:" >&2
  tail -n 200 "$LOG" >&2 || true
  exit 1
fi
rg -q '^ok screencopy$' "$SC_LOG"

WAYLAND_DISPLAY="$SOCKET" sh ./util/fbsetbg -t "$TILE_PNG"
timeout 5 bash -c "until rg -q 'Background: wallpaper set .*mode=tile' '$LOG'; do sleep 0.05; done"

SAMPLE_TX1=2
SAMPLE_TX2=$((SAMPLE_TX1 + TILE_W))
SAMPLE_TXB=$((SAMPLE_TX1 + TILE_W / 2 + 2))
if (( SAMPLE_TX2 >= W )); then
  echo "invalid tile sample coords: W=$W TILE_W=$TILE_W x1=$SAMPLE_TX1 x2=$SAMPLE_TX2" >&2
  exit 1
fi

if ! ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#ff0000' --sample-x "$SAMPLE_TX1" --sample-y "$SAMPLE_Y" >"$SC_LOG" 2>&1; then
  echo "fbwl-screencopy-client failed (tile-left):" >&2
  cat "$SC_LOG" >&2 || true
  echo "fluxbox-wayland log tail:" >&2
  tail -n 200 "$LOG" >&2 || true
  exit 1
fi
rg -q '^ok screencopy$' "$SC_LOG"

if ! ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#0000ff' --sample-x "$SAMPLE_TXB" --sample-y "$SAMPLE_Y" >"$SC_LOG" 2>&1; then
  echo "fbwl-screencopy-client failed (tile-band):" >&2
  cat "$SC_LOG" >&2 || true
  echo "fluxbox-wayland log tail:" >&2
  tail -n 200 "$LOG" >&2 || true
  exit 1
fi
rg -q '^ok screencopy$' "$SC_LOG"

if ! ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#ff0000' --sample-x "$SAMPLE_TX2" --sample-y "$SAMPLE_Y" >"$SC_LOG" 2>&1; then
  echo "fbwl-screencopy-client failed (tile-repeat):" >&2
  cat "$SC_LOG" >&2 || true
  echo "fluxbox-wayland log tail:" >&2
  tail -n 200 "$LOG" >&2 || true
  exit 1
fi
rg -q '^ok screencopy$' "$SC_LOG"

echo "ok: fbsetbg-wayland smoke passed (socket=$SOCKET log=$LOG png=$WALLPAPER_PNG tile=$TILE_PNG)"
