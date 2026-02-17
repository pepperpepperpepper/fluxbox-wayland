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
LOG="${LOG:-/tmp/fluxbox-wayland-startresizing-args-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-startresizing-args-$UID-XXXXXX")"
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

cat >"$KEYS_FILE" <<'EOF'
Mod1 1 :StartResizing NearestEdge
Mod1 2 :StartResizing NearestCorner
Mod1 3 :StartResizing NearestCornerOrEdge 10 0
Mod1 4 :StartResizing Center
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --xdg-decoration --title client-sr-args --stay-ms 20000 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Surface size: client-sr-args [0-9]+x[0-9]+' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: client-sr-args ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Focus: client-sr-args' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: client-sr-args ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

size_line="$(rg -m1 'Surface size: client-sr-args ' "$LOG")"
if [[ "$size_line" =~ ([0-9]+)x([0-9]+) ]]; then
  W0="${BASH_REMATCH[1]}"
  H0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Surface size line: $size_line" >&2
  exit 1
fi

W="$W0"
H="$H0"

assert_resize() {
  local offset="$1"
  local want_w="$2"
  local want_h="$3"
  tail -c +$((offset + 1)) "$LOG" | rg -q "Resize: client-sr-args w=$want_w h=$want_h"
  timeout 5 bash -c "until rg -q 'Surface size: client-sr-args ${want_w}x${want_h}' '$LOG'; do sleep 0.05; done"
}

# NearestEdge: choose RIGHT edge (cursor on right-middle). Down arrows must not change height.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
RX=$((X0 + W - 1))
RY=$((Y0 + H / 2))
./fbwl-input-injector --socket "$SOCKET" motion "$RX" "$RY" >/dev/null 2>&1
./fbwl-input-injector --socket "$SOCKET" key alt-1
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key enter
W=$((W + 40))
assert_resize "$OFFSET" "$W" "$H"

# NearestCorner: choose BOTTOMRIGHT (cursor on bottom-right). Both width and height must change.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
BRX=$((X0 + W - 1))
BRY=$((Y0 + H - 1))
./fbwl-input-injector --socket "$SOCKET" motion "$BRX" "$BRY" >/dev/null 2>&1
./fbwl-input-injector --socket "$SOCKET" key alt-2
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key enter
W=$((W + 40))
H=$((H + 30))
assert_resize "$OFFSET" "$W" "$H"

# NearestCornerOrEdge (10px corner region): right-middle => edge-only, so Down must not change height.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
RX=$((X0 + W - 1))
RY=$((Y0 + H / 2))
./fbwl-input-injector --socket "$SOCKET" motion "$RX" "$RY" >/dev/null 2>&1
./fbwl-input-injector --socket "$SOCKET" key alt-3
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key enter
W=$((W + 40))
assert_resize "$OFFSET" "$W" "$H"

# NearestCornerOrEdge (10px corner region): bottom-right => corner, so Down changes height.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
BRX=$((X0 + W - 1))
BRY=$((Y0 + H - 1))
./fbwl-input-injector --socket "$SOCKET" motion "$BRX" "$BRY" >/dev/null 2>&1
./fbwl-input-injector --socket "$SOCKET" key alt-3
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key down
./fbwl-input-injector --socket "$SOCKET" key enter
W=$((W + 40))
H=$((H + 30))
assert_resize "$OFFSET" "$W" "$H"

# Center: width and height must increase together (fluxbox CENTERRESIZE behaviour).
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
CX=$((X0 + W / 2))
CY=$((Y0 + H / 2))
./fbwl-input-injector --socket "$SOCKET" motion "$CX" "$CY" >/dev/null 2>&1
./fbwl-input-injector --socket "$SOCKET" key alt-4
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key right
./fbwl-input-injector --socket "$SOCKET" key enter
W=$((W + 40))
H=$((H + 40))
assert_resize "$OFFSET" "$W" "$H"

echo "ok: StartResizing args parity smoke passed (socket=$SOCKET log=$LOG cfgdir=$CFGDIR)"

