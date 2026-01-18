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
LOG="${LOG:-/tmp/fluxbox-wayland-output-management-$UID-$$.log}"
OM_LOG="${OM_LOG:-/tmp/fbwl-output-management-$UID-$$.log}"

cleanup() {
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
: >"$OM_LOG"

WLR_BACKENDS=headless \
WLR_RENDERER=pixman \
WLR_HEADLESS_OUTPUTS=2 \
./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until [[ \$(rg -c 'OutputLayout:' '$LOG') -ge 2 ]]; do sleep 0.05; done"

./fbwl-output-management-client --socket "$SOCKET" --timeout-ms 6000 --expect-heads 2 --target-index 1 --delta-y 120 >"$OM_LOG" 2>&1
rg -q '^ok output-management moved ' "$OM_LOG"

echo "ok: output-management smoke passed (socket=$SOCKET log=$LOG)"

