#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd wc

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-geometry-cmds-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-geometry-cmds-$UID-XXXXXX")"
KEYS_FILE="$CFGDIR/keys"

BORDER=6
TITLE_H=21

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.styleFile: mystyle
session.screen0.toolbar.visible: false
session.screen0.edgeSnapThreshold: 0
session.screen0.edgeResizeSnapThreshold: 0
session.keyFile: keys
EOF

cat >"$CFGDIR/mystyle" <<EOF
window.borderWidth: $BORDER
window.title.height: $TITLE_H
EOF

cat >"$KEYS_FILE" <<EOF
Mod1 1 :MoveTo 0 0
Mod1 2 :MoveTo 0 0 BottomRight
Mod1 3 :MoveTo * 0
Mod1 4 :Move 10 20
Mod1 5 :MoveTo 50% 0
Mod1 6 :ResizeTo 500 300
Mod1 7 :Resize 10 5
Mod1 8 :ResizeHorizontal 10
Mod1 9 :ResizeVertical 10
Mod1 f :ResizeTo 50% 50%
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded .*mystyle \\(border=$BORDER title_h=$TITLE_H\\)' '$LOG'; do sleep 0.05; done"

TITLE="geo-cmds"
W_INIT=400
H_INIT=200
./fbwl-smoke-client \
  --socket "$SOCKET" \
  --title "$TITLE" \
  --stay-ms 20000 \
  --xdg-decoration \
  --width "$W_INIT" \
  --height "$H_INIT" \
  >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Place: $TITLE ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Surface size: $TITLE ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 "Place: $TITLE " "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]]usable=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+)[[:space:]] ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
  USABLE_X="${BASH_REMATCH[3]}"
  USABLE_Y="${BASH_REMATCH[4]}"
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

USABLE_RIGHT=$((USABLE_X + USABLE_W))
USABLE_BOTTOM=$((USABLE_Y + USABLE_H))

# Ensure our test window is focused.
./fbwl-input-injector --socket "$SOCKET" click "$((X0 + 10))" "$((Y0 + 10))" >/dev/null 2>&1 || true

# MoveTo 0 0 (TopLeft anchor).
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1 >/dev/null 2>&1
EXP_X=$((USABLE_X + BORDER))
EXP_Y=$((USABLE_Y + TITLE_H + BORDER))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "MoveTo: $TITLE x=$EXP_X y=$EXP_Y anchor=topleft"
CUR_X=$EXP_X
CUR_Y=$EXP_Y

# MoveTo 0 0 BottomRight.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2 >/dev/null 2>&1
EXP_X=$((USABLE_RIGHT - W0 - BORDER))
EXP_Y=$((USABLE_BOTTOM - H0 - BORDER))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "MoveTo: $TITLE x=$EXP_X y=$EXP_Y anchor=bottomright"
CUR_X=$EXP_X
CUR_Y=$EXP_Y

# MoveTo * 0 (ignore x, move y only).
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-3 >/dev/null 2>&1
EXP_X=$CUR_X
EXP_Y=$((USABLE_Y + TITLE_H + BORDER))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "MoveTo: $TITLE x=$EXP_X y=$EXP_Y anchor=topleft"
CUR_Y=$EXP_Y

# Move 10 20.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-4 >/dev/null 2>&1
EXP_X=$((CUR_X + 10))
EXP_Y=$((CUR_Y + 20))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Move: $TITLE x=$EXP_X y=$EXP_Y dx=10 dy=20"
CUR_X=$EXP_X
CUR_Y=$EXP_Y

# MoveTo 50% 0.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-5 >/dev/null 2>&1
X_PCT=$(((50 * USABLE_W + 50) / 100))
EXP_X=$((USABLE_X + X_PCT + BORDER))
EXP_Y=$((USABLE_Y + TITLE_H + BORDER))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "MoveTo: $TITLE x=$EXP_X y=$EXP_Y anchor=topleft"
CUR_X=$EXP_X
CUR_Y=$EXP_Y

# ResizeTo 500 300 (inner size, height includes titlebar).
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-6 >/dev/null 2>&1
EXP_W=500
EXP_H=$((300 - TITLE_H))
if ((EXP_H < 1)); then EXP_H=1; fi
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "ResizeTo: $TITLE w=$EXP_W h=$EXP_H"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Surface size: $TITLE ${EXP_W}x${EXP_H}'; do sleep 0.05; done"
CUR_W=$EXP_W
CUR_H=$EXP_H

# Resize 10 5.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-7 >/dev/null 2>&1
EXP_W=$((CUR_W + 10))
EXP_H=$((CUR_H + 5))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: $TITLE w=$EXP_W h=$EXP_H"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Surface size: $TITLE ${EXP_W}x${EXP_H}'; do sleep 0.05; done"
CUR_W=$EXP_W
CUR_H=$EXP_H

# ResizeHorizontal 10.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-8 >/dev/null 2>&1
EXP_W=$((CUR_W + 10))
EXP_H=$CUR_H
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: $TITLE w=$EXP_W h=$EXP_H"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Surface size: $TITLE ${EXP_W}x${EXP_H}'; do sleep 0.05; done"
CUR_W=$EXP_W

# ResizeVertical 10.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-9 >/dev/null 2>&1
EXP_W=$CUR_W
EXP_H=$((CUR_H + 10))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: $TITLE w=$EXP_W h=$EXP_H"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Surface size: $TITLE ${EXP_W}x${EXP_H}'; do sleep 0.05; done"
CUR_H=$EXP_H

# ResizeTo 50% 50%.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f >/dev/null 2>&1
INNER_W=$((((50 * USABLE_W + 50) / 100) - 2 * BORDER))
INNER_H=$((((50 * USABLE_H + 50) / 100) - 2 * BORDER))
if ((INNER_W < 1)); then INNER_W=1; fi
if ((INNER_H < 1)); then INNER_H=1; fi
EXP_W=$INNER_W
EXP_H=$((INNER_H - TITLE_H))
if ((EXP_H < 1)); then EXP_H=1; fi
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "ResizeTo: $TITLE w=$EXP_W h=$EXP_H"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Surface size: $TITLE ${EXP_W}x${EXP_H}'; do sleep 0.05; done"

echo "ok: geometry commands smoke passed (socket=$SOCKET log=$LOG)"
