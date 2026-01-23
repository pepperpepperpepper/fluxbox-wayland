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
LOG="${LOG:-/tmp/fluxbox-wayland-workspaces-$UID-$$.log}"

cleanup() {
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --workspaces 3 \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title ws-a --stay-ms 10000 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title ws-b --stay-ms 10000 >/dev/null 2>&1 &
B_PID=$!

timeout 5 bash -c "until rg -q 'Focus: ws-a' '$LOG' && rg -q 'Focus: ws-b' '$LOG'; do sleep 0.05; done"

FOCUSED_VIEW=$(
  rg -o 'Focus: ws-[ab]' "$LOG" \
    | tail -n 1 \
    | awk '{print $2}'
)

case "$FOCUSED_VIEW" in
  ws-a) OTHER_VIEW=ws-b ;;
  ws-b) OTHER_VIEW=ws-a ;;
  *) echo "failed to determine focused view (got: $FOCUSED_VIEW)" >&2; exit 1 ;;
esac

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-2
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Policy: move focused to workspace 2 title=$FOCUSED_VIEW"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=1 reason=move-focused'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Workspace: view=$FOCUSED_VIEW ws=2 visible=0"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Focus: $OTHER_VIEW"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Policy: workspace switch to 2'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=2 reason=switch'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Workspace: view=$OTHER_VIEW ws=1 visible=0"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Workspace: view=$FOCUSED_VIEW ws=2 visible=1"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Focus: $FOCUSED_VIEW"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Policy: workspace switch to 1'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Workspace: apply current=1 reason=switch'
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Workspace: view=$OTHER_VIEW ws=1 visible=1"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Workspace: view=$FOCUSED_VIEW ws=2 visible=0"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Focus: $OTHER_VIEW"

echo "ok: workspaces smoke passed (socket=$SOCKET log=$LOG)"
