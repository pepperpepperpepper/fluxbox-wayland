#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1 (build first)" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd wc

need_exe ./fluxbox-wayland
need_exe ./fbwl-input-injector
need_exe ./fbwl-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="wayland-fbwl-menu-cmdparity-$UID-$$"
LOG="/tmp/fluxbox-wayland-menu-cmdparity-$UID-$$.log"
CFGDIR="$(mktemp -d "/tmp/fbwl-menu-cmdparity-$UID-XXXXXX")"

KEYS_FILE="$CFGDIR/keys"
CUSTOM_MENU="$CFGDIR/custom.menu"
MARKER="$CFGDIR/custommenu-marker"

cleanup() {
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFGDIR" 2>/dev/null || true
}
trap cleanup EXIT

rm -f "$MARKER" 2>/dev/null || true
: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: false
session.screen0.clientMenu.usePixmap: false
session.styleFile: $CFGDIR/style
EOF

cat >"$CFGDIR/style" <<'EOF'
window.title.height: 24
EOF

cat >"$CUSTOM_MENU" <<EOF
[begin] (Custom)
[exec] (TouchMarker) {sh -c 'echo ok >"$MARKER"'}
[end]
EOF

cat >"$KEYS_FILE" <<'EOF'
Mod1 F2 :ClientMenu (title=cm-a)
Mod1 F1 :CustomMenu custom.menu
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --workspaces 1 \
  --config-dir "$CFGDIR" \
  --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

# Ensure deterministic menu position.
./fbwl-input-injector --socket "$SOCKET" motion 100 100 >/dev/null 2>&1 || true

# CustomMenu: load custom.menu and execute its first item.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f1 >/dev/null 2>&1
START=$((OFFSET + 1))

timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'CustomMenu: loaded '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Menu: open at '; do sleep 0.05; done"

open_line="$(tail -c +$START "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected Menu open log line after CustomMenu (log=$LOG)" >&2
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

if [[ "$MENU_ITEMS" != "1" ]]; then
  echo "expected 1 custom menu item, got $MENU_ITEMS (line=$open_line log=$LOG)" >&2
  exit 1
fi

ITEM_H=24
MENU_TITLE_H=$ITEM_H

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$((MENU_X + 10))" "$((MENU_Y + MENU_TITLE_H + 10))" >/dev/null 2>&1
timeout 5 bash -c "until [[ -f '$MARKER' ]]; do sleep 0.05; done"

if ! rg -q '^ok$' "$MARKER"; then
  echo "expected marker file to contain ok (marker=$MARKER log=$LOG)" >&2
  exit 1
fi

# ClientMenu: filter by pattern.
./fbwl-smoke-client --socket "$SOCKET" --title cm-a --stay-ms 10000 >/dev/null 2>&1 &
A_PID=$!
timeout 5 bash -c "until rg -q 'Focus: cm-a' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title cm-b --stay-ms 10000 >/dev/null 2>&1 &
B_PID=$!
timeout 5 bash -c "until rg -q 'Focus: cm-b' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f2 >/dev/null 2>&1
START=$((OFFSET + 1))

timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Menu: open at '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'ClientMenu: item idx=0 title=cm-a '; do sleep 0.05; done"

if tail -c +$START "$LOG" | rg -q 'ClientMenu: item idx=[0-9]+ title=cm-b '; then
  echo "expected ClientMenu(pattern) to exclude cm-b (log=$LOG)" >&2
  exit 1
fi

open_line="$(tail -c +$START "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected Menu open log line after ClientMenu binding (log=$LOG)" >&2
  exit 1
fi

MENU_X=$(echo "$open_line" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
MENU_Y=$(echo "$open_line" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$((MENU_X + 10))" "$((MENU_Y + MENU_TITLE_H + 10))" >/dev/null 2>&1
AFTER_CLICK=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$AFTER_CLICK '$LOG' | rg -q 'Focus: cm-a'; do sleep 0.05; done"

echo "ok: custommenu + clientmenu(pattern) smoke passed"
