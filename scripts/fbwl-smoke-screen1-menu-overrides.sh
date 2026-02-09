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
LOG="${LOG:-/tmp/fluxbox-wayland-screen1-menu-overrides-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-screen1-menu-overrides-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

ALPHA0=201
ALPHA1=123
DELAY0=0
DELAY1=400

cat >"$CFGDIR/init" <<EOF
session.screen0.toolbar.visible: false
session.screen0.menu.alpha: $ALPHA0
session.screen0.menuDelay: $DELAY0
session.screen1.menu.alpha: $ALPHA1
session.screen1.menuDelay: $DELAY1
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
timeout 5 bash -c "until rg -q 'ScreenMap: reason=new-output screens=2' '$LOG'; do sleep 0.05; done"

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

if [[ "$S0_W" -lt 1 || "$S0_H" -lt 1 || "$S1_W" -lt 1 || "$S1_H" -lt 1 ]]; then
  echo "invalid screen layout boxes: '$SCREEN0' '$SCREEN1'" >&2
  exit 1
fi

CX1=$((S0_X + S0_W / 2))
CY1=$((S0_Y + S0_H / 2))
CX2=$((S1_X + S1_W / 2))
CY2=$((S1_Y + S1_H / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click-right "$CX1" "$CY1" >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q \"Menu: open at x=$CX1 y=$CY1 items=.* alpha=$ALPHA0 delay_ms=$DELAY0\"; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$((S0_X + 10))" "$((S0_Y + 10))" >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Menu: close reason=click-outside'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click-right "$CX2" "$CY2" >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q \"Menu: open at x=$CX2 y=$CY2 items=.* alpha=$ALPHA1 delay_ms=$DELAY1\"; do sleep 0.05; done"

echo "ok: screen1 menu overrides smoke passed (socket=$SOCKET log=$LOG)"
