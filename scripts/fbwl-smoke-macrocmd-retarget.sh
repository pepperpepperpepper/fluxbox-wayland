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

SOCKET="${SOCKET:-wayland-fbwl-macro-retarget-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-macro-retarget-$UID-$$.log}"
KEYS_FILE="$(mktemp /tmp/fbwl-keys-macro-retarget-XXXXXX)"

t_a="mc-a"
t_b="mc-b"

cleanup() {
  rm -f "$KEYS_FILE" 2>/dev/null || true
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$KEYS_FILE" <<EOF
Mod1 1 :GotoWindow 1 (title=$t_a)
Mod1 2 :GotoWindow 1 (title=$t_b)
Mod1 3 :MacroCmd {GotoWindow 1 (title=$t_b)} {MoveTo 0 0}
EOF

: >"$LOG"

env WLR_BACKENDS=headless WLR_RENDERER=pixman \
  ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --keys "$KEYS_FILE" --workspaces 1 \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title "$t_a" --stay-ms 20000 --xdg-decoration --width 260 --height 160 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title "$t_b" --stay-ms 20000 --xdg-decoration --width 260 --height 160 >/dev/null 2>&1 &
B_PID=$!

timeout 10 bash -c "until rg -q 'Place: ${t_a} ' '$LOG' && rg -q 'Place: ${t_b} ' '$LOG'; do sleep 0.05; done"

focus_by_key() {
  local key="$1"
  local title="$2"
  ./fbwl-input-injector --socket "$SOCKET" key "$key"
  timeout 5 bash -c "until rg 'Focus:' '$LOG' | tail -n 1 | rg -q 'Focus: ${title}'; do sleep 0.05; done"
}

focus_by_key alt-1 "$t_a"

# Macro should retarget after GotoWindow, so MoveTo applies to mc-b, not mc-a.
./fbwl-input-injector --socket "$SOCKET" key alt-3
timeout 5 bash -c "until rg -q 'MoveTo: ${t_b} ' '$LOG'; do sleep 0.05; done"

if rg -q "MoveTo: ${t_a} " "$LOG"; then
  echo "error: MacroCmd retargeting failed: MoveTo applied to ${t_a} (expected ${t_b})" >&2
  exit 1
fi

echo "ok: MacroCmd retarget smoke passed (socket=$SOCKET log=$LOG keys=$KEYS_FILE)"

