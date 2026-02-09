#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd wc

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-workspace-cmds-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-config-workspace-cmds-$UID-XXXXXX")"

cleanup() {
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFGDIR" 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

assert_rg_file() {
  local pattern="$1"
  local file="$2"
  if ! rg -q "$pattern" "$file"; then
    echo "expected pattern not found: $pattern" >&2
    echo "file: $file" >&2
    echo "--- file content ---" >&2
    cat "$file" >&2 || true
    echo "--- log tail ---" >&2
    tail -n 200 "$LOG" >&2 || true
    exit 1
  fi
}

cat >"$CFGDIR/init" <<EOF
session.screen0.workspaces: 2
session.keyFile: keys
EOF

cat >"$CFGDIR/keys" <<EOF
Mod1 1 :AddWorkspace
Mod1 2 :RemoveLastWorkspace
Mod1 3 :SetWorkspaceName Smoke WS
Mod1 4 :SetWorkspaceNameDialog
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title workspace-cmds --stay-ms 20000 >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Focus: workspace-cmds' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-4
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'CmdDialog: open'; do sleep 0.05; done"
./fbwl-input-injector --socket "$SOCKET" type Dialog
./fbwl-input-injector --socket "$SOCKET" key enter
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'WorkspaceName: set ws=1'; do sleep 0.05; done"
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'SaveRC: ok'; do sleep 0.05; done"
assert_rg_file '^session\.screen0\.workspaceNames:\s*1Dialog,Workspace 2,' "$CFGDIR/init"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-3
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'WorkspaceName: set ws=1'; do sleep 0.05; done"
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'SaveRC: ok'; do sleep 0.05; done"
assert_rg_file '^session\.screen0\.workspaceNames:\s*Smoke WS,Workspace 2,' "$CFGDIR/init"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: add count=3'; do sleep 0.05; done"
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'SaveRC: ok'; do sleep 0.05; done"
assert_rg_file '^session\.screen0\.workspaces:\s*3\b' "$CFGDIR/init"
assert_rg_file '^session\.screen0\.workspaceNames:\s*Smoke WS,Workspace 2,Workspace 3,' "$CFGDIR/init"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Workspace: remove-last count=2'; do sleep 0.05; done"
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'SaveRC: ok'; do sleep 0.05; done"
assert_rg_file '^session\.screen0\.workspaces:\s*2\b' "$CFGDIR/init"
assert_rg_file '^session\.screen0\.workspaceNames:\s*Smoke WS,Workspace 2,Workspace 3,' "$CFGDIR/init"

echo "ok: workspace add/remove/name smoke passed (socket=$SOCKET log=$LOG cfgdir=$CFGDIR)"
