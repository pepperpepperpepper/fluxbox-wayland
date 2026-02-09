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

cleanup() {
  if [[ -n "${X11_PID:-}" ]]; then kill "$X11_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  if [[ -n "${CFG_DIR:-}" && -d "${CFG_DIR:-}" ]]; then rm -rf "$CFG_DIR"; fi
}
trap cleanup EXIT

INC=97

run_case() {
  local case_name="$1"
  local ignore_inc="$2"

  SOCKET="wayland-fbwl-xw-inc-$UID-$$-$case_name"
  LOG="/tmp/fluxbox-wayland-xw-inc-$UID-$$-$case_name.log"
  CFG_DIR="$(mktemp -d)"

  cat >"$CFG_DIR/init" <<EOF
session.screen0.maxIgnoreIncrement: $ignore_inc
session.screen0.toolbar.visible: false
EOF

  : >"$LOG"
  WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
    --socket "$SOCKET" \
    --config-dir "$CFG_DIR" \
    >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 10 bash -c "until rg -q 'XWayland: ready DISPLAY=' '$LOG'; do sleep 0.05; done"

  DISPLAY_NAME="$(rg -m 1 'XWayland: ready DISPLAY=' "$LOG" | sed -E 's/.*DISPLAY=//')"
  if [[ -z "$DISPLAY_NAME" ]]; then
    echo "failed to parse XWayland DISPLAY from log: $LOG" >&2
    exit 1
  fi

  local title="xw-inc-$case_name"
  DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client \
    --title "$title" \
    --class "$title" \
    --instance "$title" \
    --stay-ms 10000 \
    --w 128 \
    --h 96 \
    --width-inc "$INC" \
    --height-inc "$INC" \
    >/dev/null 2>&1 &
  X11_PID=$!

  timeout 10 bash -c "until rg -q 'Place: $title ' '$LOG'; do sleep 0.05; done"

  place_line="$(rg -m1 "Place: $title " "$LOG" || true)"
  if [[ "$place_line" =~ usable=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+)[[:space:]] ]]; then
    USABLE_W="${BASH_REMATCH[3]}"
    USABLE_H="${BASH_REMATCH[4]}"
  else
    echo "failed to parse Place line: $place_line" >&2
    exit 1
  fi

  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-input-injector --socket "$SOCKET" key alt-m
  max_line="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 "Maximize: $title on w=" || true)"
  if [[ "$max_line" =~ w=([0-9]+)[[:space:]]h=([0-9]+) ]]; then
    MAX_W="${BASH_REMATCH[1]}"
    MAX_H="${BASH_REMATCH[2]}"
  else
    echo "failed to parse Maximize line: $max_line" >&2
    exit 1
  fi
  timeout 10 bash -c "until rg -q 'Surface size: $title ${MAX_W}x${MAX_H}' '$LOG'; do sleep 0.05; done"
  if [[ "$ignore_inc" != "true" ]]; then
    if (( MAX_W % INC != 0 )) || (( MAX_H % INC != 0 )); then
      echo "expected maxIgnoreIncrement=false to snap to increments of $INC, got w=$MAX_W h=$MAX_H (usable=${USABLE_W}x${USABLE_H})" >&2
      exit 1
    fi
  fi

  kill "$X11_PID" 2>/dev/null || true
  kill "$FBW_PID" 2>/dev/null || true
  wait 2>/dev/null || true
  X11_PID=""
  FBW_PID=""
  rm -rf "$CFG_DIR"
  CFG_DIR=""

  echo "$MAX_W $MAX_H"
}

read -r RAW_W RAW_H < <(run_case ignore-true true)
read -r SNAP_W SNAP_H < <(run_case ignore-false false)

if (( RAW_W % INC == 0 )) && (( RAW_H % INC == 0 )); then
  echo "unexpected: raw maximize already aligned to increment=$INC (w=$RAW_W h=$RAW_H); pick a different INC" >&2
  exit 1
fi
if (( SNAP_W > RAW_W )) || (( SNAP_H > RAW_H )); then
  echo "unexpected: snapped maximize larger than raw (raw=${RAW_W}x${RAW_H} snapped=${SNAP_W}x${SNAP_H})" >&2
  exit 1
fi
if (( SNAP_W == RAW_W )) && (( SNAP_H == RAW_H )); then
  echo "unexpected: maxIgnoreIncrement had no effect (raw=${RAW_W}x${RAW_H} snapped=${SNAP_W}x${SNAP_H})" >&2
  exit 1
fi

echo "ok: XWayland maxIgnoreIncrement smoke passed"
