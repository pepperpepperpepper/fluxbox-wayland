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
LOG="${LOG:-/tmp/fluxbox-wayland-grips-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-grips-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.screen0.workspaces: 1
session.keyFile: keys
session.styleFile: style
EOF

cat >"$CFGDIR/keys" <<'EOF'
OnLeftGrip Move1 :StartResizing bottomleft
OnRightGrip Move1 :StartResizing bottomright
EOF

cat >"$CFGDIR/style" <<'EOF'
borderWidth: 4
window.title.height: 20
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded .*\\(border=4 title_h=20\\)' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --xdg-decoration --title client-grip --stay-ms 10000 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Surface size: client-grip ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: client-grip ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: client-grip ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

size_line="$(rg -m1 'Surface size: client-grip ' "$LOG")"
if [[ "$size_line" =~ ([0-9]+)x([0-9]+) ]]; then
  W0="${BASH_REMATCH[1]}"
  H0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Surface size line: $size_line" >&2
  exit 1
fi

# Move the window away from the output edge so a left-grip resize can drag left
# without cursor clamping.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
MOVE_START_X=$((X0 + 10))
MOVE_START_Y=$((Y0 + 10))
MOVE_END_X=$((MOVE_START_X + 200))
MOVE_END_Y=$MOVE_START_Y
./fbwl-input-injector --socket "$SOCKET" drag-alt-left "$MOVE_START_X" "$MOVE_START_Y" "$MOVE_END_X" "$MOVE_END_Y"
X0=$((X0 + 200))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Move: client-grip x=$X0 y=$Y0"

# Right grip: bottomright + drag right increases width.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
START_RX=$((X0 + W0))
START_RY=$((Y0 + H0))
END_RX=$((START_RX + 40))
END_RY=$START_RY
./fbwl-input-injector --socket "$SOCKET" drag-left "$START_RX" "$START_RY" "$END_RX" "$END_RY"
W1=$((W0 + 40))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: client-grip w=$W1 h=$H0"

# Left grip: bottomleft + drag left increases width.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
START_LX=$((X0 - 1))
START_LY=$((Y0 + H0))
END_LX=$((START_LX - 40))
END_LY=$START_LY
./fbwl-input-injector --socket "$SOCKET" drag-left "$START_LX" "$START_LY" "$END_LX" "$END_LY"
W2=$((W1 + 40))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: client-grip w=$W2 h=$H0"

echo "ok: grips mouse binding context smoke passed (socket=$SOCKET log=$LOG cfgdir=$CFGDIR)"
