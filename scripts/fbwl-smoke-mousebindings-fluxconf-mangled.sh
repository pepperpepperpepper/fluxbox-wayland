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

SOCKET="${SOCKET:-wayland-fbwl-mousebind-fluxconf-mangled-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-mousebind-fluxconf-mangled-$UID-$$.log}"
CFG_DIR="$(mktemp -d /tmp/fbwl-mousebind-fluxconf-mangled-XXXXXX)"

MARK_DESKTOP="${MARK_DESKTOP:-/tmp/fbwl-mousebind-fluxconf-mangled-desktop-$UID-$$}"
MARK_WINDOW="${MARK_WINDOW:-/tmp/fbwl-mousebind-fluxconf-mangled-window-$UID-$$}"
MARK_TITLEBAR="${MARK_TITLEBAR:-/tmp/fbwl-mousebind-fluxconf-mangled-titlebar-$UID-$$}"

cleanup() {
  rm -f "$MARK_DESKTOP" "$MARK_WINDOW" "$MARK_TITLEBAR" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFG_DIR" 2>/dev/null || true
}
trap cleanup EXIT

rm -f "$MARK_DESKTOP" "$MARK_WINDOW" "$MARK_TITLEBAR"
: >"$LOG"

cat >"$CFG_DIR/init" <<EOF
session.keyFile: keys
session.styleFile: style
session.screen0.defaultDeco: NORMAL
session.screen0.toolbar.visible: false
session.screen0.tabs.intitlebar: false
EOF

cat >"$CFG_DIR/style" <<'EOF'
window.borderWidth: 4
window.title.height: 24
EOF

cat >"$CFG_DIR/keys" <<EOF
Mouse1top :ExecCommand touch '$MARK_DESKTOP'
Mouse1ow :ExecCommand touch '$MARK_WINDOW'
Mouse1ebar :ExecCommand touch '$MARK_TITLEBAR'
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFG_DIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Style: loaded ' '$LOG'; do sleep 0.05; done"

parse_place_xy() {
  local title="$1"
  local line
  line="$(rg -m1 "Place: ${title} " "$LOG" || true)"
  if [[ -z "$line" ]]; then
    echo "missing Place line for title=$title" >&2
    return 1
  fi
  if [[ "$line" =~ x=([-0-9]+)\ y=([-0-9]+)\  ]]; then
    printf "%s %s\n" "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"
    return 0
  fi
  echo "failed to parse Place line for title=$title: $line" >&2
  return 1
}

TITLE="mangled-mouse-context"
./fbwl-smoke-client --socket "$SOCKET" --title "$TITLE" --stay-ms 15000 --width 320 --height 200 --xdg-decoration >/dev/null 2>&1 &
CLIENT_PID=$!
timeout 10 bash -c "until rg -q 'Place: $TITLE ' '$LOG'; do sleep 0.05; done"

read -r X Y < <(parse_place_xy "$TITLE")

OUT_LINE="$(rg -m1 'Output: ' "$LOG" || true)"
OUT_W=1280
OUT_H=720
if [[ "$OUT_LINE" =~ ([0-9]+)x([0-9]+) ]]; then
  OUT_W="${BASH_REMATCH[1]}"
  OUT_H="${BASH_REMATCH[2]}"
fi
DESKTOP_X=$((OUT_W - 10))
DESKTOP_Y=$((OUT_H - 10))

rm -f "$MARK_DESKTOP" "$MARK_WINDOW" "$MARK_TITLEBAR"
./fbwl-input-injector --socket "$SOCKET" click "$((X + 20))" "$((Y + 20))" >/dev/null 2>&1
timeout 2 bash -c "until [[ -f '$MARK_WINDOW' ]]; do sleep 0.05; done"
if [[ -f "$MARK_DESKTOP" || -f "$MARK_TITLEBAR" ]]; then
  echo "unexpected non-window mangled binding fired on window click" >&2
  exit 1
fi

rm -f "$MARK_DESKTOP" "$MARK_WINDOW" "$MARK_TITLEBAR"
./fbwl-input-injector --socket "$SOCKET" click "$DESKTOP_X" "$DESKTOP_Y" >/dev/null 2>&1
timeout 2 bash -c "until [[ -f '$MARK_DESKTOP' ]]; do sleep 0.05; done"
if [[ -f "$MARK_WINDOW" || -f "$MARK_TITLEBAR" ]]; then
  echo "unexpected non-desktop mangled binding fired on desktop click" >&2
  exit 1
fi

rm -f "$MARK_DESKTOP" "$MARK_WINDOW" "$MARK_TITLEBAR"
./fbwl-input-injector --socket "$SOCKET" click "$((X + 20))" "$((Y - 12))" >/dev/null 2>&1
timeout 2 bash -c "until [[ -f '$MARK_TITLEBAR' ]]; do sleep 0.05; done"
if [[ -f "$MARK_DESKTOP" || -f "$MARK_WINDOW" ]]; then
  echo "unexpected non-titlebar mangled binding fired on titlebar click" >&2
  exit 1
fi

echo "ok: fluxconf-mangled Mouse#(ow|top|ebar) context parse smoke passed (socket=$SOCKET log=$LOG)"
