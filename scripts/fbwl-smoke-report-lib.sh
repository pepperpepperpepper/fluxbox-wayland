#!/usr/bin/env bash
set -euo pipefail

fbwl_report_clean_prefix() {
  local s="${1:-}"
  s="${s//$'\t'/_}"
  s="${s//$'\r'/_}"
  s="${s//$'\n'/_}"
  s="${s// /_}"
  s="${s//\//_}"
  s="${s//\\/__}"
  printf '%s' "$s"
}

fbwl_report_clean_filename() {
  local s="${1:-}"
  s="${s##*/}"
  s="${s//$'\t'/_}"
  s="${s//$'\r'/_}"
  s="${s//$'\n'/_}"
  s="${s// /_}"
  s="${s//\//_}"
  s="${s//\\/__}"
  if [[ -z "$s" ]]; then
    s="shot.png"
  fi
  if [[ "$s" == .* ]]; then
    s="shot$s"
  fi
  printf '%s' "$s"
}

fbwl_report_init() {
  local report_dir="${1:-}"
  local socket="${2:-}"
  local runtime_dir="${3:-}"

  FBWL_REPORT_DIR="$report_dir"
  FBWL_REPORT_SOCKET="$socket"
  FBWL_REPORT_RUNTIME_DIR="$runtime_dir"
  FBWL_REPORT_MANIFEST=""

  if [[ -z "$FBWL_REPORT_DIR" ]]; then
    return 0
  fi

  mkdir -p "$FBWL_REPORT_DIR"
  FBWL_REPORT_MANIFEST="$FBWL_REPORT_DIR/manifest.tsv"

  if [[ ! -f "$FBWL_REPORT_MANIFEST" ]]; then
    {
      echo "# fluxbox-wayland smoke screenshot report"
      echo "# created_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
      if command -v git >/dev/null 2>&1; then
        echo "# git: $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
      fi
      echo "# format: <file>\\t<caption>"
    } >"$FBWL_REPORT_MANIFEST"
  fi

  if ! command -v grim >/dev/null 2>&1; then
    if [[ "${FBWL_SMOKE_REPORT_STRICT:-}" == "1" ]]; then
      echo "missing required command for screenshot reports: grim" >&2
      exit 1
    fi
    echo "warn: grim not found; screenshot reports disabled (set FBWL_SMOKE_REPORT_STRICT=1 to fail)" >&2
    FBWL_REPORT_DIR=""
    FBWL_REPORT_MANIFEST=""
    return 0
  fi

  if [[ -z "$FBWL_REPORT_SOCKET" || -z "$FBWL_REPORT_RUNTIME_DIR" ]]; then
    if [[ "${FBWL_SMOKE_REPORT_STRICT:-}" == "1" ]]; then
      echo "missing socket/runtime_dir for screenshot reports (socket='$FBWL_REPORT_SOCKET' runtime_dir='$FBWL_REPORT_RUNTIME_DIR')" >&2
      exit 1
    fi
    echo "warn: missing socket/runtime_dir; screenshot reports disabled" >&2
    FBWL_REPORT_DIR=""
    FBWL_REPORT_MANIFEST=""
    return 0
  fi
}

fbwl_report_shot() {
  local file="${1:-}"
  shift || true
  local caption="${*:-}"

  if [[ -z "${FBWL_REPORT_DIR:-}" ]]; then
    return 0
  fi
  if [[ -z "$file" ]]; then
    echo "fbwl_report_shot: missing file name" >&2
    return 1
  fi

  local prefix_clean
  prefix_clean="$(fbwl_report_clean_prefix "${FBWL_REPORT_PREFIX:-}")"
  local file_clean
  file_clean="$(fbwl_report_clean_filename "$file")"
  local file_out="${prefix_clean}${file_clean}"

  local out_path="$FBWL_REPORT_DIR/$file_out"

  local caption_clean="$caption"
  caption_clean="${caption_clean//$'\t'/ }"
  caption_clean="${caption_clean//$'\r'/ }"
  caption_clean="${caption_clean//$'\n'/ }"
  if [[ -z "$caption_clean" ]]; then
    caption_clean="$file_out"
  fi

  env \
    XDG_RUNTIME_DIR="$FBWL_REPORT_RUNTIME_DIR" \
    WAYLAND_DISPLAY="$FBWL_REPORT_SOCKET" \
    XDG_SESSION_TYPE=wayland \
    grim -t png "$out_path" >/dev/null 2>&1

  printf '%s\t%s\n' "$file_out" "$caption_clean" >>"$FBWL_REPORT_MANIFEST"
}
