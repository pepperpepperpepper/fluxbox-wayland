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

cleanup() {
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  if [[ -n "${CFG_DIR:-}" && -d "${CFG_DIR:-}" ]]; then rm -rf "$CFG_DIR"; fi
}
trap cleanup EXIT

run_case() {
  local case_name="$1"
  local full_max="$2"
  local toolbar_max_over="$3"

  SOCKET="wayland-fbwl-fullmax-$UID-$$-$case_name"
  LOG="/tmp/fluxbox-wayland-fullmax-$UID-$$-$case_name.log"
  CFG_DIR="$(mktemp -d)"

  cat >"$CFG_DIR/init" <<EOF
session.screen0.fullMaximization: $full_max
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.height: 40
session.screen0.toolbar.maxOver: $toolbar_max_over
EOF

  : >"$LOG"
  WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
    --no-xwayland \
    --socket "$SOCKET" \
    --config-dir "$CFG_DIR" \
    >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Output: ' '$LOG'; do sleep 0.05; done"

  local title="fm-$case_name"
  ./fbwl-smoke-client --socket "$SOCKET" --title "$title" --stay-ms 10000 >/dev/null 2>&1 &
  CLIENT_PID=$!

  timeout 10 bash -c "until rg -q 'Place: $title ' '$LOG'; do sleep 0.05; done"

  local place_line
  place_line="$(rg -m1 "Place: $title " "$LOG" || true)"
  if [[ "$place_line" =~ usable=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+)[[:space:]]full=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+) ]]; then
    USABLE_W="${BASH_REMATCH[3]}"
    USABLE_H="${BASH_REMATCH[4]}"
    FULL_W="${BASH_REMATCH[7]}"
    FULL_H="${BASH_REMATCH[8]}"
  else
    echo "failed to parse Place line: $place_line" >&2
    exit 1
  fi

  local expected_w="$USABLE_W"
  local expected_h="$USABLE_H"
  if [[ "$full_max" == "true" ]]; then
    expected_w="$FULL_W"
    expected_h="$FULL_H"
  fi

  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-input-injector --socket "$SOCKET" key alt-m
  tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: $title on w=$expected_w h=$expected_h"
  timeout 10 bash -c "until rg -q 'Surface size: $title ${expected_w}x${expected_h}' '$LOG'; do sleep 0.05; done"

  kill "$CLIENT_PID" 2>/dev/null || true
  kill "$FBW_PID" 2>/dev/null || true
  wait 2>/dev/null || true
  CLIENT_PID=""
  FBW_PID=""
  rm -rf "$CFG_DIR"
  CFG_DIR=""
}

run_case base false false
run_case fullmax true false
run_case toolbarmaxover false true

echo "ok: fullMaximization/toolbar.maxOver smoke passed"

