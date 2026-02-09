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
LOG="${LOG:-/tmp/fluxbox-wayland-maximize-axis-toggle-$UID-$$.log}"
KEYS_FILE="$(mktemp "/tmp/fbwl-keys-max-axis-$UID-XXXXXX.keys")"

cleanup() {
  rm -f "$KEYS_FILE" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$KEYS_FILE" <<'EOF'
Mod1 m :Maximize
Mod1 i :MaximizeHorizontal
Mod1 F1 :MaximizeVertical
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

tb_line="$(rg -m1 'Toolbar: position ' "$LOG")"
if [[ "$tb_line" =~ h=([0-9]+) ]]; then
  TITLE_H="${BASH_REMATCH[1]}"
else
  echo "failed to parse toolbar title height: $tb_line" >&2
  exit 1
fi

BORDER=4

TITLE="max-axis"
W_INIT=400
H_INIT=200
./fbwl-smoke-client --socket "$SOCKET" --title "$TITLE" --stay-ms 20000 --xdg-decoration --width "$W_INIT" --height "$H_INIT" >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Place: $TITLE ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Surface size: $TITLE ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 "Place: $TITLE " "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ usable=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+)[[:space:]] ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
  USABLE_W="${BASH_REMATCH[5]}"
  USABLE_H="${BASH_REMATCH[6]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

size_line="$(rg -m1 "Surface size: $TITLE " "$LOG")"
if [[ "$size_line" =~ ([0-9]+)x([0-9]+)$ ]]; then
  W0="${BASH_REMATCH[1]}"
  H0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Surface size line: $size_line" >&2
  exit 1
fi

# Ensure our test window is focused.
./fbwl-input-injector --socket "$SOCKET" click "$((X0 + 10))" "$((Y0 + 10))" >/dev/null 2>&1 || true

EXP_W=$((USABLE_W - 2 * BORDER))
EXP_H=$((USABLE_H - TITLE_H - 2 * BORDER))

# Full maximize -> toggle H off -> toggle V off (restore).
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-m
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: $TITLE on w=$EXP_W h=$EXP_H"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Surface size: $TITLE ${EXP_W}x${EXP_H}'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-i
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "MaximizeHorizontal: $TITLE off w=$W0 h=$EXP_H"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Surface size: $TITLE ${W0}x${EXP_H}'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f1
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "MaximizeVertical: $TITLE off w=$W0 h=$H0"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Surface size: $TITLE ${W0}x${H0}'; do sleep 0.05; done"

# Full maximize -> toggle V off -> toggle H off (restore).
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-m
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: $TITLE on w=$EXP_W h=$EXP_H"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Surface size: $TITLE ${EXP_W}x${EXP_H}'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f1
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "MaximizeVertical: $TITLE off w=$EXP_W h=$H0"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Surface size: $TITLE ${EXP_W}x${H0}'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-i
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "MaximizeHorizontal: $TITLE off w=$W0 h=$H0"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Surface size: $TITLE ${W0}x${H0}'; do sleep 0.05; done"

echo "ok: maximize axis-toggle parity smoke passed (socket=$SOCKET log=$LOG keys=$KEYS_FILE)"
