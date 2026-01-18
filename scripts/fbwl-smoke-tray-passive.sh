#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd awk
need_cmd date
need_cmd dbus-run-session
need_cmd rg
need_cmd timeout

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ ! -x ./fluxbox-wayland ]]; then
  echo "missing ./fluxbox-wayland (build first)" >&2
  exit 1
fi
if [[ ! -x ./fbwl-remote ]]; then
  echo "missing ./fbwl-remote (build first)" >&2
  exit 1
fi
if [[ ! -x ./fbwl-sni-item-client ]]; then
  echo "missing ./fbwl-sni-item-client (build first)" >&2
  exit 1
fi

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-tray-passive-$UID-$$.log}"
ICON_RGB="${ICON_RGB:-#ff0000}"
: >"$LOG"

ROOT="$ROOT" XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" SOCKET="$SOCKET" LOG="$LOG" ICON_RGB="$ICON_RGB" \
dbus-run-session -- bash -c '
  set -euo pipefail

  cd "$ROOT"
  : >"$LOG"

  WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
    --no-xwayland \
    --socket "$SOCKET" \
    --workspaces 2 \
    >"$LOG" 2>&1 &
  FBW_PID=$!

  cleanup() {
    if [[ -n "${ITEM_PID:-}" ]]; then kill "$ITEM_PID" 2>/dev/null || true; fi
    if [[ -n "${FBW_PID:-}" ]]; then
      kill "$FBW_PID" 2>/dev/null || true
      wait "$FBW_PID" 2>/dev/null || true
    fi
    wait 2>/dev/null || true
  }
  trap cleanup EXIT

  timeout 5 bash -c "until rg -q \"Running fluxbox-wayland\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"SNI: watcher enabled\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"Toolbar: position \" \"$LOG\"; do sleep 0.05; done"

  ./fbwl-sni-item-client \
    --item-path /fbwl/TestItem \
    --status Passive \
    --icon-rgba "$ICON_RGB" \
    --icon-size 16 \
    --stay-ms 8000 \
    >/dev/null 2>&1 &
  ITEM_PID=$!

  timeout 5 bash -c "until rg -q \"SNI: item registered id=.*fbwl/TestItem\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"SNI: status updated id=.*fbwl/TestItem status=Passive\" \"$LOG\"; do sleep 0.05; done"

  status_line="$(rg -n -m1 "SNI: status updated id=.*fbwl/TestItem status=Passive" "$LOG" | cut -d: -f1)"
  if [[ -z "$status_line" ]]; then
    echo "failed to locate status update line" >&2
    exit 1
  fi

  end=$(( $(date +%s) + 5 ))
  while true; do
    line="$(rg -n "Toolbar: built " "$LOG" | rg " tray=0 " | tail -n1 | cut -d: -f1 || true)"
    if [[ -n "$line" && "$line" -gt "$status_line" ]]; then
      break
    fi
    if (( $(date +%s) >= end )); then
      echo "timeout waiting for tray=0 rebuild after status update" >&2
      exit 1
    fi
    sleep 0.05
  done

  ./fbwl-remote --socket "$SOCKET" quit | rg -q "^ok quitting$"
  timeout 5 bash -c "while kill -0 \"$FBW_PID\" 2>/dev/null; do sleep 0.05; done"
  wait "$FBW_PID"
  unset FBW_PID

  wait "$ITEM_PID" 2>/dev/null || true
  unset ITEM_PID

  echo "ok: tray passive-status smoke passed (socket=$SOCKET log=$LOG)"
'
