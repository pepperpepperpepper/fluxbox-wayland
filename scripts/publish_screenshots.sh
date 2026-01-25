#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage:
  scripts/publish_screenshots.sh REPORT_DIR [--prefix PREFIX]

Uploads:
  - *.png
  - manifest.tsv
  - index.html

to S3 via wtf-upload and prints the index.html URL.

Environment:
  WTF_UPLOAD_BUCKET / WTF_UPLOAD_HOST  (passed through to wtf-upload)
EOF
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd wtf-upload
need_cmd date

REPORT_DIR=""
PREFIX=""

while (($# > 0)); do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --prefix)
      shift
      PREFIX="${1:-}"
      ;;
    --prefix=*)
      PREFIX="${1#--prefix=}"
      ;;
    -*)
      echo "unknown option: $1" >&2
      usage
      exit 2
      ;;
    *)
      if [[ -z "$REPORT_DIR" ]]; then
        REPORT_DIR="$1"
      else
        echo "unexpected extra arg: $1" >&2
        usage
        exit 2
      fi
      ;;
  esac
  shift || true
done

if [[ -z "$REPORT_DIR" ]]; then
  usage
  exit 2
fi
if [[ ! -d "$REPORT_DIR" ]]; then
  echo "report dir not found: $REPORT_DIR" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEMPLATE_INDEX="$ROOT/scripts/smoke-report/index.html"

INDEX="$REPORT_DIR/index.html"
MANIFEST="$REPORT_DIR/manifest.tsv"

if [[ ! -f "$INDEX" ]]; then
  if [[ ! -f "$TEMPLATE_INDEX" ]]; then
    echo "missing index template: $TEMPLATE_INDEX" >&2
    exit 1
  fi
  cp "$TEMPLATE_INDEX" "$INDEX"
fi

shopt -s nullglob
pngs=("$REPORT_DIR"/*.png)
shopt -u nullglob

if ((${#pngs[@]} == 0)); then
  echo "no PNG screenshots found in: $REPORT_DIR" >&2
  exit 1
fi

if [[ ! -f "$MANIFEST" ]]; then
  {
    echo "# fluxbox-wayland smoke screenshot report"
    echo "# created_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "# format: <file>\\t<caption>"
    for p in "${pngs[@]}"; do
      b="$(basename "$p")"
      printf '%s\t%s\n' "$b" "$b"
    done
  } >"$MANIFEST"
fi

if [[ -z "$PREFIX" ]]; then
  sha="unknown"
  if command -v git >/dev/null 2>&1 && git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    sha="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  fi
  ts="$(date -u +%Y%m%d-%H%M%S)"
  PREFIX="fluxbox-wayland/smoke-report/${sha}/${ts}/"
fi
case "$PREFIX" in
  */) ;;
  *) PREFIX="${PREFIX}/" ;;
esac

files=("${pngs[@]}" "$MANIFEST" "$INDEX")

index_url=""

echo "==> uploading ${#pngs[@]} screenshots + index (prefix=$PREFIX)" >&2
for f in "${files[@]}"; do
  base="$(basename "$f")"
  content_type=()
  case "$base" in
    index.html) content_type=(--content-type "text/html; charset=utf-8") ;;
    manifest.tsv) content_type=(--content-type "text/plain; charset=utf-8") ;;
    *.png) content_type=(--content-type "image/png") ;;
  esac

  # Use an exact key so index.html can refer to manifest.tsv + images by filename.
  url="$(wtf-upload --key "${PREFIX}${base}" "${content_type[@]}" "$f")"
  printf '%s\t%s\n' "$base" "$url"
  if [[ "$base" == "index.html" ]]; then
    index_url="$url"
  fi
done

if [[ -n "$index_url" ]]; then
  echo "ok: report index: $index_url" >&2
else
  echo "warn: index.html URL not captured" >&2
fi
