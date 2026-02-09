#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout
need_cmd python3

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-wallpaper-$UID-$$.log}"
WALLPAPER_PNG="${WALLPAPER_PNG:-/tmp/fbwl-wallpaper-$UID-$$.png}"
SC_LOG="${SC_LOG:-/tmp/fbwl-wallpaper-screencopy-$UID-$$.log}"

cleanup() {
  rm -f "$WALLPAPER_PNG" "$SC_LOG" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
: >"$SC_LOG"

python3 - "$WALLPAPER_PNG" <<'PY'
import struct, sys, zlib

path = sys.argv[1]
w, h = 64, 64
r, g, b, a = 255, 0, 0, 255

def chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag)
    crc = zlib.crc32(data, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

png = bytearray(b"\x89PNG\r\n\x1a\n")
png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))  # RGBA
row = bytes([0]) + bytes([r, g, b, a]) * w  # filter=0, then RGBA pixels
raw = row * h
png += chunk(b"IDAT", zlib.compress(raw))
png += chunk(b"IEND", b"")

with open(path, "wb") as f:
    f.write(png)
PY

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

./fbwl-remote --socket "$SOCKET" wallpaper "$WALLPAPER_PNG" | rg -q '^ok$'
timeout 5 bash -c "until rg -q 'Background: wallpaper set' '$LOG'; do sleep 0.05; done"

if ! ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#ff0000' --sample-x 100 --sample-y 100 >"$SC_LOG" 2>&1; then
  echo "fbwl-screencopy-client failed:" >&2
  cat "$SC_LOG" >&2 || true
  echo "fluxbox-wayland log tail:" >&2
  tail -n 200 "$LOG" >&2 || true
  exit 1
fi
rg -q '^ok screencopy$' "$SC_LOG"

echo "ok: wallpaper smoke passed (socket=$SOCKET log=$LOG png=$WALLPAPER_PNG)"
