#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1 (build first)" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd sed
need_cmd timeout
need_cmd wc

need_exe ./fbwl-input-injector
need_exe ./fbx11-smoke-client
need_exe ./fluxbox-wayland

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

export XWAYLAND_NO_GLAMOR=1
export __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=swrast

SOCKET="${SOCKET:-wayland-fbwl-xwayland-xprop-clientpattern-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-xwayland-xprop-clientpattern-$UID-$$.log}"
KEYS_FILE="$(mktemp /tmp/fbwl-keys-xwayland-xprop-clientpattern-XXXXXX)"

cleanup() {
  rm -f "$KEYS_FILE" 2>/dev/null || true
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$KEYS_FILE" <<'EOF'
Mod1 1 :NextWindow {groups} (title=xprop-a)
Mod1 2 :NextWindow {groups} (title=xprop-b)
Mod1 3 :SetXProp FOO=bar
Mod1 4 :NextWindow {groups} (@FOO=.*bar.*)
Mod1 5 :NextWindow {groups} (@FOO=42)
EOF

: >"$LOG"

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
  --title xprop-a \
  --class xprop-a \
  --instance xprop-a \
  --stay-ms 20000 \
  >/dev/null 2>&1 &
A_PID=$!

DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client \
  --title xprop-b \
  --class xprop-b \
  --instance xprop-b \
  --xprop-cardinal FOO=42 \
  --stay-ms 20000 \
  >/dev/null 2>&1 &
B_PID=$!

timeout 10 bash -c "until rg -q 'Place: xprop-a ' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Place: xprop-b ' '$LOG'; do sleep 0.05; done"

focus_by_key() {
  local key="$1"
  local title="$2"
  ./fbwl-input-injector --socket "$SOCKET" key "$key"
  timeout 5 bash -c "until rg 'Focus:' '$LOG' | tail -n 1 | rg -q 'Focus: ${title}'; do sleep 0.05; done"
}

# SetXProp should update the focused XWayland window's property and allow ClientPattern (@FOO=...) matching.
focus_by_key alt-1 xprop-a
./fbwl-input-injector --socket "$SOCKET" key alt-3

focus_by_key alt-2 xprop-b
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-4
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: xprop-a'; do sleep 0.05; done"

# CARDINAL properties should match too.
focus_by_key alt-1 xprop-a
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-5
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: xprop-b'; do sleep 0.05; done"

echo "ok: XWayland ClientPattern @XPROP smoke passed (socket=$SOCKET display=$DISPLAY_NAME log=$LOG keys=$KEYS_FILE)"

