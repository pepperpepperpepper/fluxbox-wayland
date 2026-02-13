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

source scripts/fbwl-smoke-report-lib.sh

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-left-click-menu-$UID-$$.log}"
KEYS_FILE="$(mktemp /tmp/fbwl-left-click-menu-keys-XXXXXX.conf)"
MENU_FILE="$(mktemp /tmp/fbwl-left-click-menu-XXXXXX.menu)"
REPORT_DIR="${FBWL_REPORT_DIR:-${FBWL_SMOKE_REPORT_DIR:-}}"

cleanup() {
  rm -f "$KEYS_FILE" "$MENU_FILE" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$KEYS_FILE" <<'EOF'
OnDesktop Mouse1 :RootMenu
OnDesktop Mouse3 :HideMenus
Mod1 Escape :Exit
EOF

cat >"$MENU_FILE" <<'EOF'
[begin] (Fluxbox)
[exec] (Terminal) {xterm}
[exec] (Web Browser) {firefox}
[exec] (Files) {thunar}
[exec] (Editor) {gedit}
[exec] (Settings) {lxappearance}
[exec] (Audio Mixer) {pavucontrol}
[exec] (System Monitor) {htop}
[separator]
[submenu] (Fluxbox) {Fluxbox}
  [config] (Configure)
  [restart] (Restart)
  [exit] (Exit)
[end]
[end]
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --keys "$KEYS_FILE" \
  --menu "$MENU_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

# Open the root menu with a left-click on the desktop background.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click 100 100
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line after left-click" >&2
  exit 1
fi

sleep 0.2
fbwl_report_shot "left-click-root-menu.png" "Root menu via left-click (common apps)"

echo "ok: left-click root menu screenshot smoke passed (socket=$SOCKET log=$LOG)"
