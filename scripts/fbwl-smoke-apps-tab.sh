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

need_exe ./fbwl-smoke-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-apps-tab-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-apps-tab-$UID-$$.log}"
APPS_FILE="$(mktemp /tmp/fbwl-apps-tab-XXXXXX)"

cleanup() {
  rm -f "$APPS_FILE" 2>/dev/null || true
  if [[ -n "${YES_A_PID:-}" ]]; then kill "$YES_A_PID" 2>/dev/null || true; fi
  if [[ -n "${YES_B_PID:-}" ]]; then kill "$YES_B_PID" 2>/dev/null || true; fi
  if [[ -n "${NO_A_PID:-}" ]]; then kill "$NO_A_PID" 2>/dev/null || true; fi
  if [[ -n "${NO_B_PID:-}" ]]; then kill "$NO_B_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$APPS_FILE" <<'EOF'
[group]
  [app] (app_id=fbwl-tab-yes-a)
  [app] (app_id=fbwl-tab-yes-b)
  [Tab] {yes}
[end]

[group]
  [app] (app_id=fbwl-tab-no-a)
  [app] (app_id=fbwl-tab-no-b)
  [Tab] {no}
[end]
EOF

: >"$LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --apps "$APPS_FILE" \
  --workspaces 1 \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-tab-yes-a --title tab-yes-a --stay-ms 20000 >/dev/null 2>&1 &
YES_A_PID=$!

START=$((OFFSET + 1))
timeout 10 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-tab-yes-a .*group_id=1'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-tab-yes-b --title tab-yes-b --stay-ms 20000 >/dev/null 2>&1 &
YES_B_PID=$!

START=$((OFFSET + 1))
timeout 10 bash -c "until tail -c +$START '$LOG' | rg -q 'Tabs: attach reason=apps-group .*anchor=tab-yes-a .*view=tab-yes-b'; do sleep 0.05; done"
timeout 10 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-tab-yes-b .*group_id=1'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-tab-no-a --title tab-no-a --stay-ms 20000 >/dev/null 2>&1 &
NO_A_PID=$!

START=$((OFFSET + 1))
timeout 10 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-tab-no-a .*group_id=2'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-tab-no-b --title tab-no-b --stay-ms 20000 >/dev/null 2>&1 &
NO_B_PID=$!

START=$((OFFSET + 1))
timeout 10 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=fbwl-tab-no-b .*group_id=2'; do sleep 0.05; done"

if tail -c +$START "$LOG" | rg -q 'Tabs: attach reason=apps-group .*anchor=tab-no-a .*view=tab-no-b'; then
  echo "expected [Tab]{no} to prevent apps-group tab attach (saw attach for tab-no-b)" >&2
  exit 1
fi

echo "ok: apps Tab remember (yes/no) smoke passed (socket=$SOCKET log=$LOG apps=$APPS_FILE)"

