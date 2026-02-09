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
LOG="${LOG:-/tmp/fluxbox-wayland-showwindowposition-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-showwindowposition-$UID-XXXXXX")"

BORDER=4
TITLE_H=20

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
session.screen0.showwindowposition: true
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
timeout 5 bash -c "until rg -q 'Style: loaded ' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title client-pos --xdg-decoration --stay-ms 10000 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Surface size: client-pos [0-9]+x[0-9]+' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: client-pos ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: client-pos ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

size_line="$(rg -m1 'Surface size: client-pos ' "$LOG")"
if [[ "$size_line" =~ ([0-9]+)x([0-9]+) ]]; then
  W0="${BASH_REMATCH[1]}"
  H0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Surface size line: $size_line" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
MOVE_START_X=$((X0 + 10))
MOVE_START_Y=$((Y0 + 10))
MOVE_END_X=$((MOVE_START_X + 100))
MOVE_END_Y=$((MOVE_START_Y + 100))
./fbwl-input-injector --socket "$SOCKET" drag-alt-left "$MOVE_START_X" "$MOVE_START_Y" "$MOVE_END_X" "$MOVE_END_Y"

POS_X=$((X0 + 100 - BORDER))
POS_Y=$((Y0 + 100 - BORDER - TITLE_H))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "OSD: show windowposition x=$POS_X y=$POS_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'OSD: hide reason=grab-end'

X1=$((X0 + 100))
Y1=$((Y0 + 100))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
RESIZE_START_X=$((X1 + 10))
RESIZE_START_Y=$((Y1 + 10))
RESIZE_END_X=$((RESIZE_START_X + 50))
RESIZE_END_Y=$((RESIZE_START_Y + 60))
./fbwl-input-injector --socket "$SOCKET" drag-alt-right "$RESIZE_START_X" "$RESIZE_START_Y" "$RESIZE_END_X" "$RESIZE_END_Y"

W1=$((W0 + 50))
H1=$((H0 + 60))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "OSD: show windowgeometry w=$W1 h=$H1"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'OSD: hide reason=grab-end'

echo "ok: showwindowposition smoke passed (socket=$SOCKET log=$LOG)"
