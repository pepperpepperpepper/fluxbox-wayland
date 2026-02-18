#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout

need_exe ./fluxbox-remote
need_exe ./fbwl-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

fbr() {
  DISPLAY='' ./fluxbox-remote --wayland --socket "$SOCKET" "$@"
}

SOCKET="${SOCKET:-wayland-fbwl-cmdlang-if-foreach-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-cmdlang-if-foreach-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-cmdlang-if-foreach-$UID-XXXXXX")"
RCFILE="$CFGDIR/rcfile.custom"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$RCFILE" <<EOF
session.screen0.allowRemoteActions: true
session.screen0.workspaces: 1
session.keyFile: keys
session.appsFile: apps
session.styleFile: style
session.menuFile: menu
EOF

cat >"$CFGDIR/keys" <<EOF
# empty
EOF

cat >"$CFGDIR/apps" <<EOF
# empty
EOF

cat >"$CFGDIR/style" <<EOF
window.title.height: 18
EOF

cat >"$CFGDIR/menu" <<EOF
[begin] (Fluxbox)
[end]
EOF

: >"$LOG"

env WLR_BACKENDS=headless WLR_RENDERER=pixman \
  ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --workspaces 1 -rc "$RCFILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title if-a --stay-ms 20000 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title if-b --stay-ms 20000 >/dev/null 2>&1 &
B_PID=$!

timeout 10 bash -c "until rg -q 'Place: if-a ' '$LOG' && rg -q 'Place: if-b ' '$LOG'; do sleep 0.05; done"

fbr "focus (title=if-a)" | rg -q '^ok$'

# If: accept extra {tokens} and trailing garbage, and don't treat a parse failure as else.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
fbr "if {matches (title=if-a)} {setalpha 200 150} {setalpha 10 10} {setalpha 1 1} # trailing" | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Alpha: if-a focused=200 unfocused=150 reason=setalpha'; do sleep 0.05; done"
if tail -c +$START "$LOG" | rg -q 'Alpha: if-a focused=1 unfocused=1 reason=setalpha'; then
  echo "expected 4th {token} in If to be ignored (log=$LOG)" >&2
  exit 1
fi

# Foreach: invalid filter expression should act like no filter (apply to all), matching X11 behavior.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
fbr "foreach {setalpha 66 66} {badfilter}" | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Alpha: if-a focused=66 unfocused=66 reason=setalpha' && tail -c +$START '$LOG' | rg -q 'Alpha: if-b focused=66 unfocused=66 reason=setalpha'; do sleep 0.05; done"

# Foreach: accept extra {tokens} and trailing garbage.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
fbr "foreach {setalpha 77 77} {matches (title=if-a)} {ignored} trailing" | rg -q '^ok$'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Alpha: if-a focused=77 unfocused=77 reason=setalpha'; do sleep 0.05; done"
if tail -c +$START "$LOG" | rg -q 'Alpha: if-b focused=77 unfocused=77 reason=setalpha'; then
  echo "expected foreach filter to match only if-a (log=$LOG)" >&2
  exit 1
fi

fbr exit | rg -q '^ok quitting$'
timeout 5 bash -c "while kill -0 '$FBW_PID' 2>/dev/null; do sleep 0.05; done"
wait "$FBW_PID"
unset FBW_PID

echo "ok: cmdlang If/Foreach parse parity smoke passed (socket=$SOCKET log=$LOG rcfile=$RCFILE)"

