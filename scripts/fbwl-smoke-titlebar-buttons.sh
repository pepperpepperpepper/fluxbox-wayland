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
LOG="${LOG:-/tmp/fluxbox-wayland-titlebar-buttons-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-titlebar-buttons-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${MAIN_PID:-}" ]]; then kill "$MAIN_PID" 2>/dev/null || true; fi
  if [[ -n "${MENU_PID:-}" ]]; then kill "$MENU_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
# Verify per-screen overrides take precedence.
session.titlebar.left: Stick
session.titlebar.right: Shade Minimize Maximize Close
session.screen0.titlebar.left: MenuIcon Stick
session.screen0.titlebar.right: Shade Minimize Maximize LHalf RHalf Close
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

pos_line="$(rg -m1 'Toolbar: position ' "$LOG")"
if [[ "$pos_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ h=([0-9]+) ]]; then
  TB_X="${BASH_REMATCH[1]}"
  TB_Y="${BASH_REMATCH[2]}"
  TB_H="${BASH_REMATCH[3]}"
else
  echo "failed to parse Toolbar: position line: $pos_line" >&2
  exit 1
fi

TITLE_H=24
BTN_MARGIN=4
BTN_SPACING="$BTN_MARGIN"
BTN_SIZE=$((TITLE_H - 2 * BTN_MARGIN))
BORDER=4

MAIN_TITLE="tb-main"
MAIN_W=300
MAIN_H=200
./fbwl-smoke-client --socket "$SOCKET" --title "$MAIN_TITLE" --stay-ms 20000 --xdg-decoration --width "$MAIN_W" --height "$MAIN_H" >/dev/null 2>&1 &
MAIN_PID=$!

timeout 5 bash -c "until rg -q 'Decor: title-render $MAIN_TITLE' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Surface size: $MAIN_TITLE ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: $MAIN_TITLE ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: iconbar item .*title=$MAIN_TITLE' '$LOG'; do sleep 0.05; done"

size_line="$(rg -m1 "Surface size: $MAIN_TITLE " "$LOG")"
if [[ "$size_line" =~ ([0-9]+)x([0-9]+)$ ]]; then
  W0="${BASH_REMATCH[1]}"
  H0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Surface size line: $size_line" >&2
  exit 1
fi

place_line="$(rg -m1 "Place: $MAIN_TITLE " "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ usable=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+)[[:space:]] ]]; then
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

MAX_W=$((USABLE_W - 2 * BORDER))
MAX_H=$((USABLE_H - TITLE_H - 2 * BORDER))

btn_cy_for_y() {
  local vy="$1"
  echo $((vy - TITLE_H + BTN_MARGIN + BTN_SIZE / 2))
}

btn_right_x0() {
  local w="$1"
  echo $((w - BTN_MARGIN - BTN_SIZE))
}

btn_left_x0() {
  local idx="$1"
  echo $((BTN_MARGIN + idx * (BTN_SIZE + BTN_SPACING)))
}

right_x0_by_idx_from_right() {
  local w="$1"
  local idx="$2"
  local close_x0
  close_x0="$(btn_right_x0 "$w")"
  echo $((close_x0 - idx * (BTN_SIZE + BTN_SPACING)))
}

# Button indexes per init:
# Left: 0=MenuIcon 1=Stick
# Right (left-to-right): Shade Minimize Maximize LHalf RHalf Close
IDX_MENU=0
IDX_STICK=1
IDX_SHADE_FROM_RIGHT=5
IDX_MIN_FROM_RIGHT=4
IDX_MAX_FROM_RIGHT=3
IDX_LHALF_FROM_RIGHT=2
IDX_RHALF_FROM_RIGHT=1
IDX_CLOSE_FROM_RIGHT=0

