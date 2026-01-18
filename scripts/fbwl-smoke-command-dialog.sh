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
LOG="${LOG:-/tmp/fluxbox-wayland-command-dialog-$UID-$$.log}"
MARKER="${MARKER:-/tmp/fbwl-cmd-dialog-marker-$UID-$$}"

cleanup() {
  rm -f "$MARKER" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
rm -f "$MARKER"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f2
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'CmdDialog: open'

CMD="touch $MARKER"
./fbwl-input-injector --socket "$SOCKET" type "$CMD"
./fbwl-input-injector --socket "$SOCKET" key enter

timeout 5 bash -c "until rg -F -q \"CmdDialog: execute cmd=$CMD\" '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until [[ -f '$MARKER' ]]; do sleep 0.05; done"

echo "ok: command dialog smoke passed (socket=$SOCKET log=$LOG)"

