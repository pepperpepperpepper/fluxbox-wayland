#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-keybinding-cmdlang-$UID-$$.log}"
KEYS_FILE="$(mktemp "/tmp/fbwl-keys-cmdlang-$UID-XXXXXX.keys")"

cleanup() {
  rm -f "$KEYS_FILE" 2>/dev/null || true
  for pid_var in IF_YES_PID IF_NO_PID FE1_PID FE2_PID OTHER_PID FBW_PID; do
    pid="${!pid_var:-}"
    if [[ -n "$pid" ]]; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

cat >"$KEYS_FILE" <<'EOF'
Mod1 Control 1 :If {Matches (class=if-yes)} {SetTitle if-then} {SetTitle if-else}
Mod1 Control 2 :Cond {Xor {Matches (class=if-yes)} {Matches (class=if-no)}} {SetTitle xor-then} {SetTitle xor-else}
Mod1 Control 3 :If {And {Some Matches (class=foreach)} {Or {Not Every Matches (class=foreach)} {Matches (class=impossible)}}} {SetTitle logic-then} {SetTitle logic-else}
Mod1 Control 4 :Map {SetTitle foreach-hit} {Matches (class=foreach)}
Mod1 Control 5 :MacroCmd {If {Matches (class=if-yes)} {SetTitle macro-nested} {SetTitle macro-other}} {SetTitle macro-after}
Mod1 Control 6 :Delay {SetTitle delay-fired} 200000
Mod1 Control 7 :ToggleCmd {SetTitle toggle-1} {SetTitle toggle-2}
Mod1 Control 8 :ToggleCmd {SetTitle toggle-1} {SetTitle toggle-2}
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --keys "$KEYS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"

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

IF_YES_TITLE="if-yes"
IF_NO_TITLE="if-no"
FE1_TITLE="foreach-1"
FE2_TITLE="foreach-2"
OTHER_TITLE="other"

./fbwl-smoke-client --socket "$SOCKET" --title "$IF_YES_TITLE" --app-id "if-yes" --stay-ms 20000 --xdg-decoration --width 260 --height 160 >/dev/null 2>&1 &
IF_YES_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title "$IF_NO_TITLE" --app-id "if-no" --stay-ms 20000 --xdg-decoration --width 260 --height 160 >/dev/null 2>&1 &
IF_NO_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title "$FE1_TITLE" --app-id "foreach" --stay-ms 20000 --xdg-decoration --width 260 --height 160 >/dev/null 2>&1 &
FE1_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title "$FE2_TITLE" --app-id "foreach" --stay-ms 20000 --xdg-decoration --width 260 --height 160 >/dev/null 2>&1 &
FE2_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title "$OTHER_TITLE" --app-id "other" --stay-ms 20000 --xdg-decoration --width 260 --height 160 >/dev/null 2>&1 &
OTHER_PID=$!

timeout 5 bash -c "until rg -q 'Place: $IF_YES_TITLE ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: $IF_NO_TITLE ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: $FE1_TITLE ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: $FE2_TITLE ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: $OTHER_TITLE ' '$LOG'; do sleep 0.05; done"

read -r IF_YES_X IF_YES_Y < <(parse_place_xy "$IF_YES_TITLE")
read -r IF_NO_X IF_NO_Y < <(parse_place_xy "$IF_NO_TITLE")
read -r FE1_X FE1_Y < <(parse_place_xy "$FE1_TITLE")
read -r FE2_X FE2_Y < <(parse_place_xy "$FE2_TITLE")
read -r OTHER_X OTHER_Y < <(parse_place_xy "$OTHER_TITLE")

# If/Matches.
./fbwl-input-injector --socket "$SOCKET" click "$((IF_YES_X + 10))" "$((IF_YES_Y + 10))" >/dev/null 2>&1 || true
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=if-then reason=keybinding'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" click "$((IF_NO_X + 10))" "$((IF_NO_Y + 10))" >/dev/null 2>&1 || true
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=if-else reason=keybinding'; do sleep 0.05; done"

# Cond/Xor.
./fbwl-input-injector --socket "$SOCKET" click "$((IF_YES_X + 10))" "$((IF_YES_Y + 10))" >/dev/null 2>&1 || true
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-2
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=xor-then reason=keybinding'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" click "$((OTHER_X + 10))" "$((OTHER_Y + 10))" >/dev/null 2>&1 || true
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-2
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=xor-else reason=keybinding'; do sleep 0.05; done"

# And/Or/Not + Some/Every.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-3
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=logic-then reason=keybinding'; do sleep 0.05; done"

# Map/ForEach iteration: set title on both foreach windows.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-4
timeout 5 bash -c "until [[ \$(tail -c +$((OFFSET + 1)) \"$LOG\" | rg -c 'Title: set title override create_seq=[0-9]+ title=foreach-hit reason=keybinding') -ge 2 ]]; do sleep 0.05; done"

# MacroCmd with nested If braces.
./fbwl-input-injector --socket "$SOCKET" click "$((IF_YES_X + 10))" "$((IF_YES_Y + 10))" >/dev/null 2>&1 || true
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-5
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=macro-nested reason=keybinding'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=macro-after reason=keybinding'; do sleep 0.05; done"

# Delay restart semantics: press twice quickly, expect one fire.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-6
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-6
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Delay: fire cmd=SetTitle delay-fired'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=delay-fired reason=keybinding'; do sleep 0.05; done"
cnt="$(tail -c +$((OFFSET + 1)) "$LOG" | rg -c 'Delay: fire cmd=SetTitle delay-fired')"
if [[ "$cnt" != "1" ]]; then
  echo "expected exactly 1 Delay fire, got $cnt" >&2
  exit 1
fi

# ToggleCmd cycles between subcommands, and state is scoped per binding (identical ToggleCmd strings don't share state).
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-7
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=toggle-1 reason=keybinding'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-8
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=toggle-1 reason=keybinding'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-7
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=toggle-2 reason=keybinding'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-8
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=toggle-2 reason=keybinding'; do sleep 0.05; done"

echo "ok: keybinding cmdlang smoke passed (socket=$SOCKET log=$LOG keys=$KEYS_FILE)"
