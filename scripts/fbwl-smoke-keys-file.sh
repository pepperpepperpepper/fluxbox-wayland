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
LOG="${LOG:-/tmp/fluxbox-wayland-keys-$UID-$$.log}"
MARK_DEFAULT="${MARK_DEFAULT:-/tmp/fbwl-terminal-default-$UID-$$}"
MARK_OVERRIDE="${MARK_OVERRIDE:-/tmp/fbwl-keys-override-$UID-$$}"
KEYS_FILE="${KEYS_FILE:-/tmp/fbwl-keys-$UID-$$.conf}"

cleanup() {
  rm -f "$MARK_DEFAULT" "$MARK_OVERRIDE" "$KEYS_FILE" 2>/dev/null || true
  if [[ -n "${APP_PID:-}" ]]; then kill "$APP_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
rm -f "$MARK_DEFAULT" "$MARK_OVERRIDE"

cat >"$KEYS_FILE" <<EOF
# Minimal subset of Fluxbox ~/.fluxbox/keys syntax
Mod1 Return :ExecCommand touch '$MARK_OVERRIDE'
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --terminal "touch '$MARK_DEFAULT'" \
  --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title keys-client --stay-ms 10000 >/dev/null 2>&1 &
APP_PID=$!

timeout 5 bash -c "until rg -q 'Focus: keys-client' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-return

timeout 2 bash -c "until [[ -f '$MARK_OVERRIDE' ]]; do sleep 0.05; done"
if [[ -f "$MARK_DEFAULT" ]]; then
  echo "expected default terminal binding to be overridden (MARK_DEFAULT exists: $MARK_DEFAULT)" >&2
  exit 1
fi

echo "ok: keys file smoke passed (socket=$SOCKET log=$LOG keys_file=$KEYS_FILE)"

