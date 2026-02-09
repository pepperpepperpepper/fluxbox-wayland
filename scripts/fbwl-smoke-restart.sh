#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout
need_cmd wc

if [[ ! -x ./fbwl-remote ]]; then
  echo "missing ./fbwl-remote (build first)" >&2
  exit 1
fi

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-restart-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-restart-$UID-$$.log}"

cleanup() {
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until ./fbwl-remote --socket \"$SOCKET\" ping | rg -q '^ok pong$'; do sleep 0.05; done"

OFFSET="$(wc -c <"$LOG" | tr -d ' ')"
./fbwl-remote --socket "$SOCKET" restart | rg -q '^ok restarting$'

timeout 10 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Running fluxbox-wayland'; do sleep 0.05; done"
timeout 10 bash -c "until ./fbwl-remote --socket \"$SOCKET\" ping | rg -q '^ok pong$'; do sleep 0.05; done"

echo "ok: restart smoke passed (socket=$SOCKET log=$LOG)"

