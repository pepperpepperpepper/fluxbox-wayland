#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd python3

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

source scripts/fbwl-smoke-report-lib.sh

REPORT_DIR="${FBWL_REPORT_DIR:-${FBWL_SMOKE_REPORT_DIR:-}}"

parse_toolbar_built() {
  local log="$1"
  local line
  line="$(rg 'Toolbar: built x=' "$log" | tail -n 1)"
  if [[ -z "$line" ]]; then
    echo "failed to find Toolbar: built line in log: $log" >&2
    return 1
  fi
  if [[ "$line" =~ x=([-0-9]+)\ y=([-0-9]+)\ w=([0-9]+)\ h=([0-9]+).*clock_w=([0-9]+) ]]; then
    TB_X="${BASH_REMATCH[1]}"
    TB_Y="${BASH_REMATCH[2]}"
    TB_W="${BASH_REMATCH[3]}"
    TB_H="${BASH_REMATCH[4]}"
    TB_CLOCK_W="${BASH_REMATCH[5]}"
    return 0
  fi
  echo "failed to parse Toolbar: built line: $line" >&2
  return 1
}

run_case_toolbar_gradient() (
  SOCKET="wayland-fbwl-style-tex-grad-$UID-$$"
  LOG="/tmp/fluxbox-wayland-style-tex-grad-$UID-$$.log"
  CFG_DIR="$(mktemp -d "/tmp/fbwl-style-tex-grad-$UID-XXXXXX")"
  STYLE_FILE="/tmp/fbwl-style-tex-grad-$UID-$$.cfg"

  cleanup() {
    rm -f "$STYLE_FILE" 2>/dev/null || true
    if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
    wait 2>/dev/null || true
    rm -rf "$CFG_DIR" 2>/dev/null || true
  }
  trap cleanup EXIT

  cat >"$CFG_DIR/init" <<'EOF'
session.screen0.toolbar.visible: true
session.screen0.toolbar.autoHide: false
session.screen0.toolbar.alpha: 255
session.screen0.toolbar.placement: TopLeft
session.screen0.toolbar.layer: Dock
session.screen0.toolbar.tools: clock
session.screen0.allowRemoteActions: true
EOF

  cat >"$STYLE_FILE" <<'EOF'
toolbar: Flat Gradient Vertical
toolbar.height: 48
toolbar.color: #ff0000
toolbar.colorTo: #00ff00
toolbar.textColor: #ffffff
toolbar.font: monospace-10
EOF

  : >"$LOG"

  fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

  WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
    ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFG_DIR" --style "$STYLE_FILE" >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Toolbar: built x=' '$LOG'; do sleep 0.05; done"

  parse_toolbar_built "$LOG"

  if (( TB_W < 10 || TB_H < 4 )); then
    echo "unexpected toolbar size: w=$TB_W h=$TB_H" >&2
    exit 1
  fi

  SAMPLE_X=$((TB_X + TB_W - 2))
  SAMPLE_Y_TOP=$((TB_Y))
  SAMPLE_Y_BOT=$((TB_Y + TB_H - 1))

  # Our gradient table (ported from Fluxbox) intentionally divides by size, so the last row
  # does not reach colorTo exactly. Compute the expected last-row color to keep this
  # deterministic across toolbar heights.
  BOT_R=$((255 * 1 / TB_H))
  BOT_G=$((255 * (TB_H - 1) / TB_H))
  printf -v EXPECT_BOT '#%02x%02x%02x' "$BOT_R" "$BOT_G" 0

  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#ff0000' \
    --sample-x "$SAMPLE_X" --sample-y "$SAMPLE_Y_TOP" >/dev/null 2>&1
  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb "$EXPECT_BOT" \
    --sample-x "$SAMPLE_X" --sample-y "$SAMPLE_Y_BOT" >/dev/null 2>&1

  fbwl_report_shot "style-texture-gradient.png" "Toolbar vertical gradient (top red → bottom green)"
)

