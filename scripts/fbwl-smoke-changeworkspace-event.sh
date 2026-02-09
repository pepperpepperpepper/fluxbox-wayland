#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout

if [[ ! -x ./fbwl-remote ]]; then
  echo "missing ./fbwl-remote (build first)" >&2
  exit 1
fi

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-changews-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-changeworkspace-$UID-$$.log}"
CFG_DIR="$(mktemp -d /tmp/fbwl-changews-$UID-XXXXXX)"
MARK_WS="/tmp/fbwl-changews-$UID-$$"

cleanup() {
  rm -f "$MARK_WS" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFG_DIR" 2>/dev/null || true
}
trap cleanup EXIT

rm -f "$MARK_WS"

cat >"$CFG_DIR/init" <<EOF
session.screen0.workspaces: 3
session.screen0.allowRemoteActions: true
session.keyFile: keys
EOF

cat >"$CFG_DIR/keys" <<EOF
ChangeWorkspace :ExecCommand touch '$MARK_WS'
EOF

: >"$LOG"
WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFG_DIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until ./fbwl-remote --socket \"$SOCKET\" get-workspace | rg -q '^ok workspace=1$'; do sleep 0.05; done"

./fbwl-remote --socket "$SOCKET" workspace 2 | rg -q '^ok workspace=2$'
timeout 2 bash -c "until [[ -f '$MARK_WS' ]]; do sleep 0.05; done"

echo "ok: ChangeWorkspace special-event smoke passed (socket=$SOCKET log=$LOG)"
