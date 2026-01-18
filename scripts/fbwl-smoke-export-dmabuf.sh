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
LOG="${LOG:-/tmp/fluxbox-wayland-export-dmabuf-$UID-$$.log}"
CLIENT_LOG="${CLIENT_LOG:-/tmp/fbwl-smoke-export-dmabuf-client-$UID-$$.log}"
DMABUF_LOG="${DMABUF_LOG:-/tmp/fbwl-export-dmabuf-$UID-$$.log}"

cleanup() {
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
: >"$CLIENT_LOG"
: >"$DMABUF_LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title export-dmabuf --timeout-ms 3000 --stay-ms 10000 >"$CLIENT_LOG" 2>&1 &
CLIENT_PID=$!
timeout 5 bash -c "until rg -q 'Place: export-dmabuf' '$LOG'; do sleep 0.05; done"

./fbwl-export-dmabuf-client --socket "$SOCKET" --timeout-ms 4000 --allow-cancel >"$DMABUF_LOG" 2>&1
rg -q '^ok export-dmabuf (ready|cancel)' "$DMABUF_LOG"

echo "ok: export-dmabuf smoke passed (socket=$SOCKET log=$LOG)"

