#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1 (build first)" >&2; exit 1; }
}

need_cmd rg
need_cmd sed
need_cmd timeout
need_cmd Xvfb

need_exe ./fbwl-input-injector
need_exe ./fbx11-smoke-client
need_exe ./fluxbox-wayland

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

source scripts/fbwl-smoke-report-lib.sh

export XWAYLAND_NO_GLAMOR=1
export __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=swrast

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
SOCKET="${SOCKET:-wayland-fbwl-xvfb-xwayland-$UID-$$}"
XVFB_LOG="${XVFB_LOG:-/tmp/xvfb-xwayland-$UID-$$.log}"
LOG="${LOG:-/tmp/fluxbox-wayland-xvfb-xwayland-$UID-$$.log}"
TITLE="${TITLE:-xw-test-xvfb}"
REPORT_DIR="${FBWL_REPORT_DIR:-${FBWL_SMOKE_REPORT_DIR:-}}"

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
  echo "debug: DISPLAY=:${DISPLAY_NUM:-} xwayland_display=${DISPLAY_NAME:-} socket=${SOCKET:-} XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-}" >&2
  echo "debug: logs: log=${LOG:-} xvfb_log=${XVFB_LOG:-}" >&2

  if command -v xwd >/dev/null 2>&1 && [[ -n "${DISPLAY_NUM:-}" ]]; then
    local xwd_out="/tmp/fbwl-smoke-xvfb-xwayland-$UID-$$.xwd"
    if xwd -silent -root -display ":$DISPLAY_NUM" -out "$xwd_out" >/dev/null 2>&1; then
      echo "debug: wrote screenshot: $xwd_out" >&2
    fi
  fi

  dump_tail "${LOG:-}"
  dump_tail "${XVFB_LOG:-}"
  exit "$rc"
}
trap 'smoke_on_err $LINENO "$BASH_COMMAND"' ERR

wait_log_after_offset() {
  local offset="$1"
  local needle="$2"
  local timeout_s="${3:-10}"
  local end=$((SECONDS + timeout_s))
  while ((SECONDS <= end)); do
    if tail -c +$((offset + 1)) "$LOG" | rg -F -q "$needle"; then
      return 0
    fi
    sleep 0.05
  done
  echo "timeout waiting for log: $needle (log=$LOG)" >&2
  return 1
}

