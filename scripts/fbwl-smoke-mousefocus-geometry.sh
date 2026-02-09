#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout
need_cmd wc

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-mousefocus-geometry-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-mousefocus-geometry-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$CFGDIR/init" <<EOF
session.screen0.focusModel: MouseFocus
session.screen0.windowPlacement: UnderMousePlacement
session.screen0.toolbar.visible: false
session.keyFile: mykeys
EOF

cat >"$CFGDIR/mykeys" <<EOF
Mod1 F2 :Maximize
EOF

: >"$LOG"

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

POS_AX=600
POS_AY=400
POS_BX=100
POS_BY=100

# Deterministic placement: UnderMousePlacement uses the current cursor position as the frame origin.
./fbwl-input-injector --socket "$SOCKET" motion "$POS_AX" "$POS_AY" >/dev/null 2>&1 || true
./fbwl-smoke-client --socket "$SOCKET" --title geo-a --stay-ms 10000 >/dev/null 2>&1 &
A_PID=$!
timeout 5 bash -c "until rg -q 'Place: geo-a ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Focus: geo-a' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" motion "$POS_BX" "$POS_BY" >/dev/null 2>&1 || true
./fbwl-smoke-client --socket "$SOCKET" --title geo-b --stay-ms 10000 >/dev/null 2>&1 &
B_PID=$!
timeout 5 bash -c "until rg -q 'Place: geo-b ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Focus: geo-b' '$LOG'; do sleep 0.05; done"

# Maximize the focused window (geo-b) and wait for the size commit so the surface actually covers POS_A.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f2 >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 2 bash -c "until tail -c +$START '$LOG' | rg -q 'Maximize: geo-b on '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Surface size: geo-b '; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" motion "$POS_AX" "$POS_AY" >/dev/null 2>&1 || true

# MouseFocus should *not* shift focus on geometry-only changes without pointer motion.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f2 >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 2 bash -c "until tail -c +$START '$LOG' | rg -q 'Maximize: geo-b off'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Surface size: geo-b '; do sleep 0.05; done"

sleep 0.2

if tail -c +$START "$LOG" | rg -q 'Focus: geo-a'; then
  echo "unexpected focus shift to geo-a after unmaximize in MouseFocus model" >&2
  exit 1
fi

echo "ok: mousefocus geometry smoke passed (socket=$SOCKET log=$LOG)"

