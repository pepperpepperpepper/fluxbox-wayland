#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1 (build first)" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout
need_cmd Xvfb

need_exe ./fluxbox-wayland
need_exe ./fbwl-input-injector
need_exe ./fbwl-screencopy-client
need_exe ./fbwl-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

pick_display_num() {
  local base="${1:-99}"
  local d
  for ((d = base; d <= base + 200; d++)); do
    if [[ ! -e "/tmp/.X11-unix/X$d" && ! -e "/tmp/.X${d}-lock" ]]; then
      echo "$d"
      return 0
    fi
  done
  return 1
}

DISPLAY_NUM="$(pick_display_num "${DISPLAY_NUM:-99}" || true)"
if [[ -z "$DISPLAY_NUM" ]]; then
  echo "failed to find a free X display number" >&2
  exit 1
fi
SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
XVFB_LOG="${XVFB_LOG:-/tmp/xvfb-$UID-$$.log}"
LOG="${LOG:-/tmp/fluxbox-wayland-xvfb-$UID-$$.log}"
SC_LOG="${SC_LOG:-/tmp/fbwl-screencopy-xvfb-$UID-$$.log}"
CMD_MARKER="${CMD_MARKER:-/tmp/fbwl-cmd-dialog-xvfb-marker-$UID-$$}"
MENU_FILE="${MENU_FILE:-/tmp/fbwl-menu-xvfb-$UID-$$.menu}"
MENU_MARKER="${MENU_MARKER:-/tmp/fbwl-menu-xvfb-marker-$UID-$$}"
BG_COLOR="${BG_COLOR:-#336699}"

dump_tail() {
  local path="${1:-}"
  local n="${2:-120}"
  [[ -z "$path" ]] && return 0
  [[ -f "$path" ]] || return 0
  echo "----- tail -n $n $path" >&2
  tail -n "$n" "$path" >&2 || true
}

smoke_on_err() {
  local rc=$?
  trap - ERR
  set +e

  echo "error: $0 failed (rc=$rc line=${1:-} cmd=${2:-})" >&2
  echo "debug: DISPLAY=:${DISPLAY_NUM:-} socket=${SOCKET:-} XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-}" >&2
  echo "debug: logs: log=${LOG:-} xvfb_log=${XVFB_LOG:-} sc_log=${SC_LOG:-}" >&2

  if command -v xwd >/dev/null 2>&1 && [[ -n "${DISPLAY_NUM:-}" ]]; then
    local xwd_out="/tmp/fbwl-smoke-xvfb-$UID-$$.xwd"
    if xwd -silent -root -display ":$DISPLAY_NUM" -out "$xwd_out" >/dev/null 2>&1; then
      echo "debug: wrote screenshot: $xwd_out" >&2
    fi
  fi

  dump_tail "${LOG:-}"
  dump_tail "${XVFB_LOG:-}"
  dump_tail "${SC_LOG:-}"
  exit "$rc"
}
trap 'smoke_on_err $LINENO "$BASH_COMMAND"' ERR

