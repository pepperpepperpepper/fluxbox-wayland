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
LOG="${LOG:-/tmp/fluxbox-wayland-menu-$UID-$$.log}"
MENU_FILE="${MENU_FILE:-/tmp/fbwl-menu-$UID-$$.menu}"
MENU_INCLUDE_FILE="${MENU_INCLUDE_FILE:-/tmp/fbwl-menu-include-$UID-$$.menu}"
MARKER="${MARKER:-/tmp/fbwl-menu-marker-$UID-$$}"
MARKER2="${MARKER2:-/tmp/fbwl-menu-marker2-$UID-$$}"
MARKER3="${MARKER3:-/tmp/fbwl-menu-marker3-$UID-$$}"
STYLE_DIR1="${STYLE_DIR1:-/tmp/fbwl-style-dir1-$UID-$$}"
STYLE_DIR2="${STYLE_DIR2:-/tmp/fbwl-style-dir2-$UID-$$}"
WALL_DIR="${WALL_DIR:-/tmp/fbwl-wall-dir-$UID-$$}"
ICON_XPM="${ICON_XPM:-/tmp/fbwl-menu-icon-$UID-$$.xpm}"
ICON_MISSING="${ICON_MISSING:-/tmp/fbwl-menu-icon-missing-$UID-$$.xpm}"

cleanup() {
  rm -f "$MENU_FILE" "$MENU_INCLUDE_FILE" "$MARKER" "$MARKER2" "$MARKER3" "$ICON_XPM" 2>/dev/null || true
  rm -rf "$STYLE_DIR1" "$STYLE_DIR2" "$WALL_DIR" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
rm -f "$MARKER"
rm -f "$MARKER2"
rm -f "$MARKER3"

mkdir -p "$STYLE_DIR1" "$STYLE_DIR2" "$WALL_DIR"

cat >"$ICON_XPM" <<'EOF'
/* XPM */
static char * icon_xpm[] = {
"1 1 2 1",
" \tc None",
".\tc #FF0000",
"."
};
EOF

cat >"$STYLE_DIR1/dir-style.cfg" <<'EOF'
window.borderWidth: 10
window.borderColor: #112233
EOF

cat >"$STYLE_DIR2/menu-style.cfg" <<'EOF'
window.borderWidth: 12
window.borderColor: #334455
EOF

touch "$WALL_DIR/wallpaper1"

cat >"$MENU_INCLUDE_FILE" <<EOF
[exec] (TouchMarker) {sh -c 'echo ok >"$MARKER"'} <$ICON_XPM>
[nop] (NoOp)
[separator]
[encoding] {ISO-8859-1}
EOF
printf $'[exec] (Caf\\xE9) {sh -c \\047echo ok >\"%s\"\\047} <%s>\\n' "$MARKER3" "$ICON_MISSING" >>"$MENU_INCLUDE_FILE"
cat >>"$MENU_INCLUDE_FILE" <<EOF
[endencoding]
EOF

cat >"$MENU_FILE" <<EOF
[begin] (Fluxbox)
[include] ($MENU_INCLUDE_FILE)
[submenu] (Dyn) <$ICON_XPM>
  [config] (Config) <$ICON_XPM>
  [stylesdir] ($STYLE_DIR1) <$ICON_XPM>
  [stylesmenu] ($STYLE_DIR2) <$ICON_XPM>
  [wallpapers] ($WALL_DIR) {sh -c 'echo ok >"$MARKER2"'} <$ICON_XPM>
[end]
[workspaces] (Workspaces) <$ICON_XPM>
[exit] (Exit) <$ICON_XPM>
[end]
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --menu "$MENU_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

# Open the root menu with a background right-click.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line after right-click" >&2
  exit 1
fi

if [[ "$open_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ items=([0-9]+) ]]; then
  MENU_X="${BASH_REMATCH[1]}"
  MENU_Y="${BASH_REMATCH[2]}"
  MENU_ITEMS="${BASH_REMATCH[3]}"
else
  echo "failed to parse menu open line: $open_line" >&2
  exit 1
fi

ITEM_H=24
MENU_TITLE_H=$ITEM_H
if [[ "$MENU_ITEMS" != "7" ]]; then
  echo "expected 7 menu items (include + nop + separator + encoding-exec + dyn + workspaces + exit), got $MENU_ITEMS" >&2
  exit 1
fi

# Click the first menu item.
CLICK_X=$((MENU_X + 10))
CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10))
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: exec '

timeout 5 bash -c "until [[ -f '$MARKER' ]]; do sleep 0.05; done"

# Open the root menu again and run the encoding-block item.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line after second right-click" >&2
  exit 1
fi

if [[ "$open_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ items=([0-9]+) ]]; then
  MENU_X="${BASH_REMATCH[1]}"
  MENU_Y="${BASH_REMATCH[2]}"
  MENU_ITEMS="${BASH_REMATCH[3]}"
else
  echo "failed to parse menu open line: $open_line" >&2
  exit 1
fi

if [[ "$MENU_ITEMS" != "7" ]]; then
  echo "expected 7 menu items on second open, got $MENU_ITEMS" >&2
  exit 1
fi

ENC_IDX=3
ENC_CLICK_X=$((MENU_X + 10))
ENC_CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10 + ENC_IDX * ITEM_H))
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$ENC_CLICK_X" "$ENC_CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: exec label=Café cmd='
timeout 5 bash -c "until [[ -f '$MARKER3' ]]; do sleep 0.05; done"

# Open the root menu again for Dyn/Workspaces.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line after third right-click" >&2
  exit 1
fi

if [[ "$open_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ items=([0-9]+) ]]; then
  MENU_X="${BASH_REMATCH[1]}"
  MENU_Y="${BASH_REMATCH[2]}"
  MENU_ITEMS="${BASH_REMATCH[3]}"
else
  echo "failed to parse menu open line: $open_line" >&2
  exit 1
fi

if [[ "$MENU_ITEMS" != "7" ]]; then
  echo "expected 7 menu items on third open, got $MENU_ITEMS" >&2
  exit 1
fi

WS_IDX=5
WS_CLICK_X=$((MENU_X + 10))
WS_CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10 + WS_IDX * ITEM_H))

# Enter the Dyn submenu and select dynamic entries.
DYN_IDX=4
DYN_CLICK_X=$((MENU_X + 10))
DYN_CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10 + DYN_IDX * ITEM_H))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$DYN_CLICK_X" "$DYN_CLICK_Y"
submenu_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: enter-submenu ' || true)"
if [[ -z "$submenu_line" ]]; then
  echo "expected submenu enter log line after clicking Dyn" >&2
  exit 1
