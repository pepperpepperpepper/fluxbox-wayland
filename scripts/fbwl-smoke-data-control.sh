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
LOG="${LOG:-/tmp/fluxbox-wayland-data-control-$UID-$$.log}"

cleanup() {
  if [[ -n "${SET_PID:-}" ]]; then kill "$SET_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

run_one() {
  local proto="$1"
  local text="fbwl-data-control-smoke-$proto"
  local set_log="/tmp/fbwl-data-control-set-$proto-$UID-$$.log"
  : >"$set_log"

  ./fbwl-data-control-client --socket "$SOCKET" --protocol "$proto" --set "$text" --stay-ms 10000 --timeout-ms 5000 >"$set_log" 2>&1 &
  SET_PID=$!

  timeout 5 bash -c "until rg -q '^ok selection_set$' '$set_log'; do sleep 0.05; done"

  local out
  out="$(./fbwl-data-control-client --socket "$SOCKET" --protocol "$proto" --get --timeout-ms 5000)"
  if [[ "$out" != "$text" ]]; then
    echo "data-control($proto) get mismatch: expected '$text' got '$out'" >&2
    exit 1
  fi

  kill "$SET_PID" 2>/dev/null || true
  wait "$SET_PID" 2>/dev/null || true
  unset SET_PID
}

run_one ext
run_one wlr

echo "ok: data-control smoke passed (socket=$SOCKET log=$LOG)"

