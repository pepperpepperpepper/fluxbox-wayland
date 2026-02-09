#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-edge-snap-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-edge-snap-$UID-XXXXXX")"

# Pick non-default theme values to prove we snap using the *frame* (SSD) extents.
BORDER=6
TITLE_H=21

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.styleFile: mystyle
session.screen0.toolbar.visible: false
session.screen0.edgeSnapThreshold: 10
session.screen0.edgeResizeSnapThreshold: 10
EOF

cat >"$CFGDIR/mystyle" <<EOF
window.borderWidth: $BORDER
window.title.height: $TITLE_H
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'OutputLayout: ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded ' '$LOG'; do sleep 0.05; done"

OUTLINE="$(rg -m1 'OutputLayout: ' "$LOG")"
OUT_X="$(echo "$OUTLINE" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)"
OUT_Y="$(echo "$OUTLINE" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)"
OUT_W="$(echo "$OUTLINE" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)"
OUT_H="$(echo "$OUTLINE" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)"

./fbwl-smoke-client --socket "$SOCKET" --title client-snap --xdg-decoration --stay-ms 10000 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Surface size: client-snap [0-9]+x[0-9]+' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: client-snap ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: client-snap ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

size_line="$(rg -m1 'Surface size: client-snap ' "$LOG")"
if [[ "$size_line" =~ ([0-9]+)x([0-9]+) ]]; then
  W0="${BASH_REMATCH[1]}"
  H0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Surface size line: $size_line" >&2
  exit 1
fi

EXT_LEFT=$BORDER
EXT_TOP=$((TITLE_H + BORDER))
EXT_RIGHT=$BORDER
EXT_BOTTOM=$BORDER

# Move: drag to within 5px of the output top-left so it snaps.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
MOVE_START_X=$((X0 + 10))
MOVE_START_Y=$((Y0 + 10))
MOVE_END_X=$((OUT_X + EXT_LEFT + 15))
MOVE_END_Y=$((OUT_Y + EXT_TOP + 15))
./fbwl-input-injector --socket "$SOCKET" drag-alt-left "$MOVE_START_X" "$MOVE_START_Y" "$MOVE_END_X" "$MOVE_END_Y"

X1=$((OUT_X + EXT_LEFT))
Y1=$((OUT_Y + EXT_TOP))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Move: client-snap x=$X1 y=$Y1"

# Resize: drag to within 5px of output bottom-right so it snaps.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
RESIZE_START_X=$((X1 + 10))
RESIZE_START_Y=$((Y1 + 10))
RESIZE_TARGET_W=$((OUT_W - 5 - EXT_LEFT - EXT_RIGHT))
RESIZE_TARGET_H=$((OUT_H - 5 - EXT_TOP - EXT_BOTTOM))
RESIZE_END_X=$((RESIZE_START_X + (RESIZE_TARGET_W - W0)))
RESIZE_END_Y=$((RESIZE_START_Y + (RESIZE_TARGET_H - H0)))
./fbwl-input-injector --socket "$SOCKET" drag-alt-right "$RESIZE_START_X" "$RESIZE_START_Y" "$RESIZE_END_X" "$RESIZE_END_Y"

W1=$((OUT_W - EXT_LEFT - EXT_RIGHT))
H1=$((OUT_H - EXT_TOP - EXT_BOTTOM))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: client-snap w=$W1 h=$H1"
timeout 5 bash -c "until rg -q 'Surface size: client-snap ${W1}x${H1}' '$LOG'; do sleep 0.05; done"

echo "ok: edge snap smoke passed (socket=$SOCKET log=$LOG)"
