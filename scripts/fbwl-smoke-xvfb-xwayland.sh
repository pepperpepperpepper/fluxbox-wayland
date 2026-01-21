#!/usr/bin/env bash
set -euo pipefail

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

export XWAYLAND_NO_GLAMOR=1
export __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=swrast

pick_display_num() {
  local base="${1:-99}"
  local d
  for d in $(seq "$base" "$((base + 20))"); do
    if [[ ! -e "/tmp/.X11-unix/X$d" && ! -e "/tmp/.X${d}-lock" ]]; then
      echo "$d"
      return 0
    fi
  done
  return 1
}

DISPLAY_NUM="$(pick_display_num "${DISPLAY_NUM:-99}")"
SOCKET="${SOCKET:-wayland-fbwl-xvfb-xwayland-$UID-$$}"
XVFB_LOG="${XVFB_LOG:-/tmp/xvfb-xwayland-$UID-$$.log}"
LOG="${LOG:-/tmp/fluxbox-wayland-xvfb-xwayland-$UID-$$.log}"
TITLE="${TITLE:-xw-test-xvfb}"

cleanup() {
  if [[ -n "${X11_PID:-}" ]]; then kill "$X11_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  if [[ -n "${XVFB_PID:-}" ]]; then kill "$XVFB_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

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

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-alt-left 70 70 170 170

MOVE_LINE="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m 1 "Move: $TITLE" || true)"
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
./fbwl-input-injector --socket "$SOCKET" drag-alt-right 190 190 240 250

RESIZE_LINE="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m 1 "Resize: $TITLE" || true)"
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

echo "ok: Xvfb+x11 backend + XWayland smoke passed (display=:$DISPLAY_NUM xwayland=$DISPLAY_NAME socket=$SOCKET log=$LOG)"
