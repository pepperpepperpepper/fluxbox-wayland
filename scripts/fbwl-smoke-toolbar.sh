#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-toolbar-$UID-$$.log}"

cleanup() {
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --workspaces 4 \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

line="$(rg -m1 'Toolbar: position ' "$LOG")"
if [[ "$line" =~ x=([-0-9]+)\ y=([-0-9]+)\ h=([0-9]+)\ cell_w=([0-9]+)\ workspaces=([0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
  H="${BASH_REMATCH[3]}"
  CELL_W="${BASH_REMATCH[4]}"
  WS="${BASH_REMATCH[5]}"
else
  echo "failed to parse Toolbar: position line: $line" >&2
  exit 1
fi

if [[ "$WS" -lt 2 ]]; then
  echo "unexpected workspace count from toolbar: $WS" >&2
  exit 1
fi

# Click workspace 2 via toolbar (index 1).
CLICK_X=$((X0 + CELL_W + CELL_W / 2))
CLICK_Y=$((Y0 + H / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Toolbar: click workspace=2'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=2 reason=toolbar'

echo "ok: toolbar smoke passed (socket=$SOCKET log=$LOG)"
