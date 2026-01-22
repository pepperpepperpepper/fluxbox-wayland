#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd busctl
need_cmd dbus-run-session
need_cmd pw-cli
need_cmd python3
need_cmd rg
need_cmd timeout
need_cmd pipewire
need_cmd wireplumber

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

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-portal-wlr-screencast-$UID-$$.log}"
XDPW_LOG="${XDPW_LOG:-/tmp/xdg-desktop-portal-wlr-screencast-$UID-$$.log}"
PW_LOG="${PW_LOG:-/tmp/pipewire-portal-wlr-screencast-$UID-$$.log}"
WP_LOG="${WP_LOG:-/tmp/wireplumber-portal-wlr-screencast-$UID-$$.log}"
PW_NODE_LOG="${PW_NODE_LOG:-/tmp/pw-node-info-$UID-$$.log}"

: >"$LOG"
: >"$XDPW_LOG"
: >"$PW_LOG"
: >"$WP_LOG"
: >"$PW_NODE_LOG"

OUTPUT_NAME="${OUTPUT_NAME:-}"
CONF_DIR="$(mktemp -d "/tmp/fbwl-xdpw-conf-$UID-$$-XXXXXX")"

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
export LOG XDPW_LOG PW_LOG WP_LOG PW_NODE_LOG

# shellcheck disable=SC2016
dbus-run-session -- bash -c '
  set -euo pipefail

  cd "$ROOT"

  : >"$LOG"
  : >"$XDPW_LOG"
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

  APP_ID="fbwl.portal.smoke"
  REQ_BASE="/org/freedesktop/portal/desktop/request/fbwl_smoke/u${UID}_p${$}"
  SES="/org/freedesktop/portal/desktop/session/fbwl_smoke/u${UID}_p${$}"

  OUT1="$(busctl --user -j call org.freedesktop.impl.portal.desktop.wlr \
    /org/freedesktop/portal/desktop \
    org.freedesktop.impl.portal.ScreenCast \
    CreateSession "oosa{sv}" \
    "$REQ_BASE/create" "$SES" "$APP_ID" 0)"
  OUT_JSON="$OUT1" python3 - <<PY
import json, os
m = json.loads(os.environ["OUT_JSON"])
assert m.get("type") == "ua{sv}", m
assert m.get("data", [1])[0] == 0, m
PY

  OUT2="$(busctl --user -j call org.freedesktop.impl.portal.desktop.wlr \
    /org/freedesktop/portal/desktop \
    org.freedesktop.impl.portal.ScreenCast \
    SelectSources "oosa{sv}" \
    "$REQ_BASE/select" "$SES" "$APP_ID" \
    2 types u 1 multiple b false)"
  OUT_JSON="$OUT2" python3 - <<PY
import json, os
m = json.loads(os.environ["OUT_JSON"])
assert m.get("type") == "ua{sv}", m
assert m.get("data", [1])[0] == 0, m
PY

  OUT3="$(busctl --user -j call org.freedesktop.impl.portal.desktop.wlr \
    /org/freedesktop/portal/desktop \
    org.freedesktop.impl.portal.ScreenCast \
    Start "oossa{sv}" \
    "$REQ_BASE/start" "$SES" "$APP_ID" "" 0)"
  NODE_ID="$(OUT_JSON="$OUT3" python3 - <<PY
import json, os
m = json.loads(os.environ["OUT_JSON"])
assert m.get("type") == "ua{sv}", m
data = m.get("data", [])
assert len(data) >= 2 and data[0] == 0, m
results = data[1]
streams = results.get("streams")
assert isinstance(streams, dict) and streams.get("type") == "a(ua{sv})", streams
stream_list = streams.get("data", [])
assert isinstance(stream_list, list) and len(stream_list) >= 1, stream_list
node_id = stream_list[0][0]
assert isinstance(node_id, int) and node_id > 0, node_id
print(node_id)
PY
  )"
  [[ "$NODE_ID" =~ ^[0-9]+$ ]]

  timeout 10 pw-cli info "$NODE_ID" >"$PW_NODE_LOG" 2>&1
  rg -q "^[[:space:]]*id:[[:space:]]*$NODE_ID\\b" "$PW_NODE_LOG"

  echo "ok: xdg-desktop-portal-wlr screencast smoke passed (socket=$WAYLAND_DISPLAY output=$OUTPUT_NAME node_id=$NODE_ID log=$LOG xdpw_log=$XDPW_LOG)"
'
