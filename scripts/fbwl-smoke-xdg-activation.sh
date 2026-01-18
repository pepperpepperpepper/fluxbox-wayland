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
LOG="${LOG:-/tmp/fluxbox-wayland-xdg-activation-$UID-$$.log}"
CLIENT_LOG="${CLIENT_LOG:-/tmp/fbwl-xdg-activation-$UID-$$.log}"

cleanup() {
  if [[ -n "${HOLD_PID:-}" ]]; then kill "$HOLD_PID" 2>/dev/null || true; fi
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
: >"$CLIENT_LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" hold 10000 >/dev/null 2>&1 &
HOLD_PID=$!
timeout 5 bash -c "until rg -q 'New virtual pointer' '$LOG'; do sleep 0.05; done"

./fbwl-xdg-activation-client --socket "$SOCKET" --timeout-ms 8000 >"$CLIENT_LOG" 2>&1 &
CLIENT_PID=$!
timeout 5 bash -c "until rg -q '^fbwl-xdg-activation-client: ready$' '$CLIENT_LOG'; do sleep 0.05; done"

timeout 5 bash -c "until rg -q 'Place: fbwl-activate-a ' '$LOG'; do sleep 0.05; done"

XY=$(rg -m 1 'Place: fbwl-activate-a ' "$LOG" | awk '{for(i=1;i<=NF;i++){if($i ~ /^x=/){sub(/^x=/,"",$i); x=$i} if($i ~ /^y=/){sub(/^y=/,"",$i); y=$i}}} END{print x" "y}')
if [[ -z "$XY" ]]; then
  echo "failed to parse placement for fbwl-activate-a" >&2
  exit 1
fi
read -r X Y <<<"$XY"

./fbwl-input-injector --socket "$SOCKET" click "$((X+10))" "$((Y+10))" >/dev/null

wait "$CLIENT_PID"
rg -q '^ok xdg_activation$' "$CLIENT_LOG"

echo "ok: xdg-activation smoke passed (socket=$SOCKET log=$LOG)"
