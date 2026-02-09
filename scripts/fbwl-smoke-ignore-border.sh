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

run_case() {
  local ignore_border="$1"
  local expect_move="$2"
  local socket="$3"
  local log="$4"
  local title="$5"
  local cfgdir
  cfgdir="$(mktemp -d "/tmp/fbwl-ignore-border-$UID-XXXXXX")"

  : >"$log"

  cat >"$cfgdir/init" <<EOF
session.ignoreBorder: $ignore_border
session.keyFile: keys
EOF

  cat >"$cfgdir/keys" <<'EOF'
OnWindowBorder Move1 :StartMoving
EOF

  local fbw_pid="" client_pid=""
  cleanup_case() {
    rm -rf "$cfgdir" 2>/dev/null || true
    if [[ -n "$client_pid" ]]; then kill "$client_pid" 2>/dev/null || true; fi
    if [[ -n "$fbw_pid" ]]; then kill "$fbw_pid" 2>/dev/null || true; fi
    wait 2>/dev/null || true
  }
  trap cleanup_case RETURN

  WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
    --no-xwayland \
    --socket "$socket" \
    --config-dir "$cfgdir" \
    >"$log" 2>&1 &
  fbw_pid=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$log'; do sleep 0.05; done"

  ./fbwl-smoke-client --socket "$socket" --title "$title" --stay-ms 10000 --xdg-decoration >/dev/null 2>&1 &
  client_pid=$!

  timeout 5 bash -c "until rg -q 'Surface size: $title 32x32' '$log'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Place: $title ' '$log'; do sleep 0.05; done"

  local place_line size_line
  place_line="$(rg -m1 "Place: $title " "$log")"
  if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
    local x0="${BASH_REMATCH[1]}"
    local y0="${BASH_REMATCH[2]}"
  else
    echo "failed to parse Place line: $place_line" >&2
    return 1
  fi

  size_line="$(rg -m1 "Surface size: $title " "$log")"
  if [[ "$size_line" =~ ([0-9]+)x([0-9]+) ]]; then
    local w0="${BASH_REMATCH[1]}"
    local h0="${BASH_REMATCH[2]}"
  else
    echo "failed to parse Surface size line: $size_line" >&2
    return 1
  fi

  local offset start_x start_y end_x end_y
  offset=$(wc -c <"$log" | tr -d ' ')
  start_x=$((x0 + w0 + 1))
  start_y=$((y0 + 10))
  end_x=$((start_x + 100))
  end_y=$((start_y + 100))

  ./fbwl-input-injector --socket "$socket" drag-left "$start_x" "$start_y" "$end_x" "$end_y"

  if [[ "$expect_move" == "1" ]]; then
    local x1 y1
    x1=$((x0 + 100))
    y1=$((y0 + 100))
    tail -c +$((offset + 1)) "$log" | rg -q "Move: $title x=$x1 y=$y1"
  else
    if tail -c +$((offset + 1)) "$log" | rg -q "Move: $title "; then
      echo "unexpected move while session.ignoreBorder=$ignore_border" >&2
      return 1
    fi
  fi
}

SOCKET_BASE="${SOCKET_BASE:-wayland-fbwl-test-$UID-$$}"
LOG_BASE="${LOG_BASE:-/tmp/fluxbox-wayland-ignore-border-$UID-$$}"

run_case false 1 "${SOCKET_BASE}-off" "${LOG_BASE}-off.log" "client-ib-off"
run_case true 0 "${SOCKET_BASE}-on" "${LOG_BASE}-on.log" "client-ib-on"

echo "ok: ignoreBorder smoke passed (log_base=$LOG_BASE)"
