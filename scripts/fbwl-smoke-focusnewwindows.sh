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

SOCKET="${SOCKET:-wayland-fbwl-focusnew-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-focusnew-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-focusnew-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${C_PID:-}" ]]; then kill "$C_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<'EOF'
session.autoRaiseDelay: 0
session.screen0.toolbar.visible: false
session.screen0.allowRemoteActions: true
session.screen0.focusModel: ClickToFocus
session.screen0.autoRaise: false
session.screen0.clickRaises: false
session.screen0.focusNewWindows: false
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Init: focusModel=ClickToFocus .* focusNewWindows=0 ' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title fnew-a --stay-ms 20000 --xdg-decoration --width 280 --height 180 >/dev/null 2>&1 &
A_PID=$!
timeout 5 bash -c "until rg -q 'Place: fnew-a ' '$LOG'; do sleep 0.05; done"

place_a="$(rg -m1 'Place: fnew-a ' "$LOG")"
if [[ "$place_a" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
  A_X="${BASH_REMATCH[1]}"
  A_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_a" >&2
  exit 1
fi

# With focusNewWindows=false, mapping a new window should not steal focus. Focus explicitly by click.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$((A_X + 10))" "$((A_Y + 10))" >/dev/null 2>&1 || true
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Focus: fnew-a'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title fnew-b --stay-ms 20000 --xdg-decoration --width 280 --height 180 >/dev/null 2>&1 &
B_PID=$!
timeout 5 bash -c "until rg -q 'Place: fnew-b ' '$LOG'; do sleep 0.05; done"
START=$((OFFSET + 1))
if timeout 0.5 bash -c "until tail -c +$START '$LOG' | rg -q 'Focus: fnew-b'; do sleep 0.05; done"; then
  echo "unexpected Focus: fnew-b with focusNewWindows=false" >&2
  exit 1
fi

# Reconfigure focusNewWindows=true.
cat >"$CFGDIR/init" <<'EOF'
session.autoRaiseDelay: 0
session.screen0.toolbar.visible: false
session.screen0.allowRemoteActions: true
session.screen0.focusModel: ClickToFocus
session.screen0.autoRaise: false
session.screen0.clickRaises: false
session.screen0.focusNewWindows: true
EOF

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" reconfigure | rg -q '^ok reconfigure$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: reloaded init from '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: init focusModel=ClickToFocus .* focusNewWindows=1 '; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title fnew-c --stay-ms 20000 --xdg-decoration --width 280 --height 180 >/dev/null 2>&1 &
C_PID=$!
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Place: fnew-c '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Focus: fnew-c'; do sleep 0.05; done"

echo "ok: focusNewWindows parity smoke passed (socket=$SOCKET log=$LOG)"
