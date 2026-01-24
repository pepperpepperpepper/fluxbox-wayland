#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-presentation-time-$UID-$$.log}"

cleanup() {
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

BACKENDS="${WLR_BACKENDS:-headless}"
RENDERER="${WLR_RENDERER:-pixman}"

if [[ "$BACKENDS" == *x11* ]]; then
  : "${DISPLAY:?DISPLAY must be set for x11 backend}"
fi

env WLR_BACKENDS="$BACKENDS" WLR_RENDERER="$RENDERER" \
  ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

OUT="$(./fbwl-presentation-time-client --socket "$SOCKET" --timeout-ms 5000)"
if [[ "$OUT" != ok* ]]; then
  echo "presentation-time client failed: $OUT" >&2
  exit 1
fi

echo "ok: presentation-time smoke passed (socket=$SOCKET log=$LOG)"
