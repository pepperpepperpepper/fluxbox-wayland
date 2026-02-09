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
need_cmd wc

need_exe ./fbwl-input-injector
need_exe ./fbwl-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-nextwindow-clientpattern-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-nextwindow-clientpattern-$UID-$$.log}"
KEYS_FILE="$(mktemp /tmp/fbwl-keys-nextwindow-clientpattern-XXXXXX)"
APPS_FILE="$(mktemp /tmp/fbwl-apps-nextwindow-clientpattern-XXXXXX)"

cleanup() {
  rm -f "$KEYS_FILE" "$APPS_FILE" 2>/dev/null || true
  if [[ -n "${DUMMY_PID:-}" ]]; then kill "$DUMMY_PID" 2>/dev/null || true; fi
  if [[ -n "${CUR1_PID:-}" ]]; then kill "$CUR1_PID" 2>/dev/null || true; fi
  if [[ -n "${CUR2_PID:-}" ]]; then kill "$CUR2_PID" 2>/dev/null || true; fi
  if [[ -n "${HEAD0_PID:-}" ]]; then kill "$HEAD0_PID" 2>/dev/null || true; fi
  if [[ -n "${HEAD1_PID:-}" ]]; then kill "$HEAD1_PID" 2>/dev/null || true; fi
  if [[ -n "${TOP_PID:-}" ]]; then kill "$TOP_PID" 2>/dev/null || true; fi
  if [[ -n "${MAXH_PID:-}" ]]; then kill "$MAXH_PID" 2>/dev/null || true; fi
  if [[ -n "${MAXV_PID:-}" ]]; then kill "$MAXV_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$KEYS_FILE" <<'EOF'
Mod1 1 :NextWindow {groups} (layer=Top) (class=fbwl-cp-layer-top)
Mod1 2 :NextWindow {groups} (maximizedhorizontal=yes) (class=fbwl-cp-max-h)
Mod1 3 :NextWindow {groups} (maximizedvertical=yes) (class=fbwl-cp-max-v)
Mod1 4 :NextWindow {groups} (head=[mouse]) (class=fbwl-cp-head[01])
Mod1 5 :NextWindow {groups} (class=[current])
Mod1 6 :NextWindow {groups} (layer!=[current])
Mod1 7 :NextWindow {groups} (class)
Mod1 9 :NextWindow {groups} (title=cp-dummy)
Mod1 8 :NextWindow {groups} (title=cp-current-1)
EOF

cat >"$APPS_FILE" <<'EOF'
[app] (app_id=fbwl-cp-dummy)
  [Head] {1}
[end]

[app] (app_id=fbwl-cp-head0)
  [Head] {0}
[end]

[app] (app_id=fbwl-cp-head1)
  [Head] {1}
[end]

[app] (app_id=fbwl-cp-current)
  [Head] {1}
[end]

[app] (app_id=fbwl-cp-layer-top)
  [Head]  {1}
  [Layer] {Top}
[end]

[app] (app_id=fbwl-cp-max-h)
  [Head]      {1}
  [Deco]      {none}
  [Maximized] {horz}
[end]

[app] (app_id=fbwl-cp-max-v)
  [Head]      {1}
  [Deco]      {none}
  [Maximized] {vert}
[end]
EOF

: >"$LOG"

env WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=2 \
  ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --keys "$KEYS_FILE" --apps "$APPS_FILE" --workspaces 1 \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until [[ \$(rg -c 'OutputLayout:' '$LOG') -ge 2 ]]; do sleep 0.05; done"

LINE1="$(rg 'OutputLayout:' "$LOG" | head -n 1)"
LINE2="$(rg 'OutputLayout:' "$LOG" | head -n 2 | tail -n 1)"

X1="$(echo "$LINE1" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)"
Y1="$(echo "$LINE1" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)"
W1="$(echo "$LINE1" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)"
H1="$(echo "$LINE1" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)"

X2="$(echo "$LINE2" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)"
Y2="$(echo "$LINE2" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)"
W2="$(echo "$LINE2" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)"
H2="$(echo "$LINE2" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)"

CX1=$((X1 + W1 / 2))
CY1=$((Y1 + H1 / 2))
CX2=$((X2 + W2 / 2))
CY2=$((Y2 + H2 / 2))

./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-cp-head0 --title cp-head0 --stay-ms 20000 >/dev/null 2>&1 &
HEAD0_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-cp-head1 --title cp-head1 --stay-ms 20000 >/dev/null 2>&1 &
HEAD1_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-cp-layer-top --title cp-layer-top --stay-ms 20000 >/dev/null 2>&1 &
TOP_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-cp-max-h --title cp-max-h --stay-ms 20000 --width 200 --height 100 >/dev/null 2>&1 &
MAXH_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-cp-max-v --title cp-max-v --stay-ms 20000 --width 210 --height 110 >/dev/null 2>&1 &
MAXV_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-cp-dummy --title cp-dummy --stay-ms 20000 >/dev/null 2>&1 &
DUMMY_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-cp-current --title cp-current-1 --stay-ms 20000 >/dev/null 2>&1 &
CUR1_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-cp-current --title cp-current-2 --stay-ms 20000 >/dev/null 2>&1 &
CUR2_PID=$!

timeout 10 bash -c "until rg -q 'Place: cp-dummy ' '$LOG'; do sleep 0.05; done"

focus_by_key() {
  local key="$1"
  local title="$2"
  ./fbwl-input-injector --socket "$SOCKET" key "$key"
  timeout 5 bash -c "until rg 'Focus:' '$LOG' | tail -n 1 | rg -q 'Focus: ${title}'; do sleep 0.05; done"
}

focus_by_key alt-9 cp-dummy

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: cp-layer-top'; do sleep 0.05; done"

focus_by_key alt-9 cp-dummy

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: cp-max-h'; do sleep 0.05; done"

focus_by_key alt-9 cp-dummy

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-3
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: cp-max-v'; do sleep 0.05; done"

# class=[current] should resolve based on the currently focused view.

focus_by_key alt-8 cp-current-1
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-5
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: cp-current-2'; do sleep 0.05; done"

# "(class)" is shorthand for "(class=[current])".

focus_by_key alt-8 cp-current-1
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-7
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: cp-current-2'; do sleep 0.05; done"

# layer!=[current] should compare against the currently focused view's layer.

focus_by_key alt-9 cp-dummy
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-6
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: cp-layer-top'; do sleep 0.05; done"

# head=[mouse] should resolve based on cursor position.

focus_by_key alt-9 cp-dummy

./fbwl-input-injector --socket "$SOCKET" motion "$CX1" "$CY1" >/dev/null 2>&1 || true
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-4
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: cp-head0'; do sleep 0.05; done"

focus_by_key alt-9 cp-dummy

./fbwl-input-injector --socket "$SOCKET" motion "$CX2" "$CY2" >/dev/null 2>&1 || true
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-4
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: cp-head1'; do sleep 0.05; done"

echo "ok: NextWindow ClientPattern smoke passed (socket=$SOCKET log=$LOG keys=$KEYS_FILE apps=$APPS_FILE)"
