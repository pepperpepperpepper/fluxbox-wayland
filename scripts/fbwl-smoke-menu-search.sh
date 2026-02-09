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

MENU_FILE="${MENU_FILE:-/tmp/fbwl-menu-search-$UID-$$.menu}"

write_menu() {
  local marker_alpha="$1"
  local marker_beta="$2"
  local marker_gamma="$3"
  cat >"$MENU_FILE" <<EOF
[begin] (Fluxbox)
[exec] (Alpha) {sh -c 'echo ok >"$marker_alpha"'}
[exec] (BetaTwo) {sh -c 'echo ok >"$marker_beta"'}
[exec] (Gamma) {sh -c 'echo ok >"$marker_gamma"'}
[end]
EOF
}

run_case() (
  local mode="$1"
  local type_seq="$2"
  local marker_expected="$3"

  local socket="wayland-fbwl-test-$UID-$$-$mode"
  local log="/tmp/fluxbox-wayland-menu-search-$UID-$$-$mode.log"
  local cfg_dir="/tmp/fbwl-config-menu-search-$UID-$$-$mode"

  rm -rf "$cfg_dir" 2>/dev/null || true
  mkdir -p "$cfg_dir"
  cat >"$cfg_dir/init" <<EOF
session.menuSearch: $mode
EOF

  : >"$log"

  local pid=""
  cleanup_case() {
    if [[ -n "$pid" ]]; then
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
    rm -rf "$cfg_dir" 2>/dev/null || true
  }
  trap cleanup_case EXIT

  WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
    --no-xwayland \
    --socket "$socket" \
    --config-dir "$cfg_dir" \
    --menu "$MENU_FILE" \
    >"$log" 2>&1 &
  pid=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$log'; do sleep 0.05; done"

  # Open the root menu with a background right-click.
  local offset
  offset=$(wc -c <"$log" | tr -d ' ')
  ./fbwl-input-injector --socket "$socket" drag-right 100 100 100 100
  timeout 5 bash -c "until tail -c +$((offset + 1)) '$log' | rg -q 'Menu: open at '; do sleep 0.05; done"

  ./fbwl-input-injector --socket "$socket" type "$type_seq"
  ./fbwl-input-injector --socket "$socket" key enter

  timeout 5 bash -c "until [[ -f '$marker_expected' ]]; do sleep 0.05; done"
)

MARKER_A1="/tmp/fbwl-menu-search-marker-a1-$UID-$$"
MARKER_B1="/tmp/fbwl-menu-search-marker-b1-$UID-$$"
MARKER_G1="/tmp/fbwl-menu-search-marker-g1-$UID-$$"
MARKER_A2="/tmp/fbwl-menu-search-marker-a2-$UID-$$"
MARKER_B2="/tmp/fbwl-menu-search-marker-b2-$UID-$$"
MARKER_G2="/tmp/fbwl-menu-search-marker-g2-$UID-$$"

cleanup() {
  rm -f "$MENU_FILE" 2>/dev/null || true
  rm -f "$MARKER_A1" "$MARKER_B1" "$MARKER_G1" "$MARKER_A2" "$MARKER_B2" "$MARKER_G2" 2>/dev/null || true
}
trap cleanup EXIT

rm -f "$MARKER_A1" "$MARKER_B1" "$MARKER_G1" "$MARKER_A2" "$MARKER_B2" "$MARKER_G2"
write_menu "$MARKER_A1" "$MARKER_B1" "$MARKER_G1"

# Default behavior: itemstart.
run_case itemstart g "$MARKER_G1"

# "somewhere" should match within the item label (BetaTwo).
write_menu "$MARKER_A2" "$MARKER_B2" "$MARKER_G2"
run_case somewhere wo "$MARKER_B2"

echo "ok: menu search smoke passed (menuSearch=itemstart|somewhere)"
