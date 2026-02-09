#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1 (build first)" >&2; exit 1; }
}

need_cmd dbus-run-session
need_cmd rg
need_cmd timeout
need_cmd Xvfb

need_exe ./fluxbox-wayland
need_exe ./fbwl-input-injector
need_exe ./fbwl-remote
need_exe ./fbwl-screencopy-client
need_exe ./fbwl-sni-item-client

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

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
SOCKET="${SOCKET:-wayland-fbwl-xvfb-tray-$UID-$$}"
XVFB_LOG="${XVFB_LOG:-/tmp/xvfb-tray-$UID-$$.log}"
LOG="${LOG:-/tmp/fluxbox-wayland-xvfb-tray-$UID-$$.log}"
MARK_ACT="${MARK_ACT:-/tmp/fbwl-tray-xvfb-activated-$UID-$$}"
MARK_SECONDARY="${MARK_SECONDARY:-/tmp/fbwl-tray-xvfb-secondary-activated-$UID-$$}"
MARK_CONTEXT="${MARK_CONTEXT:-/tmp/fbwl-tray-xvfb-context-menu-$UID-$$}"
ICON_RGB="${ICON_RGB:-#00ff00}"
ICON_RGB2="${ICON_RGB2:-#ff0000}"

: >"$XVFB_LOG"
: >"$LOG"
rm -f "$MARK_ACT" "$MARK_SECONDARY" "$MARK_CONTEXT"

ROOT="$ROOT" \
XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
DISPLAY_NUM="$DISPLAY_NUM" \
SOCKET="$SOCKET" \
XVFB_LOG="$XVFB_LOG" \
LOG="$LOG" \
MARK_ACT="$MARK_ACT" \
MARK_SECONDARY="$MARK_SECONDARY" \
MARK_CONTEXT="$MARK_CONTEXT" \
ICON_RGB="$ICON_RGB" \
ICON_RGB2="$ICON_RGB2" \
dbus-run-session -- bash -c '
  set -euo pipefail

  kill_wait() {
    local pid="${1:-}"
    if [[ -z "$pid" ]]; then return 0; fi
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  }

  cleanup() {
    rm -f "$MARK_ACT" "$MARK_SECONDARY" "$MARK_CONTEXT" 2>/dev/null || true
    kill_wait "${ITEM_PID:-}"
    kill_wait "${FBW_PID:-}"
    kill_wait "${XVFB_PID:-}"
  }
  trap cleanup EXIT

  cd "$ROOT"
  : >"$XVFB_LOG"
  : >"$LOG"

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
    --workspaces 2 \
    >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q \"Running fluxbox-wayland\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"SNI: watcher enabled\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"Toolbar: position \" \"$LOG\"; do sleep 0.05; done"

  ./fbwl-sni-item-client \
    --item-path /fbwl/TestItem \
    --icon-rgba "$ICON_RGB" \
    --icon-size 16 \
    --update-icon-rgba "$ICON_RGB2" \
    --update-icon-on-activate \
    --activate-mark "$MARK_ACT" \
    --secondary-activate-mark "$MARK_SECONDARY" \
    --context-menu-mark "$MARK_CONTEXT" \
    --stay-ms 8000 \
    >/dev/null 2>&1 &
  ITEM_PID=$!

  timeout 5 bash -c "until rg -q \"Toolbar: tray item idx=0\" \"$LOG\"; do sleep 0.05; done"

  pos_line="$(rg -m1 "Toolbar: position " "$LOG")"
  if [[ "$pos_line" =~ x=([-0-9]+)[[:space:]]+y=([-0-9]+)[[:space:]]+h=([0-9]+)[[:space:]]+cell_w=([0-9]+)[[:space:]]+workspaces=([0-9]+) ]]; then
    X0="${BASH_REMATCH[1]}"
    Y0="${BASH_REMATCH[2]}"
    H="${BASH_REMATCH[3]}"
  else
    echo "failed to parse Toolbar: position line: $pos_line" >&2
    exit 1
  fi

  tray_line="$(rg -m1 "Toolbar: tray item idx=0" "$LOG")"
  if [[ "$tray_line" =~ lx=([-0-9]+)[[:space:]]+w=([0-9]+)[[:space:]]+id= ]]; then
    LX="${BASH_REMATCH[1]}"
    W="${BASH_REMATCH[2]}"
  else
    echo "failed to parse Toolbar: tray item line: $tray_line" >&2
    exit 1
  fi

  CLICK_X=$((X0 + LX + W / 2))
  CLICK_Y=$((Y0 + H / 2))

  timeout 5 bash -c "until rg -q \"SNI: icon updated id=.*fbwl/TestItem\" \"$LOG\"; do sleep 0.05; done"
  ./fbwl-screencopy-client --socket "$SOCKET" --sample-x "$CLICK_X" --sample-y "$CLICK_Y" --expect-rgb "$ICON_RGB" >/dev/null

  ./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
  timeout 5 bash -c "until test -f \"$MARK_ACT\"; do sleep 0.05; done"

  timeout 5 bash -c "until [[ \"\$(rg -c \"SNI: icon updated id=.*fbwl/TestItem\" \"$LOG\" 2>/dev/null || echo 0)\" -ge 2 ]]; do sleep 0.05; done"
  ./fbwl-screencopy-client --socket "$SOCKET" --sample-x "$CLICK_X" --sample-y "$CLICK_Y" --expect-rgb "$ICON_RGB2" >/dev/null

  ./fbwl-input-injector --socket "$SOCKET" click-middle "$CLICK_X" "$CLICK_Y"
  timeout 5 bash -c "until test -f \"$MARK_SECONDARY\"; do sleep 0.05; done"

  ./fbwl-input-injector --socket "$SOCKET" click-right "$CLICK_X" "$CLICK_Y"
  timeout 5 bash -c "until test -f \"$MARK_CONTEXT\"; do sleep 0.05; done"

  ./fbwl-remote --socket "$SOCKET" quit | rg -q "^ok quitting$"
  timeout 5 bash -c "while kill -0 \"$FBW_PID\" 2>/dev/null; do sleep 0.05; done"
  wait "$FBW_PID"
  unset FBW_PID

  wait "$ITEM_PID" 2>/dev/null || true
  unset ITEM_PID

  echo "ok: xvfb+x11 backend tray smoke passed (DISPLAY=:$DISPLAY_NUM socket=$SOCKET log=$LOG xvfb_log=$XVFB_LOG)"
'
