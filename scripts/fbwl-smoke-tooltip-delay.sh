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

run_case() {
  local delay_ms="$1"
  local expect_show="$2"

  (
    set -euo pipefail

    local SOCKET="wayland-fbwl-test-$UID-$$-tooltip-$delay_ms"
    local LOG="/tmp/fluxbox-wayland-tooltip-delay-$UID-$$-$delay_ms.log"
    local CFGDIR
    CFGDIR="$(mktemp -d "/tmp/fbwl-tooltip-delay-$UID-XXXXXX")"

    local FBW_PID=
    local CLIENT1_PID=
    local CLIENT2_PID=

    cleanup_case() {
      rm -rf "$CFGDIR" 2>/dev/null || true
      if [[ -n "${CLIENT2_PID:-}" ]]; then kill "$CLIENT2_PID" 2>/dev/null || true; fi
      if [[ -n "${CLIENT1_PID:-}" ]]; then kill "$CLIENT1_PID" 2>/dev/null || true; fi
      if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
      wait 2>/dev/null || true
    }
    trap cleanup_case EXIT

    : >"$LOG"

    cat >"$CFGDIR/init" <<EOF
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.widthPercent: 10
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: iconbar
session.screen0.toolbar.maxOver: true
session.screen0.tooltipDelay: $delay_ms
EOF

    BACKENDS="${WLR_BACKENDS:-headless}"
    RENDERER="${WLR_RENDERER:-pixman}"

    if [[ "$BACKENDS" == *x11* ]]; then
      : "${DISPLAY:?DISPLAY must be set for x11 backend (run under scripts/fbwl-smoke-xvfb.sh)}"
    fi

    env WLR_BACKENDS="$BACKENDS" WLR_RENDERER="$RENDERER" \
      ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFGDIR" >"$LOG" 2>&1 &
    FBW_PID=$!

    timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
    timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

    ./fbwl-smoke-client --socket "$SOCKET" --title "tooltip-title-one-very-very-very-long" --stay-ms 10000 --width 800 --height 200 >/dev/null 2>&1 &
    CLIENT1_PID=$!
    ./fbwl-smoke-client --socket "$SOCKET" --title "tooltip-title-two-very-very-very-long" --stay-ms 10000 --width 800 --height 200 >/dev/null 2>&1 &
    CLIENT2_PID=$!

    timeout 5 bash -c "until rg -q 'Toolbar: iconbar item idx=0' '$LOG'; do sleep 0.05; done"

    local TB_POS
    TB_POS=$(rg 'Toolbar: position ' "$LOG" | tail -n 1)
    local TB_X TB_Y
    TB_X=$(echo "$TB_POS" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
    TB_Y=$(echo "$TB_POS" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)

    local HOVER_X HOVER_Y
    HOVER_X=$((TB_X + 5))
    HOVER_Y=$((TB_Y + 5))

    local OFFSET
    OFFSET=$(wc -c <"$LOG" | tr -d ' ')
    ./fbwl-input-injector --socket "$SOCKET" motion "$HOVER_X" "$HOVER_Y"

    if [[ "$expect_show" == "yes" ]]; then
      if [[ "$delay_ms" -gt 0 ]]; then
        sleep 0.2
        if tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Tooltip: show '; then
          echo "expected tooltipDelay=$delay_ms to delay tooltip show, but got early show (log=$LOG)" >&2
          exit 1
        fi
      fi
      timeout 3 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tooltip: show delay=$delay_ms '; do sleep 0.05; done"
    else
      sleep 1
      if tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Tooltip: show '; then
        echo "expected tooltipDelay=$delay_ms to suppress tooltip, but got show (log=$LOG)" >&2
        exit 1
      fi
    fi

    echo "ok: tooltipDelay=$delay_ms behaved as expected (log=$LOG)"
  )
}

run_case -1 "no"
run_case 0 "yes"
run_case 500 "yes"

echo "ok: tooltipDelay smoke passed"
