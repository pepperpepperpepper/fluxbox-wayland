#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd mktemp
need_cmd rg
need_cmd timeout
need_cmd python3

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

source scripts/fbwl-smoke-report-lib.sh

SOCKET="${SOCKET:-wayland-fbwl-test-$UID-$$}"
LOG="${LOG:-/tmp/fluxbox-wayland-window-alpha-$UID-$$.log}"
CFGDIR="$(mktemp -d "/tmp/fbwl-window-alpha-$UID-XXXXXX")"
WALLPAPER_PNG="${WALLPAPER_PNG:-/tmp/fbwl-window-alpha-wallpaper-$UID-$$.png}"
REPORT_DIR="${FBWL_REPORT_DIR:-${FBWL_SMOKE_REPORT_DIR:-}}"

cleanup() {
  rm -rf "$CFGDIR" 2>/dev/null || true
  rm -f "$WALLPAPER_PNG" 2>/dev/null || true
  if [[ -n "${DEFAULT_PID:-}" ]]; then kill "$DEFAULT_PID" 2>/dev/null || true; fi
  if [[ -n "${OVERRIDE_PID:-}" ]]; then kill "$OVERRIDE_PID" 2>/dev/null || true; fi
  if [[ -n "${FBW_PID:-}" ]]; then kill "$FBW_PID" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"

make_spiral_wallpaper_png() {
  local path="$1"
  local w="${2:-512}"
  local h="${3:-512}"

  python3 - "$path" "$w" "$h" <<'PY'
import math
import struct
import sys
import zlib

path = sys.argv[1]
w = int(sys.argv[2])
h = int(sys.argv[3])
cx = (w - 1) / 2.0
cy = (h - 1) / 2.0
maxr = math.hypot(cx, cy) or 1.0

# 8x8 Bayer threshold matrix (0..63).
bayer8 = [
    [0, 48, 12, 60, 3, 51, 15, 63],
    [32, 16, 44, 28, 35, 19, 47, 31],
    [8, 56, 4, 52, 11, 59, 7, 55],
    [40, 24, 36, 20, 43, 27, 39, 23],
    [2, 50, 14, 62, 1, 49, 13, 61],
    [34, 18, 46, 30, 33, 17, 45, 29],
    [10, 58, 6, 54, 9, 57, 5, 53],
    [42, 26, 38, 22, 41, 25, 37, 21],
]

levels = 24
inv_levels = 1.0 / (levels - 1)

def dither_u8(v01: float, x: int, y: int) -> int:
    v01 = 0.0 if v01 < 0.0 else (1.0 if v01 > 1.0 else v01)
    t = (bayer8[y & 7][x & 7] / 64.0 - 0.5) * inv_levels
    q = int(v01 * (levels - 1) + t + 0.5)
    if q < 0:
        q = 0
    elif q > (levels - 1):
        q = levels - 1
    return int(q * inv_levels * 255.0 + 0.5)

def chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag)
    crc = zlib.crc32(data, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

raw = bytearray()
for y in range(h):
    raw.append(0)  # filter=0
    ny = (y - cy) / maxr
    for x in range(w):
        nx = (x - cx) / maxr
        r = math.hypot(nx, ny)
        if r > 1.0:
            r = 1.0
        a = math.atan2(ny, nx)

        # Spiral term + radial term; tuned for visible swirl/gradient.
        t = 10.0 * r + 4.0 * a

        rr = 0.5 + 0.5 * math.sin(t + 0.0)
        gg = 0.5 + 0.5 * math.sin(t + 2.094395102)
        bb = 0.5 + 0.5 * math.sin(t + 4.188790205)

        # Add a gentle radial gradient and vignette.
        g = r
        v = 0.15 + 0.85 * (1.0 - r)
        rr = (0.7 * rr + 0.3 * (1.0 - g)) * v
        gg = (0.7 * gg + 0.3 * g) * v
        bb = (0.7 * bb + 0.3 * (0.5 + 0.5 * math.sin(6.0 * a))) * v

        raw.append(dither_u8(rr, x, y))
        raw.append(dither_u8(gg, x, y))
        raw.append(dither_u8(bb, x, y))
        raw.append(255)

png = bytearray(b"\x89PNG\r\n\x1a\n")
png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))  # RGBA
png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
png += chunk(b"IEND", b"")