run_case_toolbar_pixmap_tiled() (
  SOCKET="wayland-fbwl-style-tex-pixmap-$UID-$$"
  LOG="/tmp/fluxbox-wayland-style-tex-pixmap-$UID-$$.log"
  CFG_DIR="$(mktemp -d "/tmp/fbwl-style-tex-pixmap-$UID-XXXXXX")"
  STYLE_FILE="/tmp/fbwl-style-tex-pixmap-$UID-$$.cfg"
  PIXMAP_PNG="/tmp/fbwl-style-tex-pixmap-$UID-$$.png"

  cleanup() {
    rm -f "$STYLE_FILE" "$PIXMAP_PNG" 2>/dev/null || true
    if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
    wait 2>/dev/null || true
    rm -rf "$CFG_DIR" 2>/dev/null || true
  }
  trap cleanup EXIT

  cat >"$CFG_DIR/init" <<'EOF'
session.screen0.toolbar.visible: true
session.screen0.toolbar.autoHide: false
session.screen0.toolbar.alpha: 255
session.screen0.toolbar.placement: TopLeft
session.screen0.toolbar.layer: Dock
session.screen0.toolbar.tools: clock
session.screen0.allowRemoteActions: true
EOF

  python3 - "$PIXMAP_PNG" <<'PY'
import struct, sys, zlib

path = sys.argv[1]
w, h = 64, 64

def chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag)
    crc = zlib.crc32(data, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

png = bytearray(b"\x89PNG\r\n\x1a\n")
png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))  # RGBA

raw = bytearray()
for _y in range(h):
    row = bytearray([0])  # filter=0
    for x in range(w):
        if x < w // 2:
            row += bytes([255, 0, 0, 255])
        else:
            row += bytes([0, 255, 0, 255])
    raw += row

png += chunk(b"IDAT", zlib.compress(bytes(raw)))
png += chunk(b"IEND", b"")

with open(path, "wb") as f:
    f.write(png)
PY

  cat >"$STYLE_FILE" <<EOF
toolbar: Flat Solid Tiled
toolbar.height: 48
toolbar.color: #000000
toolbar.pixmap: $PIXMAP_PNG
toolbar.textColor: #ffffff
toolbar.font: monospace-10
EOF

  : >"$LOG"

  fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

  WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
    ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFG_DIR" --style "$STYLE_FILE" >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Toolbar: built x=' '$LOG'; do sleep 0.05; done"

  parse_toolbar_built "$LOG"

  if (( TB_W < 300 || TB_H < 4 )); then
    echo "unexpected toolbar size: w=$TB_W h=$TB_H" >&2
    exit 1
  fi

  TILE_W=64
  X_SMALL=$((TB_CLOCK_W + 10))
  if (( X_SMALL < 10 )); then X_SMALL=10; fi
  if (( X_SMALL > TB_W - 2 )); then X_SMALL=$((TB_W - 2)); fi
  X_MOD=$((X_SMALL % TILE_W))

  # For our 64px-wide pattern: left half red, right half green.
  EXPECT='#ff0000'
  if (( X_MOD >= TILE_W / 2 )); then
    EXPECT='#00ff00'
  fi

  TARGET=$(((TB_W * 3) / 4))
  if (( TARGET < X_SMALL + TILE_W )); then TARGET=$((X_SMALL + TILE_W)); fi
  N=$(((TARGET - X_SMALL + TILE_W - 1) / TILE_W))
  X_LARGE=$((X_SMALL + N * TILE_W))
  if (( X_LARGE > TB_W - 2 )); then
    N=$(((TB_W - 2 - X_SMALL) / TILE_W))
    X_LARGE=$((X_SMALL + N * TILE_W))
  fi
  if (( X_LARGE <= X_SMALL )); then
    echo "failed to pick distinct tiled sample points (tb_w=$TB_W clock_w=$TB_CLOCK_W x_small=$X_SMALL x_large=$X_LARGE)" >&2
    exit 1
  fi

  SAMPLE_Y=$((TB_Y + 1))
  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb "$EXPECT" \
    --sample-x $((TB_X + X_SMALL)) --sample-y "$SAMPLE_Y" >/dev/null 2>&1
  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb "$EXPECT" \
    --sample-x $((TB_X + X_LARGE)) --sample-y "$SAMPLE_Y" >/dev/null 2>&1

  fbwl_report_shot "style-texture-pixmap-tiled.png" "Toolbar pixmap tiled repeat (two matching samples across width)"
)

