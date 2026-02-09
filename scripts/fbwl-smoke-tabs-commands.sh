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

SOCKET="wayland-fbwl-tabs-commands-$UID-$$"
LOG="/tmp/fluxbox-wayland-tabs-commands-$UID-$$.log"
CFG_DIR="$(mktemp -d)"
KEYS_FILE="$CFG_DIR/keys"

BORDER=4
TITLE_H=24

cleanup() {
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${C_PID:-}" ]]; then kill "$C_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFG_DIR" 2>/dev/null || true
}
trap cleanup EXIT

cat >"$CFG_DIR/init" <<EOF
session.screen0.toolbar.visible: false
session.screen0.windowPlacement: RowSmartPlacement
session.screen0.tabs.intitlebar: true
session.screen0.tabs.usePixmap: false
session.screen0.tab.placement: TopLeft
session.screen0.tab.width: 96
session.tabsAttachArea: Window
session.styleFile: $CFG_DIR/style
EOF

cat >"$CFG_DIR/style" <<EOF
window.borderWidth: $BORDER
window.title.height: $TITLE_H
EOF

cat >"$KEYS_FILE" <<'EOF'
OnTitlebar Mod1 Mouse1 :StartTabbing
OnTitlebar Mouse2 :ActivateTab
Mod1 1 :Tab 1
Mod1 2 :Tab 2
Mod1 3 :Tab 3
Mod1 4 :MoveTabLeft
Mod1 5 :MoveTabRight
Mod1 6 :DetachClient
EOF

: >"$LOG"
WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFG_DIR" \
  --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Output: ' '$LOG'; do sleep 0.05; done"

T_B="tabs-b"
T_A="tabs-a"
T_C="tabs-c"

./fbwl-smoke-client --socket "$SOCKET" --title "$T_B" --stay-ms 10000 --width 320 --height 200 --xdg-decoration >/dev/null 2>&1 &
B_PID=$!
timeout 10 bash -c "until rg -q 'Place: $T_B ' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title "$T_A" --stay-ms 10000 --width 320 --height 200 --xdg-decoration >/dev/null 2>&1 &
A_PID=$!
timeout 10 bash -c "until rg -q 'Place: $T_A ' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title "$T_C" --stay-ms 10000 --width 320 --height 200 --xdg-decoration >/dev/null 2>&1 &
C_PID=$!
timeout 10 bash -c "until rg -q 'Place: $T_C ' '$LOG'; do sleep 0.05; done"

place_b="$(rg -m1 "Place: $T_B " "$LOG")"
place_a="$(rg -m1 "Place: $T_A " "$LOG")"
place_c="$(rg -m1 "Place: $T_C " "$LOG")"

if [[ "$place_b" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
  bx="${BASH_REMATCH[1]}"
  by="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line for B: $place_b" >&2
  exit 1
fi
if [[ "$place_a" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
  ax="${BASH_REMATCH[1]}"
  ay="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line for A: $place_a" >&2
  exit 1
fi
if [[ "$place_c" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
  cx="${BASH_REMATCH[1]}"
  cy="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line for C: $place_c" >&2
  exit 1
fi

# StartTabbing: drag A onto B -> attach
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-alt-left "$((ax + 20))" "$((ay - TITLE_H / 2))" "$((bx + 40))" "$((by + 60))" >/dev/null 2>&1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: attach reason=drag anchor=$T_B view=$T_A'; do sleep 0.05; done"

# StartTabbing: drag C onto the active group surface (now at B's position) -> attach
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-alt-left "$((cx + 20))" "$((cy - TITLE_H / 2))" "$((bx + 40))" "$((by + 60))" >/dev/null 2>&1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: attach reason=drag anchor=$T_A view=$T_C'; do sleep 0.05; done"

# Focus A via Tab 2, then MoveTabLeft; after swap, Tab 1 should select A.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: activate reason=keybinding-tab title=$T_A'; do sleep 0.05; done"
./fbwl-input-injector --socket "$SOCKET" key alt-4

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-3
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: activate reason=keybinding-tab title=$T_C'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: activate reason=keybinding-tab title=$T_A'; do sleep 0.05; done"

# MoveTabRight; after swap back, Tab 1 should select B.
./fbwl-input-injector --socket "$SOCKET" key alt-5

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-3
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: activate reason=keybinding-tab title=$T_C'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: activate reason=keybinding-tab title=$T_B'; do sleep 0.05; done"

# ActivateTab: click Mouse2 on a non-active tab label (prefer A).
tab_a_line="$(rg "TabsUI: tab idx=[0-9]+ title=$T_A " "$LOG" | tail -n 1 || true)"
tab_c_line="$(rg "TabsUI: tab idx=[0-9]+ title=$T_C " "$LOG" | tail -n 1 || true)"

pick_title=""
pick_line=""
if [[ -n "$tab_a_line" && "$tab_a_line" =~ active=0[[:space:]]lx=([-0-9]+)[[:space:]]ly=([-0-9]+)[[:space:]]w=([0-9]+)[[:space:]]h=([0-9]+) ]]; then
  pick_title="$T_A"
  pick_line="$tab_a_line"
elif [[ -n "$tab_c_line" && "$tab_c_line" =~ active=0[[:space:]]lx=([-0-9]+)[[:space:]]ly=([-0-9]+)[[:space:]]w=([0-9]+)[[:space:]]h=([0-9]+) ]]; then
  pick_title="$T_C"
  pick_line="$tab_c_line"
else
  echo "failed to find an inactive tab label for ActivateTab" >&2
  echo "tab_a_line=$tab_a_line" >&2
  echo "tab_c_line=$tab_c_line" >&2
  exit 1
fi

if [[ "$pick_line" =~ lx=([-0-9]+)[[:space:]]ly=([-0-9]+)[[:space:]]w=([0-9]+)[[:space:]]h=([0-9]+) ]]; then
  tab_lx="${BASH_REMATCH[1]}"
  tab_ly="${BASH_REMATCH[2]}"
  tab_w="${BASH_REMATCH[3]}"
  tab_h="${BASH_REMATCH[4]}"
else
  echo "failed to parse tab label geometry: $pick_line" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click-middle "$((tab_lx + tab_w / 2))" "$((tab_ly + tab_h / 2))" >/dev/null 2>&1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -Fq \"Tabs: activate reason=keybinding-activatetab title=$pick_title\"; do sleep 0.05; done"

# DetachClient: detach the now-focused tab.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-6
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -Fq \"Tabs: detach reason=keybinding-detachclient title=$pick_title remaining=2\"; do sleep 0.05; done"

echo "ok: tabs commands smoke passed"
