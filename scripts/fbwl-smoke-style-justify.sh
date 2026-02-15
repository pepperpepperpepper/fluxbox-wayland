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
LOG="${LOG:-/tmp/fluxbox-wayland-style-justify-$UID-$$.log}"
STYLE_FILE="${STYLE_FILE:-/tmp/fbwl-style-justify-$UID-$$.cfg}"
CFG_DIR="$(mktemp -d "/tmp/fbwl-style-justify-$UID-XXXXXX")"

cleanup() {
  rm -f "$STYLE_FILE" 2>/dev/null || true
  rm -rf "$CFG_DIR" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$CFG_DIR/init" <<'EOF'
session.screen0.toolbar.visible: true
session.screen0.toolbar.autoHide: false
session.screen0.toolbar.tools: clock
session.screen0.strftimeFormat: FMTTEST
session.screen0.allowRemoteActions: true

# Keep the titlebar clear so the title buffer uses full width (no reserved button areas).
session.titlebar.left:
session.titlebar.right:
EOF

cat >"$STYLE_FILE" <<'EOF'
window.title.height: 32
window.font: monospace-10
window.justify: Right

toolbar.height: 32
toolbar.font: monospace-10
toolbar.textColor: #ffffff
toolbar.clock.justify: Right
EOF

: >"$LOG"

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFG_DIR" \
  --style "$STYLE_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: clock text=FMTTEST justify=2' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title style-justify --stay-ms 10000 --xdg-decoration >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Decor: title-render style-justify justify=2' '$LOG'; do sleep 0.05; done"

echo "ok: style justify smoke passed (socket=$SOCKET log=$LOG)"
