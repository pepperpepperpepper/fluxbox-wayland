#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1 (build first)" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd wc

need_exe ./fluxbox-wayland
need_exe ./fbwl-input-injector
need_exe ./fbwl-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-toolbar-tools-order-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-toolbar-tools-order-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${WIN_PID:-}" ]]; then kill "$WIN_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<'EOF'
session.screen0.workspaces: 3
session.screen0.toolbar.visible: true
# Intentionally non-default ordering to verify ordered-tool parity.
session.screen0.toolbar.tools: prevwindow,nextwindow,prevworkspace,iconbar,workspacename,nextworkspace,button.test,clock
session.screen0.toolbar.button.test.label: TBTEST
session.screen0.toolbar.button.test.commands: NextWorkspace
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" \
  ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFGDIR" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

POS_LINE="$(rg 'Toolbar: position ' "$LOG" | tail -n 1)"
if [[ "$POS_LINE" =~ x=([-0-9]+)\ y=([-0-9]+)\ h=([0-9]+) ]]; then
  TB_X0="${BASH_REMATCH[1]}"
  TB_Y0="${BASH_REMATCH[2]}"
  TB_H="${BASH_REMATCH[3]}"
else
  echo "failed to parse Toolbar: position line: $POS_LINE" >&2
  exit 1
fi

get_tool() {
  local tok="$1"
  local line
  line="$(rg "Toolbar: tool tok=$tok " "$LOG" | tail -n 1)"
  if [[ -z "$line" ]]; then
    echo "failed to find tool log for tok=$tok" >&2
    return 1
  fi
  if [[ "$line" =~ lx=([-0-9]+)\ w=([0-9]+) ]]; then
    echo "${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"
    return 0
  fi
  echo "failed to parse tool line for tok=$tok: $line" >&2
  return 1
}

# Spawn a window so iconbar items exist and we can assert iconbar placement relative to tool buttons.
./fbwl-smoke-client --socket "$SOCKET" --title order-win --stay-ms 20000 >/dev/null 2>&1 &
WIN_PID=$!
timeout 5 bash -c "until rg -q 'Toolbar: iconbar item .*title=order-win' '$LOG'; do sleep 0.05; done"

read -r PW_LX PW_W < <(get_tool prevwindow)
read -r NW_LX NW_W < <(get_tool nextwindow)
read -r PWS_LX PWS_W < <(get_tool prevworkspace)
read -r WSN_LX WSN_W < <(get_tool workspacename)
read -r NWS_LX NWS_W < <(get_tool nextworkspace)
read -r BTN_LX BTN_W < <(get_tool button.test)

COUNT_LINE="$(rg 'Toolbar: toolbuttons count=' "$LOG" | tail -n 1)"
if [[ "$COUNT_LINE" =~ count=([0-9]+) ]]; then
  TB_COUNT="${BASH_REMATCH[1]}"
else
  echo "failed to parse toolbuttons count line: $COUNT_LINE" >&2
  exit 1
fi
if [[ "$TB_COUNT" -ne 6 ]]; then
  echo "unexpected toolbar tool button count: got $TB_COUNT expected 6 (log=$LOG)" >&2
  exit 1
fi

if (( NW_LX < PW_LX + PW_W )); then
  echo "unexpected tool order: nextwindow overlaps prevwindow (pw=$PW_LX+$PW_W nw=$NW_LX)" >&2
  exit 1
fi
if (( PWS_LX < NW_LX + NW_W )); then
  echo "unexpected tool order: prevworkspace overlaps nextwindow (nw=$NW_LX+$NW_W pws=$PWS_LX)" >&2
  exit 1
fi
if (( WSN_LX < PWS_LX + PWS_W )); then
  echo "unexpected tool order: workspacename overlaps prevworkspace (pws=$PWS_LX+$PWS_W wsn=$WSN_LX)" >&2
  exit 1
fi
if (( NWS_LX < WSN_LX + WSN_W )); then
  echo "unexpected tool order: nextworkspace overlaps workspacename (wsn=$WSN_LX+$WSN_W nws=$NWS_LX)" >&2
  exit 1
fi
if (( BTN_LX < NWS_LX + NWS_W )); then
  echo "unexpected tool order: button.test overlaps nextworkspace (nws=$NWS_LX+$NWS_W btn=$BTN_LX)" >&2
  exit 1
fi

ICON_LINE="$(rg 'Toolbar: iconbar item ' "$LOG" | rg 'title=order-win' | tail -n 1)"
if [[ "$ICON_LINE" =~ lx=([-0-9]+)\ w=([0-9]+)\ title= ]]; then
  IB_LX="${BASH_REMATCH[1]}"
  IB_W="${BASH_REMATCH[2]}"
else
  echo "failed to parse iconbar item line: $ICON_LINE" >&2
  exit 1
fi

if (( IB_LX < PWS_LX + PWS_W )); then
  echo "unexpected tool order: iconbar is not after prevworkspace (pws=$PWS_LX+$PWS_W iconbar=$IB_LX)" >&2
  exit 1
fi
if (( IB_LX >= WSN_LX )); then
  echo "unexpected tool order: iconbar is not before workspacename (iconbar=$IB_LX wsn=$WSN_LX)" >&2
  exit 1
fi

# Click prevwindow/nextwindow and assert we execute the workspace-scoped commands.
click_tool() {
  local tok="$1"
  local lx="$2"
  local w="$3"
  local expect="$4"
  local click_x click_y offset
  click_x=$((TB_X0 + lx + w / 2))
  click_y=$((TB_Y0 + TB_H / 2))
  offset=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-input-injector --socket "$SOCKET" click "$click_x" "$click_y" >/dev/null 2>&1
  timeout 2 bash -c "until tail -c +$((offset + 1)) '$LOG' | rg -q '$expect'; do sleep 0.05; done"
}

click_tool "prevwindow" "$PW_LX" "$PW_W" 'Toolbar: click tool=prevwindow cmd=prevwindow \(workspace=\[current\]\)'
click_tool "nextwindow" "$NW_LX" "$NW_W" 'Toolbar: click tool=nextwindow cmd=nextwindow \(workspace=\[current\]\)'

echo "ok: toolbar.tools ordering + per-tool widgets smoke passed (socket=$SOCKET log=$LOG)"
