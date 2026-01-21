#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

limit=1000

mapfile -d '' files < <(
  find "$ROOT/src/wayland" -type f \( -name '*.c' -o -name '*.h' \) \
    ! -path "$ROOT/src/wayland/protocol/*" \
    -print0
)

offenders=()

for f in "${files[@]}"; do
  lines="$(wc -l < "$f")"
  if (( lines >= limit )); then
    offenders+=("$lines $f")
  fi
done

if ((${#offenders[@]} > 0)); then
  {
    printf "error: Wayland source files must stay < %d lines (excluding src/wayland/protocol/**)\n" "$limit"
    printf "error: offenders:\n"
    printf '%s\n' "${offenders[@]}" | sort -nr
  } >&2
  exit 1
fi

echo "ok: all src/wayland/*.c|*.h are < ${limit} LOC (excluding src/wayland/protocol/**)"
