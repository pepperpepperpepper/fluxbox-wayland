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
LOG="${LOG:-/tmp/fluxbox-wayland-config-dir-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-config-dir-$UID-XXXXXX")"
MARK_DEFAULT="${MARK_DEFAULT:-/tmp/fbwl-config-dir-terminal-default-$UID-$$}"
MARK_OVERRIDE="${MARK_OVERRIDE:-/tmp/fbwl-config-dir-keys-override-$UID-$$}"
MARK_MENU="${MARK_MENU:-/tmp/fbwl-config-dir-menu-$UID-$$}"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  rm -f "$MARK_DEFAULT" "$MARK_OVERRIDE" "$MARK_MENU" 2>/dev/null || true
  if [[ -n "${APP_PID:-}" ]]; then kill "$APP_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
rm -f "$MARK_DEFAULT" "$MARK_OVERRIDE" "$MARK_MENU"

cat >"$CFGDIR/init" <<EOF
session.screen0.workspaces: 3
session.keyFile: mykeys
session.appsFile: myapps
session.styleFile: mystyle
session.menuFile: mymenu
EOF

cat >"$CFGDIR/mykeys" <<EOF
# Minimal subset of Fluxbox ~/.fluxbox/keys syntax
Mod1 Return :ExecCommand touch '$MARK_OVERRIDE'
EOF

cat >"$CFGDIR/myapps" <<EOF
[app] (app_id=fbwl-config-dir-jump)
  [Workspace] {1}
  [Jump] {yes}
[end]
EOF

cat >"$CFGDIR/mystyle" <<EOF
window.title.height: 33
EOF

cat >"$CFGDIR/mymenu" <<EOF
[begin] (Fluxbox)
[exec] (TouchMenuMarker) {touch '$MARK_MENU'}
[end]
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --terminal "touch '$MARK_DEFAULT'" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Menu: loaded ' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" drag-right 50 50 50 50
./fbwl-input-injector --socket "$SOCKET" click 60 60
timeout 2 bash -c "until [[ -f '$MARK_MENU' ]]; do sleep 0.05; done"

OUT="$(./fbwl-remote --socket "$SOCKET" workspace 4 || true)"
printf '%s\n' "$OUT" | rg -q '^err workspace_out_of_range$'

./fbwl-smoke-client --socket "$SOCKET" --title cfgdir-client --app-id fbwl-config-dir-jump --stay-ms 10000 >/dev/null 2>&1 &
APP_PID=$!

timeout 5 bash -c "until rg -q 'Focus: cfgdir-client' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until ./fbwl-remote --socket \"$SOCKET\" get-workspace | rg -q '^ok workspace=2$'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-return

timeout 2 bash -c "until [[ -f '$MARK_OVERRIDE' ]]; do sleep 0.05; done"
if [[ -f "$MARK_DEFAULT" ]]; then
  echo "expected config-dir keys binding to override default terminal binding (MARK_DEFAULT exists: $MARK_DEFAULT)" >&2
  exit 1
fi

echo "ok: config-dir smoke passed (socket=$SOCKET log=$LOG cfgdir=$CFGDIR)"
