#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd Xvfb
need_cmd timeout

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

scripts=(
  scripts/fbwl-smoke-portal-wlr.sh
  scripts/fbwl-smoke-portal-wlr-screencast.sh
  scripts/fbwl-smoke-xdg-desktop-portal.sh
  scripts/fbwl-smoke-xdg-desktop-portal-screenshot.sh
)
for s in "${scripts[@]}"; do
  [[ -x "$s" ]] || { echo "missing required executable: $s" >&2; exit 1; }
done

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
XVFB_LOG="${XVFB_LOG:-/tmp/xvfb-portal-$UID-$$.log}"

cleanup() {
  if [[ -n "${XVFB_PID:-}" ]]; then
    kill "$XVFB_PID" 2>/dev/null || true
    wait "$XVFB_PID" 2>/dev/null || true
  fi
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

export DISPLAY=":$DISPLAY_NUM"
export WLR_BACKENDS="x11"
export WLR_RENDERER="${WLR_RENDERER:-pixman}"

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID-xvfb-portal-$DISPLAY_NUM-$$}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

for s in "${scripts[@]}"; do
  echo "==> $s (xvfb :$DISPLAY_NUM)"
  "$s"
done

echo "ok: Xvfb portal smoke tests passed"
