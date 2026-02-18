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

SOCKET="${SOCKET:-wayland-fbwl-cache-resources-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-cache-resources-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-cache-resources-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<'EOF'
session.screen0.toolbar.visible: false
session.screen0.allowRemoteActions: true
session.cacheLife: 7
session.cacheMax: 321
session.colorsPerChannel: 8
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Init: globals .* cacheLife=7 cacheMax=321 colorsPerChannel=8 ' '$LOG'; do sleep 0.05; done"

cat >"$CFGDIR/init" <<'EOF'
session.screen0.toolbar.visible: false
session.screen0.allowRemoteActions: true
session.cacheLife: 0
session.cacheMax: 0
session.colorsPerChannel: 4
EOF

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" reconfigure | rg -q '^ok reconfigure$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: reloaded init from '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: init globals .* cacheLife=0 cacheMax=0 colorsPerChannel=4 '; do sleep 0.05; done"

echo "ok: cache resources smoke passed (socket=$SOCKET log=$LOG)"
