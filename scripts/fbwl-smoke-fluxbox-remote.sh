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

fbr() {
  DISPLAY='' ./fluxbox-remote --wayland --socket "$SOCKET" "$@"
}

expect_err() {
  local expected_re="$1"
  shift

  set +e
  local out
  out="$("$@" 2>&1)"
  local rc=$?
  set -e

  if ((rc == 0)); then
    echo "expected non-zero exit status but command succeeded: $*" >&2
    echo "$out" >&2
    exit 1
  fi

  echo "$out" | rg -q "$expected_re" || {
    echo "expected error matching: $expected_re" >&2
    echo "$out" >&2
    exit 1
  }
}

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-fluxbox-remote-$UID-$$.log}"

cleanup() {
  if [[ -n "${B_PID:-}" ]]; then
    kill "$B_PID" 2>/dev/null || true
    wait "$B_PID" 2>/dev/null || true
  fi
  if [[ -n "${A_PID:-}" ]]; then
    kill "$A_PID" 2>/dev/null || true
    wait "$A_PID" 2>/dev/null || true
  fi
  if [[ -n "${FBW_PID:-}" ]]; then
    kill "$FBW_PID" 2>/dev/null || true
    wait "$FBW_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

: >"$LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --workspaces 3 \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

fbr ping | rg -q '^ok pong$'
fbr get-workspace | rg -q '^ok workspace=1$'

expect_err '^err empty_command$' fbr " "
expect_err '^err workspace_requires_number$' fbr workspace
expect_err '^err invalid_workspace_number$' fbr workspace 0
expect_err '^err invalid_workspace_number$' fbr workspace nope
expect_err '^err workspace_out_of_range$' fbr workspace 4
expect_err '^err unknown_command$' fbr does-not-exist

./fbwl-smoke-client --socket "$SOCKET" --title fbr-a --stay-ms 20000 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title fbr-b --stay-ms 20000 >/dev/null 2>&1 &
B_PID=$!

timeout 5 bash -c "until rg -q 'Focus: fbr-a' '$LOG' && rg -q 'Focus: fbr-b' '$LOG'; do sleep 0.05; done"

FOCUSED_LINE="$(rg -o 'Focus: fbr-[ab]' "$LOG" | tail -n 1)"
FOCUSED_VIEW="${FOCUSED_LINE#Focus: }"
case "$FOCUSED_VIEW" in
  fbr-a) OTHER_VIEW=fbr-b ;;
  fbr-b) OTHER_VIEW=fbr-a ;;
  *) echo "failed to determine focused view (got: $FOCUSED_VIEW)" >&2; exit 1 ;;
esac

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
fbr focusnext | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -F -q \"Focus: $OTHER_VIEW\"; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
fbr nextwindow | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -F -q \"Focus: $FOCUSED_VIEW\"; do sleep 0.05; done"

fbr workspace 2 | rg -q '^ok workspace=2$'
timeout 5 bash -c "until rg -q 'Workspace: apply current=2 reason=ipc' '$LOG'; do sleep 0.05; done"
fbr get-workspace | rg -q '^ok workspace=2$'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
fbr nextworkspace | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Workspace: apply current=3 reason=ipc'; do sleep 0.05; done"
fbr get-workspace | rg -q '^ok workspace=3$'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
fbr nextworkspace | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Workspace: apply current=1 reason=ipc'; do sleep 0.05; done"
fbr get-workspace | rg -q '^ok workspace=1$'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
fbr prevworkspace | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Workspace: apply current=3 reason=ipc'; do sleep 0.05; done"
fbr get-workspace | rg -q '^ok workspace=3$'

fbr exit | rg -q '^ok quitting$'
timeout 5 bash -c "while kill -0 '$FBW_PID' 2>/dev/null; do sleep 0.05; done"
wait "$FBW_PID"
unset FBW_PID

echo "ok: fluxbox-remote (Wayland IPC) smoke passed (socket=$SOCKET log=$LOG)"
