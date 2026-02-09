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
LOG="${LOG:-/tmp/fluxbox-wayland-nofocus-typing-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-nofocus-typing-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${C_PID:-}" ]]; then kill "$C_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.screen0.focusModel: ClickToFocus
session.screen0.focusNewWindows: true
session.screen0.noFocusWhileTypingDelay: 2000
session.screen0.demandsAttentionTimeout: 100
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title typing-a --stay-ms 20000 >/dev/null 2>&1 &
A_PID=$!
timeout 5 bash -c "until rg -q 'Focus: typing-a' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" type "abc"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title typing-b --stay-ms 20000 >/dev/null 2>&1 &
B_PID=$!
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Focus: blocked by typing delay target=typing-b'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Attention: start title=typing-b'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'OSD: show attention title=typing-b'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: iconbar attention title=typing-b'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Attention: toggle title=typing-b'; do sleep 0.05; done"

if rg -q 'Focus: typing-b' "$LOG"; then
  echo "unexpected focus steal: typing-b was focused despite noFocusWhileTypingDelay" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" key enter

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title typing-c --stay-ms 20000 >/dev/null 2>&1 &
C_PID=$!
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Focus: typing-c'; do sleep 0.05; done"

echo "ok: noFocusWhileTypingDelay smoke passed (socket=$SOCKET log=$LOG cfgdir=$CFGDIR)"
