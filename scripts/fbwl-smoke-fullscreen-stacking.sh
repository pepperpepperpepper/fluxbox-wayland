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
LOG="${LOG:-/tmp/fluxbox-wayland-fullscreen-stacking-$UID-$$.log}"

cleanup() {
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title client-a --stay-ms 10000 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title client-b --stay-ms 10000 >/dev/null 2>&1 &
B_PID=$!

timeout 5 bash -c "until rg -q 'Place: client-a' '$LOG' && rg -q 'Place: client-b' '$LOG'; do sleep 0.05; done"

PLACED_A=$(rg 'Place: client-a' "$LOG" | tail -n 1)
AX=$(echo "$PLACED_A" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
AY=$(echo "$PLACED_A" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)

PLACED_B=$(rg 'Place: client-b' "$LOG" | tail -n 1)
BX=$(echo "$PLACED_B" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
BY=$(echo "$PLACED_B" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)

CLICK_AX=$((AX + 5))
CLICK_AY=$((AY + 5))
CLICK_BX=$((BX + 5))
CLICK_BY=$((BY + 5))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_BX" "$CLICK_BY" "$CLICK_AX" "$CLICK_AY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Focus: client-a'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Fullscreen: client-a on'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f1
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Focus: client-b'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_BX" "$CLICK_BY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Pointer press .* hit=client-a'

echo "ok: fullscreen stacking smoke passed (socket=$SOCKET log=$LOG)"
