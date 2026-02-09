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
if [[ ! -x ./fbwl-sni-item-client ]]; then
  echo "missing ./fbwl-sni-item-client (build first)" >&2
  exit 1
fi

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

ROOT="$ROOT" dbus-run-session -- bash -s <<'EOF'
set -euo pipefail

run_case() (
  set -euo pipefail

  local label="$1"
  local key_mode="$2" # systray|alias

  ROOT="$ROOT"
  SOCKET="wayland-fbwl-tray-pin-$UID-$label-$$"
  LOG="/tmp/fluxbox-wayland-tray-pin-$UID-$label-$$.log"
  CFG_DIR="/tmp/fbwl-config-tray-pin-$UID-$label-$$"
  : >"$LOG"

  cleanup() {
    rm -rf "$CFG_DIR" 2>/dev/null || true
    if [[ -n "${PID_ALPHA:-}" ]]; then kill "$PID_ALPHA" 2>/dev/null || true; fi
    if [[ -n "${PID_BETA:-}" ]]; then kill "$PID_BETA" 2>/dev/null || true; fi
    if [[ -n "${PID_GAMMA:-}" ]]; then kill "$PID_GAMMA" 2>/dev/null || true; fi
    if [[ -n "${FBW_PID:-}" ]]; then
      kill "$FBW_PID" 2>/dev/null || true
      wait "$FBW_PID" 2>/dev/null || true
    fi
    wait 2>/dev/null || true
  }
  trap cleanup EXIT

  mkdir -p "$CFG_DIR"

  if [[ "$key_mode" == "systray" ]]; then
    cat >"$CFG_DIR/init" <<EOT
session.screen0.systray.pinLeft: gamma
session.screen0.systray.pinRight: alpha
EOT
  elif [[ "$key_mode" == "alias" ]]; then
    cat >"$CFG_DIR/init" <<EOT
session.screen0.pinLeft: gamma
session.screen0.pinRight: alpha
EOT
  else
    echo "unknown key_mode: $key_mode" >&2
    exit 1
  fi

  WLR_BACKENDS=headless WLR_RENDERER=pixman "$ROOT/fluxbox-wayland" \
    --no-xwayland \
    --socket "$SOCKET" \
    --workspaces 1 \
    --config-dir "$CFG_DIR" \
    >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q \"Running fluxbox-wayland\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"SNI: watcher enabled\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q \"Toolbar: position \" \"$LOG\"; do sleep 0.05; done"

  "$ROOT/fbwl-sni-item-client" --item-path /fbwl/Alpha --id Alpha --stay-ms 8000 >/dev/null 2>&1 &
  PID_ALPHA=$!
  "$ROOT/fbwl-sni-item-client" --item-path /fbwl/Beta --id Beta --stay-ms 8000 >/dev/null 2>&1 &
  PID_BETA=$!
  "$ROOT/fbwl-sni-item-client" --item-path /fbwl/Gamma --id Gamma --stay-ms 8000 >/dev/null 2>&1 &
  PID_GAMMA=$!

  timeout 5 bash -c "until rg -qi \"SNI: item id updated .*item_id=alpha\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -qi \"SNI: item id updated .*item_id=beta\" \"$LOG\"; do sleep 0.05; done"
  timeout 5 bash -c "until rg -qi \"SNI: item id updated .*item_id=gamma\" \"$LOG\"; do sleep 0.05; done"

  timeout 5 bash -c "until rg -q \"Toolbar: tray item idx=2\" \"$LOG\"; do sleep 0.05; done"

  L0="$(rg "Toolbar: tray item idx=0" "$LOG" | tail -n 1)"
  L1="$(rg "Toolbar: tray item idx=1" "$LOG" | tail -n 1)"
  L2="$(rg "Toolbar: tray item idx=2" "$LOG" | tail -n 1)"

  echo "$L0" | rg -qi "item_id=gamma"
  echo "$L1" | rg -qi "item_id=beta"
  echo "$L2" | rg -qi "item_id=alpha"

  kill "$FBW_PID"
  timeout 5 bash -c "while kill -0 \"$FBW_PID\" 2>/dev/null; do sleep 0.05; done"
  wait "$FBW_PID"
  unset FBW_PID

  echo "ok: tray pin ($key_mode) smoke passed (socket=$SOCKET log=$LOG)"
)

run_case "systray" "systray"
run_case "alias" "alias"

echo "ok: tray pin smoke passed"
EOF
