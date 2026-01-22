#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd dbus-run-session
need_cmd pw-cli
need_cmd rg
need_cmd timeout
need_cmd pipewire
need_cmd wireplumber

if [[ ! -x /usr/lib/xdg-desktop-portal ]]; then
  echo "missing required executable: /usr/lib/xdg-desktop-portal" >&2
  exit 1
fi

if [[ ! -x /usr/lib/xdg-desktop-portal-wlr ]]; then
  echo "missing required executable: /usr/lib/xdg-desktop-portal-wlr" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ ! -x ./fluxbox-wayland ]]; then
  echo "missing ./fluxbox-wayland (build first)" >&2
  exit 1
fi

if [[ ! -x ./fbwl-xdp-portal-client ]]; then
  echo "missing ./fbwl-xdp-portal-client (build first)" >&2
  exit 1
fi

# Force a writable runtime dir so xdg-document-portal can mount its FUSE path.
export XDG_RUNTIME_DIR="/tmp/xdg-runtime-$UID-xdp-$$"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-xdg-desktop-portal-$UID-$$.log}"
XDPW_LOG="${XDPW_LOG:-/tmp/xdg-desktop-portal-wlr-frontend-$UID-$$.log}"
XDP_LOG="${XDP_LOG:-/tmp/xdg-desktop-portal-frontend-$UID-$$.log}"
PW_LOG="${PW_LOG:-/tmp/pipewire-xdg-desktop-portal-$UID-$$.log}"
WP_LOG="${WP_LOG:-/tmp/wireplumber-xdg-desktop-portal-$UID-$$.log}"
PW_NODE_LOG="${PW_NODE_LOG:-/tmp/pw-node-info-xdg-desktop-portal-$UID-$$.log}"

: >"$LOG"
: >"$XDPW_LOG"
: >"$XDP_LOG"
: >"$PW_LOG"
: >"$WP_LOG"
: >"$PW_NODE_LOG"

OUTPUT_NAME="${OUTPUT_NAME:-}"
CONF_DIR="$(mktemp -d "/tmp/fbwl-xdp-conf-$UID-$$-XXXXXX")"

cleanup_outer() {
  rm -rf "$CONF_DIR" 2>/dev/null || true
}
trap cleanup_outer EXIT

mkdir -p "$CONF_DIR/xdg-desktop-portal-wlr"

export ROOT
export WAYLAND_DISPLAY="$SOCKET"
export XDG_CONFIG_HOME="$CONF_DIR"
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=wlroots
export OUTPUT_NAME
export LOG XDPW_LOG XDP_LOG PW_LOG WP_LOG PW_NODE_LOG

# shellcheck disable=SC2016
dbus-run-session -- bash -c '
  set -euo pipefail

  cd "$ROOT"

  : >"$LOG"
  : >"$XDPW_LOG"
  : >"$XDP_LOG"
  : >"$PW_LOG"
  : >"$WP_LOG"
  : >"$PW_NODE_LOG"

  pipewire >"$PW_LOG" 2>&1 &
  PW_PID=$!

  wireplumber >"$WP_LOG" 2>&1 &
  WP_PID=$!

  WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
    --no-xwayland \
    --socket "$WAYLAND_DISPLAY" \
    >"$LOG" 2>&1 &
  FBW_PID=$!

  cleanup() {
    set +e
    if [[ -n "${XDP_PID:-}" ]]; then
      kill "$XDP_PID" 2>/dev/null || true
      wait "$XDP_PID" 2>/dev/null || true
    fi
    if [[ -n "${XDPW_PID:-}" ]]; then
      kill "$XDPW_PID" 2>/dev/null || true
      wait "$XDPW_PID" 2>/dev/null || true
    fi
    if [[ -n "${FBW_PID:-}" ]]; then
      kill "$FBW_PID" 2>/dev/null || true
      wait "$FBW_PID" 2>/dev/null || true
    fi
    if [[ -n "${WP_PID:-}" ]]; then
      kill "$WP_PID" 2>/dev/null || true
      wait "$WP_PID" 2>/dev/null || true
    fi
    if [[ -n "${PW_PID:-}" ]]; then
      kill "$PW_PID" 2>/dev/null || true
      wait "$PW_PID" 2>/dev/null || true
    fi
  }
  trap cleanup EXIT

  timeout 10 bash -c "until rg -q \"Running fluxbox-wayland\" \"$LOG\"; do sleep 0.05; done"

  # Ensure PipeWire is up enough to accept connections.
  timeout 10 bash -c "until pw-cli info 0 >/dev/null 2>&1; do sleep 0.05; done"

  timeout 10 bash -c "until rg -q \"Output: \" \"$LOG\"; do sleep 0.05; done"
  if [[ -z "${OUTPUT_NAME:-}" ]]; then
    OUT_LINE="$(rg -m1 "Output: " "$LOG" || true)"
    OUTPUT_NAME="${OUT_LINE#*Output: }"
    OUTPUT_NAME="${OUTPUT_NAME%% *}"
    if [[ -z "$OUTPUT_NAME" ]]; then
      echo "failed to detect output name from log line: $OUT_LINE" >&2
      exit 1
    fi
  fi

  mkdir -p "$XDG_CONFIG_HOME/xdg-desktop-portal-wlr"
  cat >"$XDG_CONFIG_HOME/xdg-desktop-portal-wlr/config" <<EOF
[screencast]
chooser_type=none
output_name=$OUTPUT_NAME
max_fps=5
EOF

  /usr/lib/xdg-desktop-portal-wlr -r -l DEBUG >"$XDPW_LOG" 2>&1 &
  XDPW_PID=$!

  timeout 10 bash -c "until rg -q \"wayland: using ext_image_copy_capture\" \"$XDPW_LOG\"; do sleep 0.05; done"

  /usr/lib/xdg-desktop-portal -r -v >"$XDP_LOG" 2>&1 &
  XDP_PID=$!

  timeout 10 bash -c "until rg -q \"org.freedesktop.portal.Desktop acquired\" \"$XDP_LOG\"; do sleep 0.05; done"
  timeout 10 bash -c "until rg -q \"Using wlr\\.portal for org\\.freedesktop\\.impl\\.portal\\.ScreenCast\" \"$XDP_LOG\"; do sleep 0.05; done"

  NODE_ID="$(./fbwl-xdp-portal-client --timeout-ms 15000)"
  [[ "$NODE_ID" =~ ^[0-9]+$ ]] && [[ "$NODE_ID" -gt 0 ]]

  timeout 10 pw-cli info "$NODE_ID" >"$PW_NODE_LOG" 2>&1
  rg -q "^[[:space:]]*id:[[:space:]]*$NODE_ID\\b" "$PW_NODE_LOG"

  echo "ok: xdg-desktop-portal (frontend) screencast smoke passed (socket=$WAYLAND_DISPLAY output=$OUTPUT_NAME node_id=$NODE_ID log=$LOG xdp_log=$XDP_LOG xdpw_log=$XDPW_LOG)"
'
