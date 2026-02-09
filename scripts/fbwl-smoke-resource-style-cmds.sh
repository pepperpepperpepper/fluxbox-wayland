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
LOG="${LOG:-/tmp/fluxbox-wayland-resource-style-cmds-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-config-resource-style-$UID-XXXXXX")"

STYLE1_DIR="$CFGDIR/style1"
STYLE2_DIR="$CFGDIR/style2"
STYLE1_CFG="$STYLE1_DIR/theme.cfg"
STYLE2_CFG="$STYLE2_DIR/theme.cfg"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

mkdir -p "$STYLE1_DIR" "$STYLE2_DIR"

cat >"$STYLE1_CFG" <<'EOF'
borderWidth: 1
window.title.height: 20
EOF

cat >"$STYLE2_CFG" <<'EOF'
borderWidth: 10
window.title.height: 40
EOF

cat >"$CFGDIR/init" <<EOF
session.styleFile: $STYLE1_DIR
EOF

cat >"$CFGDIR/keys" <<EOF
Mod1 F1 :ReloadStyle
Mod1 F2 :SetStyle $STYLE2_DIR
Mod1 f :SetResourceValue session.screen0.focusModel StrictMouseFocus
Mod1 m :SetResourceValueDialog
Mod1 i :SaveRC
EOF

WLR_BACKENDS="${WLR_BACKENDS:-headless}" WLR_RENDERER="${WLR_RENDERER:-pixman}" ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q \"Style: loaded $STYLE1_CFG \\(border=1 title_h=20\\)\" '$LOG'; do sleep 0.05; done"

# SaveRC should persist runtime state back into init.
if rg -q '^session\\.screen0\\.focusModel:' "$CFGDIR/init"; then
  echo "unexpected: init already had focusModel before SaveRC" >&2
  exit 1
fi
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-i
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'SaveRC: ok'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q '^session\\.screen0\\.focusModel:' '$CFGDIR/init'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q '^session\\.autoRaiseDelay:' '$CFGDIR/init'; do sleep 0.05; done"

# ReloadStyle should re-read the current style from disk.
cat >"$STYLE1_CFG" <<'EOF'
borderWidth: 3
window.title.height: 30
EOF
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f1
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q \"Style: loaded $STYLE1_CFG \\(border=3 title_h=30\\)\"; do sleep 0.05; done"

# SetStyle should switch to a new style and persist session.styleFile.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f2
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q \"Style: loaded $STYLE2_CFG \\(border=10 title_h=40\\)\"; do sleep 0.05; done"
timeout 5 bash -c "until rg -q \"^session\\.styleFile:\\s*$STYLE2_DIR\\s*$\" '$CFGDIR/init'; do sleep 0.05; done"

# SetResourceValue should update init and apply via reconfigure.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-f
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Reconfigure: reloaded init'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q '^session\\.screen0\\.focusModel:\\s*StrictMouseFocus\\s*$' '$CFGDIR/init'; do sleep 0.05; done"

# SetResourceValueDialog should prompt and accept \"name value\" input.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-input-injector --socket "$SOCKET" key alt-m
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'CmdDialog: open'; do sleep 0.05; done"

./fbwl-input-injector --socket "$SOCKET" type "session.screen0.focusModel MouseFocus"
./fbwl-input-injector --socket "$SOCKET" key enter
timeout 5 bash -c "until rg -q '^session\\.screen0\\.focusModel:\\s*MouseFocus\\s*$' '$CFGDIR/init'; do sleep 0.05; done"

echo "ok: resource/style command parity smoke passed (socket=$SOCKET log=$LOG cfgdir=$CFGDIR)"
