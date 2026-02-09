#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1 (build first)" >&2; exit 1; }
}

need_cmd cut
need_cmd mktemp
need_cmd rg
need_cmd sed
need_cmd timeout
need_cmd wc

need_exe ./fluxbox-wayland
need_exe ./fbwl-input-injector
need_exe ./fbx11-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

export XWAYLAND_NO_GLAMOR=1
export __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json"
export __GLX_VENDOR_LIBRARY_NAME=mesa
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=swrast

SOCKET="wayland-fbwl-layer-ui-toggle-$UID-$$"
LOG="/tmp/fluxbox-wayland-layer-ui-toggle-$UID-$$.log"
CFG_DIR="$(mktemp -d "/tmp/fbwl-layer-ui-toggle-$UID-XXXXXX")"
KEYS_FILE="$CFG_DIR/keys"

cleanup() {
  if [[ -n "${DOCK_PID:-}" ]]; then kill "$DOCK_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFG_DIR" 2>/dev/null || true
}
trap cleanup EXIT

cat >"$CFG_DIR/init" <<EOF
session.screen0.defaultDeco: NONE
session.screen0.toolbar.visible: true
session.screen0.toolbar.autoHide: false
session.screen0.toolbar.layer: Dock

session.screen0.slit.placement: RightBottom
session.screen0.slit.onhead: 1
session.screen0.slit.layer: Dock
session.screen0.slit.autoHide: false
session.screen0.slit.autoRaise: false
session.screen0.slit.maxOver: false
EOF

cat >"$KEYS_FILE" <<'EOF'
Mod1 1 :ToggleToolbarHidden
Mod1 2 :ToggleToolbarAbove
Mod1 3 :ToggleSlitHidden
Mod1 4 :ToggleSlitAbove
EOF

: >"$LOG"
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
  ./fluxbox-wayland --socket "$SOCKET" --workspaces 1 --config-dir "$CFG_DIR" --keys "$KEYS_FILE" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'XWayland: ready DISPLAY=' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'ScreenMap: screen0 ' '$LOG'; do sleep 0.05; done"

DISPLAY_NAME="$(rg -m 1 'XWayland: ready DISPLAY=' "$LOG" | sed -E 's/.*DISPLAY=//')"
if [[ -z "$DISPLAY_NAME" ]]; then
  echo "failed to parse XWayland DISPLAY from log: $LOG" >&2
  exit 1
fi

SCREEN0="$(rg 'ScreenMap: screen0 ' "$LOG" | tail -n 1)"
S0_X="$(echo "$SCREEN0" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)"
S0_Y="$(echo "$SCREEN0" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)"
S0_W="$(echo "$SCREEN0" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)"
S0_H="$(echo "$SCREEN0" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)"

if [[ "$S0_W" -lt 1 || "$S0_H" -lt 1 ]]; then
  echo "invalid screen layout box: '$SCREEN0'" >&2
  exit 1
fi

CX=$((S0_X + S0_W / 2))
CY=$((S0_Y + S0_H / 2))
./fbwl-input-injector --socket "$SOCKET" motion "$CX" "$CY" >/dev/null 2>&1

timeout 10 bash -c "until rg -q 'Toolbar: built ' '$LOG'; do sleep 0.05; done"

DOCK_TITLE="dock-toggle-$UID-$$"
DISPLAY="$DISPLAY_NAME" ./fbx11-smoke-client --dock --title "$DOCK_TITLE" --class "$DOCK_TITLE" --instance "$DOCK_TITLE" --stay-ms 20000 --w 48 --h 64 >/dev/null 2>&1 &
DOCK_PID=$!
timeout 10 bash -c "until rg -q 'Slit: manage dock view title=$DOCK_TITLE' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Slit: layout ' '$LOG'; do sleep 0.05; done"

# ToggleToolbarHidden should work even when autoHide=false.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1 >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: toggleHidden hidden=1'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1 >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: toggleHidden hidden=0'; do sleep 0.05; done"

# ToggleToolbarAbove toggles between rc layer and AboveDock.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2 >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: toggleAboveDock layer=2 rc_layer=4'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2 >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Toolbar: toggleAboveDock layer=4 rc_layer=4'; do sleep 0.05; done"

# ToggleSlitHidden + ToggleSlitAbove
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-3 >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Slit: toggleHidden hidden=1'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Slit: layout .* hidden=1 .* autoHide=0'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-3 >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Slit: toggleHidden hidden=0'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Slit: layout .* hidden=0 .* autoHide=0'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-4 >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Slit: toggleAboveDock layer=2 rc_layer=4'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-4 >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Slit: toggleAboveDock layer=4 rc_layer=4'; do sleep 0.05; done"

echo "ok: layer/ui toggle commands smoke passed"
