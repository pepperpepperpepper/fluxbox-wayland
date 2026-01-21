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
LOG="${LOG:-/tmp/fluxbox-wayland-move-resize-$UID-$$.log}"

cleanup() {
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
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

./fbwl-smoke-client --socket "$SOCKET" --title client-mr --stay-ms 10000 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Surface size: client-mr 32x32' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: client-mr ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: client-mr ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

size_line="$(rg -m1 'Surface size: client-mr ' "$LOG")"
if [[ "$size_line" =~ ([0-9]+)x([0-9]+) ]]; then
  W0="${BASH_REMATCH[1]}"
  H0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Surface size line: $size_line" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-alt-left 70 70 170 170
X1=$((X0 + 100))
Y1=$((Y0 + 100))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Move: client-mr x=$X1 y=$Y1"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-alt-right 190 190 240 250
W1=$((W0 + 50))
H1=$((H0 + 60))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: client-mr w=$W1 h=$H1"

timeout 5 bash -c "until rg -q 'Surface size: client-mr ${W1}x${H1}' '$LOG'; do sleep 0.05; done"

echo "ok: move/resize smoke passed (socket=$SOCKET log=$LOG)"
