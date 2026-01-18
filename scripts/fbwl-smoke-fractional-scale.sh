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
LOG="${LOG:-/tmp/fluxbox-wayland-fractional-scale-$UID-$$.log}"
FS_LOG="${FS_LOG:-/tmp/fbwl-fractional-scale-$UID-$$.log}"

cleanup() {
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
: >"$FS_LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-fractional-scale-client --socket "$SOCKET" --timeout-ms 3000 >"$FS_LOG" 2>&1
rg -q '^ok fractional_scale preferred_scale=[0-9]+$' "$FS_LOG"

echo "ok: fractional-scale smoke passed (socket=$SOCKET log=$LOG)"

