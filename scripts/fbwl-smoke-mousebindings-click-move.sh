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

SOCKET="${SOCKET:-wayland-fbwl-mousebind-cm-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-mousebind-click-move-$UID-$$.log}"
MARK_CLICK="${MARK_CLICK:-/tmp/fbwl-mousebind-click-$UID-$$}"
MARK_MOVE="${MARK_MOVE:-/tmp/fbwl-mousebind-move-$UID-$$}"
KEYS_FILE="$(mktemp /tmp/fbwl-mousebind-click-move-keys-XXXXXX.conf)"

cleanup() {
  rm -f "$MARK_CLICK" "$MARK_MOVE" "$KEYS_FILE" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
rm -f "$MARK_CLICK" "$MARK_MOVE"

cat >"$KEYS_FILE" <<EOF
OnDesktop Click1 :ExecCommand touch '$MARK_CLICK'
OnDesktop Move1 :ExecCommand touch '$MARK_MOVE'
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" click 10 200
timeout 2 bash -c "until [[ -f '$MARK_CLICK' ]]; do sleep 0.05; done"
if [[ -f "$MARK_MOVE" ]]; then
  echo "unexpected Move1 binding fired on click (MARK_MOVE exists: $MARK_MOVE)" >&2
  exit 1
fi

rm -f "$MARK_CLICK" "$MARK_MOVE"
./fbwl-input-injector --socket "$SOCKET" drag-left 10 200 30 200
timeout 2 bash -c "until [[ -f '$MARK_MOVE' ]]; do sleep 0.05; done"
if [[ -f "$MARK_CLICK" ]]; then
  echo "unexpected Click1 binding fired on drag (MARK_CLICK exists: $MARK_CLICK)" >&2
  exit 1
fi

rm -f "$MARK_CLICK" "$MARK_MOVE"
./fbwl-input-injector --socket "$SOCKET" drag-left 10 200 12 200
timeout 2 bash -c "until [[ -f '$MARK_CLICK' ]]; do sleep 0.05; done"
if [[ -f "$MARK_MOVE" ]]; then
  echo "unexpected Move1 binding fired under threshold (MARK_MOVE exists: $MARK_MOVE)" >&2
  exit 1
fi

echo "ok: ClickN/MoveN mouse binding semantics smoke passed (socket=$SOCKET log=$LOG)"

