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

SOCKET="${SOCKET:-wayland-fbwl-default-deco-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-default-deco-$UID-$$.log}"
CFGDIR="$(mktemp -d)"

cleanup() {
  if [[ -n "${CLIENT_PID:-}" ]]; then kill "$CLIENT_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
  rm -rf "$CFGDIR"
}
trap cleanup EXIT

cat >"$CFGDIR/init" <<EOF
session.screen0.defaultDeco: NONE
EOF

: >"$LOG"
WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Init: defaultDeco=NONE mapped_ssd=0' '$LOG'; do sleep 0.05; done"

TITLE="client-nodeco"
./fbwl-smoke-client --socket "$SOCKET" --title "$TITLE" --stay-ms 10000 --xdg-decoration >/dev/null 2>&1 &
CLIENT_PID=$!

timeout 5 bash -c "until rg -q 'Surface size: $TITLE ' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: $TITLE ' '$LOG'; do sleep 0.05; done"

if timeout 2 bash -c "until rg -q 'Decor: title-render $TITLE' '$LOG'; do sleep 0.05; done"; then
  echo "unexpected decorations for $TITLE (defaultDeco=NONE)" >&2
  exit 1
fi

echo "ok: defaultDeco smoke passed (socket=$SOCKET log=$LOG)"
