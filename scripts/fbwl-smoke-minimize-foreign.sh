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
LOG="${LOG:-/tmp/fluxbox-wayland-minimize-foreign-$UID-$$.log}"

cleanup() {
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Output: ' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title client-min --stay-ms 10000 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Surface size: client-min 32x32' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: client-min ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: client-min ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  CLICK_X=$((BASH_REMATCH[1] + 5))
  CLICK_Y=$((BASH_REMATCH[2] + 5))
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Pointer press .* hit=client-min'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-foreign-toplevel-client --socket "$SOCKET" --timeout-ms 3000 minimize client-min
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Minimize: client-min on reason=foreign-request'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -qF 'hit=(none)'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-foreign-toplevel-client --socket "$SOCKET" --timeout-ms 3000 unminimize client-min
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Minimize: client-min off reason=foreign-request'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Pointer press .* hit=client-min'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-i
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Minimize: client-min on reason=keybinding'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -qF 'hit=(none)'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-i
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Minimize: client-min off reason=keybinding'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Pointer press .* hit=client-min'

echo "ok: minimize + foreign-toplevel smoke passed (socket=$SOCKET log=$LOG)"
