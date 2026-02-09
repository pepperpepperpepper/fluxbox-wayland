#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd sed
need_cmd timeout

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

export XWAYLAND_NO_GLAMOR=1
export __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=swrast

cleanup_case() {
  if [[ -n "${X11_PID_A:-}" ]]; then kill "$X11_PID_A" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  if [[ -n "${CFG_DIR:-}" && -d "${CFG_DIR:-}" ]]; then rm -rf "$CFG_DIR"; fi
  X11_PID_A=""
  FBW_PID=""
  CFG_DIR=""
}
trap cleanup_case EXIT

SOCKET="wayland-fbwl-slit-alpha-$UID-$$"
LOG="/tmp/fluxbox-wayland-slit-alpha-$UID-$$.log"
CFG_DIR="$(mktemp -d "/tmp/fbwl-slit-alpha-$UID-XXXXXX")"

cat >"$CFG_DIR/init" <<EOF
session.screen0.toolbar.visible: false
session.screen0.defaultDeco: NONE
session.screen0.focusNewWindows: true

session.screen0.slit.placement: RightBottom
session.screen0.slit.onhead: 1
session.screen0.slit.layer: Dock
session.screen0.slit.autoHide: false
session.screen0.slit.autoRaise: false
session.screen0.slit.maxOver: false
session.screen0.slit.alpha: 255
session.screen0.slit.direction: Vertical
EOF

: >"$LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
  ./fluxbox-wayland --socket "$SOCKET" --config-dir "$CFG_DIR" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'XWayland: ready DISPLAY=' '$LOG'; do sleep 0.05; done"

DISPLAY_NAME="$(rg -m 1 'XWayland: ready DISPLAY=' "$LOG" | sed -E 's/.*DISPLAY=//')"
if [[ -z "$DISPLAY_NAME" ]]; then
  echo "failed to parse XWayland DISPLAY from log: $LOG" >&2
  exit 1
fi

DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client \
  --dock \
  --title "dock-a" \
  --class "dock-a" \
  --instance "dock-a" \
  --stay-ms 20000 \
  --w 48 \
  --h 64 \
  >/dev/null 2>&1 &
X11_PID_A=$!

timeout 10 bash -c "until rg -q 'Slit: manage dock view title=dock-a' '$LOG'; do sleep 0.05; done"

LAYOUT_LINE="$(rg 'Slit: layout ' "$LOG" | tail -n 1)"
if [[ "$LAYOUT_LINE" =~ x=([-0-9]+)\ y=([-0-9]+)\ w=([0-9]+)\ h=([0-9]+) ]]; then
  SLIT_X="${BASH_REMATCH[1]}"
  SLIT_Y="${BASH_REMATCH[2]}"
  SLIT_W="${BASH_REMATCH[3]}"
  SLIT_H="${BASH_REMATCH[4]}"
else
  echo "failed to parse Slit: layout line: $LAYOUT_LINE" >&2
  exit 1
fi

if (( SLIT_W < 4 || SLIT_H < 4 )); then
  echo "unexpected slit size: w=$SLIT_W h=$SLIT_H (line: $LAYOUT_LINE)" >&2
  exit 1
fi

CLICK_X=$((SLIT_X + 2))
CLICK_Y=$((SLIT_Y + 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click-right "$CLICK_X" "$CLICK_Y" >/dev/null 2>&1 || true
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'SlitMenu: open'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" type alpha
./fbwl-input-injector --socket "$SOCKET" key enter
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Menu: enter-submenu reason=activate label=Alpha'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" type "set alpha"
./fbwl-input-injector --socket "$SOCKET" key enter
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'CmdDialog: open'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" type 128
./fbwl-input-injector --socket "$SOCKET" key enter
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Slit: set-alpha 128 \\(prompt\\)'; do sleep 0.05; done"
timeout 2 bash -c "until rg -q 'Slit: layout .* alpha=128 ' '$LOG'; do sleep 0.05; done"

echo "ok: slit alpha input smoke passed"

