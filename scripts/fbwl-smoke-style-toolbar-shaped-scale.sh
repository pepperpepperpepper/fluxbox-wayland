#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

source scripts/fbwl-smoke-report-lib.sh
REPORT_DIR="${FBWL_REPORT_DIR:-${FBWL_SMOKE_REPORT_DIR:-}}"

parse_toolbar_position() {
  local log="$1"
  local line
  line="$(rg 'Toolbar: position ' "$log" | tail -n 1)"
  if [[ -z "$line" ]]; then
    echo "failed to find Toolbar: position line in log: $log" >&2
    return 1
  fi
  if [[ "$line" =~ x=([-0-9]+)\ y=([-0-9]+)\ h=([0-9]+).*w=([0-9]+) ]]; then
    TB_X="${BASH_REMATCH[1]}"
    TB_Y="${BASH_REMATCH[2]}"
    TB_H="${BASH_REMATCH[3]}"
    TB_W="${BASH_REMATCH[4]}"
    return 0
  fi
  echo "failed to parse Toolbar: position line: $line" >&2
  return 1
}

parse_toolbar_tool() {
  local log="$1"
  local tok="$2"
  local line
  line="$(rg "Toolbar: tool tok=${tok} " "$log" | tail -n 1)"
  if [[ -z "$line" ]]; then
    echo "failed to find Toolbar: tool tok=$tok line in log: $log" >&2
    return 1
  fi
  if [[ "$line" =~ lx=([-0-9]+)\ w=([0-9]+) ]]; then
    TOOL_LX="${BASH_REMATCH[1]}"
    TOOL_W="${BASH_REMATCH[2]}"
    return 0
  fi
  echo "failed to parse Toolbar: tool tok=$tok line: $line" >&2
  return 1
}

run_case_shaped() (
  SOCKET="wayland-fbwl-style-toolbar-shaped-$UID-$$"
  LOG="/tmp/fluxbox-wayland-style-toolbar-shaped-$UID-$$.log"
  CFG_DIR="$(mktemp -d "/tmp/fbwl-style-toolbar-shaped-$UID-XXXXXX")"
  STYLE_FILE="/tmp/fbwl-style-toolbar-shaped-$UID-$$.cfg"

  cleanup() {
    rm -f "$STYLE_FILE" 2>/dev/null || true
    if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
    wait 2>/dev/null || true
    rm -rf "$CFG_DIR" 2>/dev/null || true
  }
  trap cleanup EXIT

  cat >"$CFG_DIR/init" <<'EOF'
session.screen0.allowRemoteActions: true
session.screen0.toolbar.visible: true
session.screen0.toolbar.autoHide: false
session.screen0.toolbar.tools: nextworkspace
EOF

  cat >"$STYLE_FILE" <<'EOF'
background: flat
background.color: #ff0000

toolbar: Flat Solid
toolbar.color: #0000ff
toolbar.textColor: #ffffff
toolbar.height: 50
toolbar.borderWidth: 0
toolbar.bevelWidth: 0
toolbar.shaped: true

toolbar.button: Flat Solid
toolbar.button.color: #000000
EOF

  : >"$LOG"

  fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

  WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
    ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFG_DIR" --style "$STYLE_FILE" >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Background: style solid' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

  if rg -q 'Style: ignored key=toolbar\\.shaped' "$LOG"; then
    echo "unexpected: toolbar.shaped should be parsed (not ignored)" >&2
    exit 1
  fi

  parse_toolbar_position "$LOG"

  if (( TB_W < 16 || TB_H < 16 )); then
    echo "unexpected toolbar size: w=$TB_W h=$TB_H" >&2
    exit 1
  fi

  # BottomCenter shaping cuts the TOP corners. The outermost pixel at (0,0)
  # should be transparent, revealing the solid red background.
  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#ff0000' \
    --sample-x $((TB_X + 0)) --sample-y $((TB_Y + 0)) >/dev/null 2>&1

  # A point well inside the button background should remain opaque black.
  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#000000' \
    --sample-x $((TB_X + 10)) --sample-y $((TB_Y + 25)) >/dev/null 2>&1

  fbwl_report_shot "style-toolbar-shaped.png" "Style toolbar.shaped: cut corners reveal red background"
)

run_case_scale_once() (
  local scale="$1"
  local expect_rgb="$2"

  SOCKET="wayland-fbwl-style-toolbar-scale-${scale}-$UID-$$"
  LOG="/tmp/fluxbox-wayland-style-toolbar-scale-${scale}-$UID-$$.log"
  CFG_DIR="$(mktemp -d "/tmp/fbwl-style-toolbar-scale-${scale}-$UID-XXXXXX")"
  STYLE_FILE="/tmp/fbwl-style-toolbar-scale-${scale}-$UID-$$.cfg"

  cleanup() {
    rm -f "$STYLE_FILE" 2>/dev/null || true
    if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
    wait 2>/dev/null || true
    rm -rf "$CFG_DIR" 2>/dev/null || true
  }
  trap cleanup EXIT

  cat >"$CFG_DIR/init" <<'EOF'
session.screen0.allowRemoteActions: true
session.screen0.toolbar.visible: true
session.screen0.toolbar.autoHide: false
session.screen0.toolbar.tools: nextworkspace
EOF

  cat >"$STYLE_FILE" <<EOF
background: flat
background.color: #00ffff

toolbar: Flat Solid
toolbar.color: #0000ff
toolbar.textColor: #ffffff
toolbar.height: 50
toolbar.borderWidth: 0
toolbar.bevelWidth: 0
toolbar.shaped: false

toolbar.button: Flat Solid
toolbar.button.color: #000000
toolbar.button.scale: $scale
EOF

  : >"$LOG"

  fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

  WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
    ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFG_DIR" --style "$STYLE_FILE" >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

  if rg -q 'Style: ignored key=toolbar\\.button\\.scale' "$LOG"; then
    echo "unexpected: toolbar.button.scale should be parsed (not ignored)" >&2
    exit 1
  fi

  parse_toolbar_position "$LOG"
  parse_toolbar_tool "$LOG" "nextworkspace"

  # Sample a point that is inside the arrow triangle when scale=100 (big arrow),
  # but outside the centered arrow when scale=500 (small arrow).
  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb "$expect_rgb" \
    --sample-x $((TB_X + TOOL_LX + 11)) --sample-y $((TB_Y + 25)) >/dev/null 2>&1

  fbwl_report_shot "style-toolbar-button-scale-${scale}.png" "Style toolbar.button.scale=$scale (expect $expect_rgb at sample)"
)

run_case_button_scale() (
  run_case_scale_once 100 "#ffffff"
  run_case_scale_once 500 "#000000"
)

run_case_shaped
run_case_button_scale

echo "ok: style toolbar.shaped + toolbar.button.scale smoke passed"
