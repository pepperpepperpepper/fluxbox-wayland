#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-mousebind-winbutton-ctx-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-mousebind-winbutton-ctx-$UID-$$.log}"
CFG_DIR="$(mktemp -d /tmp/fbwl-mousebind-winbutton-ctx-XXXXXX)"

MARK_WINBUTTON="${MARK_WINBUTTON:-/tmp/fbwl-mousebind-winbutton-$UID-$$}"
MARK_MIN="${MARK_MIN:-/tmp/fbwl-mousebind-minbutton-$UID-$$}"
MARK_MAX="${MARK_MAX:-/tmp/fbwl-mousebind-maxbutton-$UID-$$}"

cleanup() {
  rm -f "$MARK_WINBUTTON" "$MARK_MIN" "$MARK_MAX" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFG_DIR" 2>/dev/null || true
}
trap cleanup EXIT

rm -f "$MARK_WINBUTTON" "$MARK_MIN" "$MARK_MAX"
: >"$LOG"

cat >"$CFG_DIR/init" <<EOF
session.keyFile: keys
session.styleFile: style
session.screen0.defaultDeco: NORMAL
session.screen0.toolbar.visible: false
session.screen0.tabs.intitlebar: false
session.screen0.titlebar.left:
session.screen0.titlebar.right: Minimize Maximize Close
EOF

cat >"$CFG_DIR/style" <<'EOF'
window.borderWidth: 4
window.title.height: 24
window.bevelWidth: 4
EOF

cat >"$CFG_DIR/keys" <<EOF
OnMinButton Click1 :ExecCommand touch '$MARK_MIN'
OnMaxButton Click1 :ExecCommand touch '$MARK_MAX'
OnWinButton Click1 :ExecCommand touch '$MARK_WINBUTTON'
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFG_DIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded ' '$LOG'; do sleep 0.05; done"

TITLE="winbutton-mouse-contexts"
W0=300
H0=200
./fbwl-smoke-client --socket "$SOCKET" --title "$TITLE" --stay-ms 15000 --width "$W0" --height "$H0" --xdg-decoration >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 10 bash -c "until rg -q 'Surface size: $TITLE ' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Place: $TITLE ' '$LOG'; do sleep 0.05; done"

size_line="$(rg -m1 "Surface size: $TITLE " "$LOG" || true)"
if [[ "$size_line" =~ ([0-9]+)x([0-9]+)$ ]]; then
  W0="${BASH_REMATCH[1]}"
  H0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Surface size line: $size_line" >&2
  exit 1
fi

place_line="$(rg -m1 "Place: $TITLE " "$LOG" || true)"
if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+)\  ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $place_line" >&2
  exit 1
fi

TITLE_H=24
BTN_MARGIN=4
BTN_SPACING="$BTN_MARGIN"
BTN_SIZE=$((TITLE_H - 2 * BTN_MARGIN))

btn_cy_for_y() {
  local vy="$1"
  echo $((vy - TITLE_H + BTN_MARGIN + BTN_SIZE / 2))
}

right_x0_by_idx_from_right() {
  local w="$1"
  local idx="$2"
  local close_x0
  close_x0=$((w - BTN_MARGIN - BTN_SIZE))
  echo $((close_x0 - idx * (BTN_SIZE + BTN_SPACING)))
}

BTN_CY="$(btn_cy_for_y "$Y0")"

# Right buttons: Minimize Maximize Close (left-to-right).
# From right: 0=Close 1=Maximize 2=Minimize
IDX_CLOSE_FROM_RIGHT=0
IDX_MAX_FROM_RIGHT=1
IDX_MIN_FROM_RIGHT=2

click_expect_only() {
  local x="$1"
  local y="$2"
  local expect="$3"

  rm -f "$MARK_WINBUTTON" "$MARK_MIN" "$MARK_MAX"
  ./fbwl-input-injector --socket "$SOCKET" click "$x" "$y" >/dev/null 2>&1
  timeout 2 bash -c "until [[ -f '$expect' ]]; do sleep 0.05; done"

  local ok=0
  if [[ "$expect" == "$MARK_WINBUTTON" ]]; then
    ok=1
    [[ -f "$MARK_MIN" || -f "$MARK_MAX" ]] && ok=0
  elif [[ "$expect" == "$MARK_MIN" ]]; then
    ok=1
    [[ -f "$MARK_WINBUTTON" || -f "$MARK_MAX" ]] && ok=0
  elif [[ "$expect" == "$MARK_MAX" ]]; then
    ok=1
    [[ -f "$MARK_WINBUTTON" || -f "$MARK_MIN" ]] && ok=0
  fi
  if [[ "$ok" != 1 ]]; then
    echo "unexpected marker files after click: winbutton=$MARK_WINBUTTON min=$MARK_MIN max=$MARK_MAX" >&2
    ls -la "$MARK_WINBUTTON" "$MARK_MIN" "$MARK_MAX" 2>/dev/null || true
    exit 1
  fi
}

MIN_X0="$(right_x0_by_idx_from_right "$W0" "$IDX_MIN_FROM_RIGHT")"
MIN_CX=$((X0 + MIN_X0 + BTN_SIZE / 2))
click_expect_only "$MIN_CX" "$BTN_CY" "$MARK_MIN"

MAX_X0="$(right_x0_by_idx_from_right "$W0" "$IDX_MAX_FROM_RIGHT")"
MAX_CX=$((X0 + MAX_X0 + BTN_SIZE / 2))
click_expect_only "$MAX_CX" "$BTN_CY" "$MARK_MAX"

CLOSE_X0="$(right_x0_by_idx_from_right "$W0" "$IDX_CLOSE_FROM_RIGHT")"
CLOSE_CX=$((X0 + CLOSE_X0 + BTN_SIZE / 2))
click_expect_only "$CLOSE_CX" "$BTN_CY" "$MARK_WINBUTTON"

echo "ok: OnWinButton/OnMinButton/OnMaxButton mousebinding contexts smoke passed (socket=$SOCKET log=$LOG)"

