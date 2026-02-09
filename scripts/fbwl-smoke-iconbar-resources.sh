#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd base64
need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd wc

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

run_case_mode_minimized_workspace() {
  (
    set -euo pipefail

    local SOCKET="wayland-fbwl-test-$UID-$$-iconbar-mode"
    local LOG="/tmp/fluxbox-wayland-iconbar-mode-$UID-$$.log"
    local CFGDIR
    CFGDIR="$(mktemp -d "/tmp/fbwl-iconbar-mode-$UID-XXXXXX")"

    local FBW_PID=
    local A_PID=
    local B_PID=

    cleanup_case() {
      rm -rf "$CFGDIR" 2>/dev/null || true
      if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
      if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
      if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
      wait 2>/dev/null || true
    }
    trap cleanup_case EXIT

    : >"$LOG"

    cat >"$CFGDIR/init" <<'EOF'
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.widthPercent: 25
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: iconbar
session.screen0.toolbar.maxOver: true
session.screen0.iconbar.mode: {static groups} (minimized=yes) (workspace)
session.screen0.iconbar.usePixmap: false
session.screen0.iconbar.iconifiedPattern: ( %t )
EOF

    WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
      --no-xwayland \
      --socket "$SOCKET" \
      --workspaces 2 \
      --config-dir "$CFGDIR" \
      >"$LOG" 2>&1 &
    FBW_PID=$!

    timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
    timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

    ./fbwl-smoke-client --socket "$SOCKET" --title im-a --stay-ms 10000 >/dev/null 2>&1 &
    A_PID=$!
    ./fbwl-smoke-client --socket "$SOCKET" --title im-b --stay-ms 10000 >/dev/null 2>&1 &
    B_PID=$!

    timeout 5 bash -c "until rg -q 'Focus: im-a' '$LOG' && rg -q 'Focus: im-b' '$LOG'; do sleep 0.05; done"

    local FOCUSED_VIEW
    FOCUSED_VIEW=$(
      rg -o 'Focus: im-[ab]' "$LOG" | tail -n 1 | awk '{print $2}'
    )

    local TARGET OTHER
    case "$FOCUSED_VIEW" in
      im-a) TARGET=im-a; OTHER=im-b ;;
      im-b) TARGET=im-b; OTHER=im-a ;;
      *) echo "failed to determine focused view (got: $FOCUSED_VIEW)" >&2; exit 1 ;;
    esac

    local OFFSET
    OFFSET=$(wc -c <"$LOG" | tr -d ' ')
    ./fbwl-input-injector --socket "$SOCKET" key alt-i

    timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Minimize: $TARGET on reason=keybinding'; do sleep 0.05; done"
    timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Toolbar: iconbar item idx=0 .*title=$TARGET .*minimized=1'; do sleep 0.05; done"

    tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Toolbar: iconbar item idx=0 .*title=$TARGET minimized=1 .*label=\\( $TARGET \\)"
    if tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Toolbar: iconbar item .*title=$OTHER"; then
      echo "expected iconbar.mode minimized=yes to hide non-minimized window ($OTHER), but it appeared (log=$LOG)" >&2
      exit 1
    fi

    echo "ok: iconbar.mode minimized/workspace behaved (target=$TARGET log=$LOG)"
  )
}

