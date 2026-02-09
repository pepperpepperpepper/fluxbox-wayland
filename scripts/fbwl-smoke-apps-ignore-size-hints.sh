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
need_cmd timeout
need_exe ./fbx11-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

export XWAYLAND_NO_GLAMOR=1
export __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=swrast

SOCKET="${SOCKET:-wayland-fbwl-apps-ish-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-apps-ignore-size-hints-$UID-$$.log}"
CFG_DIR="$(mktemp -d)"
APPS_FILE="$(mktemp /tmp/fbwl-apps-ignore-size-hints-XXXXXX)"

cleanup() {
  rm -f "$APPS_FILE" 2>/dev/null || true
  if [[ -n "${X11_SNAP_PID:-}" ]]; then kill "$X11_SNAP_PID" 2>/dev/null || true; fi
  if [[ -n "${X11_IGNORE_PID:-}" ]]; then kill "$X11_IGNORE_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFG_DIR" 2>/dev/null || true
}
trap cleanup EXIT

INC=97

cat >"$CFG_DIR/init" <<EOF
session.screen0.maxIgnoreIncrement: false
session.screen0.toolbar.visible: false
EOF

cat >"$APPS_FILE" <<'EOF'
[app] (app_id=xw-hints-snap)
  [Deco] {none}
  [Maximized] {yes}
[end]

[app] (app_id=xw-hints-ignore)
  [Deco] {none}
  [Maximized] {yes}
  [IgnoreSizeHints] {yes}
[end]
EOF

: >"$LOG"
WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --socket "$SOCKET" \
  --config-dir "$CFG_DIR" \
  --apps "$APPS_FILE" \
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
  --title xw-hints-snap \
  --class xw-hints-snap \
  --instance xw-hints-snap \
  --stay-ms 10000 \
  --w 128 \
  --h 96 \
  --width-inc "$INC" \
  --height-inc "$INC" \
  >/dev/null 2>&1 &
X11_SNAP_PID=$!

DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client \
  --title xw-hints-ignore \
  --class xw-hints-ignore \
  --instance xw-hints-ignore \
  --stay-ms 10000 \
  --w 128 \
  --h 96 \
  --width-inc "$INC" \
  --height-inc "$INC" \
  >/dev/null 2>&1 &
X11_IGNORE_PID=$!

timeout 10 bash -c "until rg -q 'Maximize: xw-hints-snap on w=' '$LOG'; do sleep 0.05; done"
snap_line="$(rg -m1 'Maximize: xw-hints-snap on w=' "$LOG" || true)"
if [[ "$snap_line" =~ w=([0-9]+)[[:space:]]h=([0-9]+) ]]; then
  SNAP_W="${BASH_REMATCH[1]}"
  SNAP_H="${BASH_REMATCH[2]}"
else
  echo "failed to parse Maximize line (snap): $snap_line" >&2
  exit 1
fi

timeout 10 bash -c "until rg -q 'Maximize: xw-hints-ignore on w=' '$LOG'; do sleep 0.05; done"
ignore_line="$(rg -m1 'Maximize: xw-hints-ignore on w=' "$LOG" || true)"
if [[ "$ignore_line" =~ w=([0-9]+)[[:space:]]h=([0-9]+) ]]; then
  IGNORE_W="${BASH_REMATCH[1]}"
  IGNORE_H="${BASH_REMATCH[2]}"
else
  echo "failed to parse Maximize line (ignore): $ignore_line" >&2
  exit 1
fi
timeout 10 bash -c "until rg -q 'Surface size: xw-hints-ignore ${IGNORE_W}x${IGNORE_H}' '$LOG'; do sleep 0.05; done"

if (( SNAP_W % INC != 0 )) || (( SNAP_H % INC != 0 )); then
  echo "expected snapped maximize to align to increment=$INC, got ${SNAP_W}x${SNAP_H}" >&2
  exit 1
fi
if (( IGNORE_W % INC == 0 )) && (( IGNORE_H % INC == 0 )); then
  echo "expected IgnoreSizeHints to ignore increment=$INC, got ${IGNORE_W}x${IGNORE_H}" >&2
  exit 1
fi
if (( IGNORE_W <= SNAP_W )) || (( IGNORE_H <= SNAP_H )); then
  echo "expected IgnoreSizeHints maximize to be larger (snap=${SNAP_W}x${SNAP_H} ignore=${IGNORE_W}x${IGNORE_H})" >&2
  exit 1
fi

echo "ok: apps IgnoreSizeHints smoke passed (inc=$INC snap=${SNAP_W}x${SNAP_H} ignore=${IGNORE_W}x${IGNORE_H})"
