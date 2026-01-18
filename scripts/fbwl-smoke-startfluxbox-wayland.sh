#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd dbus-run-session

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ ! -x ./fluxbox-wayland ]]; then
  echo "missing ./fluxbox-wayland (build first)" >&2
  exit 1
fi
if [[ ! -x ./fbwl-remote ]]; then
  echo "missing ./fbwl-remote (build first)" >&2
  exit 1
fi
if [[ ! -f util/startfluxbox-wayland ]]; then
  echo "missing util/startfluxbox-wayland (build target: make util/startfluxbox-wayland)" >&2
  exit 1
fi

TMPHOME="$(mktemp -d)"
RUNTIME_DIR="$TMPHOME/xdg-runtime"
mkdir -p "$RUNTIME_DIR"
chmod 0700 "$RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-startfluxbox-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-startfluxbox-$UID-$$.log}"
MARKER="$TMPHOME/startfluxbox-wayland.marker"
STARTUP="$TMPHOME/startup"

cleanup() {
  if [[ -n "${FBW_PID:-}" ]]; then
    kill "$FBW_PID" 2>/dev/null || true
    wait "$FBW_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

cat >"$STARTUP" <<EOF
#!/bin/sh
set -eu

if [ "\${XDG_SESSION_TYPE:-}" != "wayland" ]; then
  echo "startfluxbox-wayland did not set XDG_SESSION_TYPE=wayland (got '\${XDG_SESSION_TYPE:-}')" >&2
  exit 1
fi

if [ -z "\${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
  echo "startfluxbox-wayland did not provide a DBus session (DBUS_SESSION_BUS_ADDRESS is empty)" >&2
  exit 1
fi

exec fluxbox-wayland --no-xwayland --socket "$SOCKET" --ipc-socket "$RUNTIME_DIR/fbwl-ipc.sock" \\
  -s "touch '$MARKER'; fbwl-remote quit"
EOF
chmod +x "$STARTUP"

: >"$LOG"

HOME="$TMPHOME" \
XDG_RUNTIME_DIR="$RUNTIME_DIR" \
XDG_SESSION_TYPE="tty" \
DBUS_SESSION_BUS_ADDRESS="" \
PATH="$ROOT:$PATH" \
WLR_BACKENDS=headless WLR_RENDERER=pixman \
sh util/startfluxbox-wayland --config "$STARTUP" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until test -f '$MARKER'; do sleep 0.05; done"
timeout 5 bash -c "while kill -0 '$FBW_PID' 2>/dev/null; do sleep 0.05; done"
wait "$FBW_PID"
unset FBW_PID

rg -q 'Running fluxbox-wayland' "$LOG"

echo "ok: startfluxbox-wayland smoke passed (socket=$SOCKET log=$LOG)"