cleanup() {
  if [[ -n "${X11_PID:-}" ]]; then kill "$X11_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  if [[ -n "${XVFB_PID:-}" ]]; then kill "$XVFB_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$XVFB_LOG"
: >"$LOG"

fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

Xvfb ":$DISPLAY_NUM" -screen 0 1280x720x24 -nolisten tcp -extension GLX >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!

if ! timeout 5 bash -c "until [[ -S /tmp/.X11-unix/X$DISPLAY_NUM ]]; do sleep 0.05; done"; then
  echo "Xvfb failed to start on :$DISPLAY_NUM (log: $XVFB_LOG)" >&2
  tail -n 50 "$XVFB_LOG" >&2 || true
  exit 1
fi

DISPLAY=":$DISPLAY_NUM" WLR_BACKENDS=x11 WLR_RENDERER=pixman ./fluxbox-wayland \
  --socket "$SOCKET" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'XWayland: ready DISPLAY=' '$LOG'; do sleep 0.05; done"

DISPLAY_NAME="$(rg -m 1 'XWayland: ready DISPLAY=' "$LOG" | sed -E 's/.*DISPLAY=//')"
if [[ -z "$DISPLAY_NAME" ]]; then
  echo "failed to parse XWayland DISPLAY from log: $LOG" >&2
  exit 1
fi

DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client \
  --title "$TITLE" \
  --class "$TITLE" \
  --instance "$TITLE" \
  --stay-ms 10000 \
  --w 128 \
  --h 96 \
  >/dev/null 2>&1 &
X11_PID=$!

timeout 10 bash -c "until rg -q 'XWayland: map ' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q \"Focus: $TITLE\" '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q \"Surface size: $TITLE 128x96\" '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q \"Place: $TITLE\" '$LOG'; do sleep 0.05; done"

PLACE_LINE="$(rg -m 1 "Place: $TITLE" "$LOG")"
OLD_X="$(echo "$PLACE_LINE" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)"
OLD_Y="$(echo "$PLACE_LINE" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)"

SIZE_LINE="$(rg -m 1 "Surface size: $TITLE" "$LOG")"
if [[ "$SIZE_LINE" =~ ([0-9]+)x([0-9]+) ]]; then
  OLD_W="${BASH_REMATCH[1]}"
  OLD_H="${BASH_REMATCH[2]}"
else
  echo "failed to parse Surface size line: $SIZE_LINE" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
MOVE_START_X=$((OLD_X + 10))
MOVE_START_Y=$((OLD_Y + 10))
MOVE_END_X=$((MOVE_START_X + 100))
MOVE_END_Y=$((MOVE_START_Y + 100))
./fbwl-input-injector --socket "$SOCKET" drag-alt-left "$MOVE_START_X" "$MOVE_START_Y" "$MOVE_END_X" "$MOVE_END_Y"

wait_log_after_offset "$OFFSET" "Move: $TITLE"
MOVE_LINE="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -F -m 1 "Move: $TITLE" || true)"
if [[ -z "$MOVE_LINE" ]]; then
  echo "missing expected Move log for $TITLE (log=$LOG)" >&2
  exit 1
fi

NEW_X="$(echo "$MOVE_LINE" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)"
NEW_Y="$(echo "$MOVE_LINE" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)"
if (( NEW_X != OLD_X + 100 || NEW_Y != OLD_Y + 100 )); then
  echo "unexpected move delta: $MOVE_LINE (old=$OLD_X,$OLD_Y expected=$((OLD_X + 100)),$((OLD_Y + 100)))" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
RESIZE_START_X=$((NEW_X + OLD_W - 10))
RESIZE_START_Y=$((NEW_Y + OLD_H - 10))
RESIZE_END_X=$((RESIZE_START_X + 50))
RESIZE_END_Y=$((RESIZE_START_Y + 60))
./fbwl-input-injector --socket "$SOCKET" drag-alt-right "$RESIZE_START_X" "$RESIZE_START_Y" "$RESIZE_END_X" "$RESIZE_END_Y"

wait_log_after_offset "$OFFSET" "Resize: $TITLE"
RESIZE_LINE="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -F -m 1 "Resize: $TITLE" || true)"
if [[ -z "$RESIZE_LINE" ]]; then
  echo "missing expected Resize log for $TITLE (log=$LOG)" >&2
  exit 1
fi

NEW_W="$(echo "$RESIZE_LINE" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)"
NEW_H="$(echo "$RESIZE_LINE" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)"
if (( NEW_W != 128 + 50 || NEW_H != 96 + 60 )); then
  echo "unexpected resize delta: $RESIZE_LINE (expected w=178 h=156)" >&2
  exit 1
fi

timeout 10 bash -c "until rg -q \"Surface size: $TITLE ${NEW_W}x${NEW_H}\" '$LOG'; do sleep 0.05; done"

fbwl_report_shot "xwayland.png" "XWayland client (after move/resize)"

kill "$FBW_PID" 2>/dev/null || true
wait "$FBW_PID" 2>/dev/null || true
unset FBW_PID

cleanup_xwayland_display() {
  local dname="$1"
  if [[ ! "$dname" =~ ^:([0-9]+)$ ]]; then
    return 0
  fi
  local d="${BASH_REMATCH[1]}"
  local lock="/tmp/.X${d}-lock"
  local sock="/tmp/.X11-unix/X${d}"

  # Give XWayland a moment to fully exit and remove its lock/socket.
  local end=$((SECONDS + 10))
  while ((SECONDS <= end)); do
    if [[ ! -e "$lock" && ! -S "$sock" ]]; then
      return 0
    fi
    sleep 0.05
  done

  # If still present, the lock file should contain the X server pid.
  if [[ -f "$lock" ]]; then
    local pid
    pid="$(cat "$lock" 2>/dev/null | tr -d ' ' || true)"
    if [[ "$pid" =~ ^[0-9]+$ ]]; then
      local comm
      comm="$(ps -p "$pid" -o comm= 2>/dev/null | tr -d ' ' || true)"
      if [[ "$comm" == "Xwayland" ]]; then
        kill "$pid" 2>/dev/null || true
        sleep 0.2
        kill -9 "$pid" 2>/dev/null || true
      fi
    fi
  fi

  end=$((SECONDS + 10))
  while ((SECONDS <= end)); do
    if [[ ! -e "$lock" && ! -S "$sock" ]]; then
      return 0
    fi
    sleep 0.05
  done

  echo "warning: XWayland display still present after cleanup: $dname (lock=$lock sock=$sock)" >&2
  return 0
}

cleanup_xwayland_display "$DISPLAY_NAME"

DISPLAY=":$DISPLAY_NUM" WLR_BACKENDS=x11 WLR_RENDERER=pixman scripts/fbwl-smoke-apps-rules-xwayland.sh

echo "ok: Xvfb+x11 backend + XWayland smoke passed (display=:$DISPLAY_NUM xwayland=$DISPLAY_NAME socket=$SOCKET log=$LOG)"
