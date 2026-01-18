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
LOG="${LOG:-/tmp/fluxbox-wayland-text-input-$UID-$$.log}"
TI_LOG="${TI_LOG:-/tmp/fbwl-text-input-$UID-$$.log}"
IM_LOG="${IM_LOG:-/tmp/fbwl-input-method-$UID-$$.log}"
COMMIT_TEXT="${COMMIT_TEXT:-fbwl-ime-test-$UID-$$}"

cleanup() {
  if [[ -n "${IM_PID:-}" ]]; then kill "$IM_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
: >"$TI_LOG"
: >"$IM_LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman \
./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-input-method-client --socket "$SOCKET" --timeout-ms 8000 --commit "$COMMIT_TEXT" >"$IM_LOG" 2>&1 &
IM_PID=$!

./fbwl-text-input-client --socket "$SOCKET" --timeout-ms 8000 --title text-input --expect-commit "$COMMIT_TEXT" >"$TI_LOG" 2>&1
wait "$IM_PID"

rg -q '^ok input-method committed$' "$IM_LOG"
rg -q "^ok text-input commit=${COMMIT_TEXT}\$" "$TI_LOG"

echo "ok: text-input + input-method smoke passed (socket=$SOCKET log=$LOG)"

