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

make_wallpaper_png() {
  local path="$1"
  local r="$2"
  local g="$3"
  local b="$4"
  python3 - "$path" "$r" "$g" "$b" <<'PY'
import struct, sys, zlib

path = sys.argv[1]
r, g, b = [int(x) for x in sys.argv[2:5]]
w, h = 64, 64
a = 255

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
}

run_case() (
  set -euo pipefail

  local force="$1"
  local expect_rgb="$2"
  local label="$3"

  local SOCKET="wayland-fbwl-test-$UID-$$-$force"
  local LOG="/tmp/fluxbox-wayland-pseudo-transparency-$UID-$$-$force.log"
  local SC_LOG="/tmp/fbwl-pseudo-transparency-screencopy-$UID-$$-$force.log"
  local WALLPAPER_PNG="/tmp/fbwl-pseudo-wallpaper-$UID-$$-$force.png"
  local CFGDIR
  CFGDIR="$(mktemp -d "/tmp/fbwl-pseudo-transparency-$UID-XXXXXX")"

  local FBW_PID=""
  local BOTTOM_PID=""
  local TOP_PID=""

  cleanup_case() {
    rm -rf "$CFGDIR" 2>/dev/null || true
    rm -f "$WALLPAPER_PNG" "$SC_LOG" 2>/dev/null || true
    if [[ -n "$BOTTOM_PID" ]]; then kill "$BOTTOM_PID" 2>/dev/null || true; fi
    if [[ -n "$TOP_PID" ]]; then kill "$TOP_PID" 2>/dev/null || true; fi
    if [[ -n "$FBW_PID" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
    wait 2>/dev/null || true
  }
  trap cleanup_case EXIT

  : >"$LOG"
  : >"$SC_LOG"

  fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

  cat >"$CFGDIR/init" <<EOF
session.screen0.focusModel: ClickToFocus
session.screen0.allowRemoteActions: true
session.screen0.toolbar.visible: false
session.forcePseudoTransparency: $([[ "$force" == "1" ]] && echo true || echo false)
session.keyFile: keys
EOF

  cat >"$CFGDIR/keys" <<'EOF'
Mod1 1 :MoveTo 0 0
Mod1 F :SetAlpha 0
EOF

  WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
    --no-xwayland \
    --socket "$SOCKET" \
    --config-dir "$CFGDIR" \
    >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

  make_wallpaper_png "$WALLPAPER_PNG" 255 0 0
  ./fbwl-remote --socket "$SOCKET" wallpaper "$WALLPAPER_PNG" | rg -q '^ok$'
  timeout 5 bash -c "until rg -q 'Background: wallpaper set' '$LOG'; do sleep 0.05; done"

  local W=320
  local H=240
  local bottom_title="pseudo-bottom"
  local top_title="pseudo-top"

  ./fbwl-smoke-client --socket "$SOCKET" --title "$bottom_title" --app-id "$bottom_title" \
    --stay-ms 20000 --xdg-decoration --width "$W" --height "$H" >/dev/null 2>&1 &
  BOTTOM_PID=$!
  timeout 5 bash -c "until rg -q 'Surface size: $bottom_title ' '$LOG'; do sleep 0.05; done"
  local bottom_place
  bottom_place="$(rg -m1 "Place: $bottom_title " "$LOG" || true)"
  if [[ "$bottom_place" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
    local bottom_x="${BASH_REMATCH[1]}"
    local bottom_y="${BASH_REMATCH[2]}"
  else
    echo "failed to parse bottom Place line: $bottom_place" >&2
    exit 1
  fi

  ./fbwl-smoke-client --socket "$SOCKET" --title "$top_title" --app-id "$top_title" \
    --stay-ms 20000 --xdg-decoration --width "$W" --height "$H" >/dev/null 2>&1 &
  TOP_PID=$!
  timeout 5 bash -c "until rg -q 'Surface size: $top_title ' '$LOG'; do sleep 0.05; done"
  local top_place
  top_place="$(rg -m1 "Place: $top_title " "$LOG" || true)"
  if [[ "$top_place" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
    local top_x0="${BASH_REMATCH[1]}"
    local top_y0="${BASH_REMATCH[2]}"
  else
    echo "failed to parse top Place line: $top_place" >&2
    exit 1
  fi

  # Focus + MoveTo 0 0 for bottom.
  ./fbwl-input-injector --socket "$SOCKET" click "$((bottom_x + 10))" "$((bottom_y + 10))" >/dev/null 2>&1 || true
  local OFFSET
  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-input-injector --socket "$SOCKET" key alt-1 >/dev/null 2>&1
  local START=$((OFFSET + 1))
  timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'MoveTo: $bottom_title '; do sleep 0.05; done"

  # Focus + MoveTo 0 0 for top.
  ./fbwl-input-injector --socket "$SOCKET" click "$((top_x0 + 10))" "$((top_y0 + 10))" >/dev/null 2>&1 || true
  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-input-injector --socket "$SOCKET" key alt-1 >/dev/null 2>&1
  local top_move
  START=$((OFFSET + 1))
  timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'MoveTo: $top_title '; do sleep 0.05; done"
  top_move="$(tail -c +$START "$LOG" | rg -m1 "MoveTo: $top_title " || true)"
  if [[ "$top_move" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
    local top_x="${BASH_REMATCH[1]}"
    local top_y="${BASH_REMATCH[2]}"
  else
    echo "failed to parse top MoveTo line: $top_move" >&2
    exit 1
  fi

  # Apply SetAlpha 0 on the (focused) top window.
  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-input-injector --socket "$SOCKET" key alt-f >/dev/null 2>&1
  START=$((OFFSET + 1))
  timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Alpha: $top_title focused=0 unfocused=0 reason=setalpha'; do sleep 0.05; done"

  fbwl_report_shot "pseudo-transparency-${label}.png" "Pseudo transparency (${label})"

  local sample_x=$((top_x + 20))
  local sample_y=$((top_y + 20))

  if ! ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 \
      --expect-rgb "$expect_rgb" --sample-x "$sample_x" --sample-y "$sample_y" >"$SC_LOG" 2>&1; then
    echo "fbwl-screencopy-client failed (case=$label force=$force):" >&2
    cat "$SC_LOG" >&2 || true
    echo "fluxbox-wayland log tail:" >&2
    tail -n 200 "$LOG" >&2 || true
    exit 1
  fi
  rg -q '^ok screencopy$' "$SC_LOG"

  echo "ok: pseudo transparency case passed (case=$label force=$force expect=$expect_rgb sample=$sample_x,$sample_y socket=$SOCKET)"
)

run_case 0 '#1e1e1e' 'compositing'
run_case 1 '#ff0000' 'pseudo'

echo "ok: pseudo transparency smoke passed"
