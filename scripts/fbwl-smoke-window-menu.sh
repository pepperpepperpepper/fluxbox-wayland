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
LOG="${LOG:-/tmp/fluxbox-wayland-window-menu-$UID-$$.log}"

cleanup() {
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --workspaces 2 \
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

./fbwl-smoke-client --socket "$SOCKET" --title client-winmenu --stay-ms 10000 --xdg-decoration >/dev/null 2>&1 &
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

# Right-click the titlebar to open the window menu.
TB_X=$((X0 + 10))
TB_Y=$((Y0 - TITLE_H + 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right "$TB_X" "$TB_Y" "$TB_X" "$TB_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: open-window title=client-winmenu'

# Click the first item ("Close").
MENU_X=$TB_X
MENU_Y=$TB_Y

CLICK_X=$((MENU_X + 10))
CLICK_Y=$((MENU_Y + TITLE_H / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: window-close title=client-winmenu'

timeout 5 bash -c "while kill -0 '$CLIENT_PID' 2>/dev/null; do sleep 0.05; done"

echo "ok: window-menu smoke passed (socket=$SOCKET log=$LOG)"
