#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-style-$UID-$$.log}"
STYLE_FILE="${STYLE_FILE:-/tmp/fbwl-style-$UID-$$.cfg}"

cleanup() {
  rm -f "$STYLE_FILE"
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$STYLE_FILE" <<'EOF'
# Minimal Fluxbox style subset for fbwl smoke testing.
window.borderWidth: 10
window.borderColor: #112233
window.title.height: 40
window.title.focus.color: #334455
window.title.unfocus.color: #556677
EOF

: >"$LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --style "$STYLE_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded .*\\(border=10 title_h=40\\)' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title client-style --stay-ms 10000 --xdg-decoration >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Surface size: client-style 32x32' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: client-style ' '$LOG'; do sleep 0.05; done"

place_line="$(rg -m1 'Place: client-style ' "$LOG")"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

TITLE_H=40
BORDER=10

# Move by dragging the titlebar (no Alt), in the portion of the titlebar that
# only exists when TITLE_H is applied (i.e. above the old default titlebar).
START_X=$((X0 + 10))
START_Y=$((Y0 - TITLE_H + 2))
END_X=$((START_X + 90))
END_Y=$((START_Y + 40))
DX=$((END_X - START_X))
DY=$((END_Y - START_Y))
X1=$((X0 + DX))
Y1=$((Y0 + DY))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-left "$START_X" "$START_Y" "$END_X" "$END_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Move: client-style x=$X1 y=$Y1"

# Resize by dragging bottom-right border (no Alt) at a coordinate that only
# hits the border when BORDER is applied.
W0=32
H0=32
RS_START_X=$((X1 + W0 + BORDER / 2))
RS_START_Y=$((Y1 + H0 + BORDER / 2))
RS_END_X=$((RS_START_X + 50))
RS_END_Y=$((RS_START_Y + 60))
RS_DX=$((RS_END_X - RS_START_X))
RS_DY=$((RS_END_Y - RS_START_Y))
W1=$((W0 + RS_DX))
H1=$((H0 + RS_DY))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" drag-left "$RS_START_X" "$RS_START_Y" "$RS_END_X" "$RS_END_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: client-style w=$W1 h=$H1"
timeout 5 bash -c "until rg -q 'Surface size: client-style ${W1}x${H1}' '$LOG'; do sleep 0.05; done"

echo "ok: style smoke passed (socket=$SOCKET log=$LOG style=$STYLE_FILE)"