fi
if [[ "$submenu_line" != *"label=Dyn"* ]]; then
  echo "expected Dyn submenu enter, got: $submenu_line" >&2
  exit 1
fi
if [[ "$submenu_line" != *"items=4"* ]]; then
  echo "expected Dyn submenu to have 4 items, got: $submenu_line" >&2
  exit 1
fi

# Select style from [stylesdir] (dir-style.cfg).
STYLE1_IDX=1
STYLE1_CLICK_X=$((MENU_X + 10))
STYLE1_CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10 + STYLE1_IDX * ITEM_H))
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$STYLE1_CLICK_X" "$STYLE1_CLICK_Y"
timeout 5 bash -c "until rg -q 'Menu: set-style ok path=.*dir-style\\.cfg' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded .*dir-style\\.cfg \\(border=10 title_h=24\\)' '$LOG'; do sleep 0.05; done"

# Re-open Dyn submenu and select style from [stylesmenu] (menu-style.cfg).
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line before selecting second style" >&2
  exit 1
fi
if [[ "$open_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ items=([0-9]+) ]]; then
  MENU_X="${BASH_REMATCH[1]}"
  MENU_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse menu open line: $open_line" >&2
  exit 1
fi

DYN_CLICK_X=$((MENU_X + 10))
DYN_CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10 + DYN_IDX * ITEM_H))
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
START=$((OFFSET + 1))
./fbwl-input-injector --socket "$SOCKET" click "$DYN_CLICK_X" "$DYN_CLICK_Y"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Menu: enter-submenu .*label=Dyn .*items=4'; do sleep 0.05; done"

STYLE2_IDX=2
STYLE2_CLICK_X=$((MENU_X + 10))
STYLE2_CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10 + STYLE2_IDX * ITEM_H))
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$STYLE2_CLICK_X" "$STYLE2_CLICK_Y"
timeout 5 bash -c "until rg -q 'Menu: set-style ok path=.*menu-style\\.cfg' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded .*menu-style\\.cfg \\(border=12 title_h=24\\)' '$LOG'; do sleep 0.05; done"

