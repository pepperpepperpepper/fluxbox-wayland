#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1 (build first)" >&2; exit 1; }
}

have_cmd() {
  command -v "$1" >/dev/null 2>&1
}

need_cmd dbus-run-session
need_cmd rg
need_cmd sed
need_cmd timeout

need_exe ./fluxbox-wayland
need_exe ./fbwl-remote
need_exe ./fbwl-screencopy-client
need_exe ./fbx11-xembed-tray-client

if ! have_cmd xembedsniproxy && ! have_cmd snixembed && ! have_cmd xembed-sni-proxy; then
  echo "skip: missing xembed->sni proxy (need xembedsniproxy or snixembed or xembed-sni-proxy)" >&2
  exit 0
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

export XWAYLAND_NO_GLAMOR=1
export __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=swrast

SOCKET="${SOCKET:-wayland-fbwl-xembed-tray-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-xembed-tray-$UID-$$.log}"
ICON_RGB="${ICON_RGB:-#00ff00}"
REPORT_DIR="${FBWL_REPORT_DIR:-${FBWL_SMOKE_REPORT_DIR:-}}"

: >"$LOG"

dbus-run-session -- bash -c '
  set -euo pipefail

  ROOT="'"$ROOT"'"
  SOCKET="'"$SOCKET"'"
  LOG="'"$LOG"'"
  ICON_RGB="'"$ICON_RGB"'"
  REPORT_DIR="'"$REPORT_DIR"'"

  cleanup() {
    if [[ -n "${XEMBED_PID:-}" ]]; then kill "$XEMBED_PID" 2>/dev/null || true; fi
    if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
    wait 2>/dev/null || true
  }
  trap cleanup EXIT

  cd "$ROOT"
  : >"$LOG"

  source scripts/fbwl-smoke-report-lib.sh
  fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

  FBWL_XEMBED_SNI_PROXY=auto WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
    --socket "$SOCKET" \
    >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q \"Running fluxbox-wayland\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"SNI: watcher enabled\" \"$LOG\"; do sleep 0.05; done"
  timeout 10 bash -c "until rg -q \"XWayland: ready DISPLAY=\" \"$LOG\"; do sleep 0.05; done"
  timeout 10 bash -c "until rg -q \"XEmbedProxy: started\" \"$LOG\"; do sleep 0.05; done"
  timeout 10 bash -c "until rg -q \"Toolbar: position \" \"$LOG\"; do sleep 0.05; done"

  DISPLAY_NAME="$(rg -m 1 \"XWayland: ready DISPLAY=\" \"$LOG\" | sed -E \"s/.*DISPLAY=//\")"
  if [[ -z "$DISPLAY_NAME" ]]; then
    echo "failed to parse XWayland DISPLAY from log: $LOG" >&2
    exit 1
  fi

  DISPLAY="$DISPLAY_NAME" ./fbx11-xembed-tray-client \
    --rgb "$ICON_RGB" \
    --stay-ms 12000 \
    >/dev/null 2>&1 &
  XEMBED_PID=$!

  timeout 10 bash -c "until rg -q \"Toolbar: tray item idx=0\" \"$LOG\"; do sleep 0.05; done"

  pos_line="$(rg -m1 \"Toolbar: position \" \"$LOG\")"
  if [[ "$pos_line" =~ x=([-0-9]+)[[:space:]]+y=([-0-9]+)[[:space:]]+h=([0-9]+) ]]; then
    X0="${BASH_REMATCH[1]}"
    Y0="${BASH_REMATCH[2]}"
    H="${BASH_REMATCH[3]}"
  else
    echo "failed to parse Toolbar: position line: $pos_line" >&2
    exit 1
  fi

  tray_line="$(rg -m1 \"Toolbar: tray item idx=0\" \"$LOG\")"
  if [[ "$tray_line" =~ lx=([-0-9]+)[[:space:]]+w=([0-9]+)[[:space:]]+id= ]]; then
    LX="${BASH_REMATCH[1]}"
    W="${BASH_REMATCH[2]}"
  else
    echo "failed to parse Toolbar: tray item line: $tray_line" >&2
    exit 1
  fi

  SAMPLE_X=$((X0 + LX + W / 2))
  SAMPLE_Y=$((Y0 + H / 2))

  if ! ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --sample-x "$SAMPLE_X" --sample-y "$SAMPLE_Y" --expect-rgb "$ICON_RGB" >/dev/null 2>&1; then
    echo "expected tray icon color not found at sample point (x=$SAMPLE_X y=$SAMPLE_Y rgb=$ICON_RGB)" >&2
    echo "log tail:" >&2
    tail -n 200 "$LOG" >&2 || true
    exit 1
  fi

  fbwl_report_shot "xembed-tray.png" "XEmbed tray icon (via xembed→SNI proxy)"

  ./fbwl-remote --socket "$SOCKET" quit | rg -q \"^ok quitting$\"
  timeout 5 bash -c "while kill -0 \"$FBW_PID\" 2>/dev/null; do sleep 0.05; done"
  wait "$FBW_PID"
  unset FBW_PID

  wait "$XEMBED_PID" 2>/dev/null || true
  unset XEMBED_PID

  echo "ok: xembed tray proxy smoke passed (socket=$SOCKET display=$DISPLAY_NAME log=$LOG)"
'
