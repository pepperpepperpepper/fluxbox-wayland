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
LOG="${LOG:-/tmp/fluxbox-wayland-config-dir-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-config-dir-$UID-XXXXXX")"
MARK_DEFAULT="${MARK_DEFAULT:-/tmp/fbwl-config-dir-terminal-default-$UID-$$}"
MARK_OVERRIDE="${MARK_OVERRIDE:-/tmp/fbwl-config-dir-keys-override-$UID-$$}"
MARK_RELOAD_KEYS="${MARK_RELOAD_KEYS:-/tmp/fbwl-config-dir-keys-reload-$UID-$$}"
MARK_MENU="${MARK_MENU:-/tmp/fbwl-config-dir-menu-$UID-$$}"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  rm -f "$MARK_DEFAULT" "$MARK_OVERRIDE" "$MARK_RELOAD_KEYS" "$MARK_MENU" 2>/dev/null || true
  if [[ -n "${APP_PID:-}" ]]; then kill "$APP_PID" 2>/dev/null || true; fi
  if [[ -n "${STRUT_L_VISIBLE_PID:-}" ]]; then kill "$STRUT_L_VISIBLE_PID" 2>/dev/null || true; fi
  if [[ -n "${STRUT_L_HIDDEN_PID:-}" ]]; then kill "$STRUT_L_HIDDEN_PID" 2>/dev/null || true; fi
  if [[ -n "${STRUT_R_VISIBLE_PID:-}" ]]; then kill "$STRUT_R_VISIBLE_PID" 2>/dev/null || true; fi
  if [[ -n "${TAB_PID:-}" ]]; then kill "$TAB_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
rm -f "$MARK_DEFAULT" "$MARK_OVERRIDE" "$MARK_RELOAD_KEYS" "$MARK_MENU"

cat >"$CFGDIR/init" <<EOF
session.autoRaiseDelay: 50
session.screen0.workspaces: 3
session.screen0.focusModel: StrictMouseFocus
session.screen0.allowRemoteActions: true
session.screen0.windowPlacement: AutotabPlacement
session.keyFile: mykeys
session.appsFile: myapps
session.styleFile: mystyle
session.styleOverlay: myoverlay
session.menuFile: mymenu
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.widthPercent: 50
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: workspacename,clock
session.screen0.toolbar.autoHide: true
session.screen0.toolbar.autoRaise: true
session.screen0.menuDelay: 250
session.screen0.tabs.intitlebar: false
session.screen0.tabs.maxOver: true
session.screen0.tabs.usePixmap: false
session.screen0.tab.placement: TopRight
session.screen0.tab.width: 123
session.tabPadding: 4
session.tabsAttachArea: Titlebar
session.screen0.tabFocusModel: MouseTabFocus
EOF

cat >"$CFGDIR/mykeys" <<EOF
# Minimal subset of Fluxbox ~/.fluxbox/keys syntax
Mod1 Return :ExecCommand touch '$MARK_OVERRIDE'
EOF

cat >"$CFGDIR/myapps" <<EOF
[app] (app_id=fbwl-config-dir-jump)
  [Workspace] {1}
  [Jump] {yes}
[end]
EOF

cat >"$CFGDIR/mystyle" <<EOF
window.title.height: 33
EOF

cat >"$CFGDIR/myoverlay" <<EOF
window.title.height: 44
EOF

cat >"$CFGDIR/mymenu" <<EOF
[begin] (Fluxbox)
[submenu] (Sub) {Sub}
  [exec] (TouchMenuMarker) {touch '$MARK_MENU'}
[end]
[end]
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --terminal "touch '$MARK_DEFAULT'" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'OutputLayout: ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded .*myoverlay \\(border=[0-9]+ title_h=44\\)' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Menu: loaded ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: built ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"
rg -q "Init: tabs intitlebar=0 maxOver=1 usePixmap=0 placement=TopRight width=123 padding=4 attachArea=Titlebar tabFocusModel=MouseTabFocus" "$LOG"

