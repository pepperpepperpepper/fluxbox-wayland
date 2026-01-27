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
LOG="${LOG:-/tmp/fluxbox-wayland-ipc-$UID-$$.log}"
KEYS_FILE="${KEYS_FILE:-/tmp/fbwl-ipc-keys-$UID-$$.conf}"
APPS_FILE="${APPS_FILE:-/tmp/fbwl-ipc-apps-$UID-$$.conf}"
STYLE_FILE="${STYLE_FILE:-/tmp/fbwl-ipc-style-$UID-$$.conf}"
MENU_FILE="${MENU_FILE:-/tmp/fbwl-ipc-menu-$UID-$$.menu}"

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
  rm -f "$KEYS_FILE" "$APPS_FILE" "$STYLE_FILE" "$MENU_FILE" 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$KEYS_FILE" <<EOF
# keys for ipc smoke
EOF

cat >"$APPS_FILE" <<EOF
# apps rules for ipc smoke
EOF

cat >"$STYLE_FILE" <<EOF
window.borderWidth: 7
EOF

cat >"$MENU_FILE" <<EOF
[begin] (Fluxbox)
[exec] (noop) {true}
[exit] (Exit)
[end]
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --workspaces 3 \
  --keys "$KEYS_FILE" \
  --apps "$APPS_FILE" \
  --style "$STYLE_FILE" \
  --menu "$MENU_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

./fbwl-remote --socket "$SOCKET" ping | rg -q '^ok pong$'
./fbwl-remote --socket "$SOCKET" get-workspace | rg -q '^ok workspace=1$'
./fbwl-remote --socket "$SOCKET" dump-config | rg -F -q "keys_file=$KEYS_FILE"
./fbwl-remote --socket "$SOCKET" dump-config | rg -F -q "apps_file=$APPS_FILE"
./fbwl-remote --socket "$SOCKET" dump-config | rg -F -q "style_file=$STYLE_FILE"
./fbwl-remote --socket "$SOCKET" dump-config | rg -F -q "menu_file=$MENU_FILE"

expect_err '^err empty_command$' ./fbwl-remote --socket "$SOCKET" " "
expect_err '^err workspace_requires_number$' ./fbwl-remote --socket "$SOCKET" workspace
expect_err '^err invalid_workspace_number$' ./fbwl-remote --socket "$SOCKET" workspace 0
expect_err '^err invalid_workspace_number$' ./fbwl-remote --socket "$SOCKET" workspace nope
expect_err '^err workspace_out_of_range$' ./fbwl-remote --socket "$SOCKET" workspace 4
expect_err '^err unknown_command$' ./fbwl-remote --socket "$SOCKET" does-not-exist

cat >"$STYLE_FILE" <<EOF
window.borderWidth: 9
EOF

cat >"$MENU_FILE" <<EOF
[begin] (Fluxbox)
[exec] (noop) {true}
[nop] (NoOp)
[exit] (Exit)
[end]
EOF

cat >"$APPS_FILE" <<EOF
[app] (title=ipc-.*)
  [Sticky] {yes}
[end]
EOF

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" reconfigure | rg -q '^ok reconfigure$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: reloaded keys from '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: reloaded apps from '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: reloaded style from '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Reconfigure: reloaded menu from '; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title ipc-a --stay-ms 20000 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title ipc-b --stay-ms 20000 >/dev/null 2>&1 &
B_PID=$!

timeout 5 bash -c "until rg -q 'Focus: ipc-a' '$LOG' && rg -q 'Focus: ipc-b' '$LOG'; do sleep 0.05; done"

FOCUSED_LINE="$(rg -o 'Focus: ipc-[ab]' "$LOG" | tail -n 1)"
FOCUSED_VIEW="${FOCUSED_LINE#Focus: }"
case "$FOCUSED_VIEW" in
  ipc-a) OTHER_VIEW=ipc-b ;;
  ipc-b) OTHER_VIEW=ipc-a ;;
  *) echo "failed to determine focused view (got: $FOCUSED_VIEW)" >&2; exit 1 ;;
esac

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" focus-next | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -F -q \"Focus: $OTHER_VIEW\"; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" nextwindow | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -F -q \"Focus: $FOCUSED_VIEW\"; do sleep 0.05; done"

./fbwl-remote --socket "$SOCKET" workspace 2 | rg -q '^ok workspace=2$'
timeout 5 bash -c "until rg -q 'Workspace: apply current=2 reason=ipc' '$LOG'; do sleep 0.05; done"
./fbwl-remote --socket "$SOCKET" get-workspace | rg -q '^ok workspace=2$'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" nextworkspace | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Workspace: apply current=3 reason=ipc'; do sleep 0.05; done"
./fbwl-remote --socket "$SOCKET" get-workspace | rg -q '^ok workspace=3$'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" nextworkspace | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Workspace: apply current=1 reason=ipc'; do sleep 0.05; done"
./fbwl-remote --socket "$SOCKET" get-workspace | rg -q '^ok workspace=1$'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" prevworkspace | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Workspace: apply current=3 reason=ipc'; do sleep 0.05; done"
./fbwl-remote --socket "$SOCKET" get-workspace | rg -q '^ok workspace=3$'

./fbwl-remote --socket "$SOCKET" quit | rg -q '^ok quitting$'
timeout 5 bash -c "while kill -0 '$FBW_PID' 2>/dev/null; do sleep 0.05; done"
wait "$FBW_PID"
unset FBW_PID

echo "ok: ipc smoke passed (socket=$SOCKET log=$LOG)"
