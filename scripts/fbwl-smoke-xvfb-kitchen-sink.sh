#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1 (build first)" >&2; exit 1; }
}

pick_display_num() {
  local base="${1:-99}"
  local d
  for ((d = base; d <= base + 20; d++)); do
    if [[ ! -e "/tmp/.X11-unix/X$d" && ! -e "/tmp/.X${d}-lock" ]]; then
      echo "$d"
      return 0
    fi
  done
  return 1
}

kill_wait() {
  local pid="${1:-}"
  if [[ -z "$pid" ]]; then return 0; fi
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}

need_cmd rg
need_cmd timeout
need_cmd Xvfb

need_exe ./fluxbox-wayland
need_exe ./fbwl-foreign-toplevel-client
need_exe ./fbwl-input-injector
need_exe ./fbwl-layer-shell-client
need_exe ./fbwl-remote
need_exe ./fbwl-screencopy-client
need_exe ./fbwl-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

DISPLAY_NUM="$(pick_display_num "${DISPLAY_NUM:-99}")"
SOCKET="${SOCKET:-wayland-fbwl-xvfb-ks-$UID-$$}"
XVFB_LOG="${XVFB_LOG:-/tmp/xvfb-ks-$UID-$$.log}"
LOG="${LOG:-/tmp/fluxbox-wayland-xvfb-ks-$UID-$$.log}"
SC_LOG="${SC_LOG:-/tmp/fbwl-screencopy-xvfb-ks-$UID-$$.log}"

MENU_FILE="${MENU_FILE:-/tmp/fbwl-menu-xvfb-ks-$UID-$$.menu}"
MENU_MARKER="${MENU_MARKER:-/tmp/fbwl-menu-xvfb-ks-marker-$UID-$$}"
CMD_MARKER="${CMD_MARKER:-/tmp/fbwl-cmd-xvfb-ks-marker-$UID-$$}"

APPS_FILE="${APPS_FILE:-/tmp/fbwl-apps-xvfb-ks-$UID-$$.apps}"

BG_COLOR="${BG_COLOR:-#336699}"

source scripts/fbwl-smoke-report-lib.sh
fbwl_report_init "${FBWL_SMOKE_REPORT_DIR:-}" "$SOCKET" "$XDG_RUNTIME_DIR"

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
    local xwd_out="/tmp/fbwl-smoke-xvfb-ks-$UID-$$.xwd"
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
  rm -f "$MENU_FILE" "$MENU_MARKER" "$CMD_MARKER" "$APPS_FILE" 2>/dev/null || true
  kill_wait "${FS_B_PID:-}"
  kill_wait "${FS_A_PID:-}"
  kill_wait "${MIN_PID:-}"
  kill_wait "${PANEL_PID:-}"
  kill_wait "${PLACED_PID:-}"
  kill_wait "${WINMENU_PID:-}"
  kill_wait "${APPS_STICKY_PID:-}"
  kill_wait "${APPS_JUMP_PID:-}"
  kill_wait "${MF_PID:-}"
  kill_wait "${MR_PID:-}"
  kill_wait "${IB_B_PID:-}"
  kill_wait "${IB_A_PID:-}"
  kill_wait "${HOLD_PID:-}"
  kill_wait "${FBW_PID:-}"
  kill_wait "${XVFB_PID:-}"
}
trap cleanup EXIT

: >"$XVFB_LOG"
: >"$LOG"
: >"$SC_LOG"
rm -f "$MENU_MARKER" "$CMD_MARKER"

cat >"$MENU_FILE" <<EOF
[begin] (Fluxbox)
[exec] (TouchMarker) {sh -c 'echo ok >"$MENU_MARKER"'}
[exit] (Exit)
[end]
EOF

cat >"$APPS_FILE" <<'EOF'
# Workspace IDs are 0-based (Fluxbox apps file semantics):
#   [Workspace] {0} => first workspace
[app] (app_id=fbwl-ks-apps-jump)
  [Workspace] {1}
  [Jump]      {yes}
[end]

[app] (app_id=fbwl-ks-apps-sticky)
  [Workspace] {2}
  [Sticky]    {yes}
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
  --workspaces 3 \
  --menu "$MENU_FILE" \
  --apps "$APPS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Output: ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

