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

SOCKET="${SOCKET:-wayland-fbwl-maxdisable-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-maxdisable-$UID-$$.log}"
CFG_DIR="$(mktemp -d)"

cleanup() {
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFG_DIR"
}
trap cleanup EXIT

cat >"$CFG_DIR/init" <<EOF
session.screen0.maxDisableMove: true
session.screen0.maxDisableResize: true
EOF

: >"$LOG"
WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFG_DIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Output: ' '$LOG'; do sleep 0.05; done"

TITLE="mdmr-test"
./fbwl-smoke-client --socket "$SOCKET" --title "$TITLE" --stay-ms 10000 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 10 bash -c "until rg -q 'Place: $TITLE ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 "Place: $TITLE " "$LOG" || true)"
if [[ "$place_line" =~ usable=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+)[[:space:]] ]]; then
  USABLE_W="${BASH_REMATCH[3]}"
  USABLE_H="${BASH_REMATCH[4]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-m
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: $TITLE on w=$USABLE_W h=$USABLE_H"
timeout 10 bash -c "until rg -q 'Surface size: $TITLE ${USABLE_W}x${USABLE_H}' '$LOG'; do sleep 0.05; done"

MOVE_START_X=10
MOVE_START_Y=10
MOVE_END_X=100
MOVE_END_Y=100
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-alt-left "$MOVE_START_X" "$MOVE_START_Y" "$MOVE_END_X" "$MOVE_END_Y"
sleep 0.2
if tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Move: $TITLE"; then
  echo "unexpected move while maximized (session.screen0.maxDisableMove=true)" >&2
  exit 1
fi

RESIZE_START_X=$((USABLE_W - 10))
RESIZE_START_Y=$((USABLE_H - 10))
RESIZE_END_X=$((RESIZE_START_X + 20))
RESIZE_END_Y=$((RESIZE_START_Y + 20))
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-alt-right "$RESIZE_START_X" "$RESIZE_START_Y" "$RESIZE_END_X" "$RESIZE_END_Y"
sleep 0.2
if tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: $TITLE"; then
  echo "unexpected resize while maximized (session.screen0.maxDisableResize=true)" >&2
  exit 1
fi

echo "ok: maxDisableMove/maxDisableResize smoke passed (socket=$SOCKET log=$LOG)"

