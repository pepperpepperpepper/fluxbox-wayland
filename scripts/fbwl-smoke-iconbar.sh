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
LOG="${LOG:-/tmp/fluxbox-wayland-iconbar-$UID-$$.log}"

cleanup() {
  if [[ -n "${A_PID:-}" ]]; then kill "$A_PID" 2>/dev/null || true; fi
  if [[ -n "${B_PID:-}" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --workspaces 2 \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: position ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: clock text=' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --title ib-a --stay-ms 10000 >/dev/null 2>&1 &
A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --title ib-b --stay-ms 10000 >/dev/null 2>&1 &
B_PID=$!

timeout 5 bash -c "until rg -q 'Focus: ib-a' '$LOG' && rg -q 'Focus: ib-b' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Toolbar: iconbar item .*title=ib-a' '$LOG' && rg -q 'Toolbar: iconbar item .*title=ib-b' '$LOG'; do sleep 0.05; done"

FOCUSED_VIEW=$(
  rg -o 'Focus: ib-[ab]' "$LOG" \
    | tail -n 1 \
    | awk '{print $2}'
)

case "$FOCUSED_VIEW" in
  ib-a) OTHER_VIEW=ib-b ;;
  ib-b) OTHER_VIEW=ib-a ;;
  *) echo "failed to determine focused view (got: $FOCUSED_VIEW)" >&2; exit 1 ;;
esac

pos_line="$(rg -m1 'Toolbar: position ' "$LOG")"
if [[ "$pos_line" =~ x=([-0-9]+)\ y=([-0-9]+)\ h=([0-9]+)\ cell_w=([0-9]+)\ workspaces=([0-9]+) ]]; then
  X0="${BASH_REMATCH[1]}"
  Y0="${BASH_REMATCH[2]}"
  H="${BASH_REMATCH[3]}"
else
  echo "failed to parse Toolbar: position line: $pos_line" >&2
  exit 1
fi

item_line="$(rg "Toolbar: iconbar item .*title=$OTHER_VIEW" "$LOG" | tail -n 1)"
if [[ "$item_line" =~ lx=([-0-9]+)\ w=([0-9]+)\ title= ]]; then
  LX="${BASH_REMATCH[1]}"
  W="${BASH_REMATCH[2]}"
else
  echo "failed to parse Toolbar: iconbar item line: $item_line" >&2
  exit 1
fi

CLICK_X=$((X0 + LX + W / 2))
CLICK_Y=$((Y0 + H / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Toolbar: click iconbar idx=[0-9]+ title=$OTHER_VIEW"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Focus: $OTHER_VIEW"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-i
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Minimize: $OTHER_VIEW on reason=keybinding"

timeout 5 bash -c "until rg -q \"Toolbar: iconbar item .*title=$OTHER_VIEW minimized=1\" '$LOG'; do sleep 0.05; done"

item_line="$(rg "Toolbar: iconbar item .*title=$OTHER_VIEW" "$LOG" | tail -n 1)"
if [[ "$item_line" =~ lx=([-0-9]+)\ w=([0-9]+)\ title= ]]; then
  LX="${BASH_REMATCH[1]}"
  W="${BASH_REMATCH[2]}"
else
  echo "failed to parse Toolbar: iconbar item line after minimize: $item_line" >&2
  exit 1
fi

CLICK_X=$((X0 + LX + W / 2))
CLICK_Y=$((Y0 + H / 2))

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" click "$CLICK_X" "$CLICK_Y"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Minimize: $OTHER_VIEW off reason=toolbar-iconbar"
tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Focus: $OTHER_VIEW"

echo "ok: iconbar smoke passed (socket=$SOCKET log=$LOG)"
