#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

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
LOG="${LOG:-/tmp/fluxbox-wayland-sni-$UID-$$.log}"
: >"$LOG"

ROOT="$ROOT" XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" SOCKET="$SOCKET" LOG="$LOG" \
dbus-run-session -- bash -c '
  set -euo pipefail

  cd "$ROOT"

  : >"$LOG"

  WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
    --no-xwayland \
    --socket "$SOCKET" \
    >"$LOG" 2>&1 &
  FBW_PID=$!

  cleanup() {
    if [[ -n "${FBW_PID:-}" ]]; then
      kill "$FBW_PID" 2>/dev/null || true
      wait "$FBW_PID" 2>/dev/null || true
    fi
  }
  trap cleanup EXIT

  timeout 5 bash -c "until rg -q \"Running fluxbox-wayland\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"SNI: watcher enabled\" \"$LOG\"; do sleep 0.05; done"

  OUT="$(./fbwl-sni-item-client --item-path /fbwl/TestItem --stay-ms 10)"
  echo "$OUT" | rg -q "^ok sni registered id="
  ID="$(echo "$OUT" | sed -n "s/^ok sni registered id=//p")"
  if [[ -z "$ID" ]]; then
    echo "failed to parse id from client output: $OUT" >&2
    exit 1
  fi

  timeout 5 bash -c "until rg -q \"SNI: item registered id=$ID\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"SNI: item unregistered id=$ID\" \"$LOG\"; do sleep 0.05; done"

  ./fbwl-remote --socket "$SOCKET" quit | rg -q "^ok quitting$"
  timeout 5 bash -c "while kill -0 \"$FBW_PID\" 2>/dev/null; do sleep 0.05; done"
  wait "$FBW_PID"
  unset FBW_PID

  echo "ok: sni smoke passed (id=$ID socket=$SOCKET log=$LOG)"
'
