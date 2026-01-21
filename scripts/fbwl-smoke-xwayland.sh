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

export XWAYLAND_NO_GLAMOR=1
export __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=swrast

SOCKET="${SOCKET:-wayland-fbwl-xwayland-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-xwayland-$UID-$$.log}"

cleanup() {
  if [[ -n "${X11_PID:-}" ]]; then kill "$X11_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
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
  --title xw-test \
  --class xw-test \
  --instance xw-test \
  --stay-ms 10000 \
  --w 128 \
  --h 96 \
  >/dev/null 2>&1 &
X11_PID=$!

timeout 10 bash -c "until rg -q 'XWayland: map ' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Focus: xw-test' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Surface size: xw-test 128x96' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Place: xw-test ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: xw-test ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

size_line="$(rg -m1 'Surface size: xw-test ' "$LOG")"
if [[ "$size_line" =~ ([0-9]+)x([0-9]+) ]]; then
  W0="${BASH_REMATCH[1]}"
  H0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Surface size line: $size_line" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-alt-left 70 70 170 170
X1=$((X0 + 100))
Y1=$((Y0 + 100))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Move: xw-test x=$X1 y=$Y1"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-alt-right 190 190 240 250
W1=$((W0 + 50))
H1=$((H0 + 60))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: xw-test w=$W1 h=$H1"

timeout 10 bash -c "until rg -q 'Surface size: xw-test ${W1}x${H1}' '$LOG'; do sleep 0.05; done"

echo "ok: XWayland smoke passed (socket=$SOCKET display=$DISPLAY_NAME log=$LOG)"
