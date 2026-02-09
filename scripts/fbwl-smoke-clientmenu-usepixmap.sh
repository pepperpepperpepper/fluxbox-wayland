#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1 (build first)" >&2; exit 1; }
}

need_cmd base64
need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd wc

need_exe ./fluxbox-wayland
need_exe ./fbwl-input-injector
need_exe ./fbwl-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

run_case() {
  (
    set -euo pipefail

    local label="$1"
    local usepixmap="$2"
    local want_icon="$3"

    local SOCKET="wayland-fbwl-test-$UID-$$-clientmenu-$label"
    local LOG="/tmp/fluxbox-wayland-clientmenu-$label-$UID-$$.log"
    local CFGDIR
    CFGDIR="$(mktemp -d "/tmp/fbwl-clientmenu-$label-$UID-XXXXXX")"

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

    local ICON="$CFGDIR/testicon.png"
    base64 -d >"$ICON" <<'EOF'
iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAFklEQVR4nGNg+E8hHDVg1IBRA4aLAQAxtf4QMNSIagAAAABJRU5ErkJggg==
EOF

    cat >"$CFGDIR/init" <<EOF
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: false
session.screen0.clientMenu.usePixmap: $usepixmap
session.keyFile: mykeys
EOF

    cat >"$CFGDIR/mykeys" <<'EOF'
Mod1 F2 :ClientMenu
EOF

    WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
      --no-xwayland \
      --socket "$SOCKET" \
      --workspaces 1 \
      --config-dir "$CFGDIR" \
      >"$LOG" 2>&1 &
    FBW_PID=$!

    timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

    # Ensure deterministic menu position.
    ./fbwl-input-injector --socket "$SOCKET" motion 100 100 >/dev/null 2>&1 || true

    ./fbwl-smoke-client --socket "$SOCKET" --title cm-a --app-id "$ICON" --stay-ms 10000 >/dev/null 2>&1 &
    A_PID=$!
    timeout 5 bash -c "until rg -q 'Focus: cm-a' '$LOG'; do sleep 0.05; done"

    ./fbwl-smoke-client --socket "$SOCKET" --title cm-b --app-id "$ICON" --stay-ms 10000 >/dev/null 2>&1 &
    B_PID=$!
    timeout 5 bash -c "until rg -q 'Focus: cm-b' '$LOG'; do sleep 0.05; done"

    local OFFSET
    OFFSET=$(wc -c <"$LOG" | tr -d ' ')
    ./fbwl-input-injector --socket "$SOCKET" key alt-f2 >/dev/null 2>&1
    local START=$((OFFSET + 1))

    timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Menu: open at '; do sleep 0.05; done"
    timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'ClientMenu: item idx=0 '; do sleep 0.05; done"

    local line_a
    local line_b
    line_a="$(tail -c +$START "$LOG" | rg 'ClientMenu: item idx=[0-9]+ .*title=cm-a ' | tail -n 1)"
    line_b="$(tail -c +$START "$LOG" | rg 'ClientMenu: item idx=[0-9]+ .*title=cm-b ' | tail -n 1)"
    if [[ -z "$line_a" || -z "$line_b" ]]; then
      echo "expected ClientMenu items for cm-a/cm-b (log=$LOG)" >&2
      exit 1
    fi
    if ! echo "$line_a" | rg -q " icon=$want_icon$"; then
      echo "expected cm-a icon=$want_icon (line=$line_a log=$LOG)" >&2
      exit 1
    fi
    if ! echo "$line_b" | rg -q " icon=$want_icon$"; then
      echo "expected cm-b icon=$want_icon (line=$line_b log=$LOG)" >&2
      exit 1
    fi

    local idx_a
    if [[ "$line_a" =~ idx=([0-9]+) ]]; then
      idx_a="${BASH_REMATCH[1]}"
    else
      echo "failed to parse idx from line: $line_a" >&2
      exit 1
    fi

    local open_line
    open_line="$(tail -c +$START "$LOG" | rg -m1 'Menu: open at ' || true)"
    if [[ -z "$open_line" ]]; then
      echo "expected Menu open log line after ClientMenu binding (log=$LOG)" >&2
      exit 1
    fi

    local MENU_X MENU_Y
    MENU_X=$(echo "$open_line" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
    MENU_Y=$(echo "$open_line" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)

    local ITEM_H=24
    local CLICK_X=$((MENU_X + 10))
    local CLICK_Y=$((MENU_Y + 10 + idx_a * ITEM_H))

    # Clicking a non-focused item should change focus.
    OFFSET=$(wc -c <"$LOG" | tr -d ' ')
    ./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y" >/dev/null 2>&1
    local AFTER_CLICK=$((OFFSET + 1))
    timeout 5 bash -c "until tail -c +$AFTER_CLICK '$LOG' | rg -q 'Focus: cm-a'; do sleep 0.05; done"

    echo "ok: clientMenu.usePixmap=$usepixmap behaved (icon=$want_icon log=$LOG)"
  )
}

run_case usepixmap-on true 1
run_case usepixmap-off false 0

echo "ok: clientMenu.usePixmap smoke passed"

