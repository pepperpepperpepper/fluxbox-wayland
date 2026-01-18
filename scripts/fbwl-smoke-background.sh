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
LOG="${LOG:-/tmp/fluxbox-wayland-background-$UID-$$.log}"
SC_LOG="${SC_LOG:-/tmp/fbwl-screencopy-background-$UID-$$.log}"
BG_COLOR="${BG_COLOR:-#336699}"

cleanup() {
  if [[ -n "${FBW_PID:-}" ]]; then
    kill "$FBW_PID" 2>/dev/null || true
    wait "$FBW_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

: >"$LOG"
: >"$SC_LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --bg-color "$BG_COLOR" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Background: output ' '$LOG'; do sleep 0.05; done"

./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb "$BG_COLOR" --sample-x 1 --sample-y 1 >"$SC_LOG" 2>&1
rg -q '^ok screencopy$' "$SC_LOG"

echo "ok: background smoke passed (socket=$SOCKET log=$LOG bg=$BG_COLOR)"
