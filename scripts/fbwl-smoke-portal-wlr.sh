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

ROOT="$ROOT" XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WAYLAND_DISPLAY="$SOCKET" \
  XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=wlroots \
  LOG="$LOG" XDPW_LOG="$XDPW_LOG" PW_LOG="$PW_LOG" WP_LOG="$WP_LOG" \
dbus-run-session -- bash -c '
  set -euo pipefail

  cd "$ROOT"

  : >"$LOG"
  : >"$XDPW_LOG"
  : >"$PW_LOG"
  : >"$WP_LOG"

  rm -f /tmp/out.png

  pipewire >"$PW_LOG" 2>&1 &
  PW_PID=$!

  wireplumber >"$WP_LOG" 2>&1 &
  WP_PID=$!

  WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
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

  [[ -s /tmp/out.png ]] || { echo "missing screenshot output: /tmp/out.png" >&2; exit 1; }
  file -b /tmp/out.png | rg -q "^PNG image data" || { echo "unexpected screenshot format: $(file -b /tmp/out.png)" >&2; exit 1; }

  echo "ok: xdg-desktop-portal-wlr smoke passed (socket=$WAYLAND_DISPLAY log=$LOG xdpw_log=$XDPW_LOG screenshot=/tmp/out.png)"
'
