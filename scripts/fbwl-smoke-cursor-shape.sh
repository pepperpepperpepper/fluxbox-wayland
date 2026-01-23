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
LOG="${LOG:-/tmp/fluxbox-wayland-cursor-shape-$UID-$$.log}"
CLIENT_LOG="${CLIENT_LOG:-/tmp/fbwl-cursor-shape-$UID-$$.log}"

cleanup() {
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${HOLD_PID:-}" ]]; then kill "$HOLD_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
: >"$CLIENT_LOG"

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" hold 10000 >/dev/null 2>&1 &
HOLD_PID=$!
timeout 5 bash -c "until rg -q 'New virtual pointer' '$LOG'; do sleep 0.05; done"

./fbwl-cursor-shape-client --socket "$SOCKET" --timeout-ms 5000 >"$CLIENT_LOG" 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q '^ok ready$' '$CLIENT_LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: fbwl-cursor-shape ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: fbwl-cursor-shape ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  CLICK_X=$((BASH_REMATCH[1] + 10))
  CLICK_Y=$((BASH_REMATCH[2] + 10))
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y" >/dev/null 2>&1

timeout 5 bash -c "while kill -0 $CLIENT_PID 2>/dev/null; do sleep 0.05; done"
wait "$CLIENT_PID"
unset CLIENT_PID

timeout 5 bash -c "until rg -q 'CursorShape: name=' '$LOG'; do sleep 0.05; done"

echo "ok: cursor-shape smoke passed (socket=$SOCKET log=$LOG)"
