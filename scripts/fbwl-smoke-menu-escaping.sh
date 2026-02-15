#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1 (build first)" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout

need_exe ./fluxbox-wayland
need_exe ./fbwl-input-injector

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-menu-escaping-$UID-$$.log}"
CFG_DIR="${CFG_DIR:-/tmp/fbwl-config-menu-escaping-$UID-$$}"
MENU_FILE="${MENU_FILE:-/tmp/fbwl-menu-escaping-$UID-$$.menu}"
MARKER_B="${MARKER_B:-/tmp/fbwl-menu-escaping-marker-b-$UID-$$}"

dump_tail() {
  local path="${1:-}"
  local n="${2:-120}"
  [[ -z "$path" ]] && return 0
  [[ -f "$path" ]] || return 0
  echo "----- tail -n $n $path" >&2
  tail -n "$n" "$path" >&2 || true
}

smoke_on_err() {
  local rc=$?
  trap - ERR
  set +e

  echo "error: $0 failed (rc=$rc line=${1:-} cmd=${2:-})" >&2
  echo "debug: socket=${SOCKET:-} XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-} cfg_dir=${CFG_DIR:-} menu=${MENU_FILE:-}" >&2
  echo "debug: log=${LOG:-}" >&2
  dump_tail "${LOG:-}"
  exit "$rc"
}
trap 'smoke_on_err $LINENO "$BASH_COMMAND"' ERR

cleanup() {
  rm -f "$MENU_FILE" "$MARKER_B" 2>/dev/null || true
  rm -rf "$CFG_DIR" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then
    kill "$FBW_PID" 2>/dev/null || true
    wait "$FBW_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

rm -f "$MARKER_B"
: >"$LOG"
rm -rf "$CFG_DIR" 2>/dev/null || true
mkdir -p "$CFG_DIR"

cat >"$CFG_DIR/init" <<EOF
session.menuSearch: somewhere
EOF

cat >"$MENU_FILE" <<EOF
[begin] (Fluxbox)
[exec] (Alpha) {true}
[exec] (Beta \\) Two) {sh -c 'echo ok >"$MARKER_B"'}
[exec] (Gamma) {true}
[end]
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFG_DIR" \
  --menu "$MENU_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

# Open the root menu with a background right-click.
offset="$(wc -c <"$LOG" | tr -d ' ')"
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
timeout 5 bash -c "until tail -c +$((offset + 1)) '$LOG' | rg -q 'Menu: open at '; do sleep 0.05; done"

# Select the item by searching for a substring that only exists if escaping is handled correctly.
./fbwl-input-injector --socket "$SOCKET" type "Two"
./fbwl-input-injector --socket "$SOCKET" key enter
timeout 5 bash -c "until [[ -f '$MARKER_B' ]]; do sleep 0.05; done"

echo "ok: menu escaping smoke passed (socket=$SOCKET log=$LOG)"
