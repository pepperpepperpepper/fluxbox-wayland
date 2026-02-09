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

SOCKET="${SOCKET:-wayland-fbwl-ws-nav-offset-toggle-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-ws-nav-offset-toggle-$UID-$$.log}"
KEYS_FILE="$(mktemp /tmp/fbwl-keys-ws-nav-offset-toggle-XXXXXX)"

cleanup() {
  rm -f "$KEYS_FILE" 2>/dev/null || true
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$KEYS_FILE" <<'EOF'
Mod1 8 :NextWorkspace 2
Mod1 7 :PrevWorkspace 2
Mod1 9 :NextWorkspace 0
Mod1 6 :PrevWorkspace 0
Mod1 5 :SendToNextWorkspace 2
Mod1 Control 9 :TakeToPrevWorkspace 2
Mod1 F1 :RightWorkspace 1
Mod1 F2 :LeftWorkspace 1
EOF

: >"$LOG"

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" \
  ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --workspaces 4 --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title ws-nav --stay-ms 20000 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Focus: ws-nav' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-8
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=3 reason=switch-next'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-7
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=1 reason=switch-prev'; do sleep 0.05; done"

# Toggle previous workspace.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=2 reason=switch'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-9
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=1 reason=switch-toggle'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-9
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=2 reason=switch-toggle'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-6
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=1 reason=switch-toggle'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-6
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=2 reason=switch-toggle'; do sleep 0.05; done"

# SendToNextWorkspace offset.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=1 reason=switch'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-5
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Policy: move focused to workspace 3 title=ws-nav'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=1 reason=move-focused'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: view=ws-nav ws=3 visible=0'; do sleep 0.05; done"

# Switch to workspace 3 to verify the view is there, then TakeToPrevWorkspace offset to return to workspace 1.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-3
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=3 reason=switch'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-9
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Policy: move focused to workspace 1 title=ws-nav'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=1 reason=switch'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: view=ws-nav ws=1 visible=1'; do sleep 0.05; done"

# Right/LeftWorkspace do not wrap.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-4
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=4 reason=switch'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-f1
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=1 reason=switch'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-f2
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-4
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: apply current=4 reason=switch'; do sleep 0.05; done"

echo "ok: workspace nav offsets/toggle smoke passed (socket=$SOCKET log=$LOG keys=$KEYS_FILE)"