# Re-open Dyn submenu and run a wallpapers item (touches MARKER2).
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line before selecting wallpaper" >&2
  exit 1
fi
if [[ "$open_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ items=([0-9]+) ]]; then
  MENU_X="${BASH_REMATCH[1]}"
  MENU_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse menu open line: $open_line" >&2
  exit 1
fi

DYN_CLICK_X=$((MENU_X + 10))
DYN_CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10 + DYN_IDX * ITEM_H))
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
START=$((OFFSET + 1))
./fbwl-input-injector --socket "$SOCKET" click "$DYN_CLICK_X" "$DYN_CLICK_Y"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Menu: enter-submenu .*label=Dyn .*items=4'; do sleep 0.05; done"

WALL_IDX=3
WALL_CLICK_X=$((MENU_X + 10))
WALL_CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10 + WALL_IDX * ITEM_H))
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$WALL_CLICK_X" "$WALL_CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: exec '
timeout 5 bash -c "until [[ -f '$MARKER2' ]]; do sleep 0.05; done"

# Re-open Dyn submenu and enter Config.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line before entering Config" >&2
  exit 1
fi
if [[ "$open_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ items=([0-9]+) ]]; then
  MENU_X="${BASH_REMATCH[1]}"
  MENU_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse menu open line: $open_line" >&2
  exit 1
fi

DYN_CLICK_X=$((MENU_X + 10))
DYN_CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10 + DYN_IDX * ITEM_H))
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
START=$((OFFSET + 1))
./fbwl-input-injector --socket "$SOCKET" click "$DYN_CLICK_X" "$DYN_CLICK_Y"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Menu: enter-submenu .*label=Dyn .*items=4'; do sleep 0.05; done"

CFG_IDX=0
CFG_CLICK_X=$((MENU_X + 10))
CFG_CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10 + CFG_IDX * ITEM_H))
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CFG_CLICK_X" "$CFG_CLICK_Y"
submenu_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: enter-submenu ' || true)"
if [[ -z "$submenu_line" ]]; then
  echo "expected Config submenu enter log line" >&2
  exit 1
fi
if [[ "$submenu_line" != *"label=Config"* ]]; then
  echo "expected Config submenu enter, got: $submenu_line" >&2
  exit 1
fi

AUTO_RAISE_IDX=2
AUTO_RAISE_X=$((MENU_X + 10))
AUTO_RAISE_Y=$((MENU_Y + MENU_TITLE_H + 10 + AUTO_RAISE_IDX * ITEM_H))
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$AUTO_RAISE_X" "$AUTO_RAISE_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: toggle autoRaise='

# Re-open the root menu for Workspaces.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line before clicking Workspaces" >&2
  exit 1
fi
if [[ "$open_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ items=([0-9]+) ]]; then
  MENU_X="${BASH_REMATCH[1]}"
  MENU_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse menu open line: $open_line" >&2
  exit 1
fi

WS_CLICK_X=$((MENU_X + 10))
WS_CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10 + WS_IDX * ITEM_H))

# Enter the workspace submenu.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$WS_CLICK_X" "$WS_CLICK_Y"
submenu_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: enter-submenu ' || true)"
if [[ -z "$submenu_line" ]]; then
  echo "expected submenu enter log line after clicking Workspaces" >&2
  exit 1
fi
if [[ "$submenu_line" != *"label=Workspaces"* ]]; then
  echo "expected Workspaces submenu enter, got: $submenu_line" >&2
  exit 1
fi
if [[ "$submenu_line" != *"items=4"* ]]; then
  echo "expected Workspaces submenu to have 4 items, got: $submenu_line" >&2
  exit 1
fi

# Switch to workspace 2.
WS2_CLICK_X=$((MENU_X + 10))
WS2_CLICK_Y=$((MENU_Y + MENU_TITLE_H + 10 + 1 * ITEM_H))
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$WS2_CLICK_X" "$WS2_CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: workspace-switch .* workspace=2'

echo "ok: menu smoke passed (socket=$SOCKET log=$LOG)"
