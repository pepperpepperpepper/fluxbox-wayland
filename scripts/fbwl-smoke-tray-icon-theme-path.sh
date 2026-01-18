#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd base64
need_cmd dbus-run-session
need_cmd rg
need_cmd timeout

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
if [[ ! -x ./fbwl-screencopy-client ]]; then
  echo "missing ./fbwl-screencopy-client (build first)" >&2
  exit 1
fi
if [[ ! -x ./fbwl-sni-item-client ]]; then
  echo "missing ./fbwl-sni-item-client (build first)" >&2
  exit 1
fi

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-tray-icon-theme-path-$UID-$$.log}"
ICON_NAME="${ICON_NAME:-fbwl-test-icon-theme-path-$UID-$$}"
ICON_RGB="${ICON_RGB:-#00ff00}"
: >"$LOG"

ROOT="$ROOT" XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" SOCKET="$SOCKET" LOG="$LOG" ICON_NAME="$ICON_NAME" ICON_RGB="$ICON_RGB" \
dbus-run-session -- bash -c '
  set -euo pipefail

  cd "$ROOT"

  THEME_ROOT="$(mktemp -d)"
  cleanup() {
    if [[ -n "${ITEM_PID:-}" ]]; then kill "$ITEM_PID" 2>/dev/null || true; fi
    if [[ -n "${FBW_PID:-}" ]]; then
      kill "$FBW_PID" 2>/dev/null || true
      wait "$FBW_PID" 2>/dev/null || true
    fi
    rm -rf "$THEME_ROOT" 2>/dev/null || true
    wait 2>/dev/null || true
  }
  trap cleanup EXIT

  ICONS_DIR="$THEME_ROOT/icons"
  mkdir -p "$ICONS_DIR/hicolor/16x16/status"
  base64 -d > "$ICONS_DIR/hicolor/16x16/status/$ICON_NAME.png" <<'"'"'EOF'"'"'
iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAFklEQVR4nGNg+E8hHDVg1IBRA4aLAQAxtf4QMNSIagAAAABJRU5ErkJggg==
EOF

  : >"$LOG"
  WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
    --no-xwayland \
    --socket "$SOCKET" \
    --workspaces 2 \
    >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q \"Running fluxbox-wayland\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"SNI: watcher enabled\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"Toolbar: position \" \"$LOG\"; do sleep 0.05; done"

  ./fbwl-sni-item-client \
    --item-path /fbwl/TestItem \
    --icon-name "$ICON_NAME" \
    --icon-theme-path "$ICONS_DIR" \
    --stay-ms 8000 \
    >/dev/null 2>&1 &
  ITEM_PID=$!

  timeout 5 bash -c "until rg -q \"Toolbar: tray item idx=0\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"SNI: icon theme path updated id=.*fbwl/TestItem\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"SNI: icon updated id=.*fbwl/TestItem\" \"$LOG\"; do sleep 0.05; done"

  pos_line="$(rg -m1 "Toolbar: position " "$LOG")"
  if [[ "$pos_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ h=([0-9]+)\ cell_w=([0-9]+)\ workspaces=([0-9]+) ]]; then
    X0="${BASH_REMATCH[1]}"
    Y0="${BASH_REMATCH[2]}"
    H="${BASH_REMATCH[3]}"
  else
    echo "failed to parse Toolbar: position line: $pos_line" >&2
    exit 1
  fi

  tray_line="$(rg -m1 "Toolbar: tray item idx=0" "$LOG")"
  if [[ "$tray_line" =~ lx=([-0-9]+)\ w=([0-9]+)\ id= ]]; then
    LX="${BASH_REMATCH[1]}"
    W="${BASH_REMATCH[2]}"
  else
    echo "failed to parse Toolbar: tray item line: $tray_line" >&2
    exit 1
  fi

  SAMPLE_X=$((X0 + LX + W / 2))
  SAMPLE_Y=$((Y0 + H / 2))

  ./fbwl-screencopy-client --socket "$SOCKET" --sample-x "$SAMPLE_X" --sample-y "$SAMPLE_Y" --expect-rgb "$ICON_RGB" >/dev/null

  ./fbwl-remote --socket "$SOCKET" quit | rg -q "^ok quitting$"
  timeout 5 bash -c "while kill -0 \"$FBW_PID\" 2>/dev/null; do sleep 0.05; done"
  wait "$FBW_PID"
  unset FBW_PID

  wait "$ITEM_PID" 2>/dev/null || true
  unset ITEM_PID

  echo "ok: tray icon-theme-path smoke passed (socket=$SOCKET log=$LOG)"
'

