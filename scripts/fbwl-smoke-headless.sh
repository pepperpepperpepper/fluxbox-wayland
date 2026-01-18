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
LOG="${LOG:-/tmp/fluxbox-wayland-headless-$UID-$$.log}"

cleanup() {
  if [[ -n "${FBW_PID:-}" ]]; then
    kill "$FBW_PID" 2>/dev/null || true
    wait "$FBW_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

: >"$LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --timeout-ms 2000

kill -0 "$FBW_PID" >/dev/null 2>&1

kill "$FBW_PID"
wait "$FBW_PID"
unset FBW_PID

echo "ok: headless smoke passed (socket=$SOCKET log=$LOG)"
