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

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-toolbar-autohide-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-toolbar-autohide-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-toolbar-autohide-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.autoRaiseDelay: 50

session.screen0.defaultDeco: NONE

session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.widthPercent: 50
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: workspacename
session.screen0.toolbar.layer: Top
session.screen0.toolbar.autoHide: true
session.screen0.toolbar.autoRaise: false
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
  ./fluxbox-wayland --no-xwayland -no-slit --socket "$SOCKET" --config-dir "$CFGDIR" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Init: toolbar .* autoHide=1' '$LOG'; do sleep 0.05; done"
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

if [[ "$TB_W" -lt 10 || "$TB_H" -lt 10 ]]; then
  echo "unexpected toolbar size: w=$TB_W h=$TB_H (from: $pos_line)" >&2
  exit 1
fi

# Enter the toolbar first to establish hovered=true. autoHide only hides after a hover transition.
./fbwl-input-injector --socket "$SOCKET" motion "$((TB_X + 5))" "$((TB_Y + 5))" >/dev/null 2>&1 || true

# Leave the toolbar; it should hide after session.autoRaiseDelay.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" motion "$((TB_X + TB_W / 2))" "$((TB_Y + TB_H + 20))" >/dev/null 2>&1 || true
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: autoHide hide'; do sleep 0.05; done"

# Move to the "peek" edge region; toolbar should show immediately on hover.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" motion "$((TB_X + 5))" "$TB_Y" >/dev/null 2>&1 || true
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: autoHide show'; do sleep 0.05; done"

echo "ok: toolbar autoHide parity smoke passed (socket=$SOCKET log=$LOG)"

