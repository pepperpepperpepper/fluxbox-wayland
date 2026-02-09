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
  if [[ -n "${X11_PID:-}" ]]; then kill "$X11_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  if [[ -n "${CFG_DIR:-}" && -d "${CFG_DIR:-}" ]]; then rm -rf "$CFG_DIR"; fi
  X11_PID=""
  FBW_PID=""
  CFG_DIR=""
}
trap cleanup_case EXIT

run_case() {
  local case_name="$1"
  local accept="$2"
  local expect_manage="$3"

  SOCKET="wayland-fbwl-slit-kde-$UID-$$-$case_name"
  LOG="/tmp/fluxbox-wayland-slit-kde-$UID-$$-$case_name.log"
  CFG_DIR="$(mktemp -d "/tmp/fbwl-slit-kde-$UID-XXXXXX")"

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
session.screen0.slit.acceptKdeDockapps: $accept
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

  local dock_title="dock-kde-$case_name"
  DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client \
    --kde-dockapp \
    --title "$dock_title" \
    --class "$dock_title" \
    --instance "$dock_title" \
    --stay-ms 20000 \
    --w 48 \
    --h 64 \
    >/dev/null 2>&1 &
  X11_PID=$!

  if [[ "$expect_manage" == "true" ]]; then
    timeout 10 bash -c "until rg -q 'Slit: manage dock view title=$dock_title' '$LOG'; do sleep 0.05; done"
  else
    timeout 10 bash -c "until rg -q 'XWayland: map title=$dock_title ' '$LOG'; do sleep 0.05; done"
    if rg -q "Slit: manage dock view title=$dock_title" "$LOG"; then
      echo "unexpected: KDE dockapp was managed into slit with acceptKdeDockapps=false: $dock_title" >&2
      exit 1
    fi
  fi

  cleanup_case
}

run_case accept-false false false
run_case accept-true true true

echo "ok: slit acceptKdeDockapps smoke passed"

