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

SOCKET="${SOCKET:-wayland-fbwl-apps-hidden-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-apps-hidden-$UID-$$.log}"
APPS_FILE="$(mktemp /tmp/fbwl-apps-hidden-XXXXXX)"
KEYS_FILE="$(mktemp /tmp/fbwl-keys-apps-hidden-XXXXXX)"

cleanup() {
  rm -f "$APPS_FILE" "$KEYS_FILE" 2>/dev/null || true
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${FH_PID:-}" ]]; then kill "$FH_PID" 2>/dev/null || true; fi
  if [[ -n "${BOTH_PID:-}" ]]; then kill "$BOTH_PID" 2>/dev/null || true; fi
  if [[ -n "${IH_PID:-}" ]]; then kill "$IH_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cat >"$APPS_FILE" <<'EOF'
[app] (app_id=fbwl-hidden-fh)
  [FocusHidden] {yes}
[end]

[app] (app_id=fbwl-hidden-both)
  [Hidden] {yes}
[end]

[app] (app_id=fbwl-hidden-ih)
  [IconHidden] {yes}
[end]
EOF

cat >"$KEYS_FILE" <<'EOF'
Mod1 Control 9 :NextWindow {static} (class=fbwl-hidden-.*)
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
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-hidden-a --title hidden-a --stay-ms 20000 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-hidden-fh --title hidden-fh --stay-ms 20000 >/dev/null 2>&1 &
FH_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-hidden-both --title hidden-both --stay-ms 20000 >/dev/null 2>&1 &
BOTH_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-hidden-b --title hidden-b --stay-ms 20000 >/dev/null 2>&1 &
B_PID=$!

timeout 10 bash -c "until rg -q 'Place: hidden-a ' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Place: hidden-fh ' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Place: hidden-both ' '$LOG'; do sleep 0.05; done"
timeout 10 bash -c "until rg -q 'Place: hidden-b ' '$LOG'; do sleep 0.05; done"

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

read -r A_X A_Y < <(parse_place_xy "hidden-a")
read -r B_X B_Y < <(parse_place_xy "hidden-b")

# Establish deterministic starting focus, then ensure NextWindow skips FocusHidden + Hidden windows.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$((A_X + 10))" "$((A_Y + 10))" >/dev/null 2>&1 || true
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: hidden-a'; do sleep 0.05; done"

CYCLE_START=$(wc -c <"$LOG" | tr -d ' ')
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-9
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: hidden-b'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-9
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: hidden-a'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-9
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: hidden-b'; do sleep 0.05; done"

if tail -c +$((CYCLE_START + 1)) "$LOG" | rg -q 'Focus: hidden-fh'; then
  echo "expected FocusHidden view to be skipped by NextWindow (got focus on hidden-fh)" >&2
  exit 1
fi
if tail -c +$((CYCLE_START + 1)) "$LOG" | rg -q 'Focus: hidden-both'; then
  echo "expected Hidden view to be skipped by NextWindow (got focus on hidden-both)" >&2
  exit 1
fi

timeout 5 bash -c "until rg -q 'Toolbar: iconbar item .*title=hidden-a' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: iconbar item .*title=hidden-b' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: iconbar item .*title=hidden-fh' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-hidden-ih --title hidden-ih --stay-ms 20000 >/dev/null 2>&1 &
IH_PID=$!
timeout 10 bash -c "until rg -q 'Place: hidden-ih ' '$LOG'; do sleep 0.05; done"

if rg -q 'Toolbar: iconbar item .*title=hidden-ih' "$LOG"; then
  echo "expected IconHidden view to be excluded from iconbar (found hidden-ih item)" >&2
  exit 1
fi
if rg -q 'Toolbar: iconbar item .*title=hidden-both' "$LOG"; then
  echo "expected Hidden view to be excluded from iconbar (found hidden-both item)" >&2
  exit 1
fi

echo "ok: apps hidden (FocusHidden/IconHidden/Hidden) smoke passed (socket=$SOCKET log=$LOG apps=$APPS_FILE keys=$KEYS_FILE)"
