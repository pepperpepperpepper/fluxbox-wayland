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

SOCKET="${SOCKET:-wayland-fbwl-style-window-round-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-style-window-round-$UID-$$.log}"
STYLE_FILE="${STYLE_FILE:-/tmp/fbwl-style-window-round-$UID-$$.cfg}"
CFG_DIR="$(mktemp -d "/tmp/fbwl-style-window-round-$UID-XXXXXX")"
REPORT_DIR="${FBWL_REPORT_DIR:-${FBWL_SMOKE_REPORT_DIR:-}}"

cleanup() {
  rm -f "$STYLE_FILE" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFG_DIR" 2>/dev/null || true
}
trap cleanup EXIT

cat >"$CFG_DIR/init" <<'EOF'
session.screen0.allowRemoteActions: true
session.screen0.titlebar.left: Stick
session.screen0.titlebar.right: Shade Minimize Maximize Close
EOF

cat >"$STYLE_FILE" <<'EOF'
background: Flat Solid
background.color: #00ff00

borderWidth: 0
window.title.height: 40
window.roundCorners: TopLeft TopRight

window.title.focus: Flat Solid
window.title.unfocus: Flat Solid
window.title.focus.color: #ff0000
window.title.unfocus.color: #ff0000

window.label.focus: Flat Solid
window.label.unfocus: Flat Solid
window.label.focus.color: #ff0000
window.label.unfocus.color: #ff0000

window.label.focus.textColor: #ff0000
window.label.unfocus.textColor: #ff0000
EOF

: >"$LOG"

fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
  ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFG_DIR" --style "$STYLE_FILE" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title round-client --stay-ms 10000 --xdg-decoration --width 300 --height 200 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Place: round-client ' '$LOG'; do sleep 0.05; done"

fbwl_report_shot "style-window-round-corners.png" "window.roundCorners: top corners clipped to desktop"

place_line="$(rg -m1 'Place: round-client ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

TITLE_H=40

SAMPLE_X_CORNER=$((X0 + 0))
SAMPLE_Y_CORNER=$((Y0 - TITLE_H + 0))
SAMPLE_X_INSIDE=$((X0 + 20))
SAMPLE_Y_INSIDE=$((Y0 - TITLE_H + 20))

./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#00ff00' \
  --sample-x "$SAMPLE_X_CORNER" --sample-y "$SAMPLE_Y_CORNER" >/dev/null 2>&1
./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#ff0000' \
  --sample-x "$SAMPLE_X_INSIDE" --sample-y "$SAMPLE_Y_INSIDE" >/dev/null 2>&1

echo "ok: style window roundCorners smoke passed (socket=$SOCKET log=$LOG style=$STYLE_FILE)"
