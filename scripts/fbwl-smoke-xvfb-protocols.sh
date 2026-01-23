#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1 (build first)" >&2; exit 1; }
}

need_cmd rg
need_cmd timeout
need_cmd Xvfb

need_exe ./fluxbox-wayland
need_exe ./fbwl-clipboard-client
need_exe ./fbwl-cursor-shape-client
need_exe ./fbwl-data-control-client
need_exe ./fbwl-dnd-client
need_exe ./fbwl-export-dmabuf-client
need_exe ./fbwl-fractional-scale-client
need_exe ./fbwl-idle-client
need_exe ./fbwl-input-injector
need_exe ./fbwl-input-method-client
need_exe ./fbwl-primary-selection-client
need_exe ./fbwl-relptr-client
need_exe ./fbwl-screencopy-client
need_exe ./fbwl-session-lock-client
need_exe ./fbwl-shortcuts-inhibit-client
need_exe ./fbwl-single-pixel-buffer-client
need_exe ./fbwl-smoke-client
need_exe ./fbwl-text-input-client
need_exe ./fbwl-viewporter-client
need_exe ./fbwl-xdg-activation-client
need_exe ./fbwl-xdg-decoration-client

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

pick_display_num() {
  local base="${1:-99}"
  local d
  for ((d = base; d <= base + 20; d++)); do
    if [[ ! -e "/tmp/.X11-unix/X$d" && ! -e "/tmp/.X${d}-lock" ]]; then
      echo "$d"
      return 0
    fi
  done
  return 1
}

kill_wait() {
  local pid="${1:-}"
  if [[ -z "$pid" ]]; then return 0; fi
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}

DISPLAY_NUM="$(pick_display_num "${DISPLAY_NUM:-99}")"
SOCKET="${SOCKET:-wayland-fbwl-xvfb-proto-$UID-$$}"
XVFB_LOG="${XVFB_LOG:-/tmp/xvfb-proto-$UID-$$.log}"
LOG="${LOG:-/tmp/fluxbox-wayland-xvfb-proto-$UID-$$.log}"
SC_LOG="${SC_LOG:-/tmp/fbwl-screencopy-xvfb-proto-$UID-$$.log}"
BG_COLOR="${BG_COLOR:-#336699}"
SPAWN_MARK="${SPAWN_MARK:-/tmp/fbwl-terminal-spawned-xvfb-$UID-$$}"

cleanup() {
  rm -f "$SPAWN_MARK" 2>/dev/null || true
  kill_wait "${LOCK_PID:-}"
  kill_wait "${VICTIM_PID:-}"
  kill_wait "${INHIBIT_PID:-}"
  kill_wait "${IM_PID:-}"
  kill_wait "${ACT_PID:-}"
  kill_wait "${CUR_PID:-}"
  kill_wait "${REL_PID:-}"
  kill_wait "${DND_SRC_PID:-}"
  kill_wait "${DND_DST_PID:-}"
  kill_wait "${CLIP_SET_PID:-}"
  kill_wait "${PRI_SET_PID:-}"
  kill_wait "${HOLD_PID:-}"
  kill_wait "${DMABUF_VIEW_PID:-}"
  kill_wait "${FBW_PID:-}"
  kill_wait "${XVFB_PID:-}"
}
trap cleanup EXIT

: >"$XVFB_LOG"
: >"$LOG"
: >"$SC_LOG"
rm -f "$SPAWN_MARK"

Xvfb ":$DISPLAY_NUM" -screen 0 1280x720x24 -nolisten tcp -extension GLX >"$XVFB_LOG" 2>&1 &
XVFB_PID=$!

if ! timeout 5 bash -c "until [[ -S /tmp/.X11-unix/X$DISPLAY_NUM ]]; do sleep 0.05; done"; then
  echo "Xvfb failed to start on :$DISPLAY_NUM (log: $XVFB_LOG)" >&2
  tail -n 50 "$XVFB_LOG" >&2 || true
  exit 1
fi

DISPLAY=":$DISPLAY_NUM" WLR_BACKENDS=x11 WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --bg-color "$BG_COLOR" \
  --terminal "touch '$SPAWN_MARK'" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" hold 60000 >/dev/null 2>&1 &