OUT_GEOM=$(
  rg -m1 'Output: ' "$LOG" \
    | awk '{print $NF}'
)
OUT_W=${OUT_GEOM%x*}
OUT_H=${OUT_GEOM#*x}

TB_LINE="$(rg -m1 'Toolbar: position ' "$LOG")"
if [[ "$TB_LINE" =~ x=([-0-9]+)\ y=([-0-9]+)\ h=([0-9]+)\ cell_w=([0-9]+)\ workspaces=([0-9]+) ]]; then
  TB_X0="${BASH_REMATCH[1]}"
  TB_Y0="${BASH_REMATCH[2]}"
  TB_H="${BASH_REMATCH[3]}"
  TB_CELL_W="${BASH_REMATCH[4]}"
  TB_WS="${BASH_REMATCH[5]}"
else
  echo "failed to parse Toolbar: position line: $TB_LINE" >&2
  exit 1
fi

if [[ "$TB_WS" -lt 3 ]]; then
  echo "unexpected workspace count from toolbar: $TB_WS" >&2
  exit 1
fi

./fbwl-smoke-client --socket "$SOCKET" --timeout-ms 2000

./fbwl-input-injector --socket "$SOCKET" hold 60000 >/dev/null 2>&1 &
HOLD_PID=$!
timeout 5 bash -c "until rg -q 'New virtual pointer' '$LOG' && rg -q 'New virtual keyboard' '$LOG'; do sleep 0.05; done"

timeout 5 bash -c "until rg -q 'Background: output ' '$LOG'; do sleep 0.05; done"
./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb "$BG_COLOR" --sample-x 1 --sample-y 1 >"$SC_LOG" 2>&1
rg -q '^ok screencopy$' "$SC_LOG"
fbwl_report_shot "00-start.png" "Startup (background + toolbar)"

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

fbwl_report_shot "01-menu.png" "Root menu open"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$((MENU_X + 10))" "$((MENU_Y + 10))"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: exec '
timeout 5 bash -c "until [[ -f '$MENU_MARKER' ]]; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click $((TB_X0 + TB_CELL_W + TB_CELL_W / 2)) $((TB_Y0 + TB_H / 2))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Toolbar: click workspace=2'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=2 reason=toolbar'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-3
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'OSD: show workspace=3'
fbwl_report_shot "02-osd.png" "Workspace OSD"
timeout 5 bash -c "until rg -q 'OSD: hide reason=timer' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title ks-ib-a --stay-ms 20000 >/dev/null 2>&1 &
IB_A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title ks-ib-b --stay-ms 20000 >/dev/null 2>&1 &
IB_B_PID=$!

timeout 5 bash -c "until rg -q 'Focus: ks-ib-a' '$LOG' && rg -q 'Focus: ks-ib-b' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: iconbar item .*title=ks-ib-a' '$LOG' && rg -q 'Toolbar: iconbar item .*title=ks-ib-b' '$LOG'; do sleep 0.05; done"
fbwl_report_shot "03-iconbar.png" "Iconbar with two clients"

FOCUSED_VIEW=$(
  rg -o 'Focus: ks-ib-[ab]' "$LOG" \
    | tail -n 1 \
    | awk '{print $2}'
)

case "$FOCUSED_VIEW" in
  ks-ib-a) OTHER_VIEW=ks-ib-b ;;
  ks-ib-b) OTHER_VIEW=ks-ib-a ;;
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
fbwl_report_shot "04-minimize.png" "Minimize/unminimize via iconbar"

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

kill_wait "$IB_A_PID"
unset IB_A_PID
kill_wait "$IB_B_PID"
unset IB_B_PID

./fbwl-smoke-client --socket "$SOCKET" --title ks-mr --stay-ms 20000 >/dev/null 2>&1 &
MR_PID=$!
timeout 5 bash -c "until rg -q 'Surface size: ks-mr ' '$LOG' && rg -q 'Place: ks-mr ' '$LOG'; do sleep 0.05; done"

