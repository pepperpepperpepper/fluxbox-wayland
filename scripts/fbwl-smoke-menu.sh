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
MARKER="${MARKER:-/tmp/fbwl-menu-marker-$UID-$$}"

cleanup() {
  rm -f "$MENU_FILE" "$MARKER" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
rm -f "$MARKER"

cat >"$MENU_FILE" <<EOF
[begin] (Fluxbox)
[exec] (TouchMarker) {sh -c 'echo ok >"$MARKER"'}
[exit] (Exit)
[end]
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --menu "$MENU_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

# Open the root menu with a background right-click.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: open '

# Click the first menu item.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click 110 110
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Menu: exec '

timeout 5 bash -c "until [[ -f '$MARKER' ]]; do sleep 0.05; done"

echo "ok: menu smoke passed (socket=$SOCKET log=$LOG)"

