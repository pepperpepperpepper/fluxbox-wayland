#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1" >&2; exit 1; }
}

need_cmd awk
need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd wc

need_exe ./fbwl-input-injector
need_exe ./fbwl-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-autoraise-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-autoraise-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-autoraise-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

AUTO_RAISE_DELAY_MS=500

cat >"$CFGDIR/init" <<EOF
session.autoRaiseDelay: $AUTO_RAISE_DELAY_MS
session.screen0.toolbar.visible: false
session.screen0.focusModel: MouseFocus
session.screen0.focusNewWindows: false
session.screen0.autoRaise: true
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'OutputLayout: ' '$LOG'; do sleep 0.05; done"

OUT_GEOM=$(
  rg -m1 'Output: ' "$LOG" \
    | awk '{print $NF}'
)
OUT_W=${OUT_GEOM%x*}
OUT_H=${OUT_GEOM#*x}

OUTLINE="$(rg -m1 'OutputLayout: ' "$LOG")"
OUT_X="$(printf '%s\n' "$OUTLINE" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)"
OUT_Y="$(printf '%s\n' "$OUTLINE" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)"

# Ensure the pointer starts far away from where RowSmartPlacement is likely to place test windows.
./fbwl-input-injector --socket "$SOCKET" motion "$((OUT_X + OUT_W - 2))" "$((OUT_Y + OUT_H - 2))" >/dev/null 2>&1 || true

./fbwl-smoke-client --socket "$SOCKET" --title ar-a --stay-ms 20000 --xdg-decoration --width 240 --height 160 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title ar-b --stay-ms 20000 --xdg-decoration --width 240 --height 160 >/dev/null 2>&1 &
B_PID=$!

timeout 5 bash -c "until rg -q 'Place: ar-a ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: ar-b ' '$LOG'; do sleep 0.05; done"

place_a="$(rg -m1 'Place: ar-a ' "$LOG")"
place_b="$(rg -m1 'Place: ar-b ' "$LOG")"

if [[ "$place_a" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
  A_X="${BASH_REMATCH[1]}"
  A_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_a" >&2
  exit 1
fi

if [[ "$place_b" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
  B_X="${BASH_REMATCH[1]}"
  B_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_b" >&2
  exit 1
fi

# AutoRaiseDelay should apply only to the currently focused view, and should follow focus as it
# changes. Specifically: moving from A to B before the delay expires should prevent A raising.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" motion "$((A_X + 10))" "$((A_Y + 10))" >/dev/null 2>&1 || true
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Focus: ar-a'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" motion "$((B_X + 10))" "$((B_Y + 10))" >/dev/null 2>&1 || true
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Focus: ar-b'; do sleep 0.05; done"

timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Raise: ar-b reason=autoRaiseDelay'; do sleep 0.05; done"
if tail -c +$START "$LOG" | rg -q 'Raise: ar-a reason=autoRaiseDelay'; then
  echo "unexpected autoRaiseDelay raise for ar-a after focusing ar-b" >&2
  exit 1
fi

echo "ok: autoRaiseDelay smoke passed (socket=$SOCKET log=$LOG)"

