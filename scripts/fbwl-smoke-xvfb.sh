#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout
need_cmd Xvfb

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

pick_display_num() {
  local base="${1:-99}"
  local d
  for d in $(seq "$base" "$((base + 20))"); do
    if [[ ! -S "/tmp/.X11-unix/X$d" ]]; then
      echo "$d"
      return 0
    fi
  done
  return 1
}

DISPLAY_NUM="$(pick_display_num "${DISPLAY_NUM:-99}")"
SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
XVFB_LOG="${XVFB_LOG:-/tmp/xvfb-$UID-$$.log}"
LOG="${LOG:-/tmp/fluxbox-wayland-xvfb-$UID-$$.log}"

cleanup() {
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

Xvfb ":$DISPLAY_NUM" -screen 0 1280x720x24 -nolisten tcp -extension GLX >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!

timeout 5 bash -c "until [[ -S /tmp/.X11-unix/X$DISPLAY_NUM ]]; do sleep 0.05; done"

DISPLAY=":$DISPLAY_NUM" WLR_BACKENDS=x11 WLR_RENDERER=pixman ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --timeout-ms 2000

kill -0 "$FBW_PID" >/dev/null 2>&1

kill "$FBW_PID"
wait "$FBW_PID"
unset FBW_PID

echo "ok: xvfb+x11 backend smoke passed (DISPLAY=:$DISPLAY_NUM socket=$SOCKET log=$LOG xvfb_log=$XVFB_LOG)"
