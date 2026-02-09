#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd sed
need_cmd timeout
need_cmd wc

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

export XWAYLAND_NO_GLAMOR=1
export __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=swrast

SOCKET="${SOCKET:-wayland-fbwl-focushidden-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-focushidden-$UID-$$.log}"
KEYS_FILE="$(mktemp /tmp/fbwl-focushidden-keys-XXXXXX)"

cleanup() {
  rm -f "$KEYS_FILE" 2>/dev/null || true
  if [[ -n "${X11_A_PID:-}" ]]; then kill "$X11_A_PID" 2>/dev/null || true; fi
  if [[ -n "${X11_H_PID:-}" ]]; then kill "$X11_H_PID" 2>/dev/null || true; fi
  if [[ -n "${X11_B_PID:-}" ]]; then kill "$X11_B_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$KEYS_FILE" <<'EOF'
Mod1 Control 9 :NextWindow
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --socket "$SOCKET" \
  --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'XWayland: ready DISPLAY=' '$LOG'; do sleep 0.05; done"

DISPLAY_NAME="$(rg -m 1 'XWayland: ready DISPLAY=' "$LOG" | sed -E 's/.*DISPLAY=//')"
if [[ -z "$DISPLAY_NAME" ]]; then
  echo "failed to parse XWayland DISPLAY from log: $LOG" >&2
  exit 1
fi

DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client \
  --title fh-a \
  --class fh-a \
  --instance fh-a \
  --stay-ms 10000 \
  --w 120 \
  --h 80 \
  >/dev/null 2>&1 &
X11_A_PID=$!
timeout 10 bash -c "until rg -q 'XWayland: map title=fh-a' '$LOG'; do sleep 0.05; done"

DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client \
  --title fh-hidden \
  --class fh-hidden \
  --instance fh-hidden \
  --desktop \
  --stay-ms 10000 \
  --w 120 \
  --h 80 \
  >/dev/null 2>&1 &
X11_H_PID=$!
timeout 10 bash -c "until rg -q 'XWayland: map title=fh-hidden' '$LOG'; do sleep 0.05; done"

DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client \
  --title fh-b \
  --class fh-b \
  --instance fh-b \
  --stay-ms 10000 \
  --w 120 \
  --h 80 \
  >/dev/null 2>&1 &
X11_B_PID=$!
timeout 10 bash -c "until rg -q 'XWayland: map title=fh-b' '$LOG'; do sleep 0.05; done"

timeout 10 bash -c "until rg -q 'Focus: fh-b' '$LOG'; do sleep 0.05; done"

# NextWindow should skip the focushidden desktop-type view.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-9
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: fh-a'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-9
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: fh-b'; do sleep 0.05; done"
if tail -c +$((OFFSET + 1)) "$LOG" | rg -q 'Focus: fh-hidden'; then
  echo "expected NextWindow to skip focushidden view (got focus on fh-hidden)" >&2
  exit 1
fi

echo "ok: focushidden parity smoke passed (socket=$SOCKET display=$DISPLAY_NAME log=$LOG)"
