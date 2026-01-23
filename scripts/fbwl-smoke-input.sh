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
LOG="${LOG:-/tmp/fluxbox-wayland-input-$UID-$$.log}"
SPAWN_MARK="${SPAWN_MARK:-/tmp/fbwl-terminal-spawned-$UID-$$}"

cleanup() {
  rm -f "$SPAWN_MARK" 2>/dev/null || true
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
rm -f "$SPAWN_MARK"

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --terminal "touch '$SPAWN_MARK'" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title client-a --stay-ms 10000 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title client-b --stay-ms 10000 >/dev/null 2>&1 &
B_PID=$!

timeout 5 bash -c "until rg -q 'Focus: client-a' '$LOG' && rg -q 'Focus: client-b' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: client-a ' '$LOG' && rg -q 'Place: client-b ' '$LOG'; do sleep 0.05; done"

PLACE_A_LINE="$(rg -m1 'Place: client-a ' "$LOG")"
PLACE_B_LINE="$(rg -m1 'Place: client-b ' "$LOG")"

if [[ "$PLACE_A_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  A_X="${BASH_REMATCH[1]}"
  A_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $PLACE_A_LINE" >&2
  exit 1
fi

if [[ "$PLACE_B_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  B_X="${BASH_REMATCH[1]}"
  B_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $PLACE_B_LINE" >&2
  exit 1
fi

OFFSET=$(wc -c <"$LOG" | tr -d ' ')

CURRENT_FOCUS=$(
  rg -o 'Focus: client-[ab]' "$LOG" \
    | tail -n 1 \
    | awk '{print $2}'
)

case "$CURRENT_FOCUS" in
  client-a)
    FIRST_TITLE=client-b
    FIRST_X="$B_X"
    FIRST_Y="$B_Y"
    SECOND_TITLE=client-a
    SECOND_X="$A_X"
    SECOND_Y="$A_Y"
    ;;
  client-b)
    FIRST_TITLE=client-a
    FIRST_X="$A_X"
    FIRST_Y="$A_Y"
    SECOND_TITLE=client-b
    SECOND_X="$B_X"
    SECOND_Y="$B_Y"
    ;;
  *)
    echo "failed to determine current focused client (got: $CURRENT_FOCUS)" >&2
    exit 1
    ;;
esac

./fbwl-input-injector --socket "$SOCKET" click "$((FIRST_X + 10))" "$((FIRST_Y + 10))"
./fbwl-input-injector --socket "$SOCKET" click "$((SECOND_X + 10))" "$((SECOND_Y + 10))"

timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Policy: focus \\(direct\\) title=$FIRST_TITLE'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Policy: focus \\(direct\\) title=$SECOND_TITLE'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: $FIRST_TITLE'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: $SECOND_TITLE'; do sleep 0.05; done"

LAST_FOCUS=$(
  tail -c +$((OFFSET + 1)) "$LOG" \
    | rg -o 'Focus: client-[ab]' \
    | tail -n 1 \
    | awk '{print $2}'
)

case "$LAST_FOCUS" in
  client-a) EXPECT_CYCLE=client-b ;;
  client-b) EXPECT_CYCLE=client-a ;;
  *) echo "failed to determine last focused client (got: $LAST_FOCUS)" >&2; exit 1 ;;
esac

OFFSET=$(wc -c <"$LOG" | tr -d ' ')

./fbwl-input-injector --socket "$SOCKET" key alt-f1

timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Policy: focus \\(cycle\\) title=$EXPECT_CYCLE'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Focus: $EXPECT_CYCLE'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-return

timeout 2 bash -c "until [[ -f '$SPAWN_MARK' ]]; do sleep 0.05; done"

echo "ok: input smoke passed (socket=$SOCKET log=$LOG spawn_mark=$SPAWN_MARK)"
