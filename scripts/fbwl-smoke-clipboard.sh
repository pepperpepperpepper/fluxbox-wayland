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
LOG="${LOG:-/tmp/fluxbox-wayland-clipboard-$UID-$$.log}"
SET_LOG="${SET_LOG:-/tmp/fbwl-clipboard-set-$UID-$$.log}"

TEXT="fbwl-clipboard-smoke"

cleanup() {
  if [[ -n "${SET_PID:-}" ]]; then kill "$SET_PID" 2>/dev/null || true; fi
  if [[ -n "${HOLD_PID:-}" ]]; then kill "$HOLD_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
: >"$SET_LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" hold 10000 >/dev/null 2>&1 &
HOLD_PID=$!
timeout 5 bash -c "until rg -q 'New virtual keyboard' '$LOG'; do sleep 0.05; done"

./fbwl-clipboard-client --socket "$SOCKET" --set "$TEXT" --stay-ms 10000 --timeout-ms 5000 >"$SET_LOG" 2>&1 &
SET_PID=$!

timeout 5 bash -c "until rg -q '^ok selection_set$' '$SET_LOG'; do sleep 0.05; done"

OUT="$(./fbwl-clipboard-client --socket "$SOCKET" --get --timeout-ms 5000)"
if [[ "$OUT" != "$TEXT" ]]; then
  echo "clipboard get mismatch: expected '$TEXT' got '$OUT'" >&2
  exit 1
fi

echo "ok: clipboard smoke passed (socket=$SOCKET log=$LOG)"
