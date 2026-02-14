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
LOG="${LOG:-/tmp/fluxbox-wayland-toolbar-onhead-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-toolbar-onhead-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.widthPercent: 50
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: workspacename,clock
session.screen0.toolbar.onhead: 2
session.screen0.toolbar.maxOver: false
EOF

BACKENDS="${WLR_BACKENDS:-headless}"
RENDERER="${WLR_RENDERER:-pixman}"
OUTPUTS=2

if [[ "$BACKENDS" == *x11* ]]; then
  : "${DISPLAY:?DISPLAY must be set for x11 backend (run under scripts/fbwl-smoke-xvfb-outputs.sh)}"
fi

if [[ "$BACKENDS" == *x11* ]]; then
  OUTPUTS_ENV="WLR_X11_OUTPUTS=${WLR_X11_OUTPUTS:-$OUTPUTS}"
else
  OUTPUTS_ENV="WLR_HEADLESS_OUTPUTS=${WLR_HEADLESS_OUTPUTS:-$OUTPUTS}"
fi

env WLR_BACKENDS="$BACKENDS" WLR_RENDERER="$RENDERER" "$OUTPUTS_ENV" \
  ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFGDIR" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'ScreenMap: screen0 ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'ScreenMap: screen1 ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: built ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

SCREEN0=$(rg 'ScreenMap: screen0 ' "$LOG" | tail -n 1)
S0_X=$(echo "$SCREEN0" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
S0_Y=$(echo "$SCREEN0" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
S0_W=$(echo "$SCREEN0" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)
S0_H=$(echo "$SCREEN0" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)

SCREEN1=$(rg 'ScreenMap: screen1 ' "$LOG" | tail -n 1)
S1_X=$(echo "$SCREEN1" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
S1_Y=$(echo "$SCREEN1" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
S1_W=$(echo "$SCREEN1" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)
S1_H=$(echo "$SCREEN1" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)

TB_BUILT=$(rg 'Toolbar: built ' "$LOG" | tail -n 1)
TB_POS=$(rg 'Toolbar: position ' "$LOG" | tail -n 1)

echo "$TB_BUILT" | rg -q 'onhead=2'

TB_W=$(echo "$TB_BUILT" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)
TB_X=$(echo "$TB_POS" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
TB_Y=$(echo "$TB_POS" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
TB_H_OUTER=$(echo "$TB_POS" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)

EXPECTED_TB_X=$((S1_X + (S1_W - TB_W) / 2))
if [[ "$TB_X" -ne "$EXPECTED_TB_X" || "$TB_Y" -ne "$S1_Y" ]]; then
  echo "unexpected toolbar position: got $TB_POS expected x=$EXPECTED_TB_X y=$S1_Y (screen1=$SCREEN1 built=$TB_BUILT)" >&2
  exit 1
fi

CX0=$((S0_X + S0_W / 2))
CY0=$((S0_Y + S0_H / 2))
CX1=$((S1_X + S1_W / 2))
CY1=$((S1_Y + S1_H / 2))

./fbwl-input-injector --socket "$SOCKET" click "$CX0" "$CY0"
./fbwl-smoke-client --socket "$SOCKET" --title out-a --stay-ms 10000 >/dev/null 2>&1 &
A_PID=$!

timeout 5 bash -c "until rg -q 'Place: out-a' '$LOG'; do sleep 0.05; done"
PLACED_A=$(rg 'Place: out-a' "$LOG" | tail -n 1)
AX=$(echo "$PLACED_A" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
AY=$(echo "$PLACED_A" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
if ! (( AX >= S0_X && AX < S0_X + S0_W && AY == S0_Y )); then
  echo "out-a placed unexpectedly: $PLACED_A (screen0=$SCREEN0)" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" click "$CX1" "$CY1"
./fbwl-smoke-client --socket "$SOCKET" --title out-b --stay-ms 10000 >/dev/null 2>&1 &
B_PID=$!

timeout 5 bash -c "until rg -q 'Place: out-b' '$LOG'; do sleep 0.05; done"
PLACED_B=$(rg 'Place: out-b' "$LOG" | tail -n 1)
BX=$(echo "$PLACED_B" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
BY=$(echo "$PLACED_B" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
EXPECTED_B_Y=$((S1_Y + TB_H_OUTER))
if ! (( BX >= S1_X && BX < S1_X + S1_W && BY == EXPECTED_B_Y )); then
  echo "out-b placed unexpectedly: $PLACED_B expected y=$EXPECTED_B_Y (screen1=$SCREEN1)" >&2
  exit 1
fi

echo "ok: toolbar.onhead smoke passed (socket=$SOCKET log=$LOG)"
