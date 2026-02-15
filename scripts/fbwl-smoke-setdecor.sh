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

need_exe ./fluxbox-wayland
need_exe ./fluxbox-remote
need_exe ./fbwl-smoke-client
need_exe ./fbwl-screencopy-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-setdecor-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-setdecor-$UID-$$.log}"
SC_LOG="${SC_LOG:-/tmp/fbwl-screencopy-setdecor-$UID-$$.log}"
CFG_DIR="$(mktemp -d "/tmp/fbwl-setdecor-$UID-XXXXXX")"
STYLE_FILE="/tmp/fbwl-setdecor-style-$UID-$$.cfg"

TITLE_H=30
BORDER=10
BG_RGB='#ff00ff'
BORDER_RGB='#00ff00'
TITLE_RGB='#ff0000'

cleanup() {
  rm -f "$STYLE_FILE" 2>/dev/null || true
  rm -f "$SC_LOG" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFG_DIR" 2>/dev/null || true
}
trap cleanup EXIT

fbr() {
  DISPLAY='' ./fluxbox-remote --wayland --socket "$SOCKET" "$@"
}

expect_pixel() {
  local label="$1"
  local x="$2"
  local y="$3"
  local expected_rgb="$4"

  : >"$SC_LOG"
  if ! ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 \
      --expect-rgb "$expected_rgb" --sample-x "$x" --sample-y "$y" >"$SC_LOG" 2>&1; then
    echo "fbwl-screencopy-client failed ($label):" >&2
    cat "$SC_LOG" >&2 || true
    echo "fluxbox-wayland log tail:" >&2
    tail -n 200 "$LOG" >&2 || true
    exit 1
  fi
  rg -q '^ok screencopy$' "$SC_LOG"
}

run_case() {
  local decor_value="$1"
  local expect_border_rgb="$2"
  local expect_title_rgb="$3"

  local OFFSET START

  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  fbr setdecor "$decor_value" | rg -q '^ok$'
  START=$((OFFSET + 1))
  timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'SetDecor: setdecor-client '; do sleep 0.05; done"

  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  fbr moveto 200 200 | rg -q '^ok$'
  START=$((OFFSET + 1))
  timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'MoveTo: setdecor-client x='; do sleep 0.05; done"

  local move_line
  move_line="$(tail -c +$START "$LOG" | rg -m1 'MoveTo: setdecor-client x=' || true)"
  if [[ "$move_line" =~ x=([-0-9]+)[[:space:]]y=([-0-9]+)[[:space:]] ]]; then
    local vx="${BASH_REMATCH[1]}"
    local vy="${BASH_REMATCH[2]}"
  else
    echo "failed to parse MoveTo line: $move_line" >&2
    exit 1
  fi

  local border_x=$((vx - BORDER / 2))
  local border_y=$((vy + 10))
  local title_x=$((vx + VIEW_W - 2))
  local title_y=$((vy - TITLE_H / 2))

  expect_pixel "setdecor-${decor_value}-border" "$border_x" "$border_y" "$expect_border_rgb"
  expect_pixel "setdecor-${decor_value}-title" "$title_x" "$title_y" "$expect_title_rgb"
}

cat >"$CFG_DIR/init" <<'EOF'
session.screen0.allowRemoteActions: true
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: false
session.screen0.titlebar.left:
session.screen0.titlebar.right:
EOF

cat >"$STYLE_FILE" <<EOF
window.title.height: $TITLE_H
borderWidth: $BORDER
borderColor: $BORDER_RGB
window.frame.focusColor: $BORDER_RGB
window.frame.unfocusColor: $BORDER_RGB

window.title.focus.color: $TITLE_RGB
window.title.focus.colorTo: $TITLE_RGB
window.title.unfocus.color: $TITLE_RGB
window.title.unfocus.colorTo: $TITLE_RGB

window.label.focus.color: $TITLE_RGB
window.label.focus.colorTo: $TITLE_RGB
window.label.unfocus.color: $TITLE_RGB
window.label.unfocus.colorTo: $TITLE_RGB

window.label.focus.textColor: $TITLE_RGB
window.label.unfocus.textColor: $TITLE_RGB
EOF

: >"$LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
  ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFG_DIR" \
  --style "$STYLE_FILE" \
  --bg-color "$BG_RGB" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded .*\\(border=$BORDER title_h=$TITLE_H\\)' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title setdecor-client --stay-ms 20000 \
  --width 200 --height 120 --xdg-decoration >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Surface size: setdecor-client ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: setdecor-client ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Focus: setdecor-client' '$LOG'; do sleep 0.05; done"

surface_line="$(rg -m1 'Surface size: setdecor-client ' "$LOG" || true)"
if [[ "$surface_line" =~ Surface[[:space:]]size:[[:space:]]setdecor-client[[:space:]]([0-9]+)x([0-9]+) ]]; then
  VIEW_W="${BASH_REMATCH[1]}"
  VIEW_H="${BASH_REMATCH[2]}"
else
  echo "failed to parse Surface size line: $surface_line" >&2
  exit 1
fi

# Verify SetDecor accepts the full apps-style [Deco] syntax and applies masks.
run_case border "$BORDER_RGB" "$BG_RGB"
run_case none "$BG_RGB" "$BG_RGB"
run_case normal "$BORDER_RGB" "$TITLE_RGB"

echo "ok: setdecor smoke passed (socket=$SOCKET log=$LOG)"
