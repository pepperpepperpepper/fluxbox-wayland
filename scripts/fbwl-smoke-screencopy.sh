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
LOG="${LOG:-/tmp/fluxbox-wayland-screencopy-$UID-$$.log}"
CLIENT_LOG="${CLIENT_LOG:-/tmp/fbwl-smoke-screencopy-client-$UID-$$.log}"
SC_LOG="${SC_LOG:-/tmp/fbwl-screencopy-$UID-$$.log}"

cleanup() {
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
: >"$CLIENT_LOG"
: >"$SC_LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title screencopy --timeout-ms 3000 --stay-ms 10000 >"$CLIENT_LOG" 2>&1 &
CLIENT_PID=$!
timeout 5 bash -c "until rg -q 'Place: screencopy' '$LOG'; do sleep 0.05; done"

./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 >"$SC_LOG" 2>&1
rg -q '^ok screencopy$' "$SC_LOG"

echo "ok: screencopy smoke passed (socket=$SOCKET log=$LOG)"