run_case_toolbar_parentrelative() (
  SOCKET="wayland-fbwl-style-tex-parentrel-$UID-$$"
  LOG="/tmp/fluxbox-wayland-style-tex-parentrel-$UID-$$.log"
  CFG_DIR="$(mktemp -d "/tmp/fbwl-style-tex-parentrel-$UID-XXXXXX")"
  STYLE_FILE="/tmp/fbwl-style-tex-parentrel-$UID-$$.cfg"
  WALLPAPER_PNG="/tmp/fbwl-style-tex-parentrel-wall-$UID-$$.png"

  cleanup() {
    rm -f "$STYLE_FILE" "$WALLPAPER_PNG" 2>/dev/null || true
    if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
    wait 2>/dev/null || true
    rm -rf "$CFG_DIR" 2>/dev/null || true
  }
  trap cleanup EXIT

  cat >"$CFG_DIR/init" <<'EOF'
session.screen0.toolbar.visible: true
session.screen0.toolbar.autoHide: false
session.screen0.toolbar.alpha: 255
session.screen0.toolbar.placement: TopLeft
session.screen0.toolbar.layer: Dock
session.screen0.toolbar.tools: clock
session.screen0.allowRemoteActions: true
EOF

  cat >"$STYLE_FILE" <<'EOF'
toolbar: ParentRelative
toolbar.height: 48
toolbar.textColor: #ffffff
toolbar.font: monospace-10
EOF

  python3 - "$WALLPAPER_PNG" <<'PY'
import struct, sys, zlib

path = sys.argv[1]
w, h = 64, 64
r, g, b, a = 0, 0, 255, 255

def chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag)
    crc = zlib.crc32(data, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

png = bytearray(b"\x89PNG\r\n\x1a\n")
png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))  # RGBA
row = bytes([0]) + bytes([r, g, b, a]) * w  # filter=0, then RGBA pixels
raw = row * h
png += chunk(b"IDAT", zlib.compress(raw))
png += chunk(b"IEND", b"")

with open(path, "wb") as f:
    f.write(png)
PY

  : >"$LOG"

  fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

  WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
    ./fluxbox-wayland --no-xwayland --socket "$SOCKET" --config-dir "$CFG_DIR" --style "$STYLE_FILE" >"$LOG" 2>&1 &
  FBW_PID=$!

  timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

  ./fbwl-remote --socket "$SOCKET" wallpaper "$WALLPAPER_PNG" | rg -q '^ok$'
  timeout 5 bash -c "until rg -q 'Background: wallpaper set' '$LOG'; do sleep 0.05; done"
  timeout 5 bash -c "until rg -q 'Toolbar: built x=' '$LOG'; do sleep 0.05; done"

  parse_toolbar_built "$LOG"

  SAMPLE_X=$((TB_X + TB_CLOCK_W + 50))
  if (( SAMPLE_X < TB_X + 10 )); then SAMPLE_X=$((TB_X + 10)); fi
  if (( SAMPLE_X > TB_X + TB_W - 10 )); then SAMPLE_X=$((TB_X + TB_W / 2)); fi
  # Avoid sampling right at the top edge: wallpaper scaling can produce edge-darkening
  # artifacts in the first few rows.
  SAMPLE_Y=$((TB_Y + 10))
  if (( SAMPLE_Y >= TB_Y + TB_H )); then SAMPLE_Y=$((TB_Y + TB_H / 2)); fi

  # ParentRelative should sample the wallpaper/background, not style colors.
  ./fbwl-screencopy-client --socket "$SOCKET" --timeout-ms 4000 --expect-rgb '#0000ff' \
    --sample-x "$SAMPLE_X" --sample-y "$SAMPLE_Y" >/dev/null 2>&1

  fbwl_report_shot "style-texture-parentrelative.png" "Toolbar ParentRelative (samples wallpaper)"
)

run_case_toolbar_gradient
run_case_toolbar_pixmap_tiled
run_case_toolbar_parentrelative

echo "ok: style texture smoke passed (gradient/pixmap tiled/parentrelative)"
