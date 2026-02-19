#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd tail
need_cmd timeout
need_cmd wc

need_exe ./fbwl-input-injector
need_exe ./fbwl-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-toolbar-autoraise-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-toolbar-autoraise-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-toolbar-autoraise-$UID-XXXXXX")"
APPS_FILE="$CFGDIR/apps"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${WIN_PID:-}" ]]; then kill "$WIN_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.autoRaiseDelay: 50

session.screen0.defaultDeco: NONE
session.screen0.focusNewWindows: true

session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.widthPercent: 50
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: workspacename
session.screen0.toolbar.layer: Normal
session.screen0.toolbar.autoHide: false
session.screen0.toolbar.autoRaise: true
session.screen0.toolbar.maxOver: true
EOF

cat >"$APPS_FILE" <<'EOF'
[app] (app_id=tb-autoraise-win)
  [Deco]       {none}
  [Position]   (TopLeft) {0 0}
[end]
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
  ./fluxbox-wayland --no-xwayland -no-slit --socket "$SOCKET" --config-dir "$CFGDIR" --apps "$APPS_FILE" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Init: toolbar .* autoRaise=1' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

pos_line="$(rg -m1 'Toolbar: position ' "$LOG")"
if [[ "$pos_line" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]]h=([0-9]+).*w=([0-9]+) ]]; then
  TB_X="${BASH_REMATCH[1]}"
  TB_Y="${BASH_REMATCH[2]}"
  TB_H="${BASH_REMATCH[3]}"
  TB_W="${BASH_REMATCH[4]}"
else
  echo "failed to parse Toolbar: position line: $pos_line" >&2
  exit 1
fi

WIN_W=$((TB_X + 10))
WIN_H=200
if [[ "$WIN_W" -lt 50 ]]; then
  WIN_W=50
fi

./fbwl-smoke-client --socket "$SOCKET" --app-id tb-autoraise-win --title tb-win --stay-ms 20000 --width "$WIN_W" --height "$WIN_H" >/dev/null 2>&1 &
WIN_PID=$!

timeout 10 bash -c "until rg -q 'Surface size: tb-win ${WIN_W}x${WIN_H}' '$LOG'; do sleep 0.05; done"

HOVER_X=$((TB_X + TB_W - 10))
HOVER_Y=$((TB_Y + 5))
CLICK_X=$((TB_X + 5))
CLICK_Y="$HOVER_Y"
LEAVE_X=$((TB_X + TB_W / 2))
LEAVE_Y=$((TB_Y + TB_H + 50))

# Ensure hover point is within toolbar but outside the window (so toolbar can detect hover even when lowered).
if ! (( HOVER_X >= TB_X && HOVER_X < TB_X + TB_W && HOVER_Y >= TB_Y && HOVER_Y < TB_Y + TB_H )); then
  echo "bad hover point: hover=$HOVER_X,$HOVER_Y toolbar=$TB_X,$TB_Y ${TB_W}x${TB_H}" >&2
  exit 1
fi
if (( HOVER_X >= 0 && HOVER_X < WIN_W && HOVER_Y >= 0 && HOVER_Y < WIN_H )); then
  echo "expected hover point to be outside window but got: hover=$HOVER_X,$HOVER_Y win=0,0 ${WIN_W}x${WIN_H}" >&2
  exit 1
fi

# Ensure click point is inside both the toolbar and the window (so hit-testing proves raise/lower ordering).
if ! (( CLICK_X >= TB_X && CLICK_X < TB_X + TB_W && CLICK_Y >= TB_Y && CLICK_Y < TB_Y + TB_H )); then
  echo "bad click point: click=$CLICK_X,$CLICK_Y toolbar=$TB_X,$TB_Y ${TB_W}x${TB_H}" >&2
  exit 1
fi
if ! (( CLICK_X >= 0 && CLICK_X < WIN_W && CLICK_Y >= 0 && CLICK_Y < WIN_H )); then
  echo "expected click point to be inside window but got: click=$CLICK_X,$CLICK_Y win=0,0 ${WIN_W}x${WIN_H}" >&2
  exit 1
fi

# Enter (exposed) toolbar region, then leave to trigger autoRaise lower.
./fbwl-input-injector --socket "$SOCKET" motion "$HOVER_X" "$HOVER_Y" >/dev/null 2>&1 || true

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" motion "$LEAVE_X" "$LEAVE_Y" >/dev/null 2>&1 || true
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: autoRaise lower'; do sleep 0.05; done"

# With toolbar lowered behind the window, clicks in the overlap region should hit the window.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y" >/dev/null 2>&1 || true
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Pointer press .* hit=tb-win'; do sleep 0.05; done"

# Hover an exposed region to trigger autoRaise raise, then click in the overlap region again; it should hit the toolbar.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" motion "$HOVER_X" "$HOVER_Y" >/dev/null 2>&1 || true
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: autoRaise raise'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y" >/dev/null 2>&1 || true
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: click tool=workspacename cmd='; do sleep 0.05; done"

echo "ok: toolbar autoRaise parity smoke passed (socket=$SOCKET log=$LOG)"