run_case_alignment_right_fixed_width() {
  (
    set -euo pipefail

    local SOCKET="wayland-fbwl-test-$UID-$$-iconbar-align"
    local LOG="/tmp/fluxbox-wayland-iconbar-align-$UID-$$.log"
    local CFGDIR
    CFGDIR="$(mktemp -d "/tmp/fbwl-iconbar-align-$UID-XXXXXX")"

    local FBW_PID=
    local A_PID=
    local B_PID=

    cleanup_case() {
      rm -rf "$CFGDIR" 2>/dev/null || true
      if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
      if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
      if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
      wait 2>/dev/null || true
    }
    trap cleanup_case EXIT

    : >"$LOG"

    cat >"$CFGDIR/init" <<'EOF'
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: BottomCenter
session.screen0.toolbar.widthPercent: 100
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: iconbar
session.screen0.toolbar.maxOver: true
session.screen0.iconbar.mode: {static groups} (workspace)
session.screen0.iconbar.alignment: Right
session.screen0.iconbar.iconWidth: 50
session.screen0.iconbar.usePixmap: false
EOF

    WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
      --no-xwayland \
      --socket "$SOCKET" \
      --workspaces 1 \
      --config-dir "$CFGDIR" \
      >"$LOG" 2>&1 &
    FBW_PID=$!

    timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
    timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

    ./fbwl-smoke-client --socket "$SOCKET" --title ar-a --stay-ms 10000 >/dev/null 2>&1 &
    A_PID=$!
    ./fbwl-smoke-client --socket "$SOCKET" --title ar-b --stay-ms 10000 >/dev/null 2>&1 &
    B_PID=$!

    timeout 5 bash -c "until rg -q 'Toolbar: iconbar item idx=0' '$LOG' && rg -q 'Toolbar: iconbar item idx=1' '$LOG'; do sleep 0.05; done"

    local item0
    item0="$(rg 'Toolbar: iconbar item idx=0' "$LOG" | tail -n 1)"
    if [[ "$item0" =~ lx=([-0-9]+)\ w=([0-9]+)\ title= ]]; then
      local LX="${BASH_REMATCH[1]}"
      local W="${BASH_REMATCH[2]}"
      if [[ "$W" != "50" ]]; then
        echo "expected iconWidth=50 to apply, got w=$W (line=$item0 log=$LOG)" >&2
        exit 1
      fi
      if (( LX <= 0 )); then
        echo "expected Right alignment to offset lx>0, got lx=$LX (line=$item0 log=$LOG)" >&2
        exit 1
      fi
    else
      echo "failed to parse iconbar item idx=0 line: $item0" >&2
      exit 1
    fi

    echo "ok: iconbar.alignment Right + iconWidth fixed worked (log=$LOG)"
  )
}

run_case_alignment_relative_smart_varies_by_title() {
  (
    set -euo pipefail

    local SOCKET="wayland-fbwl-test-$UID-$$-iconbar-relsmart"
    local LOG="/tmp/fluxbox-wayland-iconbar-relsmart-$UID-$$.log"
    local CFGDIR
    CFGDIR="$(mktemp -d "/tmp/fbwl-iconbar-relsmart-$UID-XXXXXX")"

    local FBW_PID=
    local A_PID=
    local B_PID=

    cleanup_case() {
      rm -rf "$CFGDIR" 2>/dev/null || true
      if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
      if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
      if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
      wait 2>/dev/null || true
    }
    trap cleanup_case EXIT

    : >"$LOG"

    cat >"$CFGDIR/init" <<'EOF'
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: BottomCenter
session.screen0.toolbar.widthPercent: 100
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: iconbar
session.screen0.toolbar.maxOver: true
session.screen0.iconbar.mode: {static groups} (workspace)
session.screen0.iconbar.alignment: RelativeSmart
session.screen0.iconbar.usePixmap: false
EOF

    WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
      --no-xwayland \
      --socket "$SOCKET" \
      --workspaces 1 \
      --config-dir "$CFGDIR" \
      >"$LOG" 2>&1 &
    FBW_PID=$!

    timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
    timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

    local SHORT_TITLE="rs-short"
    local LONG_TITLE="rs-long-very-very-very-long-title-to-force-relativesmart-width"

    ./fbwl-smoke-client --socket "$SOCKET" --title "$SHORT_TITLE" --stay-ms 10000 >/dev/null 2>&1 &
    A_PID=$!
    ./fbwl-smoke-client --socket "$SOCKET" --title "$LONG_TITLE" --stay-ms 10000 >/dev/null 2>&1 &
    B_PID=$!

    timeout 5 bash -c "until rg -q 'Toolbar: iconbar item idx=0' '$LOG' && rg -q 'Toolbar: iconbar item idx=1' '$LOG'; do sleep 0.05; done"
    timeout 5 bash -c "until rg -q 'Toolbar: iconbar item .*title=$SHORT_TITLE minimized=' '$LOG' && rg -q 'Toolbar: iconbar item .*title=$LONG_TITLE minimized=' '$LOG'; do sleep 0.05; done"

    local short_line long_line
    short_line="$(rg "Toolbar: iconbar item .*title=$SHORT_TITLE minimized=" "$LOG" | tail -n 1)"
    long_line="$(rg "Toolbar: iconbar item .*title=$LONG_TITLE minimized=" "$LOG" | tail -n 1)"

    local W_SHORT W_LONG
    if [[ "$short_line" =~ w=([0-9]+)[[:space:]]title= ]]; then
      W_SHORT="${BASH_REMATCH[1]}"
    else
      echo "failed to parse RelativeSmart short item width: $short_line" >&2
      exit 1
    fi
    if [[ "$long_line" =~ w=([0-9]+)[[:space:]]title= ]]; then
      W_LONG="${BASH_REMATCH[1]}"
    else
      echo "failed to parse RelativeSmart long item width: $long_line" >&2
      exit 1
    fi

    if (( W_LONG <= W_SHORT )); then
      echo "expected RelativeSmart to allocate more width to long title (short=$W_SHORT long=$W_LONG log=$LOG)" >&2
      exit 1
    fi

    echo "ok: iconbar.alignment RelativeSmart varied by title (short=$W_SHORT long=$W_LONG log=$LOG)"
  )
}

