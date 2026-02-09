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

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-focusmodel-aliases-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-focusmodel-aliases-$UID-XXXXXX")"
KEYS_FILE="$(mktemp /tmp/fbwl-focusmodel-aliases-keys-XXXXXX.conf)"
APPS_FILE="$(mktemp /tmp/fbwl-focusmodel-aliases-apps-XXXXXX.conf)"
STYLE_FILE="$(mktemp /tmp/fbwl-focusmodel-aliases-style-XXXXXX.conf)"
MENU_FILE="$(mktemp /tmp/fbwl-focusmodel-aliases-menu-XXXXXX.conf)"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  rm -f "$KEYS_FILE" "$APPS_FILE" "$STYLE_FILE" "$MENU_FILE" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then
    kill "$FBW_PID" 2>/dev/null || true
    wait "$FBW_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

: >"$LOG"

cat >"$KEYS_FILE" <<'EOF'
# Keep a minimal keys file around; the test drives reconfigure via fbwl-remote.
Mod1 F1 :Reconfigure
EOF

: >"$APPS_FILE"

cat >"$STYLE_FILE" <<'EOF'
window.title.height: 22
EOF

cat >"$MENU_FILE" <<'EOF'
[begin] (Fluxbox)
[end]
EOF

cat >"$CFGDIR/init" <<'EOF'
session.screen0.focusModel: SloppyFocus
session.screen0.allowRemoteActions: true
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  --keys "$KEYS_FILE" \
  --apps "$APPS_FILE" \
  --style "$STYLE_FILE" \
  --menu "$MENU_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Init: focusModel=MouseFocus' '$LOG'; do sleep 0.05; done"

cat >"$CFGDIR/init" <<'EOF'
session.screen0.focusModel: SemiSloppyFocus
session.screen0.allowRemoteActions: true
EOF

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" reconfigure | rg -q '^ok reconfigure$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: init focusModel=MouseFocus'; do sleep 0.05; done"

kill -0 "$FBW_PID" >/dev/null 2>&1

kill "$FBW_PID"
wait "$FBW_PID"
unset FBW_PID

echo "ok: focusModel aliases smoke passed (socket=$SOCKET log=$LOG)"
