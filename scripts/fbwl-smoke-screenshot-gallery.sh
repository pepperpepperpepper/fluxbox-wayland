#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage:
  scripts/fbwl-smoke-screenshot-gallery.sh [--report-dir DIR] [--prefix PREFIX] [--no-upload] [--skip-smoke]

Runs an in-depth smoke suite (scripts/fbwl-smoke-ci.sh) while collecting screenshots
into a report directory, then optionally uploads the gallery via wtf-upload.

Environment:
  FBWL_SMOKE_REPORT_STRICT=1   Fail if grim is missing (default: 1)
  WTF_UPLOAD_BUCKET / WTF_UPLOAD_HOST  Passed through to wtf-upload
EOF
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

need_cmd date
need_cmd mktemp

REPORT_DIR=""
UPLOAD_PREFIX=""
DO_UPLOAD=1
DO_SMOKE=1

while (($# > 0)); do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --report-dir)
      shift
      REPORT_DIR="${1:-}"
      ;;
    --report-dir=*)
      REPORT_DIR="${1#--report-dir=}"
      ;;
    --prefix)
      shift
      UPLOAD_PREFIX="${1:-}"
      ;;
    --prefix=*)
      UPLOAD_PREFIX="${1#--prefix=}"
      ;;
    --no-upload)
      DO_UPLOAD=0
      ;;
    --skip-smoke)
      DO_SMOKE=0
      ;;
    -*)
      echo "unknown option: $1" >&2
      usage
      exit 2
      ;;
    *)
      echo "unexpected extra arg: $1" >&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

if [[ -z "$REPORT_DIR" ]]; then
  REPORT_DIR="$(mktemp -d "/tmp/fbwl-screenshot-gallery-$UID-XXXXXX")"
fi
mkdir -p "$REPORT_DIR"

: "${FBWL_SMOKE_REPORT_STRICT:=1}"

echo "==> report dir: $REPORT_DIR" >&2

if [[ "$DO_SMOKE" == "1" ]]; then
  echo "==> running in-depth smoke suite with screenshots enabled" >&2
  FBWL_SMOKE_REPORT_DIR="$REPORT_DIR" FBWL_SMOKE_REPORT_STRICT="$FBWL_SMOKE_REPORT_STRICT" \
    scripts/fbwl-smoke-ci.sh
fi

shopt -s nullglob
pngs=("$REPORT_DIR"/*.png)
shopt -u nullglob

if ((${#pngs[@]} == 0)); then
  echo "warn: no screenshots were produced in: $REPORT_DIR" >&2
  echo "hint: ensure grim is installed and that smoke scripts call fbwl_report_shot" >&2
  exit 0
fi

if [[ "$DO_UPLOAD" == "1" ]]; then
  need_cmd wtf-upload
  if [[ -n "$UPLOAD_PREFIX" ]]; then
    scripts/publish_screenshots.sh "$REPORT_DIR" --prefix "$UPLOAD_PREFIX"
  else
    scripts/publish_screenshots.sh "$REPORT_DIR"
  fi
else
  echo "ok: screenshots collected (upload disabled)" >&2
fi

