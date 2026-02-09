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
LOG="${LOG:-/tmp/fluxbox-wayland-setenv-export-$UID-$$.log}"
KEYS_FILE="$(mktemp "/tmp/fbwl-keys-setenv-export-$UID-XXXXXX.keys")"
MARK="${MARK:-/tmp/fbwl-setenv-export-mark-$UID-$$}"

ENV1="FBWL_SMOKE_SETENV_${UID}_$$"
ENV2="FBWL_SMOKE_EXPORT_${UID}_$$"

cleanup() {
  rm -f "$KEYS_FILE" "$MARK" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
rm -f "$MARK"

cat >"$KEYS_FILE" <<EOF
Mod1 1 :SetEnv $ENV1 foo
Mod1 2 :Export $ENV2=bar
Mod1 3 :ExecCommand test "\$${ENV1}" = foo && test "\$${ENV2}" = bar && touch '$MARK'
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title setenv-export --stay-ms 15000 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Focus: setenv-export' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'SetEnv: set $ENV1'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'SetEnv: set $ENV2'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-3
timeout 2 bash -c "until [[ -f '$MARK' ]]; do sleep 0.05; done"

echo "ok: setenv/export smoke passed (socket=$SOCKET log=$LOG keys=$KEYS_FILE)"

