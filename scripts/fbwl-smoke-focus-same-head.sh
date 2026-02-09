#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd wc

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-focus-same-head-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-focus-same-head-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${C_PID:-}" ]]; then kill "$C_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.screen0.focusModel: ClickToFocus
session.screen0.focusNewWindows: false
session.screen0.focusSameHead: true
session.screen0.toolbar.visible: false
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

wait_for_place() {
  local title="$1"
  timeout 5 bash -c "until rg -q 'Place: $title ' '$LOG'; do sleep 0.05; done"
  rg "Place: $title " "$LOG" | tail -n 1
}

parse_xy() {
  local line="$1"
  local x y
  x=$(echo "$line" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
  y=$(echo "$line" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
  echo "$x $y"
}

click_focus() {
  local title="$1"
  local x="$2"
  local y="$3"
  local off
  off=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-input-injector --socket "$SOCKET" click "$((x + 10))" "$((y + 10))"
  timeout 5 bash -c "until tail -c +$((off + 1)) '$LOG' | rg -q 'Policy: focus \\(direct\\) title=$title'; do sleep 0.05; done"
  timeout 5 bash -c "until tail -c +$((off + 1)) '$LOG' | rg -q 'Focus: $title'; do sleep 0.05; done"
}

./fbwl-input-injector --socket "$SOCKET" click "$CX1" "$CY1"
./fbwl-smoke-client --socket "$SOCKET" --title fsh-a --stay-ms 10000 >/dev/null 2>&1 &
A_PID=$!

PLACED_A=$(wait_for_place fsh-a)
read -r AX AY < <(parse_xy "$PLACED_A")
if ! (( AX >= X1 && AX < X1 + W1 && AY >= Y1 && AY < Y1 + H1 )); then
  echo "fsh-a placed outside output1: $PLACED_A (box=$X1,$Y1 ${W1}x${H1})" >&2
  exit 1
fi
click_focus fsh-a "$AX" "$AY"

./fbwl-input-injector --socket "$SOCKET" click "$CX2" "$CY2"
./fbwl-smoke-client --socket "$SOCKET" --title fsh-c --stay-ms 10000 >/dev/null 2>&1 &
C_PID=$!

PLACED_C=$(wait_for_place fsh-c)
read -r CX CY < <(parse_xy "$PLACED_C")
if ! (( CX >= X2 && CX < X2 + W2 && CY >= Y2 && CY < Y2 + H2 )); then
  echo "fsh-c placed outside output2: $PLACED_C (box=$X2,$Y2 ${W2}x${H2})" >&2
  exit 1
fi
click_focus fsh-c "$CX" "$CY"

# Make fsh-a the MRU item on output1, so unfocused fallback would pick it.
click_focus fsh-a "$AX" "$AY"

./fbwl-input-injector --socket "$SOCKET" click "$CX2" "$CY2"
./fbwl-smoke-client --socket "$SOCKET" --title fsh-b --stay-ms 2000 >/dev/null 2>&1 &
B_PID=$!

PLACED_B=$(wait_for_place fsh-b)
read -r BX BY < <(parse_xy "$PLACED_B")
if ! (( BX >= X2 && BX < X2 + W2 && BY >= Y2 && BY < Y2 + H2 )); then
  echo "fsh-b placed outside output2: $PLACED_B (box=$X2,$Y2 ${W2}x${H2})" >&2
  exit 1
fi
click_focus fsh-b "$BX" "$BY"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
timeout 5 bash -c "while kill -0 $B_PID 2>/dev/null; do sleep 0.05; done"

timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Policy: focus \\(direct\\) title=fsh-c'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: fsh-c'; do sleep 0.05; done"

echo "ok: focusSameHead smoke passed (socket=$SOCKET log=$LOG)"

