#!/usr/bin/env bash
set -euo pipefail

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "missing required command: $1" >&2; exit 1; }
}

need_cmd python3

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

MAX_CODE_LINES="${MAX_CODE_LINES:-1100}"
ALLOWLIST="${ALLOWLIST:-$ROOT/scripts/fbwl-check-code-sloc-allowlist.txt}"

python3 "$ROOT/scripts/fbwl-check-code-sloc.py" \
  --root "$ROOT" \
  --max "$MAX_CODE_LINES" \
  --allowlist "$ALLOWLIST"

