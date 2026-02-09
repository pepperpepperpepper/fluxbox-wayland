#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

cleanup_case() {
  if [[ -n "${C_PID0:-}" ]]; then kill "$C_PID0" 2>/dev/null || true; fi
  if [[ -n "${C_PID1:-}" ]]; then kill "$C_PID1" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  if [[ -n "${CFG_DIR:-}" && -d "${CFG_DIR:-}" ]]; then rm -rf "$CFG_DIR"; fi
  C_PID0=""
  C_PID1=""
  FBW_PID=""
  CFG_DIR=""
}
trap cleanup_case EXIT

run_case() {
  local case_name="$1"
  local screen1_struts_line="$2"
  local exp_top0="$3"
  local exp_top1="$4"

  SOCKET="wayland-fbwl-struts-$UID-$$-$case_name"
  LOG="/tmp/fluxbox-wayland-struts-$UID-$$-$case_name.log"
  CFG_DIR="$(mktemp -d "/tmp/fbwl-struts-$UID-XXXXXX")"

  cat >"$CFG_DIR/init" <<EOF
session.screen0.toolbar.visible: false
session.screen0.defaultDeco: NONE
session.screen0.focusNewWindows: true
session.screen0.windowPlacement: UnderMousePlacement

session.screen0.struts: 0 0 10 0
session.screen0.struts.1: 0 0 12 0
session.screen0.struts.2: 0 0 30 0
EOF

  if [[ -n "$screen1_struts_line" ]]; then
    printf 'session.screen1.struts: %s\n' "$screen1_struts_line" >>"$CFG_DIR/init"
  fi

  : >"$LOG"

  BACKENDS="${WLR_BACKENDS:-headless}"
  RENDERER="${WLR_RENDERER:-pixman}"
  OUTPUTS=2

  if [[ "$BACKENDS" == *x11* ]]; then
    : "${DISPLAY:?DISPLAY must be set for x11 backend (run under scripts/fbwl-smoke-xvfb-outputs.sh)}"
  fi

  if [[ "$BACKENDS" == *x11* ]]; then
    OUTPUTS_ENV="WLR_X11_OUTPUTS=${WLR_X11_OUTPUTS:-$OUTPUTS}"
  else
    OUTPUTS_ENV="WLR_HEADLESS_OUTPUTS=${WLR_HEADLESS_OUTPUTS:-$OUTPUTS}"
  fi

  env WLR_BACKENDS="$BACKENDS" WLR_RENDERER="$RENDERER" "$OUTPUTS_ENV" \
    ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFG_DIR" >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'ScreenMap: reason=new-output screens=2' '$LOG'; do sleep 0.05; done"

  SCREEN0=$(rg 'ScreenMap: screen0 ' "$LOG" | tail -n 1)
  S0_X=$(echo "$SCREEN0" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
  S0_Y=$(echo "$SCREEN0" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
  S0_W=$(echo "$SCREEN0" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)
  S0_H=$(echo "$SCREEN0" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)

  SCREEN1=$(rg 'ScreenMap: screen1 ' "$LOG" | tail -n 1)
  S1_X=$(echo "$SCREEN1" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
  S1_Y=$(echo "$SCREEN1" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
  S1_W=$(echo "$SCREEN1" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)
  S1_H=$(echo "$SCREEN1" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)

  if [[ "$S0_W" -lt 1 || "$S0_H" -lt 1 || "$S1_W" -lt 1 || "$S1_H" -lt 1 ]]; then
    echo "invalid screen layout boxes: '$SCREEN0' '$SCREEN1'" >&2
    exit 1
  fi

  CX0=$((S0_X + S0_W / 2))
  CY0=$((S0_Y + S0_H / 2))
  CX1=$((S1_X + S1_W / 2))
  CY1=$((S1_Y + S1_H / 2))

  TITLE0="struts-$case_name-0"
  TITLE1="struts-$case_name-1"

  ./fbwl-input-injector --socket "$SOCKET" motion "$CX0" "$CY0" >/dev/null 2>&1
  ./fbwl-smoke-client --socket "$SOCKET" --title "$TITLE0" --stay-ms 20000 >/dev/null 2>&1 &
  C_PID0=$!
  timeout 10 bash -c "until rg -q 'Place: $TITLE0 ' '$LOG'; do sleep 0.05; done"

  local place0
  place0="$(rg -m1 "Place: $TITLE0 " "$LOG" || true)"
  if [[ "$place0" =~ usable=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+)[[:space:]]full=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+) ]]; then
    USABLE_X0="${BASH_REMATCH[1]}"
    USABLE_Y0="${BASH_REMATCH[2]}"
    USABLE_W0="${BASH_REMATCH[3]}"
    USABLE_H0="${BASH_REMATCH[4]}"
    FULL_X0="${BASH_REMATCH[5]}"
    FULL_Y0="${BASH_REMATCH[6]}"
    FULL_W0="${BASH_REMATCH[7]}"
    FULL_H0="${BASH_REMATCH[8]}"
  else
    echo "failed to parse Place line: $place0" >&2
    exit 1
  fi

  EXP_USABLE_X0="$FULL_X0"
  EXP_USABLE_Y0=$((FULL_Y0 + exp_top0))
  EXP_USABLE_W0="$FULL_W0"
  EXP_USABLE_H0=$((FULL_H0 - exp_top0))

  if [[ "$USABLE_X0" -ne "$EXP_USABLE_X0" || "$USABLE_Y0" -ne "$EXP_USABLE_Y0" || "$USABLE_W0" -ne "$EXP_USABLE_W0" || "$USABLE_H0" -ne "$EXP_USABLE_H0" ]]; then
    echo "unexpected usable box for $TITLE0:" >&2
    echo "  got:  usable=$USABLE_X0,$USABLE_Y0 ${USABLE_W0}x${USABLE_H0} full=$FULL_X0,$FULL_Y0 ${FULL_W0}x${FULL_H0}" >&2
    echo "  want: usable=$EXP_USABLE_X0,$EXP_USABLE_Y0 ${EXP_USABLE_W0}x${EXP_USABLE_H0}" >&2
    exit 1
  fi

  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-input-injector --socket "$SOCKET" key alt-m >/dev/null 2>&1
  tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: $TITLE0 on w=$EXP_USABLE_W0 h=$EXP_USABLE_H0"
  timeout 10 bash -c "until rg -q 'Surface size: $TITLE0 ${EXP_USABLE_W0}x${EXP_USABLE_H0}' '$LOG'; do sleep 0.05; done"

  ./fbwl-input-injector --socket "$SOCKET" motion "$CX1" "$CY1" >/dev/null 2>&1
  ./fbwl-smoke-client --socket "$SOCKET" --title "$TITLE1" --stay-ms 20000 >/dev/null 2>&1 &
  C_PID1=$!
  timeout 10 bash -c "until rg -q 'Place: $TITLE1 ' '$LOG'; do sleep 0.05; done"

  local place1
  place1="$(rg -m1 "Place: $TITLE1 " "$LOG" || true)"
  if [[ "$place1" =~ usable=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+)[[:space:]]full=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+) ]]; then
    USABLE_X1="${BASH_REMATCH[1]}"
    USABLE_Y1="${BASH_REMATCH[2]}"
    USABLE_W1="${BASH_REMATCH[3]}"
    USABLE_H1="${BASH_REMATCH[4]}"
    FULL_X1="${BASH_REMATCH[5]}"
    FULL_Y1="${BASH_REMATCH[6]}"
    FULL_W1="${BASH_REMATCH[7]}"
    FULL_H1="${BASH_REMATCH[8]}"
  else
    echo "failed to parse Place line: $place1" >&2
    exit 1
  fi

  EXP_USABLE_X1="$FULL_X1"
  EXP_USABLE_Y1=$((FULL_Y1 + exp_top1))
  EXP_USABLE_W1="$FULL_W1"
  EXP_USABLE_H1=$((FULL_H1 - exp_top1))

  if [[ "$USABLE_X1" -ne "$EXP_USABLE_X1" || "$USABLE_Y1" -ne "$EXP_USABLE_Y1" || "$USABLE_W1" -ne "$EXP_USABLE_W1" || "$USABLE_H1" -ne "$EXP_USABLE_H1" ]]; then
    echo "unexpected usable box for $TITLE1:" >&2
    echo "  got:  usable=$USABLE_X1,$USABLE_Y1 ${USABLE_W1}x${USABLE_H1} full=$FULL_X1,$FULL_Y1 ${FULL_W1}x${FULL_H1}" >&2
    echo "  want: usable=$EXP_USABLE_X1,$EXP_USABLE_Y1 ${EXP_USABLE_W1}x${EXP_USABLE_H1}" >&2
    exit 1
  fi

  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-input-injector --socket "$SOCKET" key alt-m >/dev/null 2>&1
  tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: $TITLE1 on w=$EXP_USABLE_W1 h=$EXP_USABLE_H1"
  timeout 10 bash -c "until rg -q 'Surface size: $TITLE1 ${EXP_USABLE_W1}x${EXP_USABLE_H1}' '$LOG'; do sleep 0.05; done"

  cleanup_case
}

run_case legacy "" 12 30
run_case screen_override "0 0 20 0" 12 20

echo "ok: struts smoke passed"
