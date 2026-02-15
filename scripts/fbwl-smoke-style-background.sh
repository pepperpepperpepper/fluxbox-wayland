#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd python3

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

source scripts/fbwl-smoke-report-lib.sh

REPORT_DIR="${FBWL_REPORT_DIR:-${FBWL_SMOKE_REPORT_DIR:-}}"

parse_bg_wallpaper_output() {
  local log="$1"
  local line
  line="$(rg 'Background: wallpaper output name=' "$log" | tail -n 1)"
  if [[ -z "$line" ]]; then
    echo "failed to find Background: wallpaper output line in log: $log" >&2
    return 1
  fi
  if [[ "$line" =~ x=([-0-9]+)\ y=([-0-9]+)\ w=([0-9]+)\ h=([0-9]+).*mode=([a-z]+) ]]; then
    OUT_X="${BASH_REMATCH[1]}"
    OUT_Y="${BASH_REMATCH[2]}"
    OUT_W="${BASH_REMATCH[3]}"
    OUT_H="${BASH_REMATCH[4]}"
    OUT_MODE="${BASH_REMATCH[5]}"
    return 0
  fi
  echo "failed to parse Background: wallpaper output line: $line" >&2
  return 1
}

run_case_solid() (
  SOCKET="wayland-fbwl-style-bg-solid-$UID-$$"
  LOG="/tmp/fluxbox-wayland-style-bg-solid-$UID-$$.log"
  CFG_DIR="$(mktemp -d "/tmp/fbwl-style-bg-solid-$UID-XXXXXX")"
  STYLE_FILE="/tmp/fbwl-style-bg-solid-$UID-$$.cfg"

  cleanup() {
    rm -f "$STYLE_FILE" 2>/dev/null || true
    if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
    wait 2>/dev/null || true
    rm -rf "$CFG_DIR" 2>/dev/null || true
  }
  trap cleanup EXIT

  cat >"$CFG_DIR/init" <<'EOF'
session.screen0.toolbar.visible: false
session.screen0.allowRemoteActions: true
EOF

  cat >"$STYLE_FILE" <<'EOF'
background: flat
background.color: #ff0000
EOF

  : >"$LOG"

  fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

  WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
    ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFG_DIR" --style "$STYLE_FILE" >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Background: style solid' '$LOG'; do sleep 0.05; done"

  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#ff0000' \
    --sample-x 10 --sample-y 10 >/dev/null 2>&1

  fbwl_report_shot "style-background-solid.png" "Style background: solid (#ff0000)"
)

run_case_gradient() (
  SOCKET="wayland-fbwl-style-bg-grad-$UID-$$"
  LOG="/tmp/fluxbox-wayland-style-bg-grad-$UID-$$.log"
  CFG_DIR="$(mktemp -d "/tmp/fbwl-style-bg-grad-$UID-XXXXXX")"
  STYLE_FILE="/tmp/fbwl-style-bg-grad-$UID-$$.cfg"

  cleanup() {
    rm -f "$STYLE_FILE" 2>/dev/null || true
    if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
    wait 2>/dev/null || true
    rm -rf "$CFG_DIR" 2>/dev/null || true
  }
  trap cleanup EXIT

  cat >"$CFG_DIR/init" <<'EOF'
session.screen0.toolbar.visible: false
session.screen0.allowRemoteActions: true
EOF

  cat >"$STYLE_FILE" <<'EOF'
background: Flat Gradient Vertical
background.color: #ff0000
background.colorTo: #00ff00
EOF

  : >"$LOG"

  fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

  WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
    ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFG_DIR" --style "$STYLE_FILE" >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Background: wallpaper set path=\\(style:gradient\\)' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Background: wallpaper output name=' '$LOG'; do sleep 0.05; done"

  parse_bg_wallpaper_output "$LOG"

  if (( OUT_W < 4 || OUT_H < 4 )); then
    echo "unexpected output size: w=$OUT_W h=$OUT_H" >&2
    exit 1
  fi

  SAMPLE_X=$((OUT_X + OUT_W / 2))
  SAMPLE_Y_TOP=$((OUT_Y))
  SAMPLE_Y_BOT=$((OUT_Y + OUT_H - 1))

  BOT_R=$((255 * 1 / OUT_H))
  BOT_G=$((255 * (OUT_H - 1) / OUT_H))
  printf -v EXPECT_BOT '#%02x%02x%02x' "$BOT_R" "$BOT_G" 0

  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#ff0000' \
    --sample-x "$SAMPLE_X" --sample-y "$SAMPLE_Y_TOP" >/dev/null 2>&1
  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb "$EXPECT_BOT" \
    --sample-x "$SAMPLE_X" --sample-y "$SAMPLE_Y_BOT" >/dev/null 2>&1

  fbwl_report_shot "style-background-gradient.png" "Style background: vertical gradient (top red → bottom green)"
)

