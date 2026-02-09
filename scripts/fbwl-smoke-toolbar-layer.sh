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
  local layer="$1"
  local expect_toolbar_click="$2"

  (
    set -euo pipefail

    local SOCKET="wayland-fbwl-test-$UID-$$-$layer"
    local LOG="/tmp/fluxbox-wayland-toolbar-layer-$UID-$$-$layer.log"
    local CFGDIR
    CFGDIR="$(mktemp -d "/tmp/fbwl-toolbar-layer-$UID-XXXXXX")"

    local FBW_PID=
    local CLIENT_PID=

    cleanup_case() {
      rm -rf "$CFGDIR" 2>/dev/null || true
      if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
      if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
      wait 2>/dev/null || true
    }
    trap cleanup_case EXIT

    : >"$LOG"

    cat >"$CFGDIR/init" <<EOF
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.widthPercent: 50
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: workspacename
session.screen0.toolbar.maxOver: true
session.screen0.toolbar.layer: $layer
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

    ./fbwl-smoke-client --socket "$SOCKET" --title win --stay-ms 10000 --width 800 --height 200 >/dev/null 2>&1 &
    CLIENT_PID=$!

    timeout 5 bash -c "until rg -q 'Place: win' '$LOG'; do sleep 0.05; done"
    local PLACED
    PLACED=$(rg 'Place: win' "$LOG" | tail -n 1)
    local WX WY
    WX=$(echo "$PLACED" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
    WY=$(echo "$PLACED" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
    if [[ "$WY" -ne 0 ]]; then
      echo "expected win to overlap toolbar (y=0) but got: $PLACED" >&2
      exit 1
    fi

    local TB_POS
    TB_POS=$(rg 'Toolbar: position ' "$LOG" | tail -n 1)
    local TB_X TB_Y
    TB_X=$(echo "$TB_POS" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
    TB_Y=$(echo "$TB_POS" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)

    timeout 5 bash -c "until rg -q 'Surface size: win ' '$LOG'; do sleep 0.05; done"
    local SIZE
    SIZE=$(rg 'Surface size: win ' "$LOG" | tail -n 1)
    local WW WH
    WW=$(echo "$SIZE" | rg -o '[0-9]+x[0-9]+' | head -n 1 | cut -d x -f1)
    WH=$(echo "$SIZE" | rg -o '[0-9]+x[0-9]+' | head -n 1 | cut -d x -f2)

    local CLICK_X CLICK_Y
    CLICK_X=$((TB_X + 5))
    CLICK_Y=$((TB_Y + 5))
    if ! (( CLICK_X >= WX && CLICK_X < WX + WW && CLICK_Y >= WY && CLICK_Y < WY + WH )); then
      echo "expected win to overlap toolbar click (click=$CLICK_X,$CLICK_Y) but got: place=$PLACED size=$SIZE" >&2
      exit 1
    fi

    local OFFSET
    OFFSET=$(wc -c <"$LOG" | tr -d ' ')
    ./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"

    if [[ "$expect_toolbar_click" == "yes" ]]; then
      timeout 2 bash -c "tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Toolbar: click tool=workspacename cmd='"
    else
      timeout 2 bash -c "tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Pointer press .* hit=win'"
    fi

    echo "ok: toolbar.layer=$layer behaved as expected (log=$LOG)"
  )
}

run_case "Top" "yes"
run_case "Bottom" "no"

echo "ok: toolbar.layer smoke passed"
