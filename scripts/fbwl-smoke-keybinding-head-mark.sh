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
LOG="${LOG:-/tmp/fluxbox-wayland-keybinding-head-mark-$UID-$$.log}"
KEYS_FILE="$(mktemp "/tmp/fbwl-keys-head-mark-$UID-XXXXXX.keys")"

cleanup() {
  rm -f "$KEYS_FILE" 2>/dev/null || true
  for pid_var in A_PID B_PID FBW_PID; do
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
Mod1 F1 :SendToNextHead
Mod1 F2 :SendToPrevHead
Mod1 Return :SetHead 2
Mod1 Escape :SetHead 1
Mod1 i :KeyMode Mark
Mod1 m :KeyMode Goto
Mark: Arg :MacroCmd {MarkWindow} {KeyMode default}
Goto: Arg :MacroCmd {GotoMarkedWindow} {KeyMode default}
EOF

env WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=2 \
  ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --keys "$KEYS_FILE" >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until [[ \$(rg -c 'OutputLayout:' '$LOG') -ge 2 ]]; do sleep 0.05; done"

LINE1=$(rg 'OutputLayout:' "$LOG" | head -n 1)
LINE2=$(rg 'OutputLayout:' "$LOG" | head -n 2 | tail -n 1)

X1=$(echo "$LINE1" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
Y1=$(echo "$LINE1" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
W1=$(echo "$LINE1" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)
H1=$(echo "$LINE1" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)

X2=$(echo "$LINE2" | rg -o 'x=-?[0-9]+' | head -n 1 | cut -d= -f2)
Y2=$(echo "$LINE2" | rg -o 'y=-?[0-9]+' | head -n 1 | cut -d= -f2)
W2=$(echo "$LINE2" | rg -o 'w=[0-9]+' | head -n 1 | cut -d= -f2)
H2=$(echo "$LINE2" | rg -o 'h=[0-9]+' | head -n 1 | cut -d= -f2)

if [[ "$W1" -lt 1 || "$H1" -lt 1 || "$W2" -lt 1 || "$H2" -lt 1 ]]; then
  echo "invalid output layout boxes: '$LINE1' '$LINE2'" >&2
  exit 1
fi

CX1=$((X1 + W1 / 2))
CY1=$((Y1 + H1 / 2))
CX2=$((X2 + W2 / 2))
CY2=$((Y2 + H2 / 2))

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

TITLE_A="out-a"
TITLE_B="out-b"

./fbwl-input-injector --socket "$SOCKET" click "$CX1" "$CY1" >/dev/null 2>&1 || true
./fbwl-smoke-client --socket "$SOCKET" --title "$TITLE_A" --stay-ms 20000 >/dev/null 2>&1 &
A_PID=$!

timeout 5 bash -c "until rg -q 'Place: $TITLE_A ' '$LOG'; do sleep 0.05; done"
read -r AX AY < <(parse_place_xy "$TITLE_A")

./fbwl-input-injector --socket "$SOCKET" click "$CX2" "$CY2" >/dev/null 2>&1 || true
./fbwl-smoke-client --socket "$SOCKET" --title "$TITLE_B" --stay-ms 20000 >/dev/null 2>&1 &
B_PID=$!

timeout 5 bash -c "until rg -q 'Place: $TITLE_B ' '$LOG'; do sleep 0.05; done"
read -r BX BY < <(parse_place_xy "$TITLE_B")

# Mark window A with placeholder key "1" via KeyMode + Arg.
./fbwl-input-injector --socket "$SOCKET" click "$((AX + 10))" "$((AY + 10))" >/dev/null 2>&1 || true

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-i
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'KeyMode: set to Mark'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" type 1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'MarkWindow: keycode='; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'KeyMode: set to default'; do sleep 0.05; done"

# Switch focus to window B and go back to the marked window.
./fbwl-input-injector --socket "$SOCKET" click "$((BX + 10))" "$((BY + 10))" >/dev/null 2>&1 || true

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-m
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'KeyMode: set to Goto'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" type 1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'GotoMarkedWindow: keycode='; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Policy: focus \\(marked-window\\) title=$TITLE_A '; do sleep 0.05; done"

# Head movement: move window B between the two outputs.
./fbwl-input-injector --socket "$SOCKET" click "$((BX + 10))" "$((BY + 10))" >/dev/null 2>&1 || true

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q \"Head: move title=$TITLE_B head=1 .* reason=sendtonexthead\"; do sleep 0.05; done"
LINE_MOVE=$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 "Head: move title=$TITLE_B head=1 " || true)
if [[ "$LINE_MOVE" =~ x=([-0-9]+)\ y=([-0-9]+)\  ]]; then
  MX="${BASH_REMATCH[1]}"
  MY="${BASH_REMATCH[2]}"
  if ! (( MX >= X1 && MX < X1 + W1 && MY >= Y1 && MY < Y1 + H1 )); then
    echo "window $TITLE_B moved outside output1: $LINE_MOVE (box=$X1,$Y1 ${W1}x${H1})" >&2
    exit 1
  fi
else
  echo "failed to parse head move line: $LINE_MOVE" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f2
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q \"Head: move title=$TITLE_B head=2 .* reason=sendtoprevhead\"; do sleep 0.05; done"
LINE_MOVE=$(tail -c +$((OFFSET + 1)) "$LOG" | rg -m1 "Head: move title=$TITLE_B head=2 " || true)
if [[ "$LINE_MOVE" =~ x=([-0-9]+)\ y=([-0-9]+)\  ]]; then
  MX="${BASH_REMATCH[1]}"
  MY="${BASH_REMATCH[2]}"
  if ! (( MX >= X2 && MX < X2 + W2 && MY >= Y2 && MY < Y2 + H2 )); then
    echo "window $TITLE_B moved outside output2: $LINE_MOVE (box=$X2,$Y2 ${W2}x${H2})" >&2
    exit 1
  fi
else
  echo "failed to parse head move line: $LINE_MOVE" >&2
  exit 1
fi

# Explicit SetHead to head 1 then 2.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-escape
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q \"Head: move title=$TITLE_B head=1 .* reason=sethead\"; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-return
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q \"Head: move title=$TITLE_B head=2 .* reason=sethead\"; do sleep 0.05; done"

echo "ok: keybinding head + mark smoke passed (socket=$SOCKET log=$LOG)"
