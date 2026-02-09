#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd cut
need_cmd mktemp
need_cmd rg
need_cmd sed
need_cmd tail
need_cmd timeout
need_cmd wc

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

export XWAYLAND_NO_GLAMOR=1
export __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=swrast

cleanup_case() {
  if [[ -n "${WL_PID:-}" ]]; then kill "$WL_PID" 2>/dev/null || true; fi
  if [[ -n "${X11_PID:-}" ]]; then kill "$X11_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  if [[ -n "${CFG_DIR:-}" && -d "${CFG_DIR:-}" ]]; then rm -rf "$CFG_DIR"; fi
  WL_PID=""
  X11_PID=""
  FBW_PID=""
  CFG_DIR=""
}
trap cleanup_case EXIT

run_case() {
  local case_name="$1"
  local max_over="$2"

  SOCKET="wayland-fbwl-slit-maxover-$UID-$$-$case_name"
  LOG="/tmp/fluxbox-wayland-slit-maxover-$UID-$$-$case_name.log"
  CFG_DIR="$(mktemp -d "/tmp/fbwl-slit-maxover-$UID-XXXXXX")"

  cat >"$CFG_DIR/init" <<EOF
session.screen0.toolbar.visible: false
session.screen0.defaultDeco: NONE
session.screen0.focusNewWindows: true
session.screen0.windowPlacement: UnderMousePlacement
session.screen0.struts: 0 0 0 0

session.screen0.slit.placement: RightBottom
session.screen0.slit.onhead: 1
session.screen0.slit.layer: Dock
session.screen0.slit.autoHide: false
session.screen0.slit.autoRaise: false
session.screen0.slit.maxOver: $max_over
session.screen0.slit.alpha: 255
session.screen0.slit.direction: Vertical
EOF

  : >"$LOG"

  WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
    ./fluxbox-wayland --socket "$SOCKET" --config-dir "$CFG_DIR" >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 10 bash -c "until rg -q 'XWayland: ready DISPLAY=' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'ScreenMap: screen0 ' '$LOG'; do sleep 0.05; done"

  DISPLAY_NAME="$(rg -m 1 'XWayland: ready DISPLAY=' "$LOG" | sed -E 's/.*DISPLAY=//')"
  if [[ -z "$DISPLAY_NAME" ]]; then
    echo "failed to parse XWayland DISPLAY from log: $LOG" >&2
    exit 1
  fi

  SCREEN0="$(rg 'ScreenMap: screen0 ' "$LOG" | tail -n 1)"
  S0_X="$(echo "$SCREEN0" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)"
  S0_Y="$(echo "$SCREEN0" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)"
  S0_W="$(echo "$SCREEN0" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)"
  S0_H="$(echo "$SCREEN0" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)"

  if [[ "$S0_W" -lt 1 || "$S0_H" -lt 1 ]]; then
    echo "invalid screen layout box: '$SCREEN0'" >&2
    exit 1
  fi

  CX=$((S0_X + S0_W / 2))
  CY=$((S0_Y + S0_H / 2))
  ./fbwl-input-injector --socket "$SOCKET" motion "$CX" "$CY" >/dev/null 2>&1

  local dock_title="dock-$case_name"
  DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client \
    --dock \
    --title "$dock_title" \
    --class "$dock_title" \
    --instance "$dock_title" \
    --stay-ms 20000 \
    --w 48 \
    --h 64 \
    >/dev/null 2>&1 &
  X11_PID=$!

  timeout 10 bash -c "until rg -q 'Slit: manage dock view title=$dock_title' '$LOG'; do sleep 0.05; done"
  timeout 10 bash -c "until rg -q 'Slit: layout ' '$LOG'; do sleep 0.05; done"

  local layout_line
  layout_line="$(rg 'Slit: layout ' "$LOG" | tail -n 1)"
  local thickness
  if [[ "$layout_line" =~ thickness=([0-9]+) ]]; then
    thickness="${BASH_REMATCH[1]}"
  else
    echo "failed to parse Slit layout thickness: $layout_line" >&2
    exit 1
  fi
  if [[ "$thickness" -lt 1 ]]; then
    echo "unexpected: slit thickness < 1: $layout_line" >&2
    exit 1
  fi

  local title="slit-maxover-$case_name"
  ./fbwl-smoke-client --socket "$SOCKET" --title "$title" --stay-ms 20000 >/dev/null 2>&1 &
  WL_PID=$!
  timeout 10 bash -c "until rg -q 'Place: $title ' '$LOG'; do sleep 0.05; done"

  local place_line
  place_line="$(rg -m1 "Place: $title " "$LOG" || true)"
  if [[ "$place_line" =~ usable=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+)[[:space:]]full=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+) ]]; then
    USABLE_X="${BASH_REMATCH[1]}"
    USABLE_Y="${BASH_REMATCH[2]}"
    USABLE_W="${BASH_REMATCH[3]}"
    USABLE_H="${BASH_REMATCH[4]}"
    FULL_X="${BASH_REMATCH[5]}"
    FULL_Y="${BASH_REMATCH[6]}"
    FULL_W="${BASH_REMATCH[7]}"
    FULL_H="${BASH_REMATCH[8]}"
  else
    echo "failed to parse Place line: $place_line" >&2
    exit 1
  fi

  if [[ "$max_over" == "true" ]]; then
    EXP_USABLE_X="$FULL_X"
    EXP_USABLE_Y="$FULL_Y"
    EXP_USABLE_W="$FULL_W"
    EXP_USABLE_H="$FULL_H"
  else
    EXP_USABLE_X="$FULL_X"
    EXP_USABLE_Y="$FULL_Y"
    EXP_USABLE_W=$((FULL_W - thickness))
    EXP_USABLE_H="$FULL_H"
  fi

  if [[ "$USABLE_X" -ne "$EXP_USABLE_X" || "$USABLE_Y" -ne "$EXP_USABLE_Y" || "$USABLE_W" -ne "$EXP_USABLE_W" || "$USABLE_H" -ne "$EXP_USABLE_H" ]]; then
    echo "unexpected usable box for $title (maxOver=$max_over thickness=$thickness):" >&2
    echo "  got:  usable=$USABLE_X,$USABLE_Y ${USABLE_W}x${USABLE_H} full=$FULL_X,$FULL_Y ${FULL_W}x${FULL_H}" >&2
    echo "  want: usable=$EXP_USABLE_X,$EXP_USABLE_Y ${EXP_USABLE_W}x${EXP_USABLE_H}" >&2
    exit 1
  fi

  cleanup_case
}

run_case maxover-false false
run_case maxover-true true

echo "ok: slit maxOver smoke passed"