# Stick toggle.
BTN_CY="$(btn_cy_for_y "$Y0")"
STICK_X0="$(btn_left_x0 "$IDX_STICK")"
STICK_CX=$((X0 + STICK_X0 + BTN_SIZE / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$STICK_CX" "$BTN_CY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Stick: $MAIN_TITLE on"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$STICK_CX" "$BTN_CY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Stick: $MAIN_TITLE off"

# Shade toggle.
SHADE_X0="$(right_x0_by_idx_from_right "$W0" "$IDX_SHADE_FROM_RIGHT")"
SHADE_CX=$((X0 + SHADE_X0 + BTN_SIZE / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$SHADE_CX" "$BTN_CY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Shade: $MAIN_TITLE on reason=decor-button"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$SHADE_CX" "$BTN_CY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Shade: $MAIN_TITLE off reason=decor-button"

# Minimize via titlebar, restore via toolbar iconbar click.
MIN_X0="$(right_x0_by_idx_from_right "$W0" "$IDX_MIN_FROM_RIGHT")"
MIN_CX=$((X0 + MIN_X0 + BTN_SIZE / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$MIN_CX" "$BTN_CY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Minimize: $MAIN_TITLE on reason=decor-button"
timeout 5 bash -c "until rg -q \"Toolbar: iconbar item .*title=$MAIN_TITLE minimized=1\" '$LOG'; do sleep 0.05; done"

item_line="$(rg "Toolbar: iconbar item .*title=$MAIN_TITLE" "$LOG" | tail -n 1)"
if [[ "$item_line" =~ lx=([-0-9]+)\ w=([0-9]+)\ title= ]]; then
  LX="${BASH_REMATCH[1]}"
  IW="${BASH_REMATCH[2]}"
else
  echo "failed to parse Toolbar: iconbar item line: $item_line" >&2
  exit 1
fi
ICON_CX=$((TB_X + LX + IW / 2))
ICON_CY=$((TB_Y + TB_H / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$ICON_CX" "$ICON_CY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Minimize: $MAIN_TITLE off reason=toolbar-iconbar"

# Maximize on/off via titlebar.
MAX_X0="$(right_x0_by_idx_from_right "$W0" "$IDX_MAX_FROM_RIGHT")"
MAX_CX=$((X0 + MAX_X0 + BTN_SIZE / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$MAX_CX" "$BTN_CY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: $MAIN_TITLE on w=$MAX_W h=$MAX_H"
timeout 5 bash -c "until rg -q 'Surface size: $MAIN_TITLE ${MAX_W}x${MAX_H}' '$LOG'; do sleep 0.05; done"

MAXED_X=$((USABLE_X + BORDER))
MAXED_Y=$((USABLE_Y + TITLE_H + BORDER))
MAX_CY_MAXED="$(btn_cy_for_y "$MAXED_Y")"
MAX_X0_MAXED="$(right_x0_by_idx_from_right "$MAX_W" "$IDX_MAX_FROM_RIGHT")"
MAX_CX_MAXED=$((MAXED_X + MAX_X0_MAXED + BTN_SIZE / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$MAX_CX_MAXED" "$MAX_CY_MAXED"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: $MAIN_TITLE off"
timeout 5 bash -c "until rg -q 'Surface size: $MAIN_TITLE ${W0}x${H0}' '$LOG'; do sleep 0.05; done"

# LHalf / RHalf tiling via titlebar.
L_W=$((USABLE_W / 2))
R_W=$((USABLE_W - L_W))
L_FRAME_W="$L_W"
R_FRAME_W="$R_W"
L_W=$((L_FRAME_W - 2 * BORDER))
R_W=$((R_FRAME_W - 2 * BORDER))
TILE_H=$((USABLE_H - TITLE_H - 2 * BORDER))

LHALF_X0="$(right_x0_by_idx_from_right "$W0" "$IDX_LHALF_FROM_RIGHT")"
LHALF_CX=$((X0 + LHALF_X0 + BTN_SIZE / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$LHALF_CX" "$BTN_CY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Tile: $MAIN_TITLE lhalf w=$L_W h=$TILE_H"
timeout 5 bash -c "until rg -q 'Surface size: $MAIN_TITLE ${L_W}x${TILE_H}' '$LOG'; do sleep 0.05; done"

TILE_X=$((USABLE_X + BORDER))
TILE_Y=$((USABLE_Y + TITLE_H + BORDER))
RHALF_CY="$(btn_cy_for_y "$TILE_Y")"
RHALF_X0="$(right_x0_by_idx_from_right "$L_W" "$IDX_RHALF_FROM_RIGHT")"
RHALF_CX=$((TILE_X + RHALF_X0 + BTN_SIZE / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$RHALF_CX" "$RHALF_CY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Tile: $MAIN_TITLE rhalf w=$R_W h=$TILE_H"
timeout 5 bash -c "until rg -q 'Surface size: $MAIN_TITLE ${R_W}x${TILE_H}' '$LOG'; do sleep 0.05; done"

# Close via titlebar (after rhalf, position is deterministic).
R_X=$((USABLE_X + L_FRAME_W + BORDER))
CLOSE_CY="$RHALF_CY"
CLOSE_X0="$(right_x0_by_idx_from_right "$R_W" "$IDX_CLOSE_FROM_RIGHT")"
CLOSE_CX=$((R_X + CLOSE_X0 + BTN_SIZE / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLOSE_CX" "$CLOSE_CY"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Pointer press"
timeout 5 bash -c "while kill -0 '$MAIN_PID' 2>/dev/null; do sleep 0.05; done"

# MenuIcon click opens the window menu (tested on a separate window to avoid menu state interference).
MENU_TITLE="tb-menu"
./fbwl-smoke-client --socket "$SOCKET" --title "$MENU_TITLE" --stay-ms 10000 --xdg-decoration --width 200 --height 140 >/dev/null 2>&1 &
MENU_PID=$!

timeout 5 bash -c "until rg -q 'Place: $MENU_TITLE ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 "Place: $MENU_TITLE " "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  MX="${BASH_REMATCH[1]}"
  MY="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line for $MENU_TITLE: $place_line" >&2
  exit 1
fi

MENU_X0="$(btn_left_x0 "$IDX_MENU")"
MENU_CX=$((MX + MENU_X0 + BTN_SIZE / 2))
MENU_CY="$(btn_cy_for_y "$MY")"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$MENU_CX" "$MENU_CY"
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 "Menu: open-window title=$MENU_TITLE")"
if [[ "$open_line" =~ items=([0-9]+) ]]; then
  MENU_ITEMS="${BASH_REMATCH[1]}"
else
  echo "failed to parse menu items from open line: $open_line" >&2
  exit 1
fi

CLOSE_IDX=$((MENU_ITEMS - 1))
CLICK_X=$((MENU_CX + 10))
CLICK_Y=$((MENU_CY + TITLE_H + TITLE_H * CLOSE_IDX + TITLE_H / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Menu: window-close title=$MENU_TITLE"
timeout 5 bash -c "while kill -0 '$MENU_PID' 2>/dev/null; do sleep 0.05; done"

echo "ok: titlebar buttons smoke passed (socket=$SOCKET log=$LOG)"
