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

SOCKET="${SOCKET:-wayland-fbwl-clickraises-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-clickraises-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-clickraises-$UID-XXXXXX")"

BORDER=6
TITLE_H=21

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/style" <<EOF
window.borderWidth: $BORDER
window.title.height: $TITLE_H
EOF

cat >"$CFGDIR/init" <<EOF
session.autoRaiseDelay: 0
session.styleFile: style
session.screen0.toolbar.visible: false
session.screen0.allowRemoteActions: true
session.screen0.focusModel: ClickToFocus
session.screen0.focusNewWindows: false
session.screen0.autoRaise: false
session.screen0.clickRaises: true
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Init: focusModel=ClickToFocus .* clickRaises=1 ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded ' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title cr-client --stay-ms 20000 --xdg-decoration --width 360 --height 240 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Place: cr-client ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: cr-client ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

CLICK_CLIENT_X=$((X0 + 10))
CLICK_CLIENT_Y=$((Y0 + 10))
CLICK_TITLE_X=$((X0 + 10))
CLICK_TITLE_Y=$((Y0 - BORDER - TITLE_H / 2))

# clickRaises=true: click inside client surface should raise.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_CLIENT_X" "$CLICK_CLIENT_Y" >/dev/null 2>&1 || true
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Raise: cr-client reason=click'; do sleep 0.05; done"

# Reconfigure clickRaises=false.
cat >"$CFGDIR/init" <<EOF
session.autoRaiseDelay: 0
session.styleFile: style
session.screen0.toolbar.visible: false
session.screen0.allowRemoteActions: true
session.screen0.focusModel: ClickToFocus
session.screen0.focusNewWindows: false
session.screen0.autoRaise: false
session.screen0.clickRaises: false
EOF

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" reconfigure | rg -q '^ok reconfigure$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: reloaded init from '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: init focusModel=ClickToFocus .* clickRaises=0 '; do sleep 0.05; done"

# clickRaises=false: click inside client surface should NOT raise (decor-only behavior).
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_CLIENT_X" "$CLICK_CLIENT_Y" >/dev/null 2>&1 || true
START=$((OFFSET + 1))
if timeout 0.5 bash -c "until tail -c +$START '$LOG' | rg -q 'Raise: cr-client reason=click'; do sleep 0.05; done"; then
  echo "unexpected raise on client-surface click when clickRaises=false" >&2
  exit 1
fi

# clickRaises=false: clicking decorations SHOULD raise.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_TITLE_X" "$CLICK_TITLE_Y" >/dev/null 2>&1 || true
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Raise: cr-client reason=decor'; do sleep 0.05; done"

echo "ok: clickRaises parity smoke passed (socket=$SOCKET log=$LOG)"
