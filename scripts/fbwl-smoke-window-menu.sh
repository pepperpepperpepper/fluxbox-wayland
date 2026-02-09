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
LOG="${LOG:-/tmp/fluxbox-wayland-window-menu-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-window-menu-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<'EOF'
session.menuDelay: 0
session.screen0.windowMenu: windowmenu-custom
EOF

cat >"$CFGDIR/windowmenu-custom" <<'EOF'
[begin]
  [shade]
  [stick]
  [raise]
  [lower]
  [settitledialog]
  [sendto]
  [layer]
  [alpha]
  [separator]
  [close]
[end]
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  --workspaces 3 \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

toolbar_line="$(rg -m1 'Toolbar: position ' "$LOG")"
if [[ "$toolbar_line" =~ h=([0-9]+) ]]; then
  TITLE_H="${BASH_REMATCH[1]}"
else
  echo "failed to parse toolbar title height: $toolbar_line" >&2
  exit 1
fi

./fbwl-smoke-client --socket "$SOCKET" --title client-winmenu --stay-ms 20000 --xdg-decoration >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Place: client-winmenu ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: client-winmenu ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

TB_X=$((X0 + 10))
TB_Y=$((Y0 - TITLE_H + 2))

open_menu_expect_items() {
  local expected_items="$1"
  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-input-injector --socket "$SOCKET" drag-right "$TB_X" "$TB_Y" "$TB_X" "$TB_Y"
  open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open-window title=')"
  if [[ "$open_line" =~ items=([0-9]+) ]]; then
    MENU_ITEMS="${BASH_REMATCH[1]}"
  else
    echo "failed to parse menu items from open line: $open_line" >&2
    exit 1
  fi
  if [[ "$MENU_ITEMS" != "$expected_items" ]]; then
    echo "unexpected window menu items=$MENU_ITEMS (expected $expected_items) open_line=$open_line" >&2
    exit 1
  fi
}

click_menu_item() {
  local idx="$1"
  local click_x=$((TB_X + 10))
  local click_y=$((TB_Y + TITLE_H * idx + TITLE_H / 2))
  ./fbwl-input-injector --socket "$SOCKET" click "$click_x" "$click_y"
}

EXPECTED_TOP_ITEMS=10

# Shade toggle (on/off).
open_menu_expect_items "$EXPECTED_TOP_ITEMS"
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 0
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Shade: client-winmenu on reason=window-menu'

open_menu_expect_items "$EXPECTED_TOP_ITEMS"
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 0
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Shade: client-winmenu off reason=window-menu'

# Stick toggle (on/off).
open_menu_expect_items "$EXPECTED_TOP_ITEMS"
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 1
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Stick: client-winmenu on'

open_menu_expect_items "$EXPECTED_TOP_ITEMS"
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 1
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Stick: client-winmenu off'

# Raise / Lower.
open_menu_expect_items "$EXPECTED_TOP_ITEMS"
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 2
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Raise: client-winmenu reason=window-menu'

open_menu_expect_items "$EXPECTED_TOP_ITEMS"
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 3
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Lower: client-winmenu reason=window-menu'

# Layer submenu -> set Top (arg=6).
open_menu_expect_items "$EXPECTED_TOP_ITEMS"
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 6
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: enter-submenu reason=activate'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 2
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Layer: client-winmenu set=6'

# Alpha submenu -> Focused -> 80% (sets focused=204).
open_menu_expect_items "$EXPECTED_TOP_ITEMS"
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 7
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: enter-submenu reason=activate'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 0
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: enter-submenu reason=activate'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 2
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Alpha: client-winmenu focused=204 unfocused=255 reason=window-menu'

# SendTo submenu -> move to workspace 2, then switch to workspace 2 and confirm visibility.
open_menu_expect_items "$EXPECTED_TOP_ITEMS"
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 5
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: enter-submenu reason=activate'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 1
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Policy: move focused to workspace 2'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=1 reason=window-sendto'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Policy: workspace switch( head=[0-9]+)? to 2'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=2 reason=switch'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: view=client-winmenu ws=2 visible=1'

# SetTitleDialog -> set an override title via cmd dialog.
open_menu_expect_items "$EXPECTED_TOP_ITEMS"
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item 4
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'CmdDialog: open'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" type winmenu-renamed
./fbwl-input-injector --socket "$SOCKET" key enter
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Title: set title override create_seq=[0-9]+ title=winmenu-renamed'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Decor: title-render winmenu-renamed'

# Close via last item.
open_menu_expect_items "$EXPECTED_TOP_ITEMS"
CLOSE_IDX=$((EXPECTED_TOP_ITEMS - 1))
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_menu_item "$CLOSE_IDX"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: window-close title=winmenu-renamed'

timeout 5 bash -c "while kill -0 '$CLIENT_PID' 2>/dev/null; do sleep 0.05; done"

echo "ok: window-menu smoke passed (socket=$SOCKET log=$LOG)"
