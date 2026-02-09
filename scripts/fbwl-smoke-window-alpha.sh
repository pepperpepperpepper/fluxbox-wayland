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
LOG="${LOG:-/tmp/fluxbox-wayland-window-alpha-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-window-alpha-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${DEFAULT_PID:-}" ]]; then kill "$DEFAULT_PID" 2>/dev/null || true; fi
  if [[ -n "${OVERRIDE_PID:-}" ]]; then kill "$OVERRIDE_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.screen0.focusModel: ClickToFocus
session.screen0.allowRemoteActions: true
session.appsFile: myapps
session.screen0.window.focus.alpha: 200
session.screen0.window.unfocus.alpha: 100
EOF

cat >"$CFGDIR/myapps" <<'EOF'
[app] (app_id=fbwl-alpha-override)
  [Alpha] {220 30}
[end]
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-alpha-default --title alpha-default --stay-ms 20000 >/dev/null 2>&1 &
DEFAULT_PID=$!
timeout 5 bash -c "until rg -q 'Alpha: alpha-default focused=200 unfocused=100 reason=init-default' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-alpha-override --title alpha-override --stay-ms 20000 >/dev/null 2>&1 &
OVERRIDE_PID=$!
timeout 5 bash -c "until rg -q 'Alpha: alpha-override focused=220 unfocused=30 reason=apps' '$LOG'; do sleep 0.05; done"

if rg -q 'Alpha: alpha-override .* reason=init-default' "$LOG"; then
  echo "unexpected: init-default alpha applied to app that has [Alpha] in apps file" >&2
  exit 1
fi

cat >"$CFGDIR/init" <<EOF
session.screen0.focusModel: ClickToFocus
session.screen0.allowRemoteActions: true
session.appsFile: myapps
session.screen0.window.focus.alpha: 180
session.screen0.window.unfocus.alpha: 90
EOF

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" reconfigure | rg -q '^ok'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Alpha: alpha-default focused=180 unfocused=90 reason=reconfigure-default'; do sleep 0.05; done"

if tail -c +$START "$LOG" | rg -q 'Alpha: alpha-override .* reason=reconfigure-default'; then
  echo "unexpected: reconfigure-default alpha applied to app that has [Alpha] in apps file" >&2
  exit 1
fi

echo "ok: window default alpha smoke passed (socket=$SOCKET log=$LOG cfgdir=$CFGDIR)"

