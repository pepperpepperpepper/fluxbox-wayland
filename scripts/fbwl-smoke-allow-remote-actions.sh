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

expect_err() {
  local expected_re="$1"
  shift

  set +e
  local out
  out="$("$@" 2>&1)"
  local rc=$?
  set -e

  if ((rc == 0)); then
    echo "expected non-zero exit status but command succeeded: $*" >&2
    echo "$out" >&2
    exit 1
  fi

  echo "$out" | rg -q "$expected_re" || {
    echo "expected error matching: $expected_re" >&2
    echo "$out" >&2
    exit 1
  }
}

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-allow-remote-actions-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-allow-remote-actions-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.screen0.focusModel: ClickToFocus
# Intentionally omit session.screen0.allowRemoteActions (Fluxbox/X11 default is false).
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

./fbwl-remote --socket "$SOCKET" ping | rg -q '^ok pong$'
expect_err '^err remote_actions_disabled$' ./fbwl-remote --socket "$SOCKET" get-workspace
expect_err '^err remote_actions_disabled$' ./fbwl-remote --socket "$SOCKET" reconfigure
expect_err '^err remote_actions_disabled$' ./fbwl-remote --socket "$SOCKET" quit

echo "ok: allowRemoteActions gate smoke passed (socket=$SOCKET log=$LOG cfgdir=$CFGDIR)"

