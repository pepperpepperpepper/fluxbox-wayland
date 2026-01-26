#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd busctl
need_cmd dbus-run-session
need_cmd file
need_cmd grim
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

OUT_FILE="/tmp/out.png"

# xdg-desktop-portal-wlr currently writes screenshot output to a fixed path
# (/tmp/out.png). On systems with fs.protected_regular=1, an existing /tmp/out.png
# owned by a different user cannot be overwritten even if it is 0666.
# To keep this smoke test robust in shared/persistent /tmp environments, fall
# back to running inside a private /tmp (tmpfs) via an unprivileged user+mount
# namespace when needed.
if [[ -e "$OUT_FILE" ]] && [[ ! -O "$OUT_FILE" ]] && [[ -z "${FBWL_PRIVATE_TMP:-}" ]]; then
  if command -v unshare >/dev/null 2>&1; then
    echo "note: $OUT_FILE exists and is not owned by uid=$UID ($(id -un)); retrying with a private /tmp via unshare" >&2
    export FBWL_PRIVATE_TMP=1
    exec unshare -Ur -m bash -c 'mount -t tmpfs tmpfs /tmp && exec "$0" "$@"' "$0" "$@"
  fi
  echo "skip: $OUT_FILE exists and is not owned by uid=$UID ($(id -un)); cannot isolate /tmp (missing unshare)" >&2
  exit 0
fi

cleanup_out_file() {
  if [[ -e "$OUT_FILE" ]] && [[ -O "$OUT_FILE" ]]; then
    rm -f "$OUT_FILE" 2>/dev/null || true
  fi
}
trap cleanup_out_file EXIT

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-portal-wlr-$UID-$$.log}"
XDPW_LOG="${XDPW_LOG:-/tmp/xdg-desktop-portal-wlr-$UID-$$.log}"
PW_LOG="${PW_LOG:-/tmp/pipewire-$UID-$$.log}"
WP_LOG="${WP_LOG:-/tmp/wireplumber-$UID-$$.log}"

: >"$LOG"
: >"$XDPW_LOG"
: >"$PW_LOG"
: >"$WP_LOG"

export ROOT
export WAYLAND_DISPLAY="$SOCKET"
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=wlroots
export LOG XDPW_LOG PW_LOG WP_LOG

# shellcheck disable=SC2016
dbus-run-session -- bash -c '
  set -euo pipefail

  cd "$ROOT"

  : >"$LOG"
  : >"$XDPW_LOG"
  : >"$PW_LOG"
  : >"$WP_LOG"

  OUT_FILE="/tmp/out.png"
  rm -f "$OUT_FILE" 2>/dev/null || true
  if ! : >"$OUT_FILE" 2>/dev/null; then
    echo "precondition failed: cannot create/truncate $OUT_FILE as uid=$UID ($(id -un))" >&2
    ls -la "$OUT_FILE" >&2 || true
    if command -v sysctl >/dev/null 2>&1; then
      sysctl fs.protected_regular fs.protected_fifos 2>/dev/null >&2 || true
    fi
    echo "note: xdg-desktop-portal-wlr writes screenshots to $OUT_FILE; in shared /tmp environments this may require an isolated /tmp" >&2
    exit 1
  fi

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

  /usr/lib/xdg-desktop-portal-wlr -r -l DEBUG >"$XDPW_LOG" 2>&1 &
  XDPW_PID=$!

  timeout 10 bash -c "until rg -q \"wayland: using ext_image_copy_capture\" \"$XDPW_LOG\"; do sleep 0.05; done"

  HANDLE="/org/freedesktop/portal/desktop/request/fbwl_smoke/u${UID}_p${$}"
  OUT="$(busctl --user call org.freedesktop.impl.portal.desktop.wlr \
    /org/freedesktop/portal/desktop \
    org.freedesktop.impl.portal.Screenshot \
    Screenshot "ossa{sv}" \
    "$HANDLE" "fbwl.portal.smoke" "" 0)"
  echo "$OUT" | rg -q "ua\\{sv\\} 0"
  echo "$OUT" | rg -q "file:///tmp/out.png"

  [[ -s "$OUT_FILE" ]] || { echo "missing screenshot output: $OUT_FILE" >&2; exit 1; }
  file -b "$OUT_FILE" | rg -q "^PNG image data" || { echo "unexpected screenshot format: $(file -b "$OUT_FILE")" >&2; exit 1; }

  echo "ok: xdg-desktop-portal-wlr smoke passed (socket=$WAYLAND_DISPLAY log=$LOG xdpw_log=$XDPW_LOG screenshot=$OUT_FILE)"
'
