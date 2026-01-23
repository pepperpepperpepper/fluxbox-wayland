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
LOG="${LOG:-/tmp/fluxbox-wayland-layer-shell-$UID-$$.log}"

cleanup() {
  if [[ -n "${CL_PID:-}" ]]; then kill "$CL_PID" 2>/dev/null || true; fi
  if [[ -n "${LS_PID:-}" ]]; then kill "$LS_PID" 2>/dev/null || true; fi
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

PANEL_H=200
if (( PANEL_H > OUT_H - 100 )); then PANEL_H=$((OUT_H - 100)); fi
if (( PANEL_H < 128 )); then PANEL_H=128; fi
if (( PANEL_H > OUT_H - 32 )); then PANEL_H=$((OUT_H - 32)); fi
USABLE_H=$((OUT_H - PANEL_H))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-layer-shell-client \
  --socket "$SOCKET" \
  --namespace fbwl-panel \
  --layer top \
  --anchor top \
  --height "$PANEL_H" \
  --exclusive-zone "$PANEL_H" \
  --stay-ms 10000 \
  >/dev/null 2>&1 &
LS_PID=$!

timeout 5 bash -c "until rg -q 'LayerShell: map ns=fbwl-panel' '$LOG'; do sleep 0.05; done"

tail -c +$((OFFSET + 1)) "$LOG" | rg -q "LayerShell: surface ns=fbwl-panel layer=2 pos=0,0 size=${OUT_W}x${PANEL_H} excl=${PANEL_H}"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "LayerShell: output=[^ ]+ usable=0,${PANEL_H} ${OUT_W}x${USABLE_H}"

USABLE_PAIR=$(
  tail -c +$((OFFSET + 1)) "$LOG" \
    | rg -m1 'LayerShell: output=' \
    | rg -o 'usable=-?[0-9]+,-?[0-9]+' \
    | cut -d= -f2
)
USABLE_X=${USABLE_PAIR%,*}
USABLE_Y=${USABLE_PAIR#*,}

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title placed --stay-ms 10000 >/dev/null 2>&1 &
CL_PID=$!

timeout 5 bash -c "until rg -q 'Place: placed' '$LOG'; do sleep 0.05; done"

PLACED_LINE=$(tail -c +$((OFFSET + 1)) "$LOG" | rg 'Place: placed' | tail -n 1)
PLACED_X=$(echo "$PLACED_LINE" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
PLACED_Y=$(echo "$PLACED_LINE" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
if (( PLACED_X < USABLE_X )); then
  echo "placed view is left of usable area: $PLACED_LINE (usable_x=$USABLE_X)" >&2
  exit 1
fi
if (( PLACED_Y < USABLE_Y )); then
  echo "placed view is above usable area: $PLACED_LINE (usable_y=$USABLE_Y)" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click $((PLACED_X + 5)) $((PLACED_Y + 5)) >/dev/null 2>&1 || true
./fbwl-input-injector --socket "$SOCKET" key alt-m >/dev/null 2>&1
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: placed on w=${OUT_W} h=${USABLE_H}"

echo "ok: layer-shell smoke passed (socket=$SOCKET log=$LOG output=${OUT_W}x${OUT_H} panel_h=$PANEL_H)"