run_case_mode_maximizedhorizontal_filter() {
  (
    set -euo pipefail

    local SOCKET="wayland-fbwl-test-$UID-$$-iconbar-maxh"
    local LOG="/tmp/fluxbox-wayland-iconbar-maxh-$UID-$$.log"
    local CFGDIR
    CFGDIR="$(mktemp -d "/tmp/fbwl-iconbar-maxh-$UID-XXXXXX")"
    local KEYS_FILE
    KEYS_FILE="$(mktemp "/tmp/fbwl-keys-iconbar-maxh-$UID-XXXXXX.keys")"

    local FBW_PID=
    local A_PID=
    local B_PID=

    cleanup_case() {
      rm -rf "$CFGDIR" 2>/dev/null || true
      rm -f "$KEYS_FILE" 2>/dev/null || true
      if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
      if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
      if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
      wait 2>/dev/null || true
    }
    trap cleanup_case EXIT

    : >"$LOG"

    cat >"$CFGDIR/init" <<'EOF'
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: BottomCenter
session.screen0.toolbar.widthPercent: 100
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: iconbar
session.screen0.toolbar.maxOver: true
session.screen0.iconbar.mode: {static groups} (workspace) (maximizedhorizontal=yes)
session.screen0.iconbar.usePixmap: false
EOF

    cat >"$KEYS_FILE" <<'EOF'
Mod1 i :MaximizeHorizontal
EOF

    WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
      --no-xwayland \
      --socket "$SOCKET" \
      --workspaces 1 \
      --config-dir "$CFGDIR" \
      --keys "$KEYS_FILE" \
      >"$LOG" 2>&1 &
    FBW_PID=$!

    timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
    timeout 5 bash -c "until rg -q 'Toolbar: built ' '$LOG'; do sleep 0.05; done"

    ./fbwl-smoke-client --socket "$SOCKET" --title mh-a --stay-ms 10000 >/dev/null 2>&1 &
    A_PID=$!
    ./fbwl-smoke-client --socket "$SOCKET" --title mh-b --stay-ms 10000 >/dev/null 2>&1 &
    B_PID=$!

    timeout 5 bash -c "until rg -q 'Focus: mh-a' '$LOG' && rg -q 'Focus: mh-b' '$LOG'; do sleep 0.05; done"

    local FOCUSED_VIEW
    FOCUSED_VIEW=$(
      rg -o 'Focus: mh-[ab]' "$LOG" | tail -n 1 | awk '{print $2}'
    )
    local OTHER_VIEW
    case "$FOCUSED_VIEW" in
      mh-a) OTHER_VIEW=mh-b ;;
      mh-b) OTHER_VIEW=mh-a ;;
      *) echo "failed to determine focused view (got: $FOCUSED_VIEW)" >&2; exit 1 ;;
    esac

    local OFFSET
    OFFSET=$(wc -c <"$LOG" | tr -d ' ')
    ./fbwl-input-injector --socket "$SOCKET" key alt-i
    timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'MaximizeHorizontal: $FOCUSED_VIEW on '; do sleep 0.05; done"
    timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Toolbar: iconbar item idx=0 .*title=$FOCUSED_VIEW '; do sleep 0.05; done"

    if tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Toolbar: iconbar item .*title=$OTHER_VIEW "; then
      echo "expected iconbar.mode maximizedhorizontal=yes to hide other view ($OTHER_VIEW), but it appeared (log=$LOG)" >&2
      exit 1
    fi

    echo "ok: iconbar.mode maximizedhorizontal term filtered as expected (focused=$FOCUSED_VIEW log=$LOG)"
  )
}

