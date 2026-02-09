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
  local drop_on="$2" # content|titlebar

  SOCKET="wayland-fbwl-tabsattach-$UID-$$-$case_name"
  LOG="/tmp/fluxbox-wayland-tabsattach-$UID-$$-$case_name.log"
  CFG_DIR="$(mktemp -d)"
  KEYS_FILE="$CFG_DIR/keys"

  cat >"$CFG_DIR/init" <<EOF
session.screen0.toolbar.visible: false
session.screen0.windowPlacement: RowSmartPlacement
session.screen0.tabs.intitlebar: true
session.screen0.tabs.usePixmap: false
session.screen0.tab.placement: TopLeft
session.screen0.tab.width: 96
session.tabsAttachArea: Titlebar
session.styleFile: $CFG_DIR/style
EOF

  BORDER=4
  TITLE_H=24
  cat >"$CFG_DIR/style" <<EOF
window.borderWidth: $BORDER
window.title.height: $TITLE_H
EOF

  cat >"$KEYS_FILE" <<'EOF'
OnTitlebar Mod1 Mouse1 :StartTabbing
EOF

  : >"$LOG"
  WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
    --no-xwayland \
    --socket "$SOCKET" \
    --config-dir "$CFG_DIR" \
    --keys "$KEYS_FILE" \
    >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Style: loaded ' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Output: ' '$LOG'; do sleep 0.05; done"

  local anchor="anchor-$case_name"
  local mover="mover-$case_name"

  ./fbwl-smoke-client --socket "$SOCKET" --title "$anchor" --stay-ms 10000 --width 320 --height 200 --xdg-decoration >/dev/null 2>&1 &
  C0_PID=$!
  timeout 10 bash -c "until rg -q 'Place: $anchor ' '$LOG'; do sleep 0.05; done"

  ./fbwl-smoke-client --socket "$SOCKET" --title "$mover" --stay-ms 10000 --width 320 --height 200 --xdg-decoration >/dev/null 2>&1 &
  C1_PID=$!
  timeout 10 bash -c "until rg -q 'Place: $mover ' '$LOG'; do sleep 0.05; done"

  local place_a place_b
  place_a="$(rg -m1 "Place: $anchor " "$LOG")"
  place_b="$(rg -m1 "Place: $mover " "$LOG")"

  local ax ay bx by
  if [[ "$place_a" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
    ax="${BASH_REMATCH[1]}"
    ay="${BASH_REMATCH[2]}"
  else
    echo "failed to parse Place line for anchor: $place_a" >&2
    exit 1
  fi
  if [[ "$place_b" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
    bx="${BASH_REMATCH[1]}"
    by="${BASH_REMATCH[2]}"
  else
    echo "failed to parse Place line for mover: $place_b" >&2
    exit 1
  fi

  local start_x start_y end_x end_y
  start_x=$((bx + 20))
  start_y=$((by - TITLE_H / 2))

  if [[ "$drop_on" == "content" ]]; then
    end_x=$((ax + 40))
    end_y=$((ay + 60))
  elif [[ "$drop_on" == "titlebar" ]]; then
    end_x=$((ax + 40))
    end_y=$((ay - TITLE_H / 2))
  else
    echo "invalid drop_on: $drop_on" >&2
    exit 1
  fi

  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-input-injector --socket "$SOCKET" drag-alt-left "$start_x" "$start_y" "$end_x" "$end_y" >/dev/null 2>&1

  if [[ "$drop_on" == "content" ]]; then
    # Give the compositor a moment to process release, then assert we did NOT attach.
    sleep 0.5
    if tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Tabs: attach reason=drag'; then
      echo "unexpected attach when dropping on content (Titlebar-only mode)" >&2
      exit 1
    fi
  else
    timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: attach reason=drag'; do sleep 0.05; done"
  fi

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

run_case content-drop content
run_case titlebar-drop titlebar

echo "ok: tabsAttachArea Titlebar smoke passed"
