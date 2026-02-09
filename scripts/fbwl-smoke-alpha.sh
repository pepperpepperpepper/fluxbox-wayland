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
LOG="${LOG:-/tmp/fluxbox-wayland-alpha-$UID-$$.log}"
CFG_DIR="${CFG_DIR:-/tmp/fbwl-config-alpha-$UID-$$}"

TOOLBAR_ALPHA="${TOOLBAR_ALPHA:-64}"
MENU_ALPHA="${MENU_ALPHA:-128}"

cleanup() {
  rm -rf "$CFG_DIR" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

mkdir -p "$CFG_DIR"
cat >"$CFG_DIR/init" <<EOF
session.screen0.toolbar.alpha: $TOOLBAR_ALPHA
session.screen0.menu.alpha: $MENU_ALPHA
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFG_DIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q \"Toolbar: position .*alpha=$TOOLBAR_ALPHA\" '$LOG'; do sleep 0.05; done"

# Open the root menu with a background right-click and validate alpha is applied.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q \"Menu: open at .*alpha=$MENU_ALPHA\"; do sleep 0.05; done"

echo "ok: alpha resources smoke passed (toolbar_alpha=$TOOLBAR_ALPHA menu_alpha=$MENU_ALPHA socket=$SOCKET log=$LOG)"

