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
LOG="${LOG:-/tmp/fluxbox-wayland-shortcuts-inhibit-$UID-$$.log}"
INHIBIT_LOG="${INHIBIT_LOG:-/tmp/fbwl-shortcuts-inhibit-$UID-$$.log}"
SPAWN_MARK="${SPAWN_MARK:-/tmp/fbwl-terminal-spawned-$UID-$$}"

cleanup() {
  rm -f "$SPAWN_MARK" 2>/dev/null || true
  if [[ -n "${INHIBIT_PID:-}" ]]; then
    kill "$INHIBIT_PID" 2>/dev/null || true
    wait "$INHIBIT_PID" 2>/dev/null || true
  fi
  if [[ -n "${HOLD_PID:-}" ]]; then
    kill "$HOLD_PID" 2>/dev/null || true
    wait "$HOLD_PID" 2>/dev/null || true
  fi
  if [[ -n "${FBW_PID:-}" ]]; then
    kill "$FBW_PID" 2>/dev/null || true
    wait "$FBW_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

: >"$LOG"
: >"$INHIBIT_LOG"
rm -f "$SPAWN_MARK"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --terminal "touch '$SPAWN_MARK'" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" hold 10000 >/dev/null 2>&1 &
HOLD_PID=$!
timeout 5 bash -c "until rg -q 'New virtual keyboard' '$LOG'; do sleep 0.05; done"

./fbwl-shortcuts-inhibit-client --socket "$SOCKET" --timeout-ms 5000 --stay-ms 10000 >"$INHIBIT_LOG" 2>&1 &
INHIBIT_PID=$!
timeout 5 bash -c "until rg -q '^ok shortcuts-inhibit active$' '$INHIBIT_LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-return >/dev/null 2>&1

for _ in {1..20}; do
  if [[ -f "$SPAWN_MARK" ]]; then
    echo "unexpected: terminal command executed while shortcuts were inhibited (spawn_mark=$SPAWN_MARK)" >&2
    exit 1
  fi
  sleep 0.05
done

kill "$INHIBIT_PID"
wait "$INHIBIT_PID" || true
unset INHIBIT_PID

./fbwl-input-injector --socket "$SOCKET" key alt-return >/dev/null 2>&1
timeout 2 bash -c "until [[ -f '$SPAWN_MARK' ]]; do sleep 0.05; done"

echo "ok: shortcuts-inhibit smoke passed (socket=$SOCKET log=$LOG inhibit_log=$INHIBIT_LOG spawn_mark=$SPAWN_MARK)"
