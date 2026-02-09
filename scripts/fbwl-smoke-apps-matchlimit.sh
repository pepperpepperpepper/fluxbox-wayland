#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1 (build first)" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd wc

need_exe ./fbwl-input-injector
need_exe ./fbwl-smoke-client
need_exe ./fluxbox-wayland

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-apps-matchlimit-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-apps-matchlimit-$UID-$$.log}"
KEYS_FILE="$(mktemp /tmp/fbwl-keys-apps-matchlimit-XXXXXX)"
APPS_FILE="$(mktemp /tmp/fbwl-apps-matchlimit-XXXXXX)"

cleanup() {
  rm -f "$KEYS_FILE" "$APPS_FILE" 2>/dev/null || true
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${C_PID:-}" ]]; then kill "$C_PID" 2>/dev/null || true; fi
  if [[ -n "${D_PID:-}" ]]; then kill "$D_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$KEYS_FILE" <<'EOF'
Mod1 1 :NextWindow {groups} (title=mlimit-1)
Mod1 2 :NextWindow {groups} (title=mlimit-2)
Mod1 3 :NextWindow {groups} (title=mlimit-3)
EOF

cat >"$APPS_FILE" <<'EOF'
[app] (app_id=mlimit) {2}
[end]
EOF

: >"$LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --keys "$KEYS_FILE" \
  --apps "$APPS_FILE" \
  --workspaces 1 \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --app-id mlimit --title mlimit-1 --stay-ms 20000 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --app-id mlimit --title mlimit-2 --stay-ms 20000 >/dev/null 2>&1 &
B_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --app-id mlimit --title mlimit-3 --stay-ms 20000 >/dev/null 2>&1 &
C_PID=$!

timeout 10 bash -c "until rg -q 'Place: mlimit-1 ' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Place: mlimit-2 ' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Place: mlimit-3 ' '$LOG'; do sleep 0.05; done"

timeout 5 bash -c "until rg -q 'Apps: match rule=0 title=mlimit-1 ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Apps: match rule=0 title=mlimit-2 ' '$LOG'; do sleep 0.05; done"
if rg -q 'Apps: match rule=0 title=mlimit-3 ' "$LOG"; then
  echo "unexpected match-limit behavior: rule applied to third client (log=$LOG)" >&2
  exit 1
fi

focus_by_key() {
  local key="$1"
  local title="$2"
  ./fbwl-input-injector --socket "$SOCKET" key "$key"
  timeout 5 bash -c "until rg 'Focus:' '$LOG' | tail -n 1 | rg -q 'Focus: ${title}'; do sleep 0.05; done"
}

focus_by_key alt-1 mlimit-1

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
kill "$A_PID" 2>/dev/null || true
wait "$A_PID" 2>/dev/null || true
unset A_PID
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus:'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --app-id mlimit --title mlimit-4 --stay-ms 20000 >/dev/null 2>&1 &
D_PID=$!
timeout 10 bash -c "until rg -q 'Place: mlimit-4 ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Apps: match rule=0 title=mlimit-4 ' '$LOG'; do sleep 0.05; done"

echo "ok: apps rules {N} match-limit smoke passed (socket=$SOCKET log=$LOG keys=$KEYS_FILE apps=$APPS_FILE)"

