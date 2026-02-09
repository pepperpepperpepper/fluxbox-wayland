#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd wc

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

cleanup() {
  if [[ -n "${C0_PID:-}" ]]; then kill "$C0_PID" 2>/dev/null || true; fi
  if [[ -n "${C1_PID:-}" ]]; then kill "$C1_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  if [[ -n "${CFG_DIR:-}" && -d "${CFG_DIR:-}" ]]; then rm -rf "$CFG_DIR"; fi
}
trap cleanup EXIT

run_case() {
  local case_name="$1"
  local max_over="$2"

  SOCKET="wayland-fbwl-tabsmaxover-$UID-$$-$case_name"
  LOG="/tmp/fluxbox-wayland-tabsmaxover-$UID-$$-$case_name.log"
  CFG_DIR="$(mktemp -d)"

  cat >"$CFG_DIR/init" <<EOF
session.screen0.toolbar.visible: false
session.screen0.windowPlacement: AutotabPlacement
session.screen0.tabs.intitlebar: false
session.screen0.tabs.maxOver: $max_over
session.screen0.tab.placement: TopCenter
session.screen0.tab.width: 80
session.styleFile: $CFG_DIR/style
EOF

  BORDER=4
  TITLE_H=24
  cat >"$CFG_DIR/style" <<EOF
window.borderWidth: $BORDER
window.title.height: $TITLE_H
EOF

  : >"$LOG"
  WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
    --no-xwayland \
    --socket "$SOCKET" \
    --config-dir "$CFG_DIR" \
    >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Style: loaded ' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Output: ' '$LOG'; do sleep 0.05; done"

  local t0="tab0-$case_name"
  local t1="tab1-$case_name"

  ./fbwl-smoke-client --socket "$SOCKET" --title "$t0" --stay-ms 10000 --xdg-decoration >/dev/null 2>&1 &
  C0_PID=$!
  timeout 10 bash -c "until rg -q 'Place: $t0 ' '$LOG'; do sleep 0.05; done"

  ./fbwl-smoke-client --socket "$SOCKET" --title "$t1" --stay-ms 10000 --xdg-decoration >/dev/null 2>&1 &
  C1_PID=$!
  timeout 10 bash -c "until rg -q 'Tabs: attach reason=autotab' '$LOG'; do sleep 0.05; done"
  timeout 10 bash -c "until rg -q 'Surface size: $t1 ' '$LOG'; do sleep 0.05; done"

  local place_line
  place_line="$(rg -m1 "Place: $t0 " "$LOG" || true)"
  if [[ "$place_line" =~ usable=([-0-9]+),([-0-9]+)[[:space:]]([0-9]+)x([0-9]+) ]]; then
    USABLE_W="${BASH_REMATCH[3]}"
    USABLE_H="${BASH_REMATCH[4]}"
  else
    echo "failed to parse Place line: $place_line" >&2
    exit 1
  fi

  local expected_w expected_h
  expected_w=$((USABLE_W - 2 * BORDER))
  expected_h=$((USABLE_H - TITLE_H - 2 * BORDER))
  if [[ "$max_over" != "true" ]]; then
    expected_h=$((expected_h - (TITLE_H + BORDER)))
  fi

  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-input-injector --socket "$SOCKET" key alt-m
  tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Maximize: tab[01]-$case_name on w=$expected_w h=$expected_h"
  timeout 10 bash -c "until rg -q 'Surface size: tab[01]-$case_name ${expected_w}x${expected_h}' '$LOG'; do sleep 0.05; done"

  kill "$C0_PID" 2>/dev/null || true
  kill "$C1_PID" 2>/dev/null || true
  kill "$FBW_PID" 2>/dev/null || true
  wait 2>/dev/null || true
  C0_PID=""
  C1_PID=""
  FBW_PID=""
  rm -rf "$CFG_DIR"
  CFG_DIR=""
}

run_case maxover true
run_case nomaxover false

echo "ok: tabs.maxOver smoke passed"
