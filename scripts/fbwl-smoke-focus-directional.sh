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

SOCKET="${SOCKET:-wayland-fbwl-dirfocus-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-dirfocus-$UID-$$.log}"
KEYS_FILE="$(mktemp /tmp/fbwl-keys-dirfocus-XXXXXX)"

t_center="df-center"
t_left="df-left"
t_right="df-right"
t_up="df-up"
t_down="df-down"

cleanup() {
  rm -f "$KEYS_FILE" 2>/dev/null || true
  if [[ -n "${CENTER_PID:-}" ]]; then kill "$CENTER_PID" 2>/dev/null || true; fi
  if [[ -n "${LEFT_PID:-}" ]]; then kill "$LEFT_PID" 2>/dev/null || true; fi
  if [[ -n "${RIGHT_PID:-}" ]]; then kill "$RIGHT_PID" 2>/dev/null || true; fi
  if [[ -n "${UP_PID:-}" ]]; then kill "$UP_PID" 2>/dev/null || true; fi
  if [[ -n "${DOWN_PID:-}" ]]; then kill "$DOWN_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$KEYS_FILE" <<EOF
Mod1 f :GotoWindow 1 (title=$t_center)

Mod1 1 :GotoWindow 1 (title=$t_center)
Mod1 2 :GotoWindow 1 (title=$t_left)
Mod1 3 :GotoWindow 1 (title=$t_right)
Mod1 4 :GotoWindow 1 (title=$t_up)
Mod1 5 :GotoWindow 1 (title=$t_down)

Mod1 Control 1 :MoveTo 40% 40%
Mod1 Control 2 :MoveTo 10% 40%
Mod1 Control 3 :MoveTo 70% 40%
Mod1 Control 4 :MoveTo 40% 10%
Mod1 Control 5 :MoveTo 40% 70%

Left :FocusLeft
Right :FocusRight
Up :FocusUp
Down :FocusDown
EOF

: >"$LOG"

env WLR_BACKENDS=headless WLR_RENDERER=pixman \
  ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --keys "$KEYS_FILE" --workspaces 1 \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

client_spawn() {
  local title="$1"
  local var_pid="$2"
  ./fbwl-smoke-client --socket "$SOCKET" --title "$title" --stay-ms 20000 --xdg-decoration --width 260 --height 160 >/dev/null 2>&1 &
  printf -v "$var_pid" '%s' "$!"
}

client_spawn "$t_center" CENTER_PID
client_spawn "$t_left" LEFT_PID
client_spawn "$t_right" RIGHT_PID
client_spawn "$t_up" UP_PID
client_spawn "$t_down" DOWN_PID

timeout 10 bash -c "until rg -q 'Place: ${t_center} ' '$LOG' && rg -q 'Place: ${t_left} ' '$LOG' && rg -q 'Place: ${t_right} ' '$LOG' && rg -q 'Place: ${t_up} ' '$LOG' && rg -q 'Place: ${t_down} ' '$LOG'; do sleep 0.05; done"

key() {
  ./fbwl-input-injector --socket "$SOCKET" key "$1"
}

focus_by_key() {
  local keyname="$1"
  local title="$2"
  key "$keyname"
  timeout 5 bash -c "until rg 'Focus:' '$LOG' | tail -n 1 | rg -q 'Focus: ${title}'; do sleep 0.05; done"
}

# Arrange the windows into a predictable + pattern.
focus_by_key alt-1 "$t_center"
key alt-ctrl-1
focus_by_key alt-2 "$t_left"
key alt-ctrl-2
focus_by_key alt-3 "$t_right"
key alt-ctrl-3
focus_by_key alt-4 "$t_up"
key alt-ctrl-4
focus_by_key alt-5 "$t_down"
key alt-ctrl-5
timeout 5 bash -c "until rg -q 'MoveTo: ${t_center} ' '$LOG' && rg -q 'MoveTo: ${t_left} ' '$LOG' && rg -q 'MoveTo: ${t_right} ' '$LOG' && rg -q 'MoveTo: ${t_up} ' '$LOG' && rg -q 'MoveTo: ${t_down} ' '$LOG'; do sleep 0.05; done"

focus_by_key alt-f "$t_center"
focus_by_key left "$t_left"

focus_by_key alt-f "$t_center"
focus_by_key right "$t_right"

focus_by_key alt-f "$t_center"
focus_by_key up "$t_up"

focus_by_key alt-f "$t_center"
focus_by_key down "$t_down"

echo "ok: directional focus smoke passed (socket=$SOCKET log=$LOG keys=$KEYS_FILE)"