with open(path, "wb") as f:
    f.write(png)
PY
}

cat >"$CFGDIR/init" <<EOF
session.screen0.focusModel: ClickToFocus
session.screen0.allowRemoteActions: true
session.appsFile: myapps
session.screen0.window.focus.alpha: 200
session.screen0.window.unfocus.alpha: 100
session.screen0.windowPlacement: RowSmartPlacement
session.screen0.toolbar.visible: false
EOF

cat >"$CFGDIR/myapps" <<'EOF'
[app] (app_id=fbwl-alpha-override)
  [Alpha] {220 30}
[end]
EOF

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland \
  --no-xwayland \
  --socket "$SOCKET" \
  --config-dir "$CFGDIR" \
  >"$LOG" 2>&1 &
FBW_PID=$!

timeout 5 bash -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'IPC: listening' '$LOG'; do sleep 0.05; done"

fbwl_report_init "$REPORT_DIR" "$SOCKET" "$XDG_RUNTIME_DIR"

make_spiral_wallpaper_png "$WALLPAPER_PNG" 768 768
./fbwl-remote --socket "$SOCKET" wallpaper "$WALLPAPER_PNG" | rg -q '^ok$'
timeout 5 bash -c "until rg -q 'Background: wallpaper set' '$LOG'; do sleep 0.05; done"

W=420
H=280

./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-alpha-default --title alpha-default \
  --width "$W" --height "$H" --stay-ms 20000 --xdg-decoration >/dev/null 2>&1 &
DEFAULT_PID=$!
timeout 5 bash -c "until rg -q 'Alpha: alpha-default focused=200 unfocused=100 reason=init-default' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: alpha-default ' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --app-id fbwl-alpha-override --title alpha-override \
  --width "$W" --height "$H" --stay-ms 20000 --xdg-decoration >/dev/null 2>&1 &
OVERRIDE_PID=$!
timeout 5 bash -c "until rg -q 'Alpha: alpha-override focused=220 unfocused=30 reason=apps' '$LOG'; do sleep 0.05; done"
timeout 5 bash -c "until rg -q 'Place: alpha-override ' '$LOG'; do sleep 0.05; done"

fbwl_report_shot "window-alpha.png" "Window alpha (spiral wallpaper; default + apps override)"

if rg -q 'Alpha: alpha-override .* reason=init-default' "$LOG"; then
  echo "unexpected: init-default alpha applied to app that has [Alpha] in apps file" >&2
  exit 1
fi

cat >"$CFGDIR/init" <<EOF
session.screen0.focusModel: ClickToFocus
session.screen0.allowRemoteActions: true
session.appsFile: myapps
session.screen0.window.focus.alpha: 180
session.screen0.window.unfocus.alpha: 90
EOF

OFFSET=$(wc -c <"$LOG" | tr -d ' ')
./fbwl-remote --socket "$SOCKET" reconfigure | rg -q '^ok'
START=$((OFFSET + 1))
timeout 5 bash -c "until tail -c +$START '$LOG' | rg -q 'Alpha: alpha-default focused=180 unfocused=90 reason=reconfigure-default'; do sleep 0.05; done"

if tail -c +$START "$LOG" | rg -q 'Alpha: alpha-override .* reason=reconfigure-default'; then
  echo "unexpected: reconfigure-default alpha applied to app that has [Alpha] in apps file" >&2
  exit 1
fi

echo "ok: window default alpha smoke passed (socket=$SOCKET log=$LOG cfgdir=$CFGDIR)"