HOLD_PID=$!
timeout 5 bash -c "until rg -q 'New virtual pointer' '$LOG' && rg -q 'New virtual keyboard' '$LOG'; do sleep 0.05; done"

timeout 5 bash -c "until rg -q 'Background: output ' '$LOG'; do sleep 0.05; done"
./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb "$BG_COLOR" --sample-x 1 --sample-y 1 >"$SC_LOG" 2>&1
rg -q '^ok screencopy$' "$SC_LOG"

CLIP_TEXT="fbwl-clipboard-xvfb-$UID-$$"
CLIP_LOG="/tmp/fbwl-clipboard-xvfb-set-$UID-$$.log"
: >"$CLIP_LOG"
./fbwl-clipboard-client --socket "$SOCKET" --set "$CLIP_TEXT" --stay-ms 10000 --timeout-ms 5000 >"$CLIP_LOG" 2>&1 &
CLIP_SET_PID=$!
timeout 5 bash -c "until rg -q '^ok selection_set$' '$CLIP_LOG'; do sleep 0.05; done"
CLIP_OUT="$(./fbwl-clipboard-client --socket "$SOCKET" --get --timeout-ms 5000)"
if [[ "$CLIP_OUT" != "$CLIP_TEXT" ]]; then
  echo "clipboard get mismatch: expected '$CLIP_TEXT' got '$CLIP_OUT'" >&2
  exit 1
fi
kill_wait "$CLIP_SET_PID"
unset CLIP_SET_PID

PRI_TEXT="fbwl-primary-selection-xvfb-$UID-$$"
PRI_LOG="/tmp/fbwl-primary-selection-xvfb-set-$UID-$$.log"
: >"$PRI_LOG"
./fbwl-primary-selection-client --socket "$SOCKET" --set "$PRI_TEXT" --stay-ms 10000 --timeout-ms 5000 >"$PRI_LOG" 2>&1 &
PRI_SET_PID=$!
timeout 5 bash -c "until rg -q '^ok primary_selection_set$' '$PRI_LOG'; do sleep 0.05; done"
PRI_OUT="$(./fbwl-primary-selection-client --socket "$SOCKET" --get --timeout-ms 5000)"
if [[ "$PRI_OUT" != "$PRI_TEXT" ]]; then
  echo "primary selection get mismatch: expected '$PRI_TEXT' got '$PRI_OUT'" >&2
  exit 1
fi
kill_wait "$PRI_SET_PID"
unset PRI_SET_PID

run_data_control_one() {
  local proto="$1"
  local text="fbwl-data-control-xvfb-$proto-$UID-$$"
  local set_log="/tmp/fbwl-data-control-xvfb-set-$proto-$UID-$$.log"
  : >"$set_log"

  ./fbwl-data-control-client --socket "$SOCKET" --protocol "$proto" --set "$text" --stay-ms 10000 --timeout-ms 5000 >"$set_log" 2>&1 &
  local set_pid=$!
  timeout 5 bash -c "until rg -q '^ok selection_set$' '$set_log'; do sleep 0.05; done"

  local out
  out="$(./fbwl-data-control-client --socket "$SOCKET" --protocol "$proto" --get --timeout-ms 5000)"
  if [[ "$out" != "$text" ]]; then
    echo "data-control($proto) get mismatch: expected '$text' got '$out'" >&2
    exit 1
  fi

  kill_wait "$set_pid"
}

run_data_control_one ext
run_data_control_one wlr