MR_PLACE_LINE="$(rg -m1 'Place: ks-mr ' "$LOG")"
if [[ "$MR_PLACE_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  MR_X0="${BASH_REMATCH[1]}"
  MR_Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $MR_PLACE_LINE" >&2
  exit 1
fi

MR_SIZE_LINE="$(rg -m1 'Surface size: ks-mr ' "$LOG")"
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
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Move: ks-mr x=$MR_X1 y=$MR_Y1"

MR_RS_START_X=$((MR_X1 + MR_W0 - 2))
MR_RS_START_Y=$((MR_Y1 + MR_H0 - 2))
MR_RS_END_X=$((MR_RS_START_X + 50))
MR_RS_END_Y=$((MR_RS_START_Y + 60))
MR_W1=$((MR_W0 + 50))
MR_H1=$((MR_H0 + 60))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-alt-right "$MR_RS_START_X" "$MR_RS_START_Y" "$MR_RS_END_X" "$MR_RS_END_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: ks-mr w=$MR_W1 h=$MR_H1"
timeout 5 bash -c "until rg -q 'Surface size: ks-mr ${MR_W1}x${MR_H1}' '$LOG'; do sleep 0.05; done"
fbwl_report_shot "05-move-resize.png" "Move/resize"

kill_wait "$MR_PID"
unset MR_PID

./fbwl-smoke-client --socket "$SOCKET" --title ks-mf --stay-ms 20000 >/dev/null 2>&1 &
MF_PID=$!
timeout 5 bash -c "until rg -q 'Surface size: ks-mf 32x32' '$LOG' && rg -q 'Place: ks-mf ' '$LOG'; do sleep 0.05; done"

MF_PLACE_LINE="$(rg -m1 'Place: ks-mf ' "$LOG")"
if [[ "$MF_PLACE_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  MF_CLICK_X=$((BASH_REMATCH[1] + 5))
  MF_CLICK_Y=$((BASH_REMATCH[2] + 5))
else
  echo "failed to parse Place line: $MF_PLACE_LINE" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" click "$MF_CLICK_X" "$MF_CLICK_Y" >/dev/null 2>&1 || true

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-m
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: ks-mf on w=$OUT_W h=$OUT_H"
timeout 5 bash -c "until rg -q 'Surface size: ks-mf ${OUT_W}x${OUT_H}' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-m
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: ks-mf off"
timeout 5 bash -c "until rg -q 'Surface size: ks-mf 32x32' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Fullscreen: ks-mf on w=$OUT_W h=$OUT_H"
timeout 5 bash -c "until rg -q 'Surface size: ks-mf ${OUT_W}x${OUT_H}' '$LOG'; do sleep 0.05; done"
fbwl_report_shot "06-fullscreen.png" "Fullscreen"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Fullscreen: ks-mf off"
timeout 5 bash -c "until rg -q 'Surface size: ks-mf 32x32' '$LOG'; do sleep 0.05; done"

kill_wait "$MF_PID"
unset MF_PID

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-ks-apps-jump --title ks-apps-jump --stay-ms 20000 >/dev/null 2>&1 &
APPS_JUMP_PID=$!

timeout 5 bash -c "until ./fbwl-remote --socket '$SOCKET' get-workspace | rg -q '^ok workspace=2$'; do sleep 0.05; done"
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-ks-apps-jump .*workspace_id=1 .*jump=1'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-ks-apps-sticky --title ks-apps-sticky --stay-ms 20000 >/dev/null 2>&1 &
APPS_STICKY_PID=$!

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-ks-apps-sticky .*workspace_id=2 .*sticky=1'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Workspace: view=ks-apps-sticky ws=3 visible=1'; do sleep 0.05; done"

./fbwl-remote --socket "$SOCKET" get-workspace | rg -q '^ok workspace=2$'
fbwl_report_shot "07-apps-rules.png" "Apps rules (jump + sticky)"

kill_wait "$APPS_JUMP_PID"
unset APPS_JUMP_PID
kill_wait "$APPS_STICKY_PID"
unset APPS_STICKY_PID

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Policy: workspace switch to 1'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=1 reason=switch'

./fbwl-smoke-client --socket "$SOCKET" --title ks-winmenu --stay-ms 20000 --xdg-decoration >/dev/null 2>&1 &
WINMENU_PID=$!
timeout 5 bash -c "until rg -q 'Place: ks-winmenu ' '$LOG'; do sleep 0.05; done"

WINMENU_PLACE_LINE="$(rg -m1 'Place: ks-winmenu ' "$LOG")"
if [[ "$WINMENU_PLACE_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  WINMENU_X0="${BASH_REMATCH[1]}"
  WINMENU_Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $WINMENU_PLACE_LINE" >&2
  exit 1
fi

WINMENU_TB_X=$((WINMENU_X0 + 10))
WINMENU_TB_Y=$((WINMENU_Y0 - TB_H + 2))

if (( WINMENU_TB_Y < 0 )); then
  MOVE_START_X=$((WINMENU_X0 + 10))
  MOVE_START_Y=$((WINMENU_Y0 + 10))
  MOVE_END_X=$MOVE_START_X
  MOVE_END_Y=$((MOVE_START_Y + TB_H + 40))

  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-input-injector --socket "$SOCKET" drag-alt-left "$MOVE_START_X" "$MOVE_START_Y" "$MOVE_END_X" "$MOVE_END_Y"
  move_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Move: ks-winmenu ' || true)"
  if [[ "$move_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
    WINMENU_X0="${BASH_REMATCH[1]}"
    WINMENU_Y0="${BASH_REMATCH[2]}"
  else
    echo "failed to parse Move line: $move_line" >&2
    exit 1
  fi
  WINMENU_TB_X=$((WINMENU_X0 + 10))
  WINMENU_TB_Y=$((WINMENU_Y0 - TB_H + 2))
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right "$WINMENU_TB_X" "$WINMENU_TB_Y" "$WINMENU_TB_X" "$WINMENU_TB_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: open-window title=ks-winmenu'
fbwl_report_shot "08-window-menu.png" "Window menu"

CLICK_X=$((WINMENU_TB_X + 10))
CLICK_Y=$((WINMENU_TB_Y + TB_H / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: window-close title=ks-winmenu'

timeout 5 bash -c "while kill -0 '$WINMENU_PID' 2>/dev/null; do sleep 0.05; done"
wait "$WINMENU_PID" 2>/dev/null || true
unset WINMENU_PID

PANEL_H=200
if (( PANEL_H > OUT_H - 100 )); then PANEL_H=$((OUT_H - 100)); fi
if (( PANEL_H < 128 )); then PANEL_H=128; fi
if (( PANEL_H > OUT_H - 32 )); then PANEL_H=$((OUT_H - 32)); fi
USABLE_H=$((OUT_H - PANEL_H))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-layer-shell-client \
  --socket "$SOCKET" \
  --namespace fbwl-ks-panel \
  --layer top \
  --anchor top \
  --height "$PANEL_H" \
  --exclusive-zone "$PANEL_H" \
  --stay-ms 20000 \
  >/dev/null 2>&1 &
PANEL_PID=$!

timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'LayerShell: map ns=fbwl-ks-panel'; do sleep 0.05; done"

tail -c +$((OFFSET + 1)) "$LOG" | rg -q "LayerShell: surface ns=fbwl-ks-panel layer=2 pos=0,0 size=${OUT_W}x${PANEL_H} excl=${PANEL_H}"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "LayerShell: output=[^ ]+ usable=0,${PANEL_H} ${OUT_W}x${USABLE_H}"
fbwl_report_shot "09-layer-shell.png" "Layer-shell panel (exclusive zone)"

USABLE_PAIR=$(
  tail -c +$((OFFSET + 1)) "$LOG" \
    | rg -m1 'LayerShell: output=' \
    | rg -o 'usable=-?[0-9]+,-?[0-9]+' \
    | cut -d= -f2
)
USABLE_X=${USABLE_PAIR%,*}
USABLE_Y=${USABLE_PAIR#*,}

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title ks-placed --stay-ms 20000 >/dev/null 2>&1 &
PLACED_PID=$!
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Place: ks-placed '; do sleep 0.05; done"

PLACED_LINE="$(tail -c +$((OFFSET + 1)) "$LOG" | rg 'Place: ks-placed ' | tail -n 1)"
PLACED_X=$(echo "$PLACED_LINE" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
PLACED_Y=$(echo "$PLACED_LINE" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
if (( PLACED_X < USABLE_X )); then
  echo "placed view is left of usable area: $PLACED_LINE (usable_x=$USABLE_X)" >&2
  exit 1
fi
if (( PLACED_Y < USABLE_Y )); then
  echo "placed view is above usable area: $PLACED_LINE (usable_y=$USABLE_Y)" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click $((PLACED_X + 5)) $((PLACED_Y + 5)) >/dev/null 2>&1 || true
./fbwl-input-injector --socket "$SOCKET" key alt-m >/dev/null 2>&1
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: ks-placed on w=${OUT_W} h=${USABLE_H}"
fbwl_report_shot "10-maximize-usable.png" "Maximize respects usable area"

kill_wait "$PLACED_PID"
unset PLACED_PID
kill_wait "$PANEL_PID"
unset PANEL_PID

./fbwl-smoke-client --socket "$SOCKET" --title ks-min --stay-ms 20000 >/dev/null 2>&1 &
MIN_PID=$!
timeout 5 bash -c "until rg -q 'Surface size: ks-min 32x32' '$LOG' && rg -q 'Place: ks-min ' '$LOG'; do sleep 0.05; done"

MIN_PLACE_LINE="$(rg -m1 'Place: ks-min ' "$LOG")"
if [[ "$MIN_PLACE_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  MIN_CLICK_X=$((BASH_REMATCH[1] + 5))
  MIN_CLICK_Y=$((BASH_REMATCH[2] + 5))
else
  echo "failed to parse Place line: $MIN_PLACE_LINE" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$MIN_CLICK_X" "$MIN_CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Pointer press .* hit=ks-min'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-foreign-toplevel-client --socket "$SOCKET" --timeout-ms 3000 minimize ks-min
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Minimize: ks-min on reason=foreign-request'
fbwl_report_shot "11-foreign-minimize.png" "Foreign-toplevel minimize"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$MIN_CLICK_X" "$MIN_CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -qF 'hit=(none)'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-foreign-toplevel-client --socket "$SOCKET" --timeout-ms 3000 unminimize ks-min
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Minimize: ks-min off reason=foreign-request'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$MIN_CLICK_X" "$MIN_CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Pointer press .* hit=ks-min'

kill_wait "$MIN_PID"
unset MIN_PID

./fbwl-smoke-client --socket "$SOCKET" --title ks-fs-a --stay-ms 20000 >/dev/null 2>&1 &
FS_A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title ks-fs-b --stay-ms 20000 >/dev/null 2>&1 &
FS_B_PID=$!

timeout 5 bash -c "until rg -q 'Place: ks-fs-a' '$LOG' && rg -q 'Place: ks-fs-b' '$LOG'; do sleep 0.05; done"

PLACED_A="$(rg 'Place: ks-fs-a' "$LOG" | tail -n 1)"
AX=$(echo "$PLACED_A" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
AY=$(echo "$PLACED_A" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)

PLACED_B="$(rg 'Place: ks-fs-b' "$LOG" | tail -n 1)"
BX=$(echo "$PLACED_B" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
BY=$(echo "$PLACED_B" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)

CLICK_AX=$((AX + 5))
CLICK_AY=$((AY + 5))
CLICK_BX=$((BX + 5))
CLICK_BY=$((BY + 5))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_BX" "$CLICK_BY" "$CLICK_AX" "$CLICK_AY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Focus: ks-fs-a'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Fullscreen: ks-fs-a on'

timeout 5 bash -c "until rg -q 'Toolbar: iconbar item .*title=ks-fs-a' '$LOG' && rg -q 'Toolbar: iconbar item .*title=ks-fs-b' '$LOG'; do sleep 0.05; done"

item_line="$(rg "Toolbar: iconbar item .*title=ks-fs-b" "$LOG" | tail -n 1)"
if [[ "$item_line" =~ lx=([-0-9]+)\ w=([0-9]+)\ title= ]]; then
  LX="${BASH_REMATCH[1]}"
  W="${BASH_REMATCH[2]}"
else
  echo "failed to parse Toolbar: iconbar item line for ks-fs-b: $item_line" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click $((TB_X0 + LX + W / 2)) $((TB_Y0 + TB_H / 2))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Toolbar: click iconbar idx=[0-9]+ title=ks-fs-b"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Focus: ks-fs-b'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_BX" "$CLICK_BY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Pointer press .* hit=ks-fs-a'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Fullscreen: ks-fs-a off'

WS_COUNT=3
WS_ITERS="${WS_ITERS:-5}"
ws_out="$(./fbwl-remote --socket "$SOCKET" get-workspace)"
if [[ "$ws_out" =~ ^ok[[:space:]]workspace=([0-9]+)$ ]]; then
  WS_START="${BASH_REMATCH[1]}"
else
  echo "failed to parse get-workspace output: '$ws_out'" >&2
  exit 1
fi

ws_expect="$WS_START"
for ((i = 0; i < WS_ITERS; i++)); do
  ./fbwl-remote --socket "$SOCKET" nextworkspace | rg -q '^ok$'
  ws_expect=$((ws_expect % WS_COUNT + 1))
  ws_out="$(./fbwl-remote --socket "$SOCKET" get-workspace)"
  if [[ "$ws_out" != "ok workspace=$ws_expect" ]]; then
    echo "nextworkspace iteration $i: expected 'ok workspace=$ws_expect' got '$ws_out'" >&2
    exit 1
  fi
done

for ((i = 0; i < WS_ITERS; i++)); do
  ./fbwl-remote --socket "$SOCKET" prevworkspace | rg -q '^ok$'
  ws_expect=$((((ws_expect + WS_COUNT - 2) % WS_COUNT) + 1))
  ws_out="$(./fbwl-remote --socket "$SOCKET" get-workspace)"
  if [[ "$ws_out" != "ok workspace=$ws_expect" ]]; then
    echo "prevworkspace iteration $i: expected 'ok workspace=$ws_expect' got '$ws_out'" >&2
    exit 1
  fi
done

./fbwl-remote --socket "$SOCKET" workspace "$WS_START" | rg -q "^ok workspace=$WS_START$"

FOCUS_ITERS="${FOCUS_ITERS:-6}"
cur_focus_line="$(rg -o 'Focus: ks-fs-[ab]' "$LOG" | tail -n 1 || true)"
if [[ -z "$cur_focus_line" ]]; then
  echo "failed to determine focused view for focus-next stress test (log: $LOG)" >&2
  exit 1
fi
cur_focus="${cur_focus_line#Focus: }"
case "$cur_focus" in
  ks-fs-a) expect_focus=ks-fs-b ;;
  ks-fs-b) expect_focus=ks-fs-a ;;
  *) echo "unexpected focused view for focus-next stress test: $cur_focus" >&2; exit 1 ;;
esac
for ((i = 0; i < FOCUS_ITERS; i++)); do
  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-remote --socket "$SOCKET" focus-next | rg -q '^ok$'
  timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q \"Focus: ${expect_focus}\"; do sleep 0.05; done"
  if [[ "$expect_focus" == ks-fs-a ]]; then
    expect_focus=ks-fs-b
  else
    expect_focus=ks-fs-a
  fi
done

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f2
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'CmdDialog: open'
fbwl_report_shot "12-command-dialog.png" "Command dialog"

CMD="touch $CMD_MARKER"
./fbwl-input-injector --socket "$SOCKET" type "$CMD"
./fbwl-input-injector --socket "$SOCKET" key enter

timeout 5 bash -c "until rg -F -q \"CmdDialog: execute cmd=$CMD\" '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until [[ -f '$CMD_MARKER' ]]; do sleep 0.05; done"

./fbwl-remote --socket "$SOCKET" quit | rg -q '^ok quitting$'
timeout 10 bash -c "while kill -0 '$FBW_PID' 2>/dev/null; do sleep 0.05; done"
wait "$FBW_PID" 2>/dev/null || true
unset FBW_PID

echo "ok: xvfb kitchen sink smoke passed (DISPLAY=:$DISPLAY_NUM socket=$SOCKET log=$LOG xvfb_log=$XVFB_LOG)"
