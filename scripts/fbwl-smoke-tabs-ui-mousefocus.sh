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

SOCKET="wayland-fbwl-tabsui-mousefocus-$UID-$$"
LOG="/tmp/fluxbox-wayland-tabsui-mousefocus-$UID-$$.log"
CFG_DIR="$(mktemp -d)"

cleanup() {
  if [[ -n "${C0_PID:-}" ]]; then kill "$C0_PID" 2>/dev/null || true; fi
  if [[ -n "${C1_PID:-}" ]]; then kill "$C1_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFG_DIR" 2>/dev/null || true
}
trap cleanup EXIT

cat >"$CFG_DIR/init" <<EOF
session.screen0.toolbar.visible: false
session.screen0.windowPlacement: AutotabPlacement
session.screen0.tabs.intitlebar: true
session.screen0.tabs.usePixmap: false
session.screen0.tab.placement: TopLeft
session.screen0.tab.width: 96
session.screen0.tabFocusModel: MouseTabFocus
session.tabPadding: 8
session.styleFile: $CFG_DIR/style
EOF

cat >"$CFG_DIR/style" <<EOF
window.borderWidth: 4
window.title.height: 24
EOF

: >"$LOG"
WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFG_DIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Output: ' '$LOG'; do sleep 0.05; done"

t0="tab0-tabsui-mousefocus"
t1="tab1-tabsui-mousefocus"

./fbwl-smoke-client --socket "$SOCKET" --title "$t0" --stay-ms 10000 --width 320 --height 200 --xdg-decoration >/dev/null 2>&1 &
C0_PID=$!
timeout 10 bash -c "until rg -q 'Place: $t0 ' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title "$t1" --stay-ms 10000 --width 320 --height 200 --xdg-decoration >/dev/null 2>&1 &
C1_PID=$!
timeout 10 bash -c "until rg -q 'Tabs: attach reason=autotab' '$LOG'; do sleep 0.05; done"

tab0_line="$(rg "TabsUI: tab idx=0 title=$t0 " "$LOG" | tail -n 1)"
tab1_line="$(rg "TabsUI: tab idx=1 title=$t1 " "$LOG" | tail -n 1)"

if [[ "$tab0_line" =~ active=([01])[[:space:]]lx=([-0-9]+)[[:space:]]ly=([-0-9]+)[[:space:]]w=([0-9]+)[[:space:]]h=([0-9]+) ]]; then
  TAB0_ACTIVE="${BASH_REMATCH[1]}"
  TAB0_LX="${BASH_REMATCH[2]}"
  TAB0_LY="${BASH_REMATCH[3]}"
  TAB0_W="${BASH_REMATCH[4]}"
  TAB0_H="${BASH_REMATCH[5]}"
else
  echo "failed to parse tab0 line: $tab0_line" >&2
  exit 1
fi

if [[ "$tab1_line" =~ active=([01])[[:space:]]lx=([-0-9]+)[[:space:]]ly=([-0-9]+)[[:space:]]w=([0-9]+)[[:space:]]h=([0-9]+) ]]; then
  TAB1_ACTIVE="${BASH_REMATCH[1]}"
  TAB1_LX="${BASH_REMATCH[2]}"
  TAB1_LY="${BASH_REMATCH[3]}"
  TAB1_W="${BASH_REMATCH[4]}"
  TAB1_H="${BASH_REMATCH[5]}"
else
  echo "failed to parse tab1 line: $tab1_line" >&2
  exit 1
fi

HOVER_IDX=0
HOVER_TITLE="$t0"
HOVER_X=$((TAB0_LX + TAB0_W / 2))
HOVER_Y=$((TAB0_LY + TAB0_H / 2))
if [[ "$TAB0_ACTIVE" == "1" ]]; then
  HOVER_IDX=1
  HOVER_TITLE="$t1"
  HOVER_X=$((TAB1_LX + TAB1_W / 2))
  HOVER_Y=$((TAB1_LY + TAB1_H / 2))
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" motion "$HOVER_X" "$HOVER_Y" >/dev/null 2>&1
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "TabsUI: hover idx=$HOVER_IDX title=$HOVER_TITLE"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Tabs: activate reason=tab-hover title=$HOVER_TITLE"

echo "ok: tabs UI mouse-tab-focus passed"
