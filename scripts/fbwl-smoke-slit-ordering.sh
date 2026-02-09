#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd sed
need_cmd tail
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
  if [[ -n "${X11_PID_B:-}" ]]; then kill "$X11_PID_B" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  if [[ -n "${CFG_DIR:-}" && -d "${CFG_DIR:-}" ]]; then rm -rf "$CFG_DIR"; fi
  X11_PID_A=""
  X11_PID_B=""
  FBW_PID=""
  CFG_DIR=""
}
trap cleanup_case EXIT

SOCKET="wayland-fbwl-slit-ordering-$UID-$$"
LOG="/tmp/fluxbox-wayland-slit-ordering-$UID-$$.log"
CFG_DIR="$(mktemp -d "/tmp/fbwl-slit-ordering-$UID-XXXXXX")"

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

session.slitlistFile: slitlist
EOF

cat >"$CFG_DIR/slitlist" <<EOF
dock-a
dock-b
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
  --title "dock-b" \
  --class "dock-b" \
  --instance "dock-b" \
  --stay-ms 20000 \
  --w 48 \
  --h 64 \
  >/dev/null 2>&1 &
X11_PID_B=$!

timeout 10 bash -c "until rg -q 'Slit: manage dock view title=dock-b' '$LOG'; do sleep 0.05; done"

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
timeout 10 bash -c "until rg -q 'Slit: item idx=1 title=dock-b' '$LOG'; do sleep 0.05; done"

idx0="$(rg 'Slit: item idx=0 title=' "$LOG" | tail -n 1)"
idx1="$(rg 'Slit: item idx=1 title=' "$LOG" | tail -n 1)"

if [[ "$idx0" != *"title=dock-a"* ]]; then
  echo "unexpected slit idx0 line (want dock-a): $idx0" >&2
  exit 1
fi
if [[ "$idx1" != *"title=dock-b"* ]]; then
  echo "unexpected slit idx1 line (want dock-b): $idx1" >&2
  exit 1
fi

echo "ok: slit ordering smoke passed"