OUT_GEOM=$(
  rg -m1 'Output: ' "$LOG" \
    | awk '{print $NF}'
)
OUT_W=${OUT_GEOM%x*}
OUT_H=${OUT_GEOM#*x}

OUTLINE=$(rg -m1 'OutputLayout: ' "$LOG")
OUT_X=$(echo "$OUTLINE" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
OUT_Y=$(echo "$OUTLINE" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)

BUILT_LINE="$(rg 'Toolbar: built ' "$LOG" | tail -n 1)"
if [[ "$BUILT_LINE" =~ w=([0-9]+)\ h=([0-9]+) ]]; then
  TB_W="${BASH_REMATCH[1]}"
  TB_H="${BASH_REMATCH[2]}"
else
  echo "failed to parse Toolbar: built line: $BUILT_LINE" >&2
  exit 1
fi

POS_LINE="$(rg 'Toolbar: position ' "$LOG" | tail -n 1)"
if [[ "$POS_LINE" =~ thickness=([0-9]+) ]]; then
  TB_THICKNESS="${BASH_REMATCH[1]}"
else
  echo "failed to parse Toolbar: thickness from position line: $POS_LINE" >&2
  exit 1
fi
if [[ "$POS_LINE" =~ h=([0-9]+) ]]; then
  TB_OUTER_H="${BASH_REMATCH[1]}"
else
  echo "failed to parse Toolbar: outer height from position line: $POS_LINE" >&2
  exit 1
fi
CROSS=$(((TB_OUTER_H - TB_THICKNESS) / 2))
if (( CROSS < 0 )); then CROSS=0; fi

EXPECTED_TB_W=$((((OUT_W - 2 * CROSS) * 50 / 100) + 2 * CROSS))
if [[ "$TB_W" -ne "$EXPECTED_TB_W" ]]; then
  echo "unexpected toolbar width: got $TB_W expected $EXPECTED_TB_W (out_w=$OUT_W)" >&2
  exit 1
fi
if [[ "$TB_THICKNESS" -ne 30 ]]; then
  echo "unexpected toolbar thickness: got $TB_THICKNESS expected 30" >&2
  exit 1
fi

POS_LINE="$(rg 'Toolbar: position ' "$LOG" | tail -n 1)"
if [[ "$POS_LINE" =~ x=([-0-9]+)\ y=([-0-9]+)\ h=([0-9]+)\ cell_w=([0-9]+)\ workspaces=([0-9]+) ]]; then
  TB_X="${BASH_REMATCH[1]}"
  TB_Y="${BASH_REMATCH[2]}"
  H="${BASH_REMATCH[3]}"
  CELL_W="${BASH_REMATCH[4]}"
  WS="${BASH_REMATCH[5]}"
else
  echo "failed to parse Toolbar: position line: $POS_LINE" >&2
  exit 1
fi

EXPECTED_TB_X=$((OUT_X + (OUT_W - TB_W) / 2))
if [[ "$TB_X" -ne "$EXPECTED_TB_X" ]]; then
  echo "unexpected toolbar x: got $TB_X expected $EXPECTED_TB_X (out_x=$OUT_X out_w=$OUT_W tb_w=$TB_W)" >&2
  exit 1
fi
if [[ "$TB_Y" -ne "$OUT_Y" ]]; then
  echo "unexpected toolbar y: got $TB_Y expected $OUT_Y (out_y=$OUT_Y)" >&2
  exit 1
fi

WS_WIDTH=$((CELL_W * WS))
CLICK_X=$((TB_X + WS_WIDTH + 5))
MAX_X=$((TB_X + TB_W - 1))
if (( CLICK_X > MAX_X )); then CLICK_X=$MAX_X; fi
CLICK_Y=$((TB_Y + H / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y" >/dev/null 2>&1 || true
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Toolbar: autoRaise raise'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click 10 $((OUT_Y + 200)) >/dev/null 2>&1 || true
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Toolbar: autoHide hide'; do sleep 0.05; done"
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Toolbar: autoRaise lower'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" $((OUT_Y + 1)) >/dev/null 2>&1 || true
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Toolbar: autoHide show'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 50 50 50 50
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line after right-click" >&2
  exit 1
fi

if [[ "$open_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  MENU_X="${BASH_REMATCH[1]}"
  MENU_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse menu open line: $open_line" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" motion 0 0 >/dev/null 2>&1 || true

ITEM_H=24
MENU_TITLE_H=$ITEM_H

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" motion "$((MENU_X + 10))" "$((MENU_Y + MENU_TITLE_H + 10))" >/dev/null 2>&1 || true
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Menu: enter-submenu reason=delay'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" click "$((MENU_X + 10))" "$((MENU_Y + MENU_TITLE_H + 10))"
timeout 2 bash -c "until [[ -f '$MARK_MENU' ]]; do sleep 0.05; done"

OUT="$(./fbwl-remote --socket "$SOCKET" workspace 4 || true)"
printf '%s\n' "$OUT" | rg -q '^err workspace_out_of_range$'

./fbwl-smoke-client --socket "$SOCKET" --title cfgdir-client --app-id fbwl-config-dir-jump --stay-ms 10000 >/dev/null 2>&1 &
APP_PID=$!

timeout 5 bash -c "until rg -q 'Focus: cfgdir-client' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" motion "$((OUT_X + OUT_W - 1))" "$((OUT_Y + OUT_H - 1))" >/dev/null 2>&1 || true
sleep 0.2
if tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Focus: clear reason=pointer-leave'; then
  echo "unexpected focus clear in StrictMouseFocus after leaving view" >&2
  exit 1
fi

timeout 5 bash -c "until ./fbwl-remote --socket \"$SOCKET\" get-workspace | rg -q '^ok workspace=2$'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-return

timeout 2 bash -c "until [[ -f '$MARK_OVERRIDE' ]]; do sleep 0.05; done"
if [[ -f "$MARK_DEFAULT" ]]; then
  echo "expected config-dir keys binding to override default terminal binding (MARK_DEFAULT exists: $MARK_DEFAULT)" >&2
  exit 1
fi

rm -f "$MARK_OVERRIDE" "$MARK_RELOAD_KEYS"

cat >"$CFGDIR/mykeys2" <<EOF
Mod1 Return :ExecCommand touch '$MARK_RELOAD_KEYS'
EOF

cat >"$CFGDIR/init" <<EOF
session.autoRaiseDelay: 50
session.screen0.workspaces: 3
session.screen0.focusModel: StrictMouseFocus
session.screen0.allowRemoteActions: true
session.screen0.windowPlacement: RowSmartPlacement
session.keyFile: mykeys2
session.appsFile: myapps
session.styleFile: mystyle
session.menuFile: mymenu
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: LeftCenter
session.screen0.toolbar.widthPercent: 70
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: workspacename,clock
session.screen0.toolbar.autoHide: true
session.screen0.toolbar.autoRaise: true
session.screen0.menuDelay: 250
session.screen0.tabs.intitlebar: false
session.screen0.tabs.maxOver: true
session.screen0.tabs.usePixmap: false
session.screen0.tab.placement: TopRight
session.screen0.tab.width: 123
session.tabPadding: 4
session.tabsAttachArea: Titlebar
session.screen0.tabFocusModel: MouseTabFocus
EOF

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" reconfigure | rg -q '^ok reconfigure$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: reloaded init from '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: reloaded keys from '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: built '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: position '; do sleep 0.05; done"

BUILT_LINE="$(tail -c +$START "$LOG" | rg 'Toolbar: built ' | tail -n 1)"
if [[ "$BUILT_LINE" =~ w=([0-9]+)\ h=([0-9]+) ]]; then
  NEW_TB_W="${BASH_REMATCH[1]}"
  NEW_TB_H="${BASH_REMATCH[2]}"
else
  echo "failed to parse Toolbar: built line after reconfigure: $BUILT_LINE" >&2
  exit 1
fi

POS_LINE="$(tail -c +$START "$LOG" | rg 'Toolbar: position ' | tail -n 1)"
if [[ "$POS_LINE" =~ thickness=([0-9]+) ]]; then
  NEW_TB_THICKNESS="${BASH_REMATCH[1]}"
else
  echo "failed to parse Toolbar: thickness line after reconfigure: $POS_LINE" >&2
  exit 1
fi

CROSS=$(((NEW_TB_W - NEW_TB_THICKNESS) / 2))
if (( CROSS < 0 )); then CROSS=0; fi

EXPECTED_TB_H=$((((OUT_H - 2 * CROSS) * 70 / 100) + 2 * CROSS))
if [[ "$NEW_TB_THICKNESS" -ne 30 ]]; then
  echo "unexpected toolbar thickness after reconfigure: got $NEW_TB_THICKNESS expected 30" >&2
  exit 1
fi
if [[ "$NEW_TB_H" -ne "$EXPECTED_TB_H" ]]; then
  echo "unexpected toolbar height after reconfigure: got $NEW_TB_H expected $EXPECTED_TB_H (out_h=$OUT_H)" >&2
  exit 1
fi

POS_LINE="$(tail -c +$START "$LOG" | rg 'Toolbar: position ' | tail -n 1)"
if [[ "$POS_LINE" =~ x=([-0-9]+)\ y=([-0-9]+)\ h=([0-9]+)\ cell_w=([0-9]+)\ workspaces=([0-9]+) ]]; then
  TB_X="${BASH_REMATCH[1]}"
  TB_Y="${BASH_REMATCH[2]}"
  H="${BASH_REMATCH[3]}"
else
  echo "failed to parse Toolbar: position line after reconfigure: $POS_LINE" >&2
  exit 1
fi

EXPECTED_TB_X="$OUT_X"
EXPECTED_TB_Y=$((OUT_Y + (OUT_H - H) / 2))
if [[ "$TB_X" -ne "$EXPECTED_TB_X" ]]; then
  echo "unexpected left toolbar x: got $TB_X expected $EXPECTED_TB_X (out_x=$OUT_X)" >&2
  exit 1
fi
if [[ "$TB_Y" -ne "$EXPECTED_TB_Y" ]]; then
  echo "unexpected left toolbar y: got $TB_Y expected $EXPECTED_TB_Y (out_y=$OUT_Y out_h=$OUT_H tb_h=$H)" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title cfgdir-strut-left-visible --stay-ms 10000 >/dev/null 2>&1 &
STRUT_L_VISIBLE_PID=$!
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Place: cfgdir-strut-left-visible '; do sleep 0.05; done"
place_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Place: cfgdir-strut-left-visible ')"
if [[ "$place_line" =~ usable=([-0-9]+),([-0-9]+)[[:space:]] ]]; then
  USABLE_X="${BASH_REMATCH[1]}"
else
  echo "failed to parse usable box from Place line: $place_line" >&2
  exit 1
fi
EXPECTED_USABLE_X="$OUT_X"
if [[ "$USABLE_X" -ne "$EXPECTED_USABLE_X" ]]; then
  echo "unexpected usable.x with left toolbar visible: got $USABLE_X expected $EXPECTED_USABLE_X" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" motion $((OUT_X + 1)) $((TB_Y + 1)) >/dev/null 2>&1 || true
./fbwl-input-injector --socket "$SOCKET" motion $((OUT_X + OUT_W - 10)) $((OUT_Y + OUT_H / 2)) >/dev/null 2>&1 || true
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Toolbar: autoHide hide'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title cfgdir-strut-left-hidden --stay-ms 10000 >/dev/null 2>&1 &
STRUT_L_HIDDEN_PID=$!
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Place: cfgdir-strut-left-hidden '; do sleep 0.05; done"
place_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Place: cfgdir-strut-left-hidden ')"
if [[ "$place_line" =~ usable=([-0-9]+),([-0-9]+)[[:space:]] ]]; then
  USABLE_X="${BASH_REMATCH[1]}"
else
  echo "failed to parse usable box from Place line: $place_line" >&2
  exit 1
fi
EXPECTED_USABLE_X="$OUT_X"
if [[ "$USABLE_X" -ne "$EXPECTED_USABLE_X" ]]; then
  echo "unexpected usable.x with left toolbar hidden: got $USABLE_X expected $EXPECTED_USABLE_X" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" key alt-return
timeout 2 bash -c "until [[ -f '$MARK_RELOAD_KEYS' ]]; do sleep 0.05; done"
if [[ -f "$MARK_OVERRIDE" ]]; then
  echo "expected init reload to switch keyFile to mykeys2, but old binding still triggered ($MARK_OVERRIDE exists)" >&2
  exit 1
fi

cat >"$CFGDIR/init" <<EOF
session.autoRaiseDelay: 50
session.screen0.workspaces: 3
session.screen0.focusModel: StrictMouseFocus
session.screen0.allowRemoteActions: true
session.screen0.windowPlacement: RowSmartPlacement
session.keyFile: mykeys2
session.appsFile: myapps
session.styleFile: mystyle
session.menuFile: mymenu
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: RightCenter
session.screen0.toolbar.widthPercent: 70
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: workspacename,clock
session.screen0.toolbar.autoHide: true
session.screen0.toolbar.autoRaise: true
session.screen0.menuDelay: 250
session.screen0.tabs.intitlebar: false
session.screen0.tabs.maxOver: true
session.screen0.tabs.usePixmap: false
session.screen0.tab.placement: TopRight
session.screen0.tab.width: 123
session.tabPadding: 4
session.tabsAttachArea: Titlebar
session.screen0.tabFocusModel: MouseTabFocus
EOF

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" reconfigure | rg -q '^ok reconfigure$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: reloaded init from '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: built '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: position '; do sleep 0.05; done"

BUILT_LINE="$(tail -c +$START "$LOG" | rg 'Toolbar: built ' | tail -n 1)"
  if [[ "$BUILT_LINE" =~ w=([0-9]+)\ h=([0-9]+) ]]; then
    NEW_TB_W="${BASH_REMATCH[1]}"
    NEW_TB_H="${BASH_REMATCH[2]}"
  else
    echo "failed to parse Toolbar: built line after right toolbar reconfigure: $BUILT_LINE" >&2
    exit 1
  fi
  POS_LINE="$(tail -c +$START "$LOG" | rg 'Toolbar: position ' | tail -n 1)"
  if [[ "$POS_LINE" =~ thickness=([0-9]+) ]]; then
    NEW_TB_THICKNESS="${BASH_REMATCH[1]}"
  else
    echo "failed to parse Toolbar: thickness from position line after right toolbar reconfigure: $POS_LINE" >&2
    exit 1
  fi
  if [[ "$POS_LINE" =~ [[:space:]]w=([0-9]+) ]]; then
    NEW_TB_OUTER_W="${BASH_REMATCH[1]}"
  else
    echo "failed to parse Toolbar: outer width from position line after right toolbar reconfigure: $POS_LINE" >&2
    exit 1
  fi
  if [[ "$POS_LINE" =~ h=([0-9]+) ]]; then
    NEW_TB_OUTER_H="${BASH_REMATCH[1]}"
  else
    echo "failed to parse Toolbar: outer height from position line after right toolbar reconfigure: $POS_LINE" >&2
    exit 1
  fi
  NEW_TB_OUTER_THICKNESS="$NEW_TB_OUTER_W"
  if (( NEW_TB_OUTER_H < NEW_TB_OUTER_W )); then
    NEW_TB_OUTER_THICKNESS="$NEW_TB_OUTER_H"
  fi
  NEW_CROSS=$(((NEW_TB_OUTER_THICKNESS - NEW_TB_THICKNESS) / 2))
  if (( NEW_CROSS < 0 )); then NEW_CROSS=0; fi

  if [[ "$NEW_TB_THICKNESS" -ne 30 ]]; then
    echo "unexpected right toolbar thickness: got $NEW_TB_THICKNESS expected 30" >&2
    exit 1
  fi
  if [[ "$NEW_CROSS" -ne "$CROSS" ]]; then
    echo "unexpected right toolbar cross size: got $NEW_CROSS expected $CROSS" >&2
    exit 1
  fi

  EXPECTED_NEW_TB_W=$((30 + 2 * CROSS))
  EXPECTED_NEW_TB_H=$((((OUT_H - 2 * CROSS) * 70 / 100) + 2 * CROSS))
  if [[ "$NEW_TB_W" -ne "$EXPECTED_NEW_TB_W" ]]; then
    echo "unexpected right toolbar width: got $NEW_TB_W expected $EXPECTED_NEW_TB_W (thickness=30 cross=$CROSS)" >&2
    exit 1
  fi
  if [[ "$NEW_TB_H" -ne "$EXPECTED_NEW_TB_H" ]]; then
    echo "unexpected right toolbar height: got $NEW_TB_H expected $EXPECTED_NEW_TB_H (out_h=$OUT_H cross=$CROSS)" >&2
    exit 1
  fi

  if [[ "$POS_LINE" =~ x=([-0-9]+)\ y=([-0-9]+)\ h=([0-9]+)\ cell_w=([0-9]+)\ workspaces=([0-9]+) ]]; then
    TB_X="${BASH_REMATCH[1]}"
    TB_Y="${BASH_REMATCH[2]}"
    H="${BASH_REMATCH[3]}"
  else
    echo "failed to parse Toolbar: position line after right toolbar reconfigure: $POS_LINE" >&2
    exit 1
  fi

  EXPECTED_TB_X=$((OUT_X + OUT_W - NEW_TB_W))
  EXPECTED_TB_Y=$((OUT_Y + (OUT_H - H) / 2))
  if [[ "$TB_X" -ne "$EXPECTED_TB_X" ]]; then
    echo "unexpected right toolbar x: got $TB_X expected $EXPECTED_TB_X (out_x=$OUT_X out_w=$OUT_W)" >&2
    exit 1
fi
if [[ "$TB_Y" -ne "$EXPECTED_TB_Y" ]]; then
  echo "unexpected right toolbar y: got $TB_Y expected $EXPECTED_TB_Y (out_y=$OUT_Y out_h=$OUT_H tb_h=$H)" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title cfgdir-strut-right-visible --stay-ms 10000 >/dev/null 2>&1 &
STRUT_R_VISIBLE_PID=$!
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Place: cfgdir-strut-right-visible '; do sleep 0.05; done"
place_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Place: cfgdir-strut-right-visible ')"
if [[ "$place_line" =~ usable=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+)[[:space:]] ]]; then
  USABLE_X="${BASH_REMATCH[1]}"
  USABLE_W="${BASH_REMATCH[3]}"
else
  echo "failed to parse usable box from Place line: $place_line" >&2
  exit 1
fi
EXPECTED_USABLE_X="$OUT_X"
EXPECTED_USABLE_W="$OUT_W"
if [[ "$USABLE_X" -ne "$EXPECTED_USABLE_X" ]]; then
  echo "unexpected usable.x with right toolbar visible: got $USABLE_X expected $EXPECTED_USABLE_X" >&2
  exit 1
fi
if [[ "$USABLE_W" -ne "$EXPECTED_USABLE_W" ]]; then
  echo "unexpected usable.w with right toolbar visible: got $USABLE_W expected $EXPECTED_USABLE_W" >&2
  exit 1
fi

cat >"$CFGDIR/init" <<EOF
session.autoRaiseDelay: 50
session.screen0.workspaces: 3
session.screen0.focusModel: StrictMouseFocus
session.screen0.allowRemoteActions: true
session.screen0.windowPlacement: AutotabPlacement
session.keyFile: mykeys2
session.appsFile: myapps
session.styleFile: mystyle
session.menuFile: mymenu
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: RightCenter
session.screen0.toolbar.widthPercent: 70
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: workspacename,clock
session.screen0.toolbar.autoHide: true
session.screen0.toolbar.autoRaise: true
session.screen0.menuDelay: 250
session.screen0.tabs.intitlebar: false
session.screen0.tabs.maxOver: true
session.screen0.tabs.usePixmap: false
session.screen0.tab.placement: TopRight
session.screen0.tab.width: 123
session.tabPadding: 4
session.tabsAttachArea: Titlebar
session.screen0.tabFocusModel: MouseTabFocus
EOF

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" reconfigure | rg -q '^ok reconfigure$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: reloaded init from '; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title cfgdir-tab --app-id fbwl-config-dir-tab --stay-ms 3000 >/dev/null 2>&1 &
TAB_PID=$!
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: attach reason=autotab'; do sleep 0.05; done"

echo "ok: config-dir smoke passed (socket=$SOCKET log=$LOG cfgdir=$CFGDIR)"
