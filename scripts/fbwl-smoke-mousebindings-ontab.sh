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

SOCKET="wayland-fbwl-mousebind-ontab-$UID-$$"
LOG="/tmp/fluxbox-wayland-mousebind-ontab-$UID-$$.log"
CFG_DIR="$(mktemp -d)"
MARK_TAB="/tmp/fbwl-mousebind-ontab-$UID-$$"

cleanup() {
  rm -f "$MARK_TAB" 2>/dev/null || true
  if [[ -n "${C0_PID:-}" ]]; then kill "$C0_PID" 2>/dev/null || true; fi
  if [[ -n "${C1_PID:-}" ]]; then kill "$C1_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFG_DIR" 2>/dev/null || true
}
trap cleanup EXIT

rm -f "$MARK_TAB"

cat >"$CFG_DIR/init" <<EOF
session.screen0.toolbar.visible: false
session.screen0.windowPlacement: AutotabPlacement
session.screen0.tabs.intitlebar: true
session.screen0.tabs.usePixmap: false
session.screen0.tab.placement: TopLeft
session.screen0.tab.width: 96
session.tabPadding: 8
session.styleFile: $CFG_DIR/style
session.keyFile: keys
EOF

cat >"$CFG_DIR/style" <<EOF
window.borderWidth: 4
window.title.height: 24
EOF

cat >"$CFG_DIR/keys" <<EOF
OnTab Mouse1 :ExecCommand touch '$MARK_TAB'
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

t0="tab0-mousebind-ontab"
t1="tab1-mousebind-ontab"

./fbwl-smoke-client --socket "$SOCKET" --title "$t0" --stay-ms 10000 --width 320 --height 200 --xdg-decoration >/dev/null 2>&1 &
C0_PID=$!
timeout 10 bash -c "until rg -q 'Place: $t0 ' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title "$t1" --stay-ms 10000 --width 320 --height 200 --xdg-decoration >/dev/null 2>&1 &
C1_PID=$!
timeout 10 bash -c "until rg -q 'Tabs: attach reason=autotab' '$LOG'; do sleep 0.05; done"

tab0_line="$(rg "TabsUI: tab idx=0 title=$t0 " "$LOG" | tail -n 1)"
if [[ -z "$tab0_line" ]]; then
  echo "missing tab0 TabsUI line" >&2
  exit 1
fi
if [[ "$tab0_line" =~ lx=([-0-9]+)[[:space:]]ly=([-0-9]+)[[:space:]]w=([0-9]+)[[:space:]]h=([0-9]+) ]]; then
  TAB0_LX="${BASH_REMATCH[1]}"
  TAB0_LY="${BASH_REMATCH[2]}"
  TAB0_W="${BASH_REMATCH[3]}"
  TAB0_H="${BASH_REMATCH[4]}"
else
  echo "failed to parse tab0 line: $tab0_line" >&2
  exit 1
fi

CLICK_X=$((TAB0_LX + TAB0_W / 2))
CLICK_Y=$((TAB0_LY + TAB0_H / 2))

./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y" >/dev/null 2>&1
timeout 2 bash -c "until [[ -f '$MARK_TAB' ]]; do sleep 0.05; done"

echo "ok: OnTab mouse binding context smoke passed (socket=$SOCKET log=$LOG)"

