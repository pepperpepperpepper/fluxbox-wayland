#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_exe() {
  [[ -x "$1" ]] || { echo "missing required executable: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_exe ./fbwl-input-injector

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

SOCKET="${SOCKET:-wayland-fbwl-apps-deco-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-apps-deco-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-apps-deco-$UID-XXXXXX")"
APPS_FILE="$(mktemp "/tmp/fbwl-apps-deco-$UID-XXXXXX.apps")"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  rm -f "$APPS_FILE" 2>/dev/null || true
  if [[ -n "${TAB_A_PID:-}" ]]; then kill "$TAB_A_PID" 2>/dev/null || true; fi
  if [[ -n "${TAB_B_PID:-}" ]]; then kill "$TAB_B_PID" 2>/dev/null || true; fi
  if [[ -n "${PID:-}" ]]; then kill "$PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

# Keep the titlebar layout deterministic and minimal: only a Minimize button.
cat >"$CFGDIR/init" <<'EOF'
session.titlebar.left:
session.titlebar.right: Minimize
EOF

cat >"$APPS_FILE" <<'EOF'
[app] (app_id=deco-tool)
  [Deco] {TOOL}
[end]

[app] (app_id=deco-tiny)
  [Deco] {TINY}
[end]

[app] (app_id=deco-border)
  [Deco] {BORDER}
[end]

[app] (app_id=deco-none)
  [Deco] {NONE}
[end]

[group]
  [app] (app_id=deco-tab-a)
  [app] (app_id=deco-tab-b)
  [Deco] {TAB}
[end]

[app] (app_id=deco-save)
  [Deco] {0x1001}
  [Close] {yes}
[end]
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  --apps "$APPS_FILE" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

TITLE_H=24
BTN_MARGIN=4
BTN_SIZE=$((TITLE_H - 2 * BTN_MARGIN))

spawn_one() {
  local app_id="$1"
  local title="$2"
  OFFSET=$(wc -c <"$LOG" | tr -d ' ')
  ./fbwl-smoke-client --socket "$SOCKET" --app-id "$app_id" --title "$title" --stay-ms 20000 --xdg-decoration --width 200 --height 120 >/dev/null 2>&1 &
  PID=$!
  START=$((OFFSET + 1))
  timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=$app_id .*'; do sleep 0.05; done"
  timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Surface size: $title '; do sleep 0.05; done"
  timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Place: $title '; do sleep 0.05; done"
}

kill_one() {
  if [[ -n "${PID:-}" ]]; then
    kill "$PID" 2>/dev/null || true
    PID=""
  fi
}

parse_place_xy() {
  local title="$1"
  local line
  line="$(rg -m1 "Place: $title " "$LOG")"
  if [[ "$line" =~ x=([-0-9]+)\ y=([-0-9]+)\  ]]; then
    echo "${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"
    return 0
  fi
  echo "failed to parse Place line: $line" >&2
  return 1
}

parse_surface_size_wh() {
  local title="$1"
  local line
  line="$(rg -m1 "Surface size: $title " "$LOG")"
  if [[ "$line" =~ ([0-9]+)x([0-9]+)$ ]]; then
    echo "${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"
    return 0
  fi
  echo "failed to parse Surface size line: $line" >&2
  return 1
}

click_minimize_button() {
  local title="$1"
  local xy wh
  xy="$(parse_place_xy "$title")"
  wh="$(parse_surface_size_wh "$title")"
  local x0 y0 w0 h0
  x0="$(awk '{print $1}' <<<"$xy")"
  y0="$(awk '{print $2}' <<<"$xy")"
  w0="$(awk '{print $1}' <<<"$wh")"
  h0="$(awk '{print $2}' <<<"$wh")"
  : "$h0" >/dev/null

  local cx cy
  cx=$((x0 + w0 - BTN_MARGIN - BTN_SIZE / 2))
  cy=$((y0 - TITLE_H + BTN_MARGIN + BTN_SIZE / 2))
  ./fbwl-input-injector --socket "$SOCKET" click "$cx" "$cy"
}

# TOOL: titlebar yes, Minimize button suppressed.
spawn_one "deco-tool" "deco-tool"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=deco-tool .*deco=0x1 '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Decor: title-render deco-tool'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_minimize_button "deco-tool"
sleep 0.2
if tail -c +$((OFFSET + 1)) "$LOG" | rg -q "Minimize: deco-tool on reason=decor-button"; then
  echo "unexpected minimize via titlebar in TOOL deco" >&2
  exit 1
fi
kill_one

# TINY: titlebar yes, Minimize button enabled.
spawn_one "deco-tiny" "deco-tiny"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=deco-tiny .*deco=0x9 '; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Decor: title-render deco-tiny'; do sleep 0.05; done"

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
click_minimize_button "deco-tiny"
timeout 5 bash -c "until tail -c +$((OFFSET + 1)) '$LOG' | rg -q 'Minimize: deco-tiny on reason=decor-button'; do sleep 0.05; done"
kill_one

# BORDER: no titlebar/title text.
spawn_one "deco-border" "deco-border"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=deco-border .*deco=0x4 '; do sleep 0.05; done"
sleep 0.2
if tail -c +$START "$LOG" | rg -q "Decor: title-render deco-border"; then
  echo "unexpected titlebar/title text in BORDER deco" >&2
  exit 1
fi
kill_one

# NONE: no titlebar/title text.
spawn_one "deco-none" "deco-none"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: applied .*app_id=deco-none .*deco=0x0 '; do sleep 0.05; done"
sleep 0.2
if tail -c +$START "$LOG" | rg -q "Decor: title-render deco-none"; then
  echo "unexpected titlebar/title text in NONE deco" >&2
  exit 1
fi
kill_one

# TAB: applies to both group members; ensure tabs UI appears and is forced external (no titlebar in TAB deco).
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id deco-tab-a --title deco-tab-a --stay-ms 20000 --xdg-decoration --width 240 --height 120 >/dev/null 2>&1 &
TAB_A_PID=$!
./fbwl-smoke-client --socket "$SOCKET" --app-id deco-tab-b --title deco-tab-b --stay-ms 20000 --xdg-decoration --width 240 --height 120 >/dev/null 2>&1 &
TAB_B_PID=$!
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Tabs: attach reason=apps-group .*view=deco-tab-b'; do sleep 0.05; done"
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'TabsUI: bar title=deco-tab-.* intitlebar=0 '; do sleep 0.05; done"

# Save-on-close: unknown mask must round-trip as hex.
OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-smoke-client --socket "$SOCKET" --app-id deco-save --title deco-save --stay-ms 300 --xdg-decoration --width 200 --height 120 >/dev/null 2>&1
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Apps: save-on-close wrote '; do sleep 0.05; done"
rg -q "\\[Deco\\][[:space:]]*\\{0x1001\\}" "$APPS_FILE"

echo "ok: apps [Deco] presets+mask smoke passed (socket=$SOCKET log=$LOG apps=$APPS_FILE)"

