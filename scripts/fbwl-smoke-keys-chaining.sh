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

SOCKET="${SOCKET:-wayland-fbwl-keys-chaining-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-keys-chaining-$UID-$$.log}"
MARK_CHAIN="${MARK_CHAIN:-/tmp/fbwl-keys-chain-$UID-$$}"
MARK_ALTF2="${MARK_ALTF2:-/tmp/fbwl-keys-chain-altf2-$UID-$$}"
KEYS_FILE="$(mktemp /tmp/fbwl-keys-chaining-XXXXXX.conf)"

cleanup() {
  rm -f "$MARK_CHAIN" "$MARK_ALTF2" "$KEYS_FILE" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
rm -f "$MARK_CHAIN" "$MARK_ALTF2"

cat >"$KEYS_FILE" <<EOF
Mod1 F1 Mod1 F2 :ExecCommand touch '$MARK_CHAIN'
Mod1 F2 :ExecCommand touch '$MARK_ALTF2'
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-f2
timeout 2 bash -c "until [[ -f '$MARK_ALTF2' ]]; do sleep 0.05; done"

rm -f "$MARK_CHAIN" "$MARK_ALTF2"
./fbwl-input-injector --socket "$SOCKET" key alt-f1
./fbwl-input-injector --socket "$SOCKET" key alt-f2
timeout 2 bash -c "until [[ -f '$MARK_CHAIN' ]]; do sleep 0.05; done"
if [[ -f "$MARK_ALTF2" ]]; then
  echo "unexpected default binding fired during chain (MARK_ALTF2 exists: $MARK_ALTF2)" >&2
  exit 1
fi

rm -f "$MARK_CHAIN" "$MARK_ALTF2"
./fbwl-input-injector --socket "$SOCKET" key alt-f1
./fbwl-input-injector --socket "$SOCKET" key escape
./fbwl-input-injector --socket "$SOCKET" key alt-f2
timeout 2 bash -c "until [[ -f '$MARK_ALTF2' ]]; do sleep 0.05; done"
if [[ -f "$MARK_CHAIN" ]]; then
  echo "unexpected chain binding fired after ESC abort (MARK_CHAIN exists: $MARK_CHAIN)" >&2
  exit 1
fi

echo "ok: keys chaining smoke passed (socket=$SOCKET log=$LOG)"