CS_LOG="/tmp/fbwl-cursor-shape-xvfb-$UID-$$.log"
: >"$CS_LOG"
./fbwl-cursor-shape-client --socket "$SOCKET" --timeout-ms 5000 >"$CS_LOG" 2>&1 &
CUR_PID=$!
timeout 5 bash -c "until rg -q '^ok ready$' '$CS_LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: fbwl-cursor-shape ' '$LOG'; do sleep 0.05; done"
CS_PLACE_LINE="$(rg -m1 'Place: fbwl-cursor-shape ' "$LOG")"
if [[ "$CS_PLACE_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  CS_X="${BASH_REMATCH[1]}"
  CS_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $CS_PLACE_LINE" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" click "$((CS_X + 10))" "$((CS_Y + 10))" >/dev/null 2>&1
timeout 5 bash -c "while kill -0 $CUR_PID 2>/dev/null; do sleep 0.05; done"
wait "$CUR_PID"
unset CUR_PID
timeout 5 bash -c "until rg -q 'CursorShape: name=' '$LOG'; do sleep 0.05; done"

VP_LOG="/tmp/fbwl-viewporter-xvfb-$UID-$$.log"
: >"$VP_LOG"
./fbwl-viewporter-client --socket "$SOCKET" --timeout-ms 3000 >"$VP_LOG" 2>&1
rg -q '^ok viewporter$' "$VP_LOG"

FS_LOG="/tmp/fbwl-fractional-scale-xvfb-$UID-$$.log"
: >"$FS_LOG"
./fbwl-fractional-scale-client --socket "$SOCKET" --timeout-ms 3000 >"$FS_LOG" 2>&1
rg -q '^ok fractional_scale preferred_scale=[0-9]+$' "$FS_LOG"

XDG_DECOR_LOG="/tmp/fbwl-xdg-decoration-xvfb-$UID-$$.log"
: >"$XDG_DECOR_LOG"
./fbwl-xdg-decoration-client --socket "$SOCKET" --timeout-ms 3000 >"$XDG_DECOR_LOG" 2>&1
rg -q '^ok xdg_decoration server_side$' "$XDG_DECOR_LOG"

IDLE_LOG="/tmp/fbwl-idle-xvfb-$UID-$$.log"
: >"$IDLE_LOG"
./fbwl-idle-client --socket "$SOCKET" --timeout-ms 4000 >"$IDLE_LOG" 2>&1
rg -q '^ok idle_notify idle_inhibit$' "$IDLE_LOG"

./fbwl-single-pixel-buffer-client --socket "$SOCKET" --timeout-ms 2000 >/dev/null 2>&1

ACT_LOG="/tmp/fbwl-xdg-activation-xvfb-$UID-$$.log"
: >"$ACT_LOG"
./fbwl-xdg-activation-client --socket "$SOCKET" --timeout-ms 8000 >"$ACT_LOG" 2>&1 &
ACT_PID=$!
timeout 5 bash -c "until rg -q '^fbwl-xdg-activation-client: ready$' '$ACT_LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: fbwl-activate-a ' '$LOG'; do sleep 0.05; done"

XY=$(
  rg -m 1 'Place: fbwl-activate-a ' "$LOG" \
    | awk '{for(i=1;i<=NF;i++){if($i ~ /^x=/){sub(/^x=/,"",$i); x=$i} if($i ~ /^y=/){sub(/^y=/,"",$i); y=$i}}} END{print x" "y}'
)
if [[ -z "$XY" ]]; then
  echo "failed to parse placement for fbwl-activate-a" >&2
  exit 1
fi
read -r X Y <<<"$XY"
./fbwl-input-injector --socket "$SOCKET" click "$((X+10))" "$((Y+10))" >/dev/null

wait "$ACT_PID"
rg -q '^ok xdg_activation$' "$ACT_LOG"
unset ACT_PID

COMMIT_TEXT="fbwl-ime-xvfb-$UID-$$"
TI_LOG="/tmp/fbwl-text-input-xvfb-$UID-$$.log"
IM_LOG="/tmp/fbwl-input-method-xvfb-$UID-$$.log"
: >"$TI_LOG"
: >"$IM_LOG"

./fbwl-input-method-client --socket "$SOCKET" --timeout-ms 8000 --commit "$COMMIT_TEXT" >"$IM_LOG" 2>&1 &
IM_PID=$!
./fbwl-text-input-client --socket "$SOCKET" --timeout-ms 8000 --title text-input-xvfb --expect-commit "$COMMIT_TEXT" >"$TI_LOG" 2>&1
wait "$IM_PID"
unset IM_PID

rg -q '^ok input-method committed$' "$IM_LOG"
rg -q "^ok text-input commit=${COMMIT_TEXT}\$" "$TI_LOG"

DMABUF_VIEW_LOG="/tmp/fbwl-smoke-export-dmabuf-xvfb-view-$UID-$$.log"
: >"$DMABUF_VIEW_LOG"
./fbwl-smoke-client --socket "$SOCKET" --title export-dmabuf-xvfb --timeout-ms 3000 --stay-ms 10000 >"$DMABUF_VIEW_LOG" 2>&1 &
DMABUF_VIEW_PID=$!
timeout 5 bash -c "until rg -q 'Place: export-dmabuf-xvfb' '$LOG'; do sleep 0.05; done"

DMABUF_LOG="/tmp/fbwl-export-dmabuf-xvfb-$UID-$$.log"
: >"$DMABUF_LOG"
./fbwl-export-dmabuf-client --socket "$SOCKET" --timeout-ms 4000 --allow-cancel >"$DMABUF_LOG" 2>&1
rg -q '^ok export-dmabuf (ready|cancel)' "$DMABUF_LOG"

kill_wait "$DMABUF_VIEW_PID"
unset DMABUF_VIEW_PID

SRC_LOG="/tmp/fbwl-dnd-src-xvfb-$UID-$$.log"
DST_LOG="/tmp/fbwl-dnd-dst-xvfb-$UID-$$.log"
: >"$SRC_LOG"
: >"$DST_LOG"

DND_TEXT="fbwl-dnd-xvfb-$UID-$$"
./fbwl-dnd-client --role src --socket "$SOCKET" --text "$DND_TEXT" --timeout-ms 7000 >"$SRC_LOG" 2>&1 &
DND_SRC_PID=$!
timeout 5 bash -c "until rg -q '^fbwl-dnd-client: ready$' '$SRC_LOG'; do sleep 0.05; done"

./fbwl-dnd-client --role dst --socket "$SOCKET" --text "$DND_TEXT" --timeout-ms 7000 >"$DST_LOG" 2>&1 &
DND_DST_PID=$!
timeout 5 bash -c "until rg -q '^fbwl-dnd-client: ready$' '$DST_LOG'; do sleep 0.05; done"

timeout 5 bash -c "until rg -q 'Place: fbwl-dnd-src ' '$LOG' && rg -q 'Place: fbwl-dnd-dst ' '$LOG'; do sleep 0.05; done"
SRC_PLACE_LINE="$(rg -m1 'Place: fbwl-dnd-src ' "$LOG")"
DST_PLACE_LINE="$(rg -m1 'Place: fbwl-dnd-dst ' "$LOG")"

if [[ "$SRC_PLACE_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  SRC_X="${BASH_REMATCH[1]}"
  SRC_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $SRC_PLACE_LINE" >&2
  exit 1
fi
if [[ "$DST_PLACE_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  DST_X="${BASH_REMATCH[1]}"
  DST_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $DST_PLACE_LINE" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" drag-left "$((SRC_X + 10))" "$((SRC_Y + 10))" "$((DST_X + 10))" "$((DST_Y + 10))" >/dev/null

wait "$DND_DST_PID"
unset DND_DST_PID
rg -q '^ok dnd$' "$DST_LOG"
wait "$DND_SRC_PID"
unset DND_SRC_PID
rg -q '^ok dnd source$' "$SRC_LOG"

REL_LOG="/tmp/fbwl-relptr-xvfb-$UID-$$.log"
: >"$REL_LOG"
./fbwl-relptr-client --socket "$SOCKET" --timeout-ms 8000 >"$REL_LOG" 2>&1 &
REL_PID=$!
timeout 5 bash -c "until rg -q '^fbwl-relptr-client: ready$' '$REL_LOG'; do sleep 0.05; done"

timeout 5 bash -c "until rg -q 'Place: fbwl-relptr-client ' '$LOG'; do sleep 0.05; done"
REL_PLACE_LINE="$(rg -m1 'Place: fbwl-relptr-client ' "$LOG")"
if [[ "$REL_PLACE_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  REL_X="${BASH_REMATCH[1]}"
  REL_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $REL_PLACE_LINE" >&2
  exit 1
fi

REL_CLICK_X=$((REL_X + 10))
REL_CLICK_Y=$((REL_Y + 10))
REL_DRAG_END_X=$((REL_CLICK_X + 40))
REL_DRAG_END_Y=$((REL_CLICK_Y + 40))

./fbwl-input-injector --socket "$SOCKET" click "$REL_CLICK_X" "$REL_CLICK_Y" >/dev/null
timeout 5 bash -c "until rg -q '^ok locked$' '$REL_LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" drag-left "$REL_CLICK_X" "$REL_CLICK_Y" "$REL_DRAG_END_X" "$REL_DRAG_END_Y" >/dev/null

wait "$REL_PID"
unset REL_PID
rg -q '^ok relative$' "$REL_LOG"

INHIBIT_LOG="/tmp/fbwl-shortcuts-inhibit-xvfb-$UID-$$.log"
: >"$INHIBIT_LOG"
rm -f "$SPAWN_MARK"

./fbwl-shortcuts-inhibit-client --socket "$SOCKET" --timeout-ms 5000 --stay-ms 10000 >"$INHIBIT_LOG" 2>&1 &
INHIBIT_PID=$!
timeout 5 bash -c "until rg -q '^ok shortcuts-inhibit active$' '$INHIBIT_LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" key alt-return >/dev/null 2>&1
for _ in {1..20}; do
  if [[ -f "$SPAWN_MARK" ]]; then
    echo "unexpected: terminal command executed while shortcuts were inhibited (spawn_mark=$SPAWN_MARK)" >&2
    exit 1
  fi
  sleep 0.05
done

kill_wait "$INHIBIT_PID"
unset INHIBIT_PID

./fbwl-input-injector --socket "$SOCKET" key alt-return >/dev/null 2>&1
timeout 2 bash -c "until [[ -f '$SPAWN_MARK' ]]; do sleep 0.05; done"

VICTIM_LOG="/tmp/fbwl-session-lock-victim-xvfb-$UID-$$.log"
: >"$VICTIM_LOG"
VICTIM_TITLE="victim-xvfb"
./fbwl-smoke-client --socket "$SOCKET" --title "$VICTIM_TITLE" --stay-ms 9000 >"$VICTIM_LOG" 2>&1 &
VICTIM_PID=$!
timeout 5 bash -c "until rg -q \"Place: $VICTIM_TITLE \" '$LOG'; do sleep 0.05; done"

VICTIM_PLACE_LINE="$(rg -m1 "Place: $VICTIM_TITLE " "$LOG")"
if [[ "$VICTIM_PLACE_LINE" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
  VICTIM_X="${BASH_REMATCH[1]}"
  VICTIM_Y="${BASH_REMATCH[2]}"
else
  echo "failed to parse Place line: $VICTIM_PLACE_LINE" >&2
  exit 1
fi

./fbwl-input-injector --socket "$SOCKET" click "$((VICTIM_X + 10))" "$((VICTIM_Y + 10))" >/dev/null 2>&1
timeout 5 bash -c "until rg -q \"Pointer press .*hit=$VICTIM_TITLE\" '$LOG'; do sleep 0.05; done"

LOCK_LOG="/tmp/fbwl-session-lock-xvfb-$UID-$$.log"
: >"$LOCK_LOG"
./fbwl-session-lock-client --socket "$SOCKET" --timeout-ms 6000 --locked-ms 800 >"$LOCK_LOG" 2>&1 &
LOCK_PID=$!
timeout 5 bash -c "until rg -q '^ok session-lock locked$' '$LOCK_LOG'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" click "$((VICTIM_X + 10))" "$((VICTIM_Y + 10))" >/dev/null 2>&1
timeout 5 bash -c "until rg -q 'Pointer press .*hit=\\(none\\)' '$LOG'; do sleep 0.05; done"

wait "$LOCK_PID"
unset LOCK_PID
rg -q '^ok session-lock unlocked$' "$LOCK_LOG"

./fbwl-input-injector --socket "$SOCKET" click "$((VICTIM_X + 10))" "$((VICTIM_Y + 10))" >/dev/null 2>&1
timeout 5 bash -c "until [[ \$(rg -c \"Pointer press .*hit=$VICTIM_TITLE\" '$LOG') -ge 2 ]]; do sleep 0.05; done"

echo "ok: xvfb+x11 backend protocols smoke passed (DISPLAY=:$DISPLAY_NUM socket=$SOCKET log=$LOG xvfb_log=$XVFB_LOG)"
