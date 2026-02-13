#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout
need_exe ./fbwl-input-injector

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-apps-rules-$UID-$$.log}"

APPS_FILE="$(mktemp /tmp/fbwl-apps-rules-XXXXXX)"

cleanup() {
  rm -f "$APPS_FILE" 2>/dev/null || true
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${G1_PID:-}" ]]; then kill "$G1_PID" 2>/dev/null || true; fi
  if [[ -n "${G2_PID:-}" ]]; then kill "$G2_PID" 2>/dev/null || true; fi
  if [[ -n "${MAXH_PID:-}" ]]; then kill "$MAXH_PID" 2>/dev/null || true; fi
  if [[ -n "${MAXV_PID:-}" ]]; then kill "$MAXV_PID" 2>/dev/null || true; fi
  if [[ -n "${NODECO_PID:-}" ]]; then kill "$NODECO_PID" 2>/dev/null || true; fi
  if [[ -n "${PLACED_PID:-}" ]]; then kill "$PLACED_PID" 2>/dev/null || true; fi
  if [[ -n "${SHADED_PID:-}" ]]; then kill "$SHADED_PID" 2>/dev/null || true; fi
  if [[ -n "${ALPHA_PID:-}" ]]; then kill "$ALPHA_PID" 2>/dev/null || true; fi
  if [[ -n "${FOCUS_PID:-}" ]]; then kill "$FOCUS_PID" 2>/dev/null || true; fi
  if [[ -n "${SAVE_PID:-}" ]]; then kill "$SAVE_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$APPS_FILE" <<'EOF'
# Workspace IDs are 0-based (Fluxbox apps file semantics):
#   [Workspace] {0} => first workspace

[app] (app_id=fbwl-apps-jump)
  [Workspace] {1}
  [Jump]      {yes}
[end]

[app] (app_id=fbwl-apps-sticky)
  [Workspace] {2}
  [Sticky]    {yes}
[end]

[group]
  [app] (app_id=fbwl-apps-grp-a)
  [app] (app_id=fbwl-apps-grp-b)
  [Sticky] {yes}
[end]

[app] (app_id=fbwl-apps-nodeco)
  [Deco]  {none}
  [Layer] {Top}
[end]

[app] (app_id=fbwl-apps-placed)
  [Head]       {0}
  [Dimensions] {200 120}
  [Position]   (TopLeft) {0 0}
[end]

[app] (app_id=fbwl-apps-shaded)
  [Shaded] {yes}
[end]

[app] (app_id=fbwl-apps-max-h)
  [Deco]      {none}
  [Maximized] {horz}
[end]

[app] (app_id=fbwl-apps-max-v)
  [Deco]      {none}
  [Maximized] {vert}
[end]

[app] (app_id=fbwl-apps-alpha)
  [Alpha] {200 100}
[end]

[app] (app_id=fbwl-apps-focus-refuse)
  [FocusProtection] {refuse}
[end]

[app] (app_id=fbwl-apps-save)
  [Deco]       {none}
  [Dimensions] {60 40}
  [Position]   (TopLeft) {300 0}
  [Close]      {yes}
[end]
EOF

: >"$LOG"

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --workspaces 3 \
  --apps "$APPS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-apps-jump --title apps-jump --stay-ms 10000 >/dev/null 2>&1 &
A_PID=$!

timeout 5 bash -c "until ./fbwl-remote --socket '$SOCKET' get-workspace | rg -q '^ok workspace=2$'; do sleep 0.05; done"

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-apps-jump .*workspace_id=1 .*jump=1'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-apps-sticky --title apps-sticky --stay-ms 10000 >/dev/null 2>&1 &
B_PID=$!

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-apps-sticky .*workspace_id=2 .*sticky=1'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Workspace: view=apps-sticky ws=3 visible=1'; do sleep 0.05; done"

./fbwl-remote --socket "$SOCKET" get-workspace | rg -q '^ok workspace=2$'

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-apps-grp-a --title apps-grp-a --stay-ms 10000 >/dev/null 2>&1 &
G1_PID=$!

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-apps-grp-a .*sticky=1 .*group_id=1'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-apps-grp-b --title apps-grp-b --stay-ms 10000 >/dev/null 2>&1 &
G2_PID=$!

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Tabs: attach reason=apps-group .*anchor=apps-grp-a .*view=apps-grp-b'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-apps-grp-b .*sticky=1 .*group_id=1'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-apps-nodeco --title apps-nodeco --stay-ms 10000 >/dev/null 2>&1 &
NODECO_PID=$!

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-apps-nodeco .*deco=0x0 .*layer=6'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-apps-placed --title apps-placed --stay-ms 10000 >/dev/null 2>&1 &
PLACED_PID=$!

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-apps-placed .*head=0 .*dims=200x120 .*pos=0,0 .*anchor=0'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: remember position .*app_id=fbwl-apps-placed .*anchor=TopLeft'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-apps-shaded --title apps-shaded --stay-ms 10000 >/dev/null 2>&1 &
SHADED_PID=$!

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-apps-shaded .*shaded=1'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Shade: .* on reason=apps'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-apps-max-h --title apps-max-h --stay-ms 10000 --width 200 --height 120 >/dev/null 2>&1 &
MAXH_PID=$!

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-apps-max-h .*maximized=1'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'MaximizeAxes: apps-max-h horz=1 vert=0 '; do sleep 0.05; done"

maxh_line="$(tail -c +$START "$LOG" | rg -m1 'MaximizeAxes: apps-max-h horz=1 vert=0 ')"
if [[ "$maxh_line" =~ w=([0-9]+)\ h=([0-9]+) ]]; then
  MAXH_W="${BASH_REMATCH[1]}"
  MAXH_H="${BASH_REMATCH[2]}"
else
  echo "failed to parse MaximizeAxes line (apps-max-h): $maxh_line" >&2
  exit 1
fi
if [[ "$MAXH_H" -ne 120 ]]; then
  echo "unexpected MaximizeAxes height for apps-max-h: expected=120 got=$MAXH_H line=$maxh_line" >&2
  exit 1
fi
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Surface size: apps-max-h ${MAXH_W}x${MAXH_H}'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-apps-max-v --title apps-max-v --stay-ms 10000 --width 210 --height 110 >/dev/null 2>&1 &
MAXV_PID=$!

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-apps-max-v .*maximized=1'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'MaximizeAxes: apps-max-v horz=0 vert=1 '; do sleep 0.05; done"

maxv_line="$(tail -c +$START "$LOG" | rg -m1 'MaximizeAxes: apps-max-v horz=0 vert=1 ')"
if [[ "$maxv_line" =~ w=([0-9]+)\ h=([0-9]+) ]]; then
  MAXV_W="${BASH_REMATCH[1]}"
  MAXV_H="${BASH_REMATCH[2]}"
else
  echo "failed to parse MaximizeAxes line (apps-max-v): $maxv_line" >&2
  exit 1
fi
if [[ "$MAXV_W" -ne 210 ]]; then
  echo "unexpected MaximizeAxes width for apps-max-v: expected=210 got=$MAXV_W line=$maxv_line" >&2
  exit 1
fi
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Surface size: apps-max-v ${MAXV_W}x${MAXV_H}'; do sleep 0.05; done"

kill "$MAXH_PID" 2>/dev/null || true
wait "$MAXH_PID" 2>/dev/null || true
MAXH_PID=""
kill "$MAXV_PID" 2>/dev/null || true
wait "$MAXV_PID" 2>/dev/null || true
MAXV_PID=""

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-apps-alpha --title apps-alpha --stay-ms 10000 >/dev/null 2>&1 &
ALPHA_PID=$!

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-apps-alpha .*alpha=200,100'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Alpha: .* focused=200 unfocused=100 reason=apps'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-apps-focus-refuse --title apps-focus-refuse --stay-ms 10000 >/dev/null 2>&1 &
FOCUS_PID=$!

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-apps-focus-refuse .*focus_protect=0x2'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'FocusNew: refused .*app_id=fbwl-apps-focus-refuse'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-apps-save --title apps-save --stay-ms 20000 >/dev/null 2>&1 &
SAVE_PID=$!

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: match rule=.* app_id=fbwl-apps-save'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: remember position title=apps-save'; do sleep 0.05; done"

remember_line="$(tail -c +$START "$LOG" | rg -m1 'Apps: remember position title=apps-save ')"
if [[ "$remember_line" =~ content=([-0-9]+),([-0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Apps remember position line (content): $remember_line" >&2
  exit 1
fi

if [[ "$remember_line" =~ size=([0-9]+)x([0-9]+) ]]; then
  W0="${BASH_REMATCH[1]}"
  H0="${BASH_REMATCH[2]}"
else
  echo "failed to parse Apps remember position line (size): $remember_line" >&2
  exit 1
fi

timeout 5 bash -c "until rg -q 'Surface size: apps-save ${W0}x${H0}' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
MOVE_START_X=$((X0 + 10))
MOVE_START_Y=$((Y0 + 10))
MOVE_END_X=$((MOVE_START_X + 100))
MOVE_END_Y=$((MOVE_START_Y + 100))
./fbwl-input-injector --socket "$SOCKET" drag-alt-left "$MOVE_START_X" "$MOVE_START_Y" "$MOVE_END_X" "$MOVE_END_Y"
X1=$((X0 + 100))
Y1=$((Y0 + 100))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Move: apps-save x=$X1 y=$Y1"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
RESIZE_START_X=$((X1 + 10))
RESIZE_START_Y=$((Y1 + 10))
RESIZE_END_X=$((RESIZE_START_X + 50))
RESIZE_END_Y=$((RESIZE_START_Y + 60))
./fbwl-input-injector --socket "$SOCKET" drag-alt-right "$RESIZE_START_X" "$RESIZE_START_Y" "$RESIZE_END_X" "$RESIZE_END_Y"
W1=$((W0 + 50))
H1=$((H0 + 60))
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Resize: apps-save w=$W1 h=$H1"

timeout 5 bash -c "until rg -q 'Surface size: apps-save ${W1}x${H1}' '$LOG'; do sleep 0.05; done"

kill "$SAVE_PID" 2>/dev/null || true
wait "$SAVE_PID" 2>/dev/null || true
SAVE_PID=""

timeout 5 bash -c "until rg -q 'Apps: save-on-close wrote ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -Fq \"[Position] (TopLeft) {${X1} ${Y1}}\" '$APPS_FILE'; do sleep 0.05; done"
timeout 5 bash -c "until rg -Fq \"[Dimensions] {${W1} ${H1}}\" '$APPS_FILE'; do sleep 0.05; done"

echo "ok: apps rules smoke passed (socket=$SOCKET log=$LOG apps=$APPS_FILE)"
