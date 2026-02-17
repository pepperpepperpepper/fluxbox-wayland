#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd wc

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

source scripts/fbwl-smoke-report-lib.sh
REPORT_DIR="${FBWL_REPORT_DIR:-${FBWL_SMOKE_REPORT_DIR:-}}"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-style-menu-underline-color-$UID-$$.log}"
SC_LOG="${SC_LOG:-/tmp/fbwl-style-menu-underline-color-screencopy-$UID-$$.log}"
STYLE_FILE="${STYLE_FILE:-/tmp/fbwl-style-menu-underline-color-$UID-$$.cfg}"
MENU_FILE="${MENU_FILE:-/tmp/fbwl-menu-underline-color-$UID-$$.menu}"
CFGDIR="$(mktemp -d "/tmp/fbwl-style-menu-underline-color-$UID-XXXXXX")"

cleanup() {
  rm -f "$STYLE_FILE" "$MENU_FILE" "$SC_LOG" 2>/dev/null || true
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
: >"$SC_LOG"

fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

cat >"$CFGDIR/init" <<'EOF'
session.screen0.toolbar.visible: false
session.screen0.allowRemoteActions: true
session.screen0.menu.alpha: 255
session.menuSearch: itemstart
EOF

cat >"$STYLE_FILE" <<'EOF'
menu.frame: Flat Solid
menu.frame.color: #000000
menu.frame.colorTo: #000000
menu.frame.textColor: #ffffff
menu.frame.underlineColor: #ff00ff

menu.hilite: Flat Solid
menu.hilite.color: #000000
menu.hilite.colorTo: #000000
menu.hilite.textColor: #ffffff

menu.borderWidth: 0
menu.bevelWidth: 0
menu.itemHeight: 24
EOF

# Minimal root menu file: no [begin] label => no menu title bar.
cat >"$MENU_FILE" <<'EOF'
[exec] (Alpha) {true}
[exec] (Beta) {true}
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  --style "$STYLE_FILE" \
  --menu "$MENU_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

# Open the root menu with a background right-click.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-right 100 100 100 100
open_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 'Menu: open at ' || true)"
if [[ -z "$open_line" ]]; then
  echo "expected menu open log line after right-click" >&2
  exit 1
fi

if [[ "$open_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ items=([0-9]+) ]]; then
  MENU_X="${BASH_REMATCH[1]}"
  MENU_Y="${BASH_REMATCH[2]}"
  MENU_ITEMS="${BASH_REMATCH[3]}"
else
  echo "failed to parse menu open line: $open_line" >&2
  exit 1
fi

if [[ "$MENU_ITEMS" != "2" ]]; then
  echo "expected 2 menu items, got $MENU_ITEMS" >&2
  exit 1
fi

ITEM_H=24
BEVEL=0
LEFT_RESERVE=$((BEVEL + ITEM_H + 1))
SAMPLE_X=$((MENU_X + LEFT_RESERVE + 4))
BETA_ROW_Y=$((MENU_Y + ITEM_H))

find_magenta_dy() {
  local base_y="$1"
  for dy in $(seq 0 $((ITEM_H - 1))); do
    if ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 \
      --expect-rgb '#ff00ff' --sample-x "$SAMPLE_X" --sample-y "$((base_y + dy))" >"$SC_LOG" 2>&1; then
      echo "$dy"
      return 0
    fi
  done
  return 1
}

if dy0="$(find_magenta_dy "$BETA_ROW_Y")"; then
  echo "unexpected underline before typing (dy=$dy0)" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" type b
./fbwl-input-injector --socket "$SOCKET" hold 100

fbwl_report_shot "style-menu-underline-color.png" "Menu underlineColor (type-ahead underline)"

dy1="$(find_magenta_dy "$BETA_ROW_Y")"
echo "ok: underline detected at dy=$dy1"
echo "ok: style menu.frame.underlineColor smoke passed"

