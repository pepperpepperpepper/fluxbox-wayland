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

fbr() {
  DISPLAY='' ./fluxbox-remote --wayland --socket "$SOCKET" "$@"
}

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-cli-rc-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-cli-rc-$UID-XXXXXX")"
RCFILE="$CFGDIR/rcfile.custom"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then
    kill "$FBW_PID" 2>/dev/null || true
    wait "$FBW_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

: >"$LOG"

cat >"$RCFILE" <<EOF
session.screen0.allowRemoteActions: true
session.screen0.workspaces: 2
session.keyFile: keys
session.appsFile: apps
session.styleFile: style
session.menuFile: menu

session.screen0.toolbar.visible: true
session.screen0.toolbar.tools: workspacename,clock
EOF

cat >"$CFGDIR/keys" <<EOF
# empty
EOF

cat >"$CFGDIR/apps" <<EOF
# empty
EOF

cat >"$CFGDIR/style" <<EOF
window.title.height: 33
EOF

cat >"$CFGDIR/menu" <<EOF
[begin] (Fluxbox)
[end]
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  -rc "$RCFILE" \
  -no-toolbar \
  -no-slit \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q \"Init: loaded .*rcfile\\.custom\" '$LOG'; do sleep 0.05; done"

rg -q "Init: toolbar visible=0" "$LOG"
if rg -q "Toolbar: built" "$LOG"; then
  echo "expected -no-toolbar to disable toolbar build (found 'Toolbar: built' in log)" >&2
  exit 1
fi
if rg -q "Slit: layout" "$LOG"; then
  echo "expected -no-slit to disable slit layout (found 'Slit: layout' in log)" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
fbr saverc | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'SaveRC: ok'; do sleep 0.05; done"

rg -q '^session\.screen0\.workspaceNames:' "$RCFILE"
if [[ -e "$CFGDIR/init" ]]; then
  echo "unexpected $CFGDIR/init created; expected SaveRC to update -rc file instead" >&2
  exit 1
fi

fbr exit | rg -q '^ok quitting$'
timeout 5 bash -c "while kill -0 '$FBW_PID' 2>/dev/null; do sleep 0.05; done"
wait "$FBW_PID"
unset FBW_PID

echo "ok: -rc/-no-toolbar/-no-slit parity smoke passed (socket=$SOCKET log=$LOG rcfile=$RCFILE)"
