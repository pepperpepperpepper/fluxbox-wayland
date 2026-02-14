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
LOG="${LOG:-/tmp/fluxbox-wayland-screen1-toolbar-overrides-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-screen1-toolbar-overrides-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: true
session.screen0.toolbar.onhead: 2
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.widthPercent: 50
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: clock

# screen1 overrides should be used because the toolbar is on head 2 (screen1).
session.screen1.toolbar.placement: TopLeft
session.screen1.toolbar.widthPercent: 25
session.screen1.toolbar.height: 40
session.screen1.toolbar.tools: clock
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
timeout 5 bash -c "until rg -q 'ScreenMap: screen1 ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: built ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

SCREEN1=$(rg 'ScreenMap: screen1 ' "$LOG" | tail -n 1)
S1_X=$(echo "$SCREEN1" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
S1_Y=$(echo "$SCREEN1" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
S1_W=$(echo "$SCREEN1" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)

TB_BUILT=$(rg 'Toolbar: built ' "$LOG" | tail -n 1)
TB_POS=$(rg 'Toolbar: position ' "$LOG" | tail -n 1)

echo "$TB_BUILT" | rg -q 'onhead=2'

TB_W=$(echo "$TB_BUILT" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)
TB_H=$(echo "$TB_BUILT" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)
TB_X=$(echo "$TB_POS" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
TB_Y=$(echo "$TB_POS" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
TB_THICKNESS=$(echo "$TB_POS" | rg -o 'thickness=[0-9]+' | head -n 1 | cut -d= -f2)

CROSS=$(((TB_H - TB_THICKNESS) / 2))
if (( CROSS < 0 )); then CROSS=0; fi
EXPECTED_W=$((((S1_W - 2 * CROSS) * 25 / 100) + 2 * CROSS))
EXPECTED_THICKNESS=40

if [[ "$TB_THICKNESS" -ne "$EXPECTED_THICKNESS" || "$TB_W" -ne "$EXPECTED_W" ]]; then
  echo "unexpected toolbar size: got $TB_BUILT $TB_POS expected w=$EXPECTED_W thickness=$EXPECTED_THICKNESS (screen1=$SCREEN1)" >&2
  exit 1
fi

if [[ "$TB_X" -ne "$S1_X" || "$TB_Y" -ne "$S1_Y" ]]; then
  echo "unexpected toolbar position: got $TB_POS expected x=$S1_X y=$S1_Y (screen1=$SCREEN1)" >&2
  exit 1
fi

echo "ok: screen1 toolbar overrides smoke passed (socket=$SOCKET log=$LOG)"
