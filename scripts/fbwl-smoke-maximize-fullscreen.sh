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
LOG="${LOG:-/tmp/fluxbox-wayland-maximize-fullscreen-$UID-$$.log}"

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

OUT_GEOM=$(
  rg -m1 'Output: ' "$LOG" \
    | awk '{print $NF}'
)
OUT_W=${OUT_GEOM%x*}
OUT_H=${OUT_GEOM#*x}

./fbwl-smoke-client --socket "$SOCKET" --title client-mf --stay-ms 10000 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Surface size: client-mf 32x32' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: client-mf ' "$LOG" || true)"
if [[ "$place_line" =~ usable=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+)[[:space:]] ]]; then
  USABLE_W="${BASH_REMATCH[3]}"
  USABLE_H="${BASH_REMATCH[4]}"
else
  echo "failed to parse Place line for client-mf: $place_line" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-m
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: client-mf on w=$USABLE_W h=$USABLE_H"
timeout 5 bash -c "until rg -q 'Surface size: client-mf ${USABLE_W}x${USABLE_H}' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-m
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: client-mf off"
timeout 5 bash -c "until rg -q 'Surface size: client-mf 32x32' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Fullscreen: client-mf on w=$OUT_W h=$OUT_H"
timeout 5 bash -c "until rg -q 'Surface size: client-mf ${OUT_W}x${OUT_H}' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Fullscreen: client-mf off"
timeout 5 bash -c "until rg -q 'Surface size: client-mf 32x32' '$LOG'; do sleep 0.05; done"

echo "ok: maximize/fullscreen smoke passed (socket=$SOCKET log=$LOG output=${OUT_W}x${OUT_H})"
