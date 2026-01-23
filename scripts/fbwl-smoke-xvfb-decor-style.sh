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
need_exe ./fbwl-smoke-client

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

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

kill_wait() {
  local pid="${1:-}"
  if [[ -z "$pid" ]]; then return 0; fi
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}

DISPLAY_NUM="$(pick_display_num "${DISPLAY_NUM:-99}")"
XVFB_LOG="${XVFB_LOG:-/tmp/xvfb-decor-style-$UID-$$.log}"

cleanup() {
  kill_wait "${XVFB_PID:-}"
}
trap cleanup EXIT

: >"$XVFB_LOG"

Xvfb ":$DISPLAY_NUM" -screen 0 1280x720x24 -nolisten tcp -extension GLX >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!

if ! timeout 5 bash -c "until [[ -S /tmp/.X11-unix/X$DISPLAY_NUM ]]; do sleep 0.05; done"; then
  echo "Xvfb failed to start on :$DISPLAY_NUM (log: $XVFB_LOG)" >&2
  tail -n 50 "$XVFB_LOG" >&2 || true
  exit 1
fi

DISPLAY=":$DISPLAY_NUM" WLR_BACKENDS=x11 WLR_RENDERER=pixman scripts/fbwl-smoke-ssd.sh
DISPLAY=":$DISPLAY_NUM" WLR_BACKENDS=x11 WLR_RENDERER=pixman scripts/fbwl-smoke-style.sh

echo "ok: xvfb decorations+style smoke passed (DISPLAY=:$DISPLAY_NUM xvfb_log=$XVFB_LOG)"
