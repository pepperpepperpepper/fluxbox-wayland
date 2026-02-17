#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd python3
need_cmd wc

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

source scripts/fbwl-smoke-report-lib.sh
REPORT_DIR="${FBWL_REPORT_DIR:-${FBWL_SMOKE_REPORT_DIR:-}}"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-style-menu-round-corners-$UID-$$.log}"
SC_LOG="${SC_LOG:-/tmp/fbwl-style-menu-round-corners-screencopy-$UID-$$.log}"
STYLE_FILE="${STYLE_FILE:-/tmp/fbwl-style-menu-round-corners-$UID-$$.cfg}"
MENU_FILE="${MENU_FILE:-/tmp/fbwl-menu-round-corners-$UID-$$.menu}"
WALLPAPER_PNG="${WALLPAPER_PNG:-/tmp/fbwl-menu-round-corners-wallpaper-$UID-$$.png}"
CFGDIR="$(mktemp -d "/tmp/fbwl-style-menu-round-corners-$UID-XXXXXX")"

cleanup() {
  rm -f "$STYLE_FILE" "$MENU_FILE" "$WALLPAPER_PNG" "$SC_LOG" 2>/dev/null || true
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
: >"$SC_LOG"

fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

cat >"$CFGDIR/init" <<'EOF'
session.screen0.toolbar.visible: false
session.screen0.allowRemoteActions: true
session.screen0.menu.alpha: 255
EOF

cat >"$STYLE_FILE" <<'EOF'
menu.frame: Flat Solid
menu.frame.color: #00ff00
menu.frame.colorTo: #00ff00
menu.hilite: Flat Solid
menu.hilite.color: #0000ff
menu.hilite.colorTo: #0000ff
menu.roundCorners: TopLeft TopRight BottomLeft BottomRight
menu.borderWidth: 0
menu.bevelWidth: 0
menu.itemHeight: 24
EOF

# Minimal root menu file: no [begin] label => no menu title bar.
cat >"$MENU_FILE" <<'EOF'
[exec] (One) {true}
[exec] (Two) {true}
EOF

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

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  --style "$STYLE_FILE" \
  --menu "$MENU_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

./fbwl-remote --socket "$SOCKET" wallpaper "$WALLPAPER_PNG" | rg -q '^ok$'
timeout 5 bash -c "until rg -q 'Background: wallpaper set' '$LOG'; do sleep 0.05; done"

# Open the root menu with a background right-click.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line after right-click" >&2
  exit 1
fi

if [[ "$open_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ items=([0-9]+) ]]; then
  MENU_X="${BASH_REMATCH[1]}"
  MENU_Y="${BASH_REMATCH[2]}"
  MENU_ITEMS="${BASH_REMATCH[3]}"
else
  echo "failed to parse menu open line: $open_line" >&2
  exit 1
fi

if [[ "$MENU_ITEMS" != "2" ]]; then
  echo "expected 2 menu items, got $MENU_ITEMS" >&2
  exit 1
fi

fbwl_report_shot "style-menu-round-corners.png" "Menu roundCorners (style key)"

# Menu geometry is deterministic here:
# - menu width is fixed at 200
# - menu has no title bar
# - menu.itemHeight is set to 24
# - 2 items => height=48
MENU_W=200
ITEM_H=24
OUTER_W=$MENU_W
OUTER_H=$((2 * ITEM_H))

# Top-left pixel should be cut out and show the red wallpaper.
./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 \
  --expect-rgb '#ff0000' --sample-x "$MENU_X" --sample-y "$MENU_Y" >"$SC_LOG" 2>&1
rg -q '^ok screencopy$' "$SC_LOG"

# A pixel inside the first row (highlight) should be blue.
./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 \
  --expect-rgb '#0000ff' --sample-x "$((MENU_X + 5))" --sample-y "$((MENU_Y + 12))" >"$SC_LOG" 2>&1
rg -q '^ok screencopy$' "$SC_LOG"

# A pixel inside the second row (non-highlighted) should be green.
./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 \
  --expect-rgb '#00ff00' --sample-x "$((MENU_X + 5))" --sample-y "$((MENU_Y + 36))" >"$SC_LOG" 2>&1
rg -q '^ok screencopy$' "$SC_LOG"

# Bottom-right pixel should be cut out and show the red wallpaper.
./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 \
  --expect-rgb '#ff0000' --sample-x "$((MENU_X + OUTER_W - 1))" --sample-y "$((MENU_Y + OUTER_H - 1))" >"$SC_LOG" 2>&1
rg -q '^ok screencopy$' "$SC_LOG"

echo "ok: style menu.roundCorners smoke passed"

