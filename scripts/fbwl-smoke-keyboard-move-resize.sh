#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-keyboard-move-resize-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-keyboard-move-resize-$UID-XXXXXX")"
KEYS_FILE="$CFGDIR/keys"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.screen0.toolbar.visible: false
session.screen0.edgeSnapThreshold: 0
session.screen0.edgeResizeSnapThreshold: 0
EOF

cat >"$KEYS_FILE" <<EOF
Mod1 1 :StartMoving
Mod1 2 :StartResizing BottomRight
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title client-kbd-mr --stay-ms 10000 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Surface size: client-kbd-mr [0-9]+x[0-9]+' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: client-kbd-mr ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Focus: client-kbd-mr' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: client-kbd-mr ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

size_line="$(rg -m1 'Surface size: client-kbd-mr ' "$LOG")"
if [[ "$size_line" =~ ([0-9]+)x([0-9]+) ]]; then
  W0="${BASH_REMATCH[1]}"
  H0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Surface size line: $size_line" >&2
  exit 1
fi

# Cancel a keyboard move grab with Escape (should restore original x/y).
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key escape
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Move: cancel client-kbd-mr x=$X0 y=$Y0"

# Commit a keyboard move grab with Enter.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key enter
X1=$((X0 + 30))
Y1=$((Y0 + 20))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Move: client-kbd-mr x=$X1 y=$Y1"

# Commit a keyboard resize grab with Enter.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key enter
W1=$((W0 + 40))
H1=$((H0 + 30))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: client-kbd-mr w=$W1 h=$H1"
timeout 5 bash -c "until rg -q 'Surface size: client-kbd-mr ${W1}x${H1}' '$LOG'; do sleep 0.05; done"

echo "ok: keyboard move/resize smoke passed (socket=$SOCKET log=$LOG)"