cleanup() {
  rm -f "$CMD_MARKER" "$MENU_FILE" "$MENU_MARKER" 2>/dev/null || true
  if [[ -n "${MR_PID:-}" ]]; then
    kill "$MR_PID" 2>/dev/null || true
    wait "$MR_PID" 2>/dev/null || true
  fi
  if [[ -n "${A_PID:-}" ]]; then
    kill "$A_PID" 2>/dev/null || true
    wait "$A_PID" 2>/dev/null || true
  fi
  if [[ -n "${B_PID:-}" ]]; then
    kill "$B_PID" 2>/dev/null || true
    wait "$B_PID" 2>/dev/null || true
  fi
  if [[ -n "${HOLD_PID:-}" ]]; then
    kill "$HOLD_PID" 2>/dev/null || true
    wait "$HOLD_PID" 2>/dev/null || true
  fi
  if [[ -n "${FBW_PID:-}" ]]; then
    kill "$FBW_PID" 2>/dev/null || true
    wait "$FBW_PID" 2>/dev/null || true
  fi
  if [[ -n "${XVFB_PID:-}" ]]; then
    kill "$XVFB_PID" 2>/dev/null || true
    wait "$XVFB_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

: >"$XVFB_LOG"
: >"$LOG"
: >"$SC_LOG"
rm -f "$CMD_MARKER"
rm -f "$MENU_MARKER"

cat >"$MENU_FILE" <<EOF
[begin] (Fluxbox)
[exec] (TouchMarker) {sh -c 'echo ok >"$MENU_MARKER"'}
[exit] (Exit)
[end]
EOF

Xvfb ":$DISPLAY_NUM" -screen 0 1280x720x24 -nolisten tcp -extension GLX >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!

if ! timeout 5 bash -c "until [[ -S /tmp/.X11-unix/X$DISPLAY_NUM ]]; do sleep 0.05; done"; then
  echo "Xvfb failed to start on :$DISPLAY_NUM (log: $XVFB_LOG)" >&2
  tail -n 50 "$XVFB_LOG" >&2 || true
  exit 1
fi

DISPLAY=":$DISPLAY_NUM" WLR_BACKENDS=x11 WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --bg-color "$BG_COLOR" \
  --menu "$MENU_FILE" \
  --workspaces 3 \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --timeout-ms 2000

./fbwl-input-injector --socket "$SOCKET" hold 60000 >/dev/null 2>&1 &
HOLD_PID=$!
timeout 5 bash -c "until rg -q 'New virtual pointer' '$LOG' && rg -q 'New virtual keyboard' '$LOG'; do sleep 0.05; done"

timeout 5 bash -c "until rg -q 'Background: output ' '$LOG'; do sleep 0.05; done"
./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb "$BG_COLOR" --sample-x 1 --sample-y 1 >"$SC_LOG" 2>&1
rg -q '^ok screencopy$' "$SC_LOG"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line after right-click" >&2
  exit 1
fi
if [[ "$open_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  MENU_X="${BASH_REMATCH[1]}"
  MENU_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse menu open line: $open_line" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$((MENU_X + 10))" "$((MENU_Y + 10))"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: exec '
timeout 5 bash -c "until [[ -f '$MENU_MARKER' ]]; do sleep 0.05; done"

timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"
pos_line="$(rg -m1 'Toolbar: position ' "$LOG")"
if [[ "$pos_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ h=([0-9]+)\ cell_w=([0-9]+)\ workspaces=([0-9]+) ]]; then
  TB_X0="${BASH_REMATCH[1]}"
  TB_Y0="${BASH_REMATCH[2]}"
  TB_H="${BASH_REMATCH[3]}"
  TB_CELL_W="${BASH_REMATCH[4]}"
  TB_WS="${BASH_REMATCH[5]}"
else
  echo "failed to parse Toolbar: position line: $pos_line" >&2
  exit 1
fi

if [[ "$TB_WS" -lt 2 ]]; then
  echo "unexpected workspace count from toolbar: $TB_WS" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
tool_line="$(rg 'Toolbar: tool tok=nextworkspace ' "$LOG" | tail -n 1)"
if [[ "$tool_line" =~ lx=([-0-9]+)\ w=([0-9]+) ]]; then
  TB_LX="${BASH_REMATCH[1]}"
  TB_W="${BASH_REMATCH[2]}"
else
  echo "failed to parse Toolbar: tool tok=nextworkspace line: $tool_line" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" click $((TB_X0 + TB_LX + TB_W / 2)) $((TB_Y0 + TB_H / 2))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Toolbar: click tool=nextworkspace cmd=nextworkspace'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=2 reason=switch-next'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-3
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'OSD: show workspace=3'
timeout 5 bash -c "until rg -q 'OSD: hide reason=timer' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title xvfb-ib-a --stay-ms 20000 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title xvfb-ib-b --stay-ms 20000 >/dev/null 2>&1 &
B_PID=$!

timeout 5 bash -c "until rg -q 'Focus: xvfb-ib-a' '$LOG' && rg -q 'Focus: xvfb-ib-b' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: iconbar item .*title=xvfb-ib-a' '$LOG' && rg -q 'Toolbar: iconbar item .*title=xvfb-ib-b' '$LOG'; do sleep 0.05; done"

FOCUSED_VIEW=$(
  rg -o 'Focus: xvfb-ib-[ab]' "$LOG" \
    | tail -n 1 \
    | awk '{print $2}'
)

case "$FOCUSED_VIEW" in
  xvfb-ib-a) OTHER_VIEW=xvfb-ib-b ;;
  xvfb-ib-b) OTHER_VIEW=xvfb-ib-a ;;
  *) echo "failed to determine focused view (got: $FOCUSED_VIEW)" >&2; exit 1 ;;
esac

item_line="$(rg "Toolbar: iconbar item .*title=$OTHER_VIEW" "$LOG" | tail -n 1)"
if [[ "$item_line" =~ lx=([-0-9]+)\ w=([0-9]+)\ title= ]]; then
  LX="${BASH_REMATCH[1]}"
  W="${BASH_REMATCH[2]}"
else
  echo "failed to parse Toolbar: iconbar item line: $item_line" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click $((TB_X0 + LX + W / 2)) $((TB_Y0 + TB_H / 2))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Toolbar: click iconbar idx=[0-9]+ title=$OTHER_VIEW"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Focus: $OTHER_VIEW"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-i
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Minimize: $OTHER_VIEW on reason=keybinding"
timeout 5 bash -c "until rg -q \"Toolbar: iconbar item .*title=$OTHER_VIEW minimized=1\" '$LOG'; do sleep 0.05; done"

item_line="$(rg "Toolbar: iconbar item .*title=$OTHER_VIEW" "$LOG" | tail -n 1)"
if [[ "$item_line" =~ lx=([-0-9]+)\ w=([0-9]+)\ title= ]]; then
  LX="${BASH_REMATCH[1]}"
  W="${BASH_REMATCH[2]}"
else
  echo "failed to parse Toolbar: iconbar item line after minimize: $item_line" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click $((TB_X0 + LX + W / 2)) $((TB_Y0 + TB_H / 2))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Minimize: $OTHER_VIEW off reason=toolbar-iconbar"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Focus: $OTHER_VIEW"

./fbwl-smoke-client --socket "$SOCKET" --title xvfb-mr --stay-ms 20000 >/dev/null 2>&1 &
MR_PID=$!
timeout 5 bash -c "until rg -q 'Surface size: xvfb-mr ' '$LOG' && rg -q 'Place: xvfb-mr ' '$LOG'; do sleep 0.05; done"

MR_PLACE_LINE="$(rg -m1 'Place: xvfb-mr ' "$LOG")"
if [[ "$MR_PLACE_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  MR_X0="${BASH_REMATCH[1]}"
  MR_Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $MR_PLACE_LINE" >&2
  exit 1
fi

MR_SIZE_LINE="$(rg -m1 'Surface size: xvfb-mr ' "$LOG")"
if [[ "$MR_SIZE_LINE" =~ ([0-9]+)x([0-9]+) ]]; then
  MR_W0="${BASH_REMATCH[1]}"
  MR_H0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Surface size line: $MR_SIZE_LINE" >&2
  exit 1
fi

MR_START_X=$((MR_X0 + 5))
MR_START_Y=$((MR_Y0 + 5))
MR_END_X=$((MR_START_X + 100))
MR_END_Y=$((MR_START_Y + 100))
MR_X1=$((MR_X0 + 100))
MR_Y1=$((MR_Y0 + 100))

./fbwl-input-injector --socket "$SOCKET" click "$MR_START_X" "$MR_START_Y" >/dev/null 2>&1 || true
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-alt-left "$MR_START_X" "$MR_START_Y" "$MR_END_X" "$MR_END_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Move: xvfb-mr x=$MR_X1 y=$MR_Y1"

MR_RS_START_X=$((MR_X1 + MR_W0 - 2))
MR_RS_START_Y=$((MR_Y1 + MR_H0 - 2))
MR_RS_END_X=$((MR_RS_START_X + 50))
MR_RS_END_Y=$((MR_RS_START_Y + 60))
MR_W1=$((MR_W0 + 50))
MR_H1=$((MR_H0 + 60))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-alt-right "$MR_RS_START_X" "$MR_RS_START_Y" "$MR_RS_END_X" "$MR_RS_END_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: xvfb-mr w=$MR_W1 h=$MR_H1"
timeout 5 bash -c "until rg -q 'Surface size: xvfb-mr ${MR_W1}x${MR_H1}' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f2
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'CmdDialog: open'

CMD="touch $CMD_MARKER"
./fbwl-input-injector --socket "$SOCKET" type "$CMD"
./fbwl-input-injector --socket "$SOCKET" key enter

timeout 5 bash -c "until rg -F -q \"CmdDialog: execute cmd=$CMD\" '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until [[ -f '$CMD_MARKER' ]]; do sleep 0.05; done"

kill -0 "$FBW_PID" >/dev/null 2>&1

kill "$FBW_PID"
wait "$FBW_PID"
unset FBW_PID

echo "ok: xvfb+x11 backend smoke passed (DISPLAY=:$DISPLAY_NUM socket=$SOCKET log=$LOG xvfb_log=$XVFB_LOG sc_log=$SC_LOG menu=$MENU_FILE)"
