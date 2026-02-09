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
LOG="${LOG:-/tmp/fluxbox-wayland-clientpattern-quirk-$UID-$$.log}"
APPS_FILE="$(mktemp "/tmp/fbwl-apps-clientpattern-quirk-$UID-XXXXXX.apps")"

cleanup() {
  rm -f "$APPS_FILE" 2>/dev/null || true
  if [[ -n "${APP_PID:-}" ]]; then kill "$APP_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

# Fluxbox/X11 quirk: anchored regex is "^PATTERN$" (no extra parens), so "|"
# binds looser than the anchors. This pattern should match strings that start
# with "foo" OR end with "bar".
cat >"$APPS_FILE" <<'EOF'
[app] (title=foo|bar)
  [Workspace] {1}
  [Jump] {yes}
[end]
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --workspaces 2 \
  --apps "$APPS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title foo123 --stay-ms 8000 --xdg-decoration >/dev/null 2>&1 &
APP_PID=$!

timeout 10 bash -c "until rg -q 'Workspace: switch head=0 ws=2 reason=apps-jump' '$LOG'; do sleep 0.05; done"

echo "ok: clientpattern regex quirk parity smoke passed (socket=$SOCKET log=$LOG apps=$APPS_FILE)"
