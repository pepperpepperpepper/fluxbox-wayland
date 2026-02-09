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
LOG="${LOG:-/tmp/fluxbox-wayland-multi-output-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-multi-output-$UID-XXXXXX")"

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
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.widthPercent: 50
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: workspacename,clock
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
timeout 5 bash -c "until [[ \$(rg -c 'OutputLayout:' '$LOG') -ge 2 ]]; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: built ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

SCREEN0=$(rg 'ScreenMap: screen0 ' "$LOG" | tail -n 1)
S0_X=$(echo "$SCREEN0" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
S0_Y=$(echo "$SCREEN0" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
S0_W=$(echo "$SCREEN0" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)

TB_BUILT=$(rg 'Toolbar: built ' "$LOG" | tail -n 1)
TB_W=$(echo "$TB_BUILT" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)

TB_POS=$(rg 'Toolbar: position ' "$LOG" | tail -n 1)
TB_X=$(echo "$TB_POS" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
TB_Y=$(echo "$TB_POS" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)

EXPECTED_TB_X=$((S0_X + (S0_W - TB_W) / 2))
if [[ "$TB_X" -ne "$EXPECTED_TB_X" || "$TB_Y" -ne "$S0_Y" ]]; then
  echo "unexpected toolbar position: got $TB_POS expected x=$EXPECTED_TB_X y=$S0_Y (screen0=$SCREEN0 built=$TB_BUILT)" >&2
  exit 1
fi

LINE1=$(rg 'OutputLayout:' "$LOG" | head -n 1)
LINE2=$(rg 'OutputLayout:' "$LOG" | head -n 2 | tail -n 1)

X1=$(echo "$LINE1" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
Y1=$(echo "$LINE1" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
W1=$(echo "$LINE1" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)
H1=$(echo "$LINE1" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)

X2=$(echo "$LINE2" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
Y2=$(echo "$LINE2" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
W2=$(echo "$LINE2" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)
H2=$(echo "$LINE2" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)

if [[ "$W1" -lt 1 || "$H1" -lt 1 || "$W2" -lt 1 || "$H2" -lt 1 ]]; then
  echo "invalid output layout boxes: '$LINE1' '$LINE2'" >&2
  exit 1
fi

CX1=$((X1 + W1 / 2))
CY1=$((Y1 + H1 / 2))
CX2=$((X2 + W2 / 2))
CY2=$((Y2 + H2 / 2))

./fbwl-input-injector --socket "$SOCKET" click "$CX1" "$CY1"
./fbwl-smoke-client --socket "$SOCKET" --title out-a --stay-ms 10000 >/dev/null 2>&1 &
A_PID=$!

timeout 5 bash -c "until rg -q 'Place: out-a' '$LOG'; do sleep 0.05; done"
PLACED_A=$(rg 'Place: out-a' "$LOG" | tail -n 1)
	AX=$(echo "$PLACED_A" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
	AY=$(echo "$PLACED_A" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
	if ! (( AX >= X1 && AX < X1 + W1 && AY >= Y1 && AY < Y1 + H1 )); then
	  echo "out-a placed outside output1: $PLACED_A (box=$X1,$Y1 ${W1}x${H1})" >&2
	  exit 1
	fi

./fbwl-input-injector --socket "$SOCKET" click "$CX2" "$CY2"
./fbwl-smoke-client --socket "$SOCKET" --title out-b --stay-ms 10000 >/dev/null 2>&1 &
B_PID=$!

timeout 5 bash -c "until rg -q 'Place: out-b' '$LOG'; do sleep 0.05; done"
PLACED_B=$(rg 'Place: out-b' "$LOG" | tail -n 1)
	BX=$(echo "$PLACED_B" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
	BY=$(echo "$PLACED_B" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
	if ! (( BX >= X2 && BX < X2 + W2 && BY >= Y2 && BY < Y2 + H2 )); then
	  echo "out-b placed outside output2: $PLACED_B (box=$X2,$Y2 ${W2}x${H2})" >&2
	  exit 1
	fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-m
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: out-b on w=$W2 h=$H2"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Fullscreen: out-b on w=$W2 h=$H2"

# Workspace semantics: per-head workspaces (Xinerama-like).
# Switching workspaces affects only the head under the pointer.

# Switch head 0 to workspace 2 (hide out-a, keep out-b visible).
./fbwl-input-injector --socket "$SOCKET" click "$CX1" "$CY1"
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=2 reason=switch'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: view=out-a ws=1 visible=0'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: view=out-b ws=1 visible=1'

# Switch head 1 to workspace 2 (hide out-b as well), then back to workspace 1.
./fbwl-input-injector --socket "$SOCKET" click "$CX2" "$CY2"
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=2 reason=switch'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: view=out-b ws=1 visible=0'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=1 reason=switch'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: view=out-b ws=1 visible=1'

# Restore head 0 to workspace 1 (show out-a again).
./fbwl-input-injector --socket "$SOCKET" click "$CX1" "$CY1"
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=1 reason=switch'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: view=out-a ws=1 visible=1'

echo "ok: multi-output smoke passed (socket=$SOCKET log=$LOG)"
