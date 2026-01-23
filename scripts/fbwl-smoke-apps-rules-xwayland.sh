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

SOCKET="${SOCKET:-wayland-fbwl-xwayland-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-apps-rules-xwayland-$UID-$$.log}"
APPS_FILE="${APPS_FILE:-/tmp/fbwl-apps-rules-xwayland-$UID-$$.conf}"

cleanup() {
  rm -f "$APPS_FILE" 2>/dev/null || true
  if [[ -n "${X11_PID:-}" ]]; then kill "$X11_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$APPS_FILE" <<EOF
[app] (name=fbwl-xw-inst)
  [Workspace] {1}
  [Jump] {yes}
[end]
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --socket "$SOCKET" \
  --workspaces 3 \
  --apps "$APPS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'XWayland: ready DISPLAY=' '$LOG'; do sleep 0.05; done"

DISPLAY_NAME="$(rg -m 1 'XWayland: ready DISPLAY=' "$LOG" | sed -E 's/.*DISPLAY=//')"
if [[ -z "$DISPLAY_NAME" ]]; then
  echo "failed to parse XWayland DISPLAY from log: $LOG" >&2
  exit 1
fi

DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client \
  --title xw-apps \
  --class fbwl-xw-class \
  --instance fbwl-xw-inst \
  --stay-ms 10000 \
  --w 128 \
  --h 96 \
  >/dev/null 2>&1 &
X11_PID=$!

timeout 10 bash -c "until rg -q 'XWayland: map ' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Focus: xw-apps' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until ./fbwl-remote --socket \"$SOCKET\" get-workspace | rg -q '^ok workspace=2$'; do sleep 0.05; done"

echo "ok: apps rules (XWayland instance match) smoke passed (socket=$SOCKET display=$DISPLAY_NAME log=$LOG apps=$APPS_FILE)"