run_case_mode_workspacename_filter() {
  (
    set -euo pipefail

    local SOCKET="wayland-fbwl-test-$UID-$$-iconbar-wsname"
    local LOG="/tmp/fluxbox-wayland-iconbar-wsname-$UID-$$.log"
    local CFGDIR
    CFGDIR="$(mktemp -d "/tmp/fbwl-iconbar-wsname-$UID-XXXXXX")"

    local FBW_PID=
    local A_PID=
    local B_PID=

    cleanup_case() {
      rm -rf "$CFGDIR" 2>/dev/null || true
      if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
      if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
      if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
      wait 2>/dev/null || true
    }
    trap cleanup_case EXIT

    : >"$LOG"

    cat >"$CFGDIR/init" <<'EOF'
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: BottomCenter
session.screen0.toolbar.widthPercent: 100
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: iconbar
session.screen0.toolbar.maxOver: true
session.screen0.workspaceNames: ws-a, ws-b
session.screen0.iconbar.mode: {static groups} (workspacename=ws-b)
session.screen0.iconbar.usePixmap: false
EOF

    WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
      --no-xwayland \
      --socket "$SOCKET" \
      --workspaces 2 \
      --config-dir "$CFGDIR" \
      >"$LOG" 2>&1 &
    FBW_PID=$!

    timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
    timeout 5 bash -c "until rg -q 'Toolbar: built ' '$LOG'; do sleep 0.05; done"

    ./fbwl-smoke-client --socket "$SOCKET" --title wn-a --stay-ms 10000 >/dev/null 2>&1 &
    A_PID=$!
    ./fbwl-smoke-client --socket "$SOCKET" --title wn-b --stay-ms 10000 >/dev/null 2>&1 &
    B_PID=$!

    timeout 5 bash -c "until rg -q 'Focus: wn-a' '$LOG' && rg -q 'Focus: wn-b' '$LOG'; do sleep 0.05; done"

    local FOCUSED_VIEW
    FOCUSED_VIEW=$(
      rg -o 'Focus: wn-[ab]' "$LOG" | tail -n 1 | awk '{print $2}'
    )

    local TARGET OTHER
    case "$FOCUSED_VIEW" in
      wn-a) TARGET=wn-a; OTHER=wn-b ;;
      wn-b) TARGET=wn-b; OTHER=wn-a ;;
      *) echo "failed to determine focused view (got: $FOCUSED_VIEW)" >&2; exit 1 ;;
    esac

    local OFFSET
    OFFSET=$(wc -c <"$LOG" | tr -d ' ')
    ./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-2

    timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: view=$TARGET ws=2 '; do sleep 0.05; done"
    timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Toolbar: iconbar item idx=0 .*title=$TARGET '; do sleep 0.05; done"

    if tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Toolbar: iconbar item .*title=$OTHER "; then
      echo "expected iconbar.mode workspacename=ws-b to hide other view ($OTHER), but it appeared (log=$LOG)" >&2
      exit 1
    fi

    echo "ok: iconbar.mode workspacename term filtered as expected (target=$TARGET log=$LOG)"
  )
}

