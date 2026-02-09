#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout
need_cmd sed

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

# Keep XWayland stable in CI/headless.
export XWAYLAND_NO_GLAMOR=1
export __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=swrast

SOCKET="${SOCKET:-wayland-fbwl-xwayland-attn-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-xwayland-window-attn-$UID-$$.log}"

cleanup() {
  for pid_var in URG_PID FOCUS_PID FBW_PID; do
    pid="${!pid_var:-}"
    if [[ -n "$pid" ]]; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --socket "$SOCKET" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'XWayland: ready DISPLAY=' '$LOG'; do sleep 0.05; done"

DISPLAY_NAME="$(rg -m 1 'XWayland: ready DISPLAY=' "$LOG" | sed -E 's/.*DISPLAY=//')"
if [[ -z "$DISPLAY_NAME" ]]; then
  echo "failed to parse XWayland DISPLAY from log: $LOG" >&2
  exit 1
fi

DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client \
  --title xw-focus \
  --class xw-focus \
  --instance xw-focus \
  --stay-ms 15000 \
  --w 220 \
  --h 140 \
  >/dev/null 2>&1 &
FOCUS_PID=$!

DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client \
  --title xw-urgent \
  --class xw-urgent \
  --instance xw-urgent \
  --urgent-after-ms 1000 \
  --stay-ms 15000 \
  --w 220 \
  --h 140 \
  >/dev/null 2>&1 &
URG_PID=$!

timeout 10 bash -c "until rg -q 'Place: xw-focus ' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Place: xw-urgent ' '$LOG'; do sleep 0.05; done"

place_focus="$(rg -m1 'Place: xw-focus ' "$LOG")"
if [[ "$place_focus" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  FX="${BASH_REMATCH[1]}"
  FY="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_focus" >&2
  exit 1
fi

# Ensure xw-focus is focused before the urgent hint fires.
./fbwl-input-injector --socket "$SOCKET" click "$((FX + 10))" "$((FY + 10))" >/dev/null 2>&1 || true
timeout 10 bash -c "until rg -q 'Focus: xw-focus' '$LOG'; do sleep 0.05; done"

# Urgency hint on the background window should trigger attention UI.
timeout 10 bash -c "until rg -q 'Attention: start title=xw-urgent ' '$LOG'; do sleep 0.05; done"

echo "ok: xwayland urgency triggers attention (socket=$SOCKET display=$DISPLAY_NAME log=$LOG)"

