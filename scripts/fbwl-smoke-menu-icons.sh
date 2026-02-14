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

source scripts/fbwl-smoke-report-lib.sh

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
XVFB_LOG="${XVFB_LOG:-/tmp/xvfb-menu-icons-$UID-$$.log}"
LOG="${LOG:-/tmp/fluxbox-wayland-menu-icons-$UID-$$.log}"
MENU_FILE="${MENU_FILE:-/tmp/fbwl-menu-icons-$UID-$$.menu}"
ICON_XPM="${ICON_XPM:-/tmp/fbwl-menu-icons-$UID-$$.xpm}"
ICON_MISSING="${ICON_MISSING:-/tmp/fbwl-menu-icons-missing-$UID-$$.xpm}"
MARKER1="${MARKER1:-/tmp/fbwl-menu-icons-marker1-$UID-$$}"
MARKER2="${MARKER2:-/tmp/fbwl-menu-icons-marker2-$UID-$$}"

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
  echo "debug: logs: log=${LOG:-} xvfb_log=${XVFB_LOG:-}" >&2
  dump_tail "${LOG:-}"
  dump_tail "${XVFB_LOG:-}"
  exit "$rc"
}
trap 'smoke_on_err $LINENO "$BASH_COMMAND"' ERR

cleanup() {
  rm -f "$MENU_FILE" "$ICON_XPM" "$MARKER1" "$MARKER2" 2>/dev/null || true
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
rm -f "$MARKER1" "$MARKER2"

cat >"$ICON_XPM" <<'EOF'
/* XPM */
static char * icon_xpm[] = {
"1 1 2 1",
" \tc None",
".\tc #FF0000",
"."
};
EOF

cat >"$MENU_FILE" <<EOF
[begin] (Fluxbox)
[exec] (WithIcon) {sh -c 'echo ok >"$MARKER1"'} <$ICON_XPM>
[nop] (NoIcon)
[exec] (MissingIcon) {sh -c 'echo ok >"$MARKER2"'} <$ICON_MISSING>
[end]
EOF

Xvfb ":$DISPLAY_NUM" -screen 0 1280x720x24 -nolisten tcp -extension GLX >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!
if ! timeout 5 bash -c "until [[ -S /tmp/.X11-unix/X$DISPLAY_NUM ]]; do sleep 0.05; done"; then
  echo "Xvfb failed to start on :$DISPLAY_NUM (log: $XVFB_LOG)" >&2
  dump_tail "$XVFB_LOG" 60
  exit 1
fi

DISPLAY=":$DISPLAY_NUM" WLR_BACKENDS=x11 WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --menu "$MENU_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

REPORT_DIR="${FBWL_REPORT_DIR:-${FBWL_SMOKE_REPORT_DIR:-}}"
fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

# Open the root menu with a background right-click.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line after right-click" >&2
  exit 1
fi

fbwl_report_shot "menu-icons.png" "Menu icon alignment (with icon / missing icon / no icon)"

if [[ "$open_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ items=([0-9]+) ]]; then
  MENU_X="${BASH_REMATCH[1]}"
  MENU_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse menu open line: $open_line" >&2
  exit 1
fi

ITEM_H=24
MENU_TITLE_H=$ITEM_H

# Click the first item (WithIcon) to validate exec works.
CLICK_X=$((MENU_X + 10))
CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10))
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
timeout 5 bash -c "until [[ -f '$MARKER1' ]]; do sleep 0.05; done"

# Open again and click the third item (MissingIcon).
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line after second right-click" >&2
  exit 1
fi
if [[ "$open_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ items=([0-9]+) ]]; then
  MENU_X="${BASH_REMATCH[1]}"
  MENU_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse menu open line: $open_line" >&2
  exit 1
fi

IDX=2
CLICK_X=$((MENU_X + 10))
CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10 + IDX * ITEM_H))
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
timeout 5 bash -c "until [[ -f '$MARKER2' ]]; do sleep 0.05; done"

echo "ok: menu-icons smoke passed (DISPLAY=:$DISPLAY_NUM socket=$SOCKET log=$LOG)"
