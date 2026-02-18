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
need_cmd sed
need_cmd tail
need_cmd timeout
need_cmd wc

need_exe ./fbwl-input-injector
need_exe ./fbx11-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

export XWAYLAND_NO_GLAMOR=1
export __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=swrast
export FBWL_XEMBED_SNI_PROXY=0

SOCKET="${SOCKET:-wayland-fbwl-slit-autoraise-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-slit-autoraise-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-slit-autoraise-$UID-XXXXXX")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${X11_PID:-}" ]]; then kill "$X11_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$CFGDIR/init" <<EOF
session.autoRaiseDelay: 50

session.screen0.toolbar.visible: false
session.screen0.defaultDeco: NONE
session.screen0.focusNewWindows: true

session.screen0.slit.placement: RightBottom
session.screen0.slit.onhead: 1
session.screen0.slit.layer: Dock
session.screen0.slit.autoHide: false
session.screen0.slit.autoRaise: true
session.screen0.slit.maxOver: false
session.screen0.slit.alpha: 255
session.screen0.slit.direction: Vertical
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
  ./fluxbox-wayland --socket "$SOCKET" --config-dir "$CFGDIR" >"$LOG" 2>&1 &
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
X11_PID=$!

timeout 10 bash -c "until rg -q 'Slit: manage dock view title=dock-a' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Slit: layout why=' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Slit: item idx=0 title=dock-a ' '$LOG'; do sleep 0.05; done"

layout="$(rg 'Slit: layout why=' "$LOG" | tail -n 1)"
item_line="$(rg 'Slit: item idx=0 title=dock-a ' "$LOG" | tail -n 1)"
if [[ "$item_line" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
  ITEM_X="${BASH_REMATCH[1]}"
  ITEM_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse slit item line: $item_line" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" motion "$((ITEM_X + 2))" "$((ITEM_Y + 2))" >/dev/null 2>&1 || true
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Slit: autoRaise raise'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" motion 0 0 >/dev/null 2>&1 || true
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Slit: autoRaise lower'; do sleep 0.05; done"

echo "ok: slit autoRaise parity smoke passed (socket=$SOCKET log=$LOG)"
