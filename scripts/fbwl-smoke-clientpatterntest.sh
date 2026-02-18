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
LOG="${LOG:-/tmp/fluxbox-wayland-clientpatterntest-$UID-$$.log}"

cleanup() {
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

fbr() {
  DISPLAY='' ./fluxbox-remote --wayland --socket "$SOCKET" "$@"
}

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title cpt-a --app-id cpt-a --stay-ms 20000 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title cpt-b --app-id cpt-b --stay-ms 20000 >/dev/null 2>&1 &
B_PID=$!

timeout 5 bash -c "until rg -q 'Place: cpt-a ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: cpt-b ' '$LOG'; do sleep 0.05; done"

fbr clientpatterntest '(title=cpt-a)' | rg -q '^ok$'
fbr result | rg -q $'^0x[0-9a-fA-F]+\\tcpt-a$'

fbr clientpatterntest '(title=does-not-exist)' | rg -q '^ok$'
fbr result | rg -q '^0$'

echo "ok: clientpatterntest smoke passed (socket=$SOCKET log=$LOG)"