run_case_usepixmap_icon_file() {
  (
    set -euo pipefail

    local SOCKET="wayland-fbwl-test-$UID-$$-iconbar-usepixmap"
    local LOG="/tmp/fluxbox-wayland-iconbar-usepixmap-$UID-$$.log"
    local CFGDIR
    CFGDIR="$(mktemp -d "/tmp/fbwl-iconbar-usepixmap-$UID-XXXXXX")"

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

    local ICON="$CFGDIR/testicon.png"
    base64 -d >"$ICON" <<'EOF'
iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAFklEQVR4nGNg+E8hHDVg1IBRA4aLAQAxtf4QMNSIagAAAABJRU5ErkJggg==
EOF

    cat >"$CFGDIR/init" <<EOF
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.widthPercent: 30
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: iconbar
session.screen0.toolbar.maxOver: true
session.screen0.iconbar.mode: {static groups} (workspace)
session.screen0.iconbar.usePixmap: true
EOF

    WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
      --no-xwayland \
      --socket "$SOCKET" \
      --workspaces 1 \
      --config-dir "$CFGDIR" \
      >"$LOG" 2>&1 &
    FBW_PID=$!

    timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
    timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

    ./fbwl-smoke-client --socket "$SOCKET" --title up-x --app-id "$ICON" --stay-ms 10000 >/dev/null 2>&1 &
    CLIENT_PID=$!

    timeout 5 bash -c "until rg -q 'Toolbar: iconbar item idx=0 .*title=up-x .*icon=1' '$LOG'; do sleep 0.05; done"

    echo "ok: iconbar.usePixmap loaded icon from app_id path (log=$LOG)"
  )
}

run_case_icontextpadding_tooltip() {
  (
    set -euo pipefail

    local SOCKET="wayland-fbwl-test-$UID-$$-iconbar-padding"
    local LOG="/tmp/fluxbox-wayland-iconbar-padding-$UID-$$.log"
    local CFGDIR
    CFGDIR="$(mktemp -d "/tmp/fbwl-iconbar-padding-$UID-XXXXXX")"

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

    cat >"$CFGDIR/init" <<'EOF'
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.widthPercent: 10
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: iconbar
session.screen0.toolbar.maxOver: true
session.screen0.iconbar.mode: {static groups} (workspace)
session.screen0.iconbar.usePixmap: false
session.screen0.iconbar.iconTextPadding: 1000
session.screen0.tooltipDelay: 0
EOF

    WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
      --no-xwayland \
      --socket "$SOCKET" \
      --workspaces 1 \
      --config-dir "$CFGDIR" \
      >"$LOG" 2>&1 &
    FBW_PID=$!

    timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
    timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

    ./fbwl-smoke-client --socket "$SOCKET" --title pad-x --stay-ms 10000 >/dev/null 2>&1 &
    CLIENT_PID=$!

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

    timeout 3 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tooltip: show delay=0 '; do sleep 0.05; done"

    echo "ok: iconbar.iconTextPadding triggers tooltip (log=$LOG)"
  )
}

run_case_mode_minimized_workspace
run_case_alignment_right_fixed_width
run_case_alignment_relative_smart_varies_by_title
run_case_mode_maximizedhorizontal_filter
run_case_mode_workspacename_filter
run_case_usepixmap_icon_file
run_case_icontextpadding_tooltip

echo "ok: iconbar resources smoke passed"
