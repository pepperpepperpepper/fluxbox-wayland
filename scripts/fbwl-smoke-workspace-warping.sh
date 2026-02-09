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
LOG="${LOG:-/tmp/fluxbox-wayland-workspace-warping-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-workspace-warping-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.screen0.toolbar.visible: false
session.screen0.workspacewarping: true
session.screen0.workspacewarpinghorizontal: true
session.screen0.workspacewarpingvertical: false
session.screen0.workspacewarpinghorizontaloffset: 1
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --workspaces 2 \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'OutputLayout: ' '$LOG'; do sleep 0.05; done"

OUTLINE="$(rg -m1 'OutputLayout: ' "$LOG")"
OUT_X="$(echo "$OUTLINE" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)"
OUT_Y="$(echo "$OUTLINE" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)"
OUT_W="$(echo "$OUTLINE" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)"

./fbwl-smoke-client --socket "$SOCKET" --title client-warp --stay-ms 10000 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Place: client-warp ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: client-warp ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
START_X=$((X0 + 10))
START_Y=$((Y0 + 10))
END_X=$((OUT_X + OUT_W - 1))
END_Y=$START_Y

./fbwl-input-injector --socket "$SOCKET" drag-alt-left "$START_X" "$START_Y" "$END_X" "$END_Y"

tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Policy: workspace switch( head=[0-9]+)? to 2'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=2 reason=warp'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Policy: move focused to workspace 2'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: view=client-warp ws=2 visible=1'

echo "ok: workspace warping smoke passed (socket=$SOCKET log=$LOG)"
