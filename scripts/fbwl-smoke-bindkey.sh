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

SOCKET="${SOCKET:-wayland-fbwl-bindkey-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-bindkey-$UID-$$.log}"
KEYS_FILE="${KEYS_FILE:-/tmp/fbwl-bindkey-keys-$UID-$$.conf}"
MARK_OLD="${MARK_OLD:-/tmp/fbwl-bindkey-old-$UID-$$}"
MARK_NEW="${MARK_NEW:-/tmp/fbwl-bindkey-new-$UID-$$}"

cleanup() {
  rm -f "$KEYS_FILE" "$MARK_OLD" "$MARK_NEW" 2>/dev/null || true
  if [[ -n "${APP_PID:-}" ]]; then kill "$APP_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
rm -f "$KEYS_FILE" "$MARK_OLD" "$MARK_NEW"

cat >"$KEYS_FILE" <<EOF
Mod1 i :ExecCommand touch '$MARK_OLD'
Mod1 F2 :BindKey Mod1 i :ExecCommand touch '$MARK_NEW'
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title bindkey-client --stay-ms 10000 >/dev/null 2>&1 &
APP_PID=$!
timeout 5 bash -c "until rg -q 'Focus: bindkey-client' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-i
timeout 2 bash -c "until [[ -f '$MARK_OLD' ]]; do sleep 0.05; done"
rm -f "$MARK_OLD"

./fbwl-input-injector --socket "$SOCKET" key alt-f2
timeout 2 bash -c "until rg -q \"^Mod1 i :ExecCommand touch '$MARK_NEW'$\" '$KEYS_FILE'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-i
timeout 2 bash -c "until [[ -f '$MARK_NEW' ]]; do sleep 0.05; done"
if [[ -f "$MARK_OLD" ]]; then
  echo "expected new binding to override old binding (MARK_OLD exists: $MARK_OLD)" >&2
  exit 1
fi

echo "ok: BindKey smoke passed (socket=$SOCKET log=$LOG keys_file=$KEYS_FILE)"
