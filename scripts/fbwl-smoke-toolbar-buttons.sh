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
LOG="${LOG:-/tmp/fluxbox-wayland-toolbar-buttons-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-toolbar-buttons-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.screen0.workspaces: 4
session.screen0.toolbar.visible: true
session.screen0.toolbar.tools: workspacename,clock,button.test
session.screen0.allowRemoteActions: true
session.screen0.toolbar.button.test.label: TBTEST
session.screen0.toolbar.button.test.commands: NextWorkspace:PrevWorkspace:Workspace 3
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" \
  ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFGDIR" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

line="$(rg -m1 'Toolbar: position ' "$LOG")"
if [[ "$line" =~ x=([-0-9]+)\ y=([-0-9]+)\ h=([0-9]+)\ cell_w=([0-9]+)\ workspaces=([0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
  H="${BASH_REMATCH[3]}"
  # CELL_W="${BASH_REMATCH[4]}"
  # WS="${BASH_REMATCH[5]}"
else
  echo "failed to parse Toolbar: position line: $line" >&2
  exit 1
fi

tool_line="$(rg 'Toolbar: tool tok=button.test ' "$LOG" | tail -n 1)"
if [[ "$tool_line" =~ lx=([-0-9]+)\ w=([0-9]+) ]]; then
  TB_LX="${BASH_REMATCH[1]}"
  TB_W="${BASH_REMATCH[2]}"
else
  echo "failed to parse Toolbar: tool tok=button.test line: $tool_line" >&2
  exit 1
fi

CLICK_X=$((X0 + TB_LX + TB_W / 2))
CLICK_Y=$((Y0 + H / 2))

fbr() {
  DISPLAY='' ./fluxbox-remote --wayland --socket "$SOCKET" "$@"
}

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
fbr relabelbutton button.test TBNEW | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'RelabelButton: button\\.test label=TBNEW found=1'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: tool tok=button\\.test '; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Toolbar: click button\\.test button=1 cmd=NextWorkspace'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=2 reason=switch-next'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click-middle "$CLICK_X" "$CLICK_Y"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Toolbar: click button\\.test button=2 cmd=PrevWorkspace'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=1 reason=switch-prev'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click-right "$CLICK_X" "$CLICK_Y"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Toolbar: click button\\.test button=3 cmd=Workspace 3'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=3 reason=switch'; do sleep 0.05; done"

echo "ok: toolbar.buttons smoke passed (socket=$SOCKET log=$LOG)"
