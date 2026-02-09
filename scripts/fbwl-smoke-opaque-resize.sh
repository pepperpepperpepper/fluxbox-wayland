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
  local label="$1"
  local opaque_resize="$2"
  local resize_delay_ms="$3"
  local expect_apply_delay="$4"

  local socket="wayland-fbwl-test-$UID-$$-$label"
  local log="/tmp/fluxbox-wayland-opaque-resize-$UID-$$-$label.log"
  local cfgdir
  cfgdir="$(mktemp -d "/tmp/fbwl-opaque-resize-$UID-$label-XXXXXX")"

  local fbw_pid=""
  local client_pid=""

  cleanup_case() {
    rm -rf "$cfgdir" 2>/dev/null || true
    if [[ -n "$client_pid" ]]; then kill "$client_pid" 2>/dev/null || true; fi
    if [[ -n "$fbw_pid" ]]; then kill "$fbw_pid" 2>/dev/null || true; fi
    wait 2>/dev/null || true
  }
  trap cleanup_case RETURN

  : >"$log"

  cat >"$cfgdir/init" <<EOF
session.screen0.toolbar.visible: false
session.screen0.opaqueResize: $opaque_resize
session.screen0.opaqueResizeDelay: $resize_delay_ms
EOF

  WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
    --no-xwayland \
    --socket "$socket" \
    --config-dir "$cfgdir" \
    >"$log" 2>&1 &
  fbw_pid=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$log'; do sleep 0.05; done"

  ./fbwl-smoke-client --socket "$socket" --title "client-$label" --stay-ms 10000 >/dev/null 2>&1 &
  client_pid=$!

  timeout 5 bash -c "until rg -q 'Surface size: client-$label [0-9]+x[0-9]+' '$log'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Place: client-$label ' '$log'; do sleep 0.05; done"

  place_line="$(rg -m1 "Place: client-$label " "$log")"
  if [[ "$place_line" =~ x=([-0-9]+)\ y=([-0-9]+) ]]; then
    X0="${BASH_REMATCH[1]}"
    Y0="${BASH_REMATCH[2]}"
  else
    echo "failed to parse Place line: $place_line" >&2
    exit 1
  fi

  size_line="$(rg -m1 "Surface size: client-$label " "$log")"
  if [[ "$size_line" =~ ([0-9]+)x([0-9]+) ]]; then
    W0="${BASH_REMATCH[1]}"
    H0="${BASH_REMATCH[2]}"
  else
    echo "failed to parse Surface size line: $size_line" >&2
    exit 1
  fi

  OFFSET=$(wc -c <"$log" | tr -d ' ')
  RESIZE_START_X=$((X0 + 10))
  RESIZE_START_Y=$((Y0 + 10))
  RESIZE_END_X=$((RESIZE_START_X + 50))
  RESIZE_END_Y=$((RESIZE_START_Y + 60))

  ./fbwl-input-injector --socket "$socket" drag-alt-right-hold \
    "$RESIZE_START_X" "$RESIZE_START_Y" "$RESIZE_END_X" "$RESIZE_END_Y" 300

  W1=$((W0 + 50))
  H1=$((H0 + 60))

  chunk="$(tail -c +$((OFFSET + 1)) "$log")"

  if [[ "$expect_apply_delay" == "yes" ]]; then
    apply_line="$(echo "$chunk" | rg -n -m1 "Resize: apply-delay client-$label w=$W1 h=$H1" || true)"
    release_line="$(echo "$chunk" | rg -n -m1 "Resize: client-$label w=$W1 h=$H1" || true)"
    if [[ -z "$apply_line" || -z "$release_line" ]]; then
      echo "missing apply-delay or release log in case $label" >&2
      echo "$chunk" >&2
      exit 1
    fi
    apply_no="${apply_line%%:*}"
    release_no="${release_line%%:*}"
    if ((apply_no >= release_no)); then
      echo "expected apply-delay before release in case $label" >&2
      echo "$chunk" >&2
      exit 1
    fi
  else
    if echo "$chunk" | rg -q "Resize: apply-delay "; then
      echo "unexpected apply-delay log in case $label" >&2
      echo "$chunk" >&2
      exit 1
    fi
  fi

  tail -c +$((OFFSET + 1)) "$log" | rg -q "Resize: client-$label w=$W1 h=$H1"
  timeout 5 bash -c "until rg -q 'Surface size: client-$label ${W1}x${H1}' '$log'; do sleep 0.05; done"

  echo "ok: opaque resize case passed (label=$label)"
}

run_case outline false 50 no
run_case opaque true 50 yes

echo "ok: opaque/outline resize smoke passed"
