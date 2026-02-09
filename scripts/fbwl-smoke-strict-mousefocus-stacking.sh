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
LOG="${LOG:-/tmp/fluxbox-wayland-strict-mousefocus-stacking-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-strict-mousefocus-stacking-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$CFGDIR/init" <<EOF
session.screen0.focusModel: StrictMouseFocus
session.screen0.windowPlacement: UnderMousePlacement
session.screen0.toolbar.visible: false
session.keyFile: mykeys
EOF

cat >"$CFGDIR/mykeys" <<EOF
Mod1 F2 :Lower
EOF

: >"$LOG"

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

# Ensure deterministic placement: UnderMousePlacement uses the current cursor position as the frame origin.
./fbwl-input-injector --socket "$SOCKET" motion 100 100 >/dev/null 2>&1 || true

./fbwl-smoke-client --socket "$SOCKET" --title stack-a --stay-ms 10000 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title stack-b --stay-ms 10000 >/dev/null 2>&1 &
B_PID=$!

timeout 5 bash -c "until rg -q 'Place: stack-a ' '$LOG' && rg -q 'Place: stack-b ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Focus: stack-a' '$LOG' && rg -q 'Focus: stack-b' '$LOG'; do sleep 0.05; done"

FOCUSED_VIEW=$(
  rg -o 'Focus: stack-[ab]' "$LOG" \
    | tail -n 1 \
    | awk '{print $2}'
)

case "$FOCUSED_VIEW" in
  stack-a) OTHER_VIEW=stack-b ;;
  stack-b) OTHER_VIEW=stack-a ;;
  *) echo "failed to determine focused view (got: $FOCUSED_VIEW)" >&2; exit 1 ;;
esac

# StrictMouseFocus should be able to shift focus on stacking changes even without pointer motion.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f2 >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 2 bash -c "until tail -c +$START '$LOG' | rg -q \"Lower: $FOCUSED_VIEW \"; do sleep 0.05; done"
timeout 2 bash -c "until tail -c +$START '$LOG' | rg -q \"Focus: $OTHER_VIEW\"; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f2 >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 2 bash -c "until tail -c +$START '$LOG' | rg -q \"Lower: $OTHER_VIEW \"; do sleep 0.05; done"
timeout 2 bash -c "until tail -c +$START '$LOG' | rg -q \"Focus: $FOCUSED_VIEW\"; do sleep 0.05; done"

echo "ok: strict-mousefocus stacking smoke passed (socket=$SOCKET log=$LOG)"
