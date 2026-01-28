#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout

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
  if [[ -n "${NODECO_PID:-}" ]]; then kill "$NODECO_PID" 2>/dev/null || true; fi
  if [[ -n "${PLACED_PID:-}" ]]; then kill "$PLACED_PID" 2>/dev/null || true; fi
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
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-apps-nodeco .*deco=0 .*layer=6'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-apps-placed --title apps-placed --stay-ms 10000 >/dev/null 2>&1 &
PLACED_PID=$!

START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-apps-placed .*head=0 .*dims=200x120 .*pos=0,0 .*anchor=0'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: remember position .*app_id=fbwl-apps-placed .*anchor=TopLeft'; do sleep 0.05; done"

echo "ok: apps rules smoke passed (socket=$SOCKET log=$LOG apps=$APPS_FILE)"
