#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout

need_exe ./fbwl-input-injector
need_exe ./fbwl-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-activate-focus-pattern-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-activate-focus-pattern-$UID-$$.log}"
KEYS_FILE="$(mktemp /tmp/fbwl-keys-activate-focus-pattern-XXXXXX)"

cleanup() {
  rm -f "$KEYS_FILE" 2>/dev/null || true
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$KEYS_FILE" <<'EOF'
Mod1 9 :GotoWindow 1 (title=af-b)
Mod1 8 :GotoWindow 1 (title=af-a)
Mod1 1 :Activate (title=af-a)
Mod1 2 :Focus (title=af-b)
EOF

: >"$LOG"

env WLR_BACKENDS=headless WLR_RENDERER=pixman \
  ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --keys "$KEYS_FILE" --workspaces 1 \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title af-a --stay-ms 20000 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title af-b --stay-ms 20000 >/dev/null 2>&1 &
B_PID=$!

timeout 10 bash -c "until rg -q 'Place: af-a ' '$LOG' && rg -q 'Place: af-b ' '$LOG'; do sleep 0.05; done"

focus_by_key() {
  local key="$1"
  local title="$2"
  ./fbwl-input-injector --socket "$SOCKET" key "$key"
  timeout 5 bash -c "until rg 'Focus:' '$LOG' | tail -n 1 | rg -q 'Focus: ${title}'; do sleep 0.05; done"
}

# Use GotoWindow as a helper so the test isn't circular.
focus_by_key alt-9 af-b
focus_by_key alt-1 af-a
focus_by_key alt-2 af-b

echo "ok: Activate/Focus pattern smoke passed (socket=$SOCKET log=$LOG keys=$KEYS_FILE)"

