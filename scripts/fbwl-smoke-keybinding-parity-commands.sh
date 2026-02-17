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
LOG="${LOG:-/tmp/fluxbox-wayland-keybinding-parity-$UID-$$.log}"
KEYS_FILE="$(mktemp "/tmp/fbwl-keys-parity-$UID-XXXXXX.keys")"

cleanup() {
  rm -f "$KEYS_FILE" 2>/dev/null || true
  for pid_var in SOLO_PID GRP1_PID GRP2_PID FBW_PID; do
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
Mod1 F1 :Attach (title=grp.*)
Mod1 F2 :GoToWindow 2 {groups}
Mod1 f :NextGroup
Mod1 m :PrevGroup
Mod1 i :ShowDesktop
Mod1 1 :Unclutter
Mod1 2 :ArrangeWindows
Mod1 3 :Shade
Mod1 4 :ShadeOn
Mod1 5 :ShadeOff
Mod1 6 :Stick
Mod1 7 :StickOn
Mod1 8 :StickOff
Mod1 9 :ToggleDecor
Mod1 Control 1 :SetDecor NONE
Mod1 Control 2 :SetDecor NORMAL
Mod1 Control 3 :SetAlpha 200 150
Mod1 Control 4 :SetAlpha +10 -10
Mod1 Control 5 :SetAlpha
Mod1 Control 6 :SetTitle key-title
Mod1 Control 7 :SetTitle {}
Mod1 Control 8 :SetTitleDialog
Mod1 Control 9 :Minimize
Mod1 Return :Deiconify
Mod1 Escape :CloseAllWindows
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

GRP1_TITLE="grp1"
GRP2_TITLE="grp2"
SOLO_TITLE="solo"

./fbwl-smoke-client --socket "$SOCKET" --title "$GRP1_TITLE" --app-id "grp1" --stay-ms 20000 --xdg-decoration --width 260 --height 160 >/dev/null 2>&1 &
GRP1_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title "$GRP2_TITLE" --app-id "grp2" --stay-ms 20000 --xdg-decoration --width 260 --height 160 >/dev/null 2>&1 &
GRP2_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title "$SOLO_TITLE" --app-id "solo" --stay-ms 20000 --xdg-decoration --width 260 --height 160 >/dev/null 2>&1 &
SOLO_PID=$!

timeout 5 bash -c "until rg -q 'Place: $GRP1_TITLE ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: $GRP2_TITLE ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: $SOLO_TITLE ' '$LOG'; do sleep 0.05; done"

read -r GRP1_X GRP1_Y < <(parse_place_xy "$GRP1_TITLE")
read -r GRP2_X GRP2_Y < <(parse_place_xy "$GRP2_TITLE")
read -r SOLO_X SOLO_Y < <(parse_place_xy "$SOLO_TITLE")

# Establish deterministic focus order: focus grp2, then grp1 so Attach picks grp1 as the anchor.
./fbwl-input-injector --socket "$SOCKET" click "$((GRP2_X + 10))" "$((GRP2_Y + 10))" >/dev/null 2>&1 || true
./fbwl-input-injector --socket "$SOCKET" click "$((GRP1_X + 10))" "$((GRP1_Y + 10))" >/dev/null 2>&1 || true

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Tabs: attach reason=attach-cmd anchor=${GRP1_TITLE} view=${GRP2_TITLE}'; do sleep 0.05; done"

# NextGroup should skip the inactive group member and jump to the other group.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Policy: focus \\(cycle-group\\) title=${SOLO_TITLE} '; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-m
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Policy: focus \\(cycle-group-rev\\) title=${GRP1_TITLE} '; do sleep 0.05; done"

# GoToWindow 2 {groups}: with grp1 focused, the #2 group is "solo".
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f2
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Policy: focus \\(goto-window\\) title=${SOLO_TITLE} '; do sleep 0.05; done"

# Shade toggle + on/off variants.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-3
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Shade: ${SOLO_TITLE} on reason=keybinding'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-3
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Shade: ${SOLO_TITLE} off reason=keybinding'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-4
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Shade: ${SOLO_TITLE} on reason=keybinding'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-5
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Shade: ${SOLO_TITLE} off reason=keybinding'; do sleep 0.05; done"

# Stick toggle + on/off variants.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-6
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Stick: ${SOLO_TITLE} on'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-6
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Stick: ${SOLO_TITLE} off'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-7
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Stick: ${SOLO_TITLE} on'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-8
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Stick: ${SOLO_TITLE} off'; do sleep 0.05; done"

# SetAlpha: explicit + relative + reset.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-3
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Alpha: ${SOLO_TITLE} focused=200 unfocused=150 reason=setalpha'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-4
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Alpha: ${SOLO_TITLE} focused=210 unfocused=140 reason=setalpha'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-5
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Alpha: ${SOLO_TITLE} focused=255 unfocused=255 reason=setalpha-default'; do sleep 0.05; done"

# ToggleDecor + SetDecor.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-9
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'ToggleDecor: ${SOLO_TITLE} off reason=keybinding'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-9
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'ToggleDecor: ${SOLO_TITLE} on reason=keybinding'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'SetDecor: ${SOLO_TITLE} value=NONE enabled=0 .* reason=keybinding'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-2
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'SetDecor: ${SOLO_TITLE} value=NORMAL enabled=1 .* reason=keybinding'; do sleep 0.05; done"

# SetTitle + SetTitleDialog.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-6
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=key-title reason=keybinding'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Decor: title-render key-title'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-7
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: cleared title override create_seq=[0-9]+ reason=keybinding'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Decor: title-render ${SOLO_TITLE}'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-8
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'CmdDialog: open'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" type dialog-title
./fbwl-input-injector --socket "$SOCKET" key enter
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: set title override create_seq=[0-9]+ title=dialog-title reason=set-title-dialog'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Decor: title-render dialog-title'; do sleep 0.05; done"

# Restore the base title so later log matching stays simple.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-7
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Title: cleared title override create_seq=[0-9]+ reason=keybinding'; do sleep 0.05; done"

# ShowDesktop toggles minimize on/off for windows on the current workspace.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-i
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Minimize: ${GRP1_TITLE} on reason=showdesktop'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Minimize: ${SOLO_TITLE} on reason=showdesktop'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-i
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Minimize: ${GRP1_TITLE} off reason=showdesktop'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Minimize: ${SOLO_TITLE} off reason=showdesktop'; do sleep 0.05; done"

# Deiconify restores the last minimized window on the current workspace by default.
./fbwl-input-injector --socket "$SOCKET" click "$((SOLO_X + 10))" "$((SOLO_Y + 10))" >/dev/null 2>&1 || true
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-ctrl-9
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Minimize: ${SOLO_TITLE} on reason=keybinding'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-return
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Minimize: ${SOLO_TITLE} off reason=deiconify'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Deiconify: head='; do sleep 0.05; done"

# Unclutter + ArrangeWindows should run without error and log their actions.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Unclutter: head='; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-2
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'ArrangeWindows: head='; do sleep 0.05; done"

# CloseAllWindows should close all clients on all workspaces.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-escape
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'CloseAllWindows: count='; do sleep 0.05; done"
timeout 5 bash -c "while kill -0 '$GRP1_PID' 2>/dev/null; do sleep 0.05; done"
timeout 5 bash -c "while kill -0 '$GRP2_PID' 2>/dev/null; do sleep 0.05; done"
timeout 5 bash -c "while kill -0 '$SOLO_PID' 2>/dev/null; do sleep 0.05; done"

echo "ok: keybinding parity commands smoke passed (socket=$SOCKET log=$LOG keys=$KEYS_FILE)"
