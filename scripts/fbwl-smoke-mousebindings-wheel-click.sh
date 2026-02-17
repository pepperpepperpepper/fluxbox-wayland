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
  (
    set -euo pipefail

    local NAME="$1"
    local WITH_BINDING="$2"

    local SOCKET="wayland-fbwl-mousebind-wheel-$UID-$$-$NAME"
    local LOG="/tmp/fluxbox-wayland-mousebind-wheel-$UID-$$-$NAME.log"
    local CFG_DIR
    CFG_DIR="$(mktemp -d "/tmp/fbwl-mousebind-wheel-$NAME-XXXXXX")"

    local MARK_DEFAULT="/tmp/fbwl-wheel-default-$UID-$$-$NAME"
    local MARK_BIND="/tmp/fbwl-wheel-bind-$UID-$$-$NAME"

    local FBW_PID=

    cleanup_case() {
      rm -f "$MARK_DEFAULT" "$MARK_BIND" 2>/dev/null || true
      rm -rf "$CFG_DIR" 2>/dev/null || true
      if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
      wait 2>/dev/null || true
    }
    trap cleanup_case EXIT

    : >"$LOG"
    rm -f "$MARK_DEFAULT" "$MARK_BIND"

    cat >"$CFG_DIR/init" <<EOF
session.keyFile: keys
session.styleFile: style
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: true
session.screen0.toolbar.placement: TopCenter
session.screen0.toolbar.widthPercent: 100
session.screen0.toolbar.height: 30
session.screen0.toolbar.tools: button.smoke
session.screen0.toolbar.button.smoke.label: Smoke
session.screen0.toolbar.button.smoke.commands: :::ExecCommand touch '$MARK_DEFAULT'
EOF

    : >"$CFG_DIR/style"

    if [[ "$WITH_BINDING" == "1" ]]; then
      cat >"$CFG_DIR/keys" <<EOF
OnToolbar Click4 :ExecCommand touch '$MARK_BIND'
EOF
    else
      : >"$CFG_DIR/keys"
    fi

    WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
      --no-xwayland \
      --socket "$SOCKET" \
      --config-dir "$CFG_DIR" \
      >"$LOG" 2>&1 &
    FBW_PID=$!

    timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
    timeout 5 bash -c "until rg -q 'Toolbar: built ' '$LOG'; do sleep 0.05; done"
    timeout 5 bash -c "until rg -qF 'Toolbar: tool tok=button.smoke ' '$LOG'; do sleep 0.05; done"
    timeout 5 bash -c "until rg -q 'Keys: loaded ' '$LOG'; do sleep 0.05; done"

    local BUILT_LINE
    BUILT_LINE="$(rg 'Toolbar: built ' "$LOG" | tail -n 1)"
    if [[ "$BUILT_LINE" =~ x=([-0-9]+)\ y=([-0-9]+)\ w=([0-9]+)\ h=([0-9]+) ]]; then
      local TB_X="${BASH_REMATCH[1]}"
      local TB_Y="${BASH_REMATCH[2]}"
      local TB_W="${BASH_REMATCH[3]}"
      local TB_H="${BASH_REMATCH[4]}"
      :
    else
      echo "failed to parse Toolbar: built line: $BUILT_LINE" >&2
      exit 1
    fi

    local TOOL_LINE
    TOOL_LINE="$(rg -F 'Toolbar: tool tok=button.smoke ' "$LOG" | tail -n 1)"
    if [[ "$TOOL_LINE" =~ lx=([-0-9]+)\ w=([0-9]+) ]]; then
      local LX="${BASH_REMATCH[1]}"
      local W="${BASH_REMATCH[2]}"
      :
    else
      echo "failed to parse Toolbar: tool line: $TOOL_LINE" >&2
      exit 1
    fi

    local SCROLL_X=$((TB_X + LX + W / 2))
    local SCROLL_Y=$((TB_Y + TB_H / 2))
    if (( SCROLL_X < TB_X || SCROLL_X >= TB_X + TB_W )); then
      echo "computed scroll X out of toolbar bounds: x=$SCROLL_X toolbar_x=$TB_X w=$TB_W (case=$NAME log=$LOG)" >&2
      exit 1
    fi

    local OFFSET
    OFFSET=$(wc -c <"$LOG" | tr -d ' ')

    rm -f "$MARK_DEFAULT" "$MARK_BIND"
    ./fbwl-input-injector --socket "$SOCKET" scroll-up "$SCROLL_X" "$SCROLL_Y" >/dev/null 2>&1

    if [[ "$WITH_BINDING" == "1" ]]; then
      timeout 2 bash -c "until [[ -f '$MARK_BIND' ]]; do sleep 0.05; done"
      if [[ -f "$MARK_DEFAULT" ]]; then
        echo "unexpected: toolbar default wheel command ran even though OnToolbar Click4 binding exists (case=$NAME log=$LOG)" >&2
        exit 1
      fi
      if tail -c +$((OFFSET + 1)) "$LOG" | rg -qF 'Toolbar: click button.smoke '; then
        echo "unexpected: toolbar click handler ran even though OnToolbar Click4 binding should preempt it (case=$NAME log=$LOG)" >&2
        exit 1
      fi
    else
      timeout 2 bash -c "until [[ -f '$MARK_DEFAULT' ]]; do sleep 0.05; done"
      timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -qF 'Toolbar: click button.smoke button=4'; do sleep 0.05; done"
      if [[ -f "$MARK_BIND" ]]; then
        echo "unexpected: OnToolbar Click4 binding ran in no-binding case (case=$NAME log=$LOG)" >&2
        exit 1
      fi
    fi

    echo "ok: wheel click mousebinding parity case passed (name=$NAME binding=$WITH_BINDING socket=$SOCKET log=$LOG)"
  )
}

run_case baseline 0
run_case override 1

echo "ok: wheel Click4/Click5 mousebinding parity smoke passed"

