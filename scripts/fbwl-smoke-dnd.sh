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
LOG="${LOG:-/tmp/fluxbox-wayland-dnd-$UID-$$.log}"
SRC_LOG="${SRC_LOG:-/tmp/fbwl-dnd-src-$UID-$$.log}"
DST_LOG="${DST_LOG:-/tmp/fbwl-dnd-dst-$UID-$$.log}"

TEXT="fbwl-dnd-smoke"

cleanup() {
  if [[ -n "${SRC_PID:-}" ]]; then kill "$SRC_PID" 2>/dev/null || true; fi
  if [[ -n "${DST_PID:-}" ]]; then kill "$DST_PID" 2>/dev/null || true; fi
  if [[ -n "${HOLD_PID:-}" ]]; then kill "$HOLD_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
: >"$SRC_LOG"
: >"$DST_LOG"

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland --no-xwayland --socket "$SOCKET" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" hold 10000 >/dev/null 2>&1 &
HOLD_PID=$!
timeout 5 bash -c "until rg -q 'New virtual pointer' '$LOG'; do sleep 0.05; done"

./fbwl-dnd-client --role src --socket "$SOCKET" --text "$TEXT" --timeout-ms 7000 >"$SRC_LOG" 2>&1 &
SRC_PID=$!
timeout 5 bash -c "until rg -q '^fbwl-dnd-client: ready$' '$SRC_LOG'; do sleep 0.05; done"

./fbwl-dnd-client --role dst --socket "$SOCKET" --text "$TEXT" --timeout-ms 7000 >"$DST_LOG" 2>&1 &
DST_PID=$!
timeout 5 bash -c "until rg -q '^fbwl-dnd-client: ready$' '$DST_LOG'; do sleep 0.05; done"

timeout 5 bash -c "until rg -q 'Place: fbwl-dnd-src ' '$LOG' && rg -q 'Place: fbwl-dnd-dst ' '$LOG'; do sleep 0.05; done"
SRC_PLACE_LINE="$(rg -m1 'Place: fbwl-dnd-src ' "$LOG")"
DST_PLACE_LINE="$(rg -m1 'Place: fbwl-dnd-dst ' "$LOG")"

if [[ "$SRC_PLACE_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  SRC_X="${BASH_REMATCH[1]}"
  SRC_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $SRC_PLACE_LINE" >&2
  exit 1
fi
if [[ "$DST_PLACE_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  DST_X="${BASH_REMATCH[1]}"
  DST_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $DST_PLACE_LINE" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" drag-left "$((SRC_X + 10))" "$((SRC_Y + 10))" "$((DST_X + 10))" "$((DST_Y + 10))" >/dev/null

wait "$DST_PID"
rg -q '^ok dnd$' "$DST_LOG"
wait "$SRC_PID"
rg -q '^ok dnd source$' "$SRC_LOG"

echo "ok: dnd smoke passed (socket=$SOCKET log=$LOG)"
