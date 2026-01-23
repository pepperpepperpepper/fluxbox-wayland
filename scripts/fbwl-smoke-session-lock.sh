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
LOG="${LOG:-/tmp/fluxbox-wayland-session-lock-$UID-$$.log}"
LOCK_LOG="${LOCK_LOG:-/tmp/fbwl-session-lock-$UID-$$.log}"

cleanup() {
  if [[ -n "${LOCK_PID:-}" ]]; then
    kill "$LOCK_PID" 2>/dev/null || true
    wait "$LOCK_PID" 2>/dev/null || true
  fi
  if [[ -n "${CLIENT_PID:-}" ]]; then
    kill "$CLIENT_PID" 2>/dev/null || true
    wait "$CLIENT_PID" 2>/dev/null || true
  fi
  if [[ -n "${FBW_PID:-}" ]]; then
    kill "$FBW_PID" 2>/dev/null || true
    wait "$FBW_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

: >"$LOG"
: >"$LOCK_LOG"

WLR_BACKENDS="${WLR_BACKENDS:-headless}" \
WLR_RENDERER="${WLR_RENDERER:-pixman}" \
WLR_HEADLESS_OUTPUTS="${WLR_HEADLESS_OUTPUTS:-1}" \
./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title victim --stay-ms 9000 >/dev/null 2>&1 &
CLIENT_PID=$!
timeout 5 bash -c "until rg -q 'Focus: victim ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: victim ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: victim ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  CLICK_X=$((BASH_REMATCH[1] + 10))
  CLICK_Y=$((BASH_REMATCH[2] + 10))
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y" >/dev/null 2>&1
timeout 5 bash -c "until rg -q 'Pointer press .*hit=victim' '$LOG'; do sleep 0.05; done"

./fbwl-session-lock-client --socket "$SOCKET" --timeout-ms 6000 --locked-ms 800 >"$LOCK_LOG" 2>&1 &
LOCK_PID=$!
timeout 5 bash -c "until rg -q '^ok session-lock locked$' '$LOCK_LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y" >/dev/null 2>&1
timeout 5 bash -c "until rg -q 'Pointer press .*hit=\\(none\\)' '$LOG'; do sleep 0.05; done"

wait "$LOCK_PID"
rg -q '^ok session-lock unlocked$' "$LOCK_LOG"

./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y" >/dev/null 2>&1
timeout 5 bash -c "until [[ \$(rg -c 'Pointer press .*hit=victim' '$LOG') -ge 2 ]]; do sleep 0.05; done"

echo "ok: session-lock smoke passed (socket=$SOCKET log=$LOG)"