run_case_mod() (
  SOCKET="wayland-fbwl-style-bg-mod-$UID-$$"
  LOG="/tmp/fluxbox-wayland-style-bg-mod-$UID-$$.log"
  CFG_DIR="$(mktemp -d "/tmp/fbwl-style-bg-mod-$UID-XXXXXX")"
  STYLE_FILE="/tmp/fbwl-style-bg-mod-$UID-$$.cfg"

  cleanup() {
    rm -f "$STYLE_FILE" 2>/dev/null || true
    if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
    wait 2>/dev/null || true
    rm -rf "$CFG_DIR" 2>/dev/null || true
  }
  trap cleanup EXIT

  cat >"$CFG_DIR/init" <<'EOF'
session.screen0.toolbar.visible: false
session.screen0.allowRemoteActions: true
EOF

  cat >"$STYLE_FILE" <<'EOF'
background: mod
background.color: #ff0000
background.colorTo: #0000ff
background.modX: 4
background.modY: 4
EOF

  : >"$LOG"

  fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

  WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
    ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFG_DIR" --style "$STYLE_FILE" >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Background: wallpaper set path=\\(style:mod\\)' '$LOG'; do sleep 0.05; done"

  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#ff0000' \
    --sample-x 0 --sample-y 0 >/dev/null 2>&1
  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#0000ff' \
    --sample-x 1 --sample-y 1 >/dev/null 2>&1

  fbwl_report_shot "style-background-mod.png" "Style background: mod grid (fg red, bg blue)"
)

run_case_pixmap_tiled() (
  SOCKET="wayland-fbwl-style-bg-pixmap-$UID-$$"
  LOG="/tmp/fluxbox-wayland-style-bg-pixmap-$UID-$$.log"
  CFG_DIR="$(mktemp -d "/tmp/fbwl-style-bg-pixmap-$UID-XXXXXX")"
  STYLE_FILE="/tmp/fbwl-style-bg-pixmap-$UID-$$.cfg"
  PIXMAP_PNG="/tmp/fbwl-style-bg-pixmap-$UID-$$.png"

  cleanup() {
    rm -f "$STYLE_FILE" "$PIXMAP_PNG" 2>/dev/null || true
    if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
    wait 2>/dev/null || true
    rm -rf "$CFG_DIR" 2>/dev/null || true
  }
  trap cleanup EXIT

  cat >"$CFG_DIR/init" <<'EOF'
session.screen0.toolbar.visible: false
session.screen0.allowRemoteActions: true
EOF

  python3 - "$PIXMAP_PNG" <<'PY'
import struct, sys, zlib

path = sys.argv[1]
w, h = 64, 64

def chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag)
    crc = zlib.crc32(data, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

png = bytearray(b"\x89PNG\r\n\x1a\n")
png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))  # RGBA

raw = bytearray()
for _y in range(h):
    row = bytearray([0])  # filter=0
    for x in range(w):
        if x < w // 2:
            row += bytes([255, 0, 0, 255])
        else:
            row += bytes([0, 255, 0, 255])
    raw += row

png += chunk(b"IDAT", zlib.compress(bytes(raw)))
png += chunk(b"IEND", b"")

with open(path, "wb") as f:
    f.write(png)
PY

  cat >"$STYLE_FILE" <<EOF
background: tiled
background.pixmap: $PIXMAP_PNG
EOF

  : >"$LOG"

  fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

  WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
    ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFG_DIR" --style "$STYLE_FILE" >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Background: style pixmap' '$LOG'; do sleep 0.05; done"

  # Ensure tiling repeats (x and x+64 map to the same column in the tile).
  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#ff0000' \
    --sample-x 10 --sample-y 10 >/dev/null 2>&1
  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#ff0000' \
    --sample-x $((10 + 64 * 3)) --sample-y 10 >/dev/null 2>&1

  fbwl_report_shot "style-background-pixmap-tiled.png" "Style background: pixmap tiled repeat"
)

run_case_solid
run_case_gradient
run_case_mod
run_case_pixmap_tiled

echo "ok: style background smoke passed"
