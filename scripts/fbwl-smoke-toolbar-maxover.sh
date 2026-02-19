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
need_cmd timeout

need_exe ./fbwl-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

cleanup_case() {
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  if [[ -n "${CFG_DIR:-}" && -d "${CFG_DIR:-}" ]]; then rm -rf "$CFG_DIR" 2>/dev/null || true; fi
  CLIENT_PID=""
  FBW_PID=""
  CFG_DIR=""
}
trap cleanup_case EXIT

run_case() {
  local case_name="$1"
  local auto_hide="$2"
  local max_over="$3"
  local expect_strut="$4"

  SOCKET="wayland-fbwl-toolbar-maxover-$UID-$$-$case_name"
  LOG="/tmp/fluxbox-wayland-toolbar-maxover-$UID-$$-$case_name.log"
  CFG_DIR="$(mktemp -d "/tmp/fbwl-toolbar-maxover-$UID-XXXXXX")"

  cat >"$CFG_DIR/init" <<EOF
session.screen0.defaultDeco: NONE
session.screen0.focusNewWindows: true
session.screen0.windowPlacement: UnderMousePlacement
session.screen0.struts: 0 0 0 0

session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.widthPercent: 50
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: workspacename
session.screen0.toolbar.layer: Top
session.screen0.toolbar.autoHide: $auto_hide
session.screen0.toolbar.autoRaise: false
session.screen0.toolbar.maxOver: $max_over
EOF

  : >"$LOG"

  WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
    ./fluxbox-wayland --no-xwayland -no-slit --socket "$SOCKET" --config-dir "$CFG_DIR" >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

  tb_line="$(rg -m1 'Toolbar: position ' "$LOG")"
  if [[ "$tb_line" =~ h=([0-9]+) ]]; then
    TB_H="${BASH_REMATCH[1]}"
  else
    echo "failed to parse Toolbar height from: $tb_line" >&2
    exit 1
  fi
  if [[ "$TB_H" -lt 1 ]]; then
    echo "unexpected: toolbar height < 1: $tb_line" >&2
    exit 1
  fi

  title="tb-maxover-$case_name"
  ./fbwl-smoke-client --socket "$SOCKET" --title "$title" --stay-ms 20000 >/dev/null 2>&1 &
  CLIENT_PID=$!
  timeout 10 bash -c "until rg -q 'Place: $title ' '$LOG'; do sleep 0.05; done"

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

  if [[ "$expect_strut" == "yes" ]]; then
    EXP_USABLE_X="$FULL_X"
    EXP_USABLE_Y=$((FULL_Y + TB_H))
    EXP_USABLE_W="$FULL_W"
    EXP_USABLE_H=$((FULL_H - TB_H))
  else
    EXP_USABLE_X="$FULL_X"
    EXP_USABLE_Y="$FULL_Y"
    EXP_USABLE_W="$FULL_W"
    EXP_USABLE_H="$FULL_H"
  fi

  if [[ "$USABLE_X" -ne "$EXP_USABLE_X" || "$USABLE_Y" -ne "$EXP_USABLE_Y" || "$USABLE_W" -ne "$EXP_USABLE_W" || "$USABLE_H" -ne "$EXP_USABLE_H" ]]; then
    echo "unexpected usable box for $title (autoHide=$auto_hide maxOver=$max_over toolbar_h=$TB_H):" >&2
    echo "  got:  usable=$USABLE_X,$USABLE_Y ${USABLE_W}x${USABLE_H} full=$FULL_X,$FULL_Y ${FULL_W}x${FULL_H}" >&2
    echo "  want: usable=$EXP_USABLE_X,$EXP_USABLE_Y ${EXP_USABLE_W}x${EXP_USABLE_H}" >&2
    exit 1
  fi

  cleanup_case
}

run_case strut false false yes
run_case maxover false true no
run_case autohide true false no

echo "ok: toolbar maxOver smoke passed"
