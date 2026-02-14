#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd wc

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-doubleclick-$UID-$$.log}"
MARK_SINGLE="${MARK_SINGLE:-/tmp/fbwl-doubleclick-single-$UID-$$}"
MARK_DOUBLE="${MARK_DOUBLE:-/tmp/fbwl-doubleclick-double-$UID-$$}"
KEYS_FILE="$(mktemp /tmp/fbwl-doubleclick-keys-XXXXXX.conf)"

cleanup() {
  rm -f "$MARK_SINGLE" "$MARK_DOUBLE" "$KEYS_FILE" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
rm -f "$MARK_SINGLE" "$MARK_DOUBLE"

cat >"$KEYS_FILE" <<EOF
OnTitlebar Mouse1 :ExecCommand touch '$MARK_SINGLE'
OnTitlebar Double Mouse1 :ExecCommand touch '$MARK_DOUBLE'
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

toolbar_line="$(rg -m1 'Toolbar: position ' "$LOG")"
if [[ "$toolbar_line" =~ thickness=([0-9]+) ]]; then
  TITLE_H="${BASH_REMATCH[1]}"
else
  echo "failed to parse toolbar title height: $toolbar_line" >&2
  exit 1
fi

./fbwl-smoke-client --socket "$SOCKET" --title client-doubleclick --stay-ms 10000 --xdg-decoration >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Place: client-doubleclick ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: client-doubleclick ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

TB_X=$((X0 + 10))
TB_Y=$((Y0 - TITLE_H + 2))

./fbwl-input-injector --socket "$SOCKET" click "$TB_X" "$TB_Y"
timeout 2 bash -c "until [[ -f '$MARK_SINGLE' ]]; do sleep 0.05; done"
if [[ -f "$MARK_DOUBLE" ]]; then
  echo "double-click binding fired on single click (MARK_DOUBLE exists: $MARK_DOUBLE)" >&2
  exit 1
fi

rm -f "$MARK_SINGLE" "$MARK_DOUBLE"

./fbwl-input-injector --socket "$SOCKET" click "$TB_X" "$TB_Y" "$TB_X" "$TB_Y"
timeout 2 bash -c "until [[ -f '$MARK_DOUBLE' ]]; do sleep 0.05; done"

echo "ok: double-click mouse binding smoke passed (socket=$SOCKET log=$LOG)"
