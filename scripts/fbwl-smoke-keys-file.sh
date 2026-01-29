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
LOG="${LOG:-/tmp/fluxbox-wayland-keys-$UID-$$.log}"
MARK_DEFAULT="${MARK_DEFAULT:-/tmp/fbwl-terminal-default-$UID-$$}"
MARK_OVERRIDE="${MARK_OVERRIDE:-/tmp/fbwl-keys-override-$UID-$$}"
MARK_OVERRIDE2="${MARK_OVERRIDE2:-/tmp/fbwl-keys-override2-$UID-$$}"
MARK_MOUSE="${MARK_MOUSE:-/tmp/fbwl-keys-mouse-$UID-$$}"
MARK_MOUSE2="${MARK_MOUSE2:-/tmp/fbwl-keys-mouse2-$UID-$$}"
KEYS_FILE="${KEYS_FILE:-/tmp/fbwl-keys-$UID-$$.conf}"
APPS_FILE="$(mktemp /tmp/fbwl-keys-apps-XXXXXX)"

cleanup() {
  rm -f "$MARK_DEFAULT" "$MARK_OVERRIDE" "$MARK_OVERRIDE2" "$MARK_MOUSE" "$MARK_MOUSE2" "$KEYS_FILE" "$APPS_FILE" 2>/dev/null || true
  if [[ -n "${APP_PID:-}" ]]; then kill "$APP_PID" 2>/dev/null || true; fi
  if [[ -n "${TAB_A_PID:-}" ]]; then kill "$TAB_A_PID" 2>/dev/null || true; fi
  if [[ -n "${TAB_B_PID:-}" ]]; then kill "$TAB_B_PID" 2>/dev/null || true; fi
  if [[ -n "${FOCUS_A_PID:-}" ]]; then kill "$FOCUS_A_PID" 2>/dev/null || true; fi
  if [[ -n "${FOCUS_B_PID:-}" ]]; then kill "$FOCUS_B_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
rm -f "$MARK_DEFAULT" "$MARK_OVERRIDE" "$MARK_OVERRIDE2" "$MARK_MOUSE" "$MARK_MOUSE2"

cat >"$KEYS_FILE" <<EOF
# Minimal subset of Fluxbox ~/.fluxbox/keys syntax
Mod1 Return :ExecCommand touch '$MARK_OVERRIDE'
Mod1 F1 :Reconfigure
OnDesktop Mouse1 :ExecCommand touch '$MARK_MOUSE'
EOF

cat >"$APPS_FILE" <<'EOF'
[group]
  [app] (app_id=fbwl-keys-tab-a)
  [app] (app_id=fbwl-keys-tab-b)
[end]
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --terminal "touch '$MARK_DEFAULT'" \
  --keys "$KEYS_FILE" \
  --apps "$APPS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title keys-client --stay-ms 10000 >/dev/null 2>&1 &
APP_PID=$!

timeout 5 bash -c "until rg -q 'Focus: keys-client' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-return

timeout 2 bash -c "until [[ -f '$MARK_OVERRIDE' ]]; do sleep 0.05; done"
if [[ -f "$MARK_DEFAULT" ]]; then
  echo "expected default terminal binding to be overridden (MARK_DEFAULT exists: $MARK_DEFAULT)" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" click 10 200
timeout 2 bash -c "until [[ -f '$MARK_MOUSE' ]]; do sleep 0.05; done"

rm -f "$MARK_OVERRIDE" "$MARK_MOUSE"

cat >"$KEYS_FILE" <<EOF
Mod1 Return :ExecCommand touch '$MARK_OVERRIDE2'
Mod1 F1 :Reconfigure
OnDesktop Mouse1 :ExecCommand touch '$MARK_MOUSE2'
Mod1 F :NextTab
Mod1 M :PrevTab
Mod1 1 :Tab 1
Mod1 2 :Tab 2
Mod1 I :NextWindow {groups} (class=fbwl-keys-focus-a)
Mod1 Escape :NextWindow {groups} (class=fbwl-keys-focus-b)
EOF

./fbwl-input-injector --socket "$SOCKET" key alt-return
timeout 2 bash -c "until [[ -f '$MARK_OVERRIDE' ]]; do sleep 0.05; done"
if [[ -f "$MARK_OVERRIDE2" ]]; then
  echo "expected keys binding to not change before Reconfigure (MARK_OVERRIDE2 exists: $MARK_OVERRIDE2)" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" click 10 200
timeout 2 bash -c "until [[ -f '$MARK_MOUSE' ]]; do sleep 0.05; done"
if [[ -f "$MARK_MOUSE2" ]]; then
  echo "expected mouse binding to not change before Reconfigure (MARK_MOUSE2 exists: $MARK_MOUSE2)" >&2
  exit 1
fi

rm -f "$MARK_OVERRIDE" "$MARK_MOUSE"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f1
timeout 2 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Reconfigure: reloaded keys from '; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-return
timeout 2 bash -c "until [[ -f '$MARK_OVERRIDE2' ]]; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" click 10 200
timeout 2 bash -c "until [[ -f '$MARK_MOUSE2' ]]; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title keys-tab-a --app-id fbwl-keys-tab-a --stay-ms 10000 >/dev/null 2>&1 &
TAB_A_PID=$!
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: keys-tab-a'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title keys-tab-b --app-id fbwl-keys-tab-b --stay-ms 10000 >/dev/null 2>&1 &
TAB_B_PID=$!
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: attach reason=apps-group'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: activate reason=keybinding-nexttab title=keys-tab-'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-m
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: activate reason=keybinding-prevtab title=keys-tab-'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-1
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: activate reason=keybinding-tab title=keys-tab-'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: activate reason=keybinding-tab title=keys-tab-'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title keys-focus-a --app-id fbwl-keys-focus-a --stay-ms 10000 >/dev/null 2>&1 &
FOCUS_A_PID=$!
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: keys-focus-a'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --title keys-focus-b --app-id fbwl-keys-focus-b --stay-ms 10000 >/dev/null 2>&1 &
FOCUS_B_PID=$!
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: keys-focus-b'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-i
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: keys-focus-a'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-escape
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: keys-focus-b'; do sleep 0.05; done"

echo "ok: keys file smoke passed (socket=$SOCKET log=$LOG keys_file=$KEYS_FILE)"
