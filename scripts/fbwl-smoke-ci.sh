#!/usr/bin/env bash
set -euo pipefail

have_cmd() {
  command -v "$1" >/dev/null 2>&1
}

have_exe() {
  [[ -x "$1" ]]
}

need_cmd() {
  local cmd="$1"
  if ! have_cmd "$cmd"; then
    echo "missing required command: $cmd" >&2
    exit 1
  fi
}

extract_need_cmds() {
  local script="$1"
  rg --no-filename -o -N 'need_cmd[[:space:]]+[A-Za-z0-9_.-]+' "$script" \
    | awk '{print $2}' \
    | sort -u
}

extract_need_exes() {
  local script="$1"
  rg --no-filename -o -N 'need_exe[[:space:]]+[^[:space:]]+' "$script" \
    | awk '{print $2}' \
    | sort -u
}

extract_dot_exes() {
  local script="$1"
  rg --no-filename -o -N '\\./(fluxbox-wayland|startfluxbox-wayland|fluxbox-remote|fbwl-[A-Za-z0-9_.-]+|fbx11-[A-Za-z0-9_.-]+)' "$script" \
    | sort -u
}

extract_x_test_exes() {
  local script="$1"
  rg --no-filename -o -N -e '-x[[:space:]]+(/[^[:space:]]+|\\./[^[:space:]]+)' "$script" \
    | awk '{print $NF}' \
    | sort -u
}

extract_smoke_script_refs() {
  local script="$1"
  rg --no-filename -o -N 'scripts/fbwl-smoke-[A-Za-z0-9_.-]+\\.sh' "$script" \
    | sort -u
}

collect_script_closure() {
  local root_script="$1"

  declare -A seen=()
  local -a stack=("$root_script")
  local -a out=()

  while ((${#stack[@]} > 0)); do
    local idx=$(( ${#stack[@]} - 1 ))
    local s="${stack[$idx]}"
    unset "stack[$idx]"

    [[ -n "${seen[$s]:-}" ]] && continue
    seen["$s"]=1
    out+=("$s")

    while IFS= read -r ref; do
      [[ -z "$ref" ]] && continue
      if [[ ! -f "$ref" ]]; then
        echo "error: referenced smoke script not found: $ref (from $s)" >&2
        exit 1
      fi
      stack+=("$ref")
    done < <(extract_smoke_script_refs "$s")
  done

  printf '%s\n' "${out[@]}"
}

collect_required_cmds() {
  local root_script="$1"
  collect_script_closure "$root_script" \
    | while IFS= read -r s; do extract_need_cmds "$s"; done \
    | sort -u
}

collect_required_exes() {
  local root_script="$1"
  collect_script_closure "$root_script" \
    | while IFS= read -r s; do
        extract_need_exes "$s"
        extract_x_test_exes "$s"
        extract_dot_exes "$s"
      done \
    | sort -u
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

need_cmd awk
need_cmd rg
need_cmd sort

scripts=(
  scripts/fbwl-check-wayland-loc.sh
  scripts/fbwl-smoke-headless.sh
  scripts/fbwl-smoke-log-protocol.sh
  scripts/fbwl-smoke-background.sh
  scripts/fbwl-smoke-xvfb.sh
  scripts/fbwl-smoke-xvfb-decor-style.sh
  scripts/fbwl-smoke-xvfb-policy.sh
  scripts/fbwl-smoke-xvfb-kitchen-sink.sh
  scripts/fbwl-smoke-xvfb-outputs.sh
  scripts/fbwl-smoke-xvfb-protocols.sh
  scripts/fbwl-smoke-xvfb-tray.sh
  scripts/fbwl-smoke-xvfb-xwayland.sh
  scripts/fbwl-smoke-xvfb-portal.sh
  scripts/fbwl-smoke-xwayland.sh
  scripts/fbwl-smoke-ipc.sh
  scripts/fbwl-smoke-startfluxbox-wayland.sh
  scripts/fbwl-smoke-fluxbox-remote.sh
  scripts/fbwl-smoke-sni.sh
  scripts/fbwl-smoke-tray.sh
  scripts/fbwl-smoke-tray-iconname.sh
  scripts/fbwl-smoke-tray-icon-theme-path.sh
  scripts/fbwl-smoke-tray-attention.sh
  scripts/fbwl-smoke-tray-overlay.sh
  scripts/fbwl-smoke-tray-passive.sh
  scripts/fbwl-smoke-clipboard.sh
  scripts/fbwl-smoke-data-control.sh
  scripts/fbwl-smoke-primary-selection.sh
  scripts/fbwl-smoke-cursor-shape.sh
  scripts/fbwl-smoke-presentation-time.sh
  scripts/fbwl-smoke-dnd.sh
  scripts/fbwl-smoke-relptr.sh
  scripts/fbwl-smoke-screencopy.sh
  scripts/fbwl-smoke-export-dmabuf.sh
  scripts/fbwl-smoke-output-management.sh
  scripts/fbwl-smoke-output-power.sh
  scripts/fbwl-smoke-xdg-output.sh
  scripts/fbwl-smoke-viewporter.sh
  scripts/fbwl-smoke-fractional-scale.sh
  scripts/fbwl-smoke-xdg-activation.sh
  scripts/fbwl-smoke-xdg-decoration.sh
  scripts/fbwl-smoke-ssd.sh
  scripts/fbwl-smoke-style.sh
  scripts/fbwl-smoke-menu.sh
  scripts/fbwl-smoke-window-menu.sh
  scripts/fbwl-smoke-toolbar.sh
  scripts/fbwl-smoke-iconbar.sh
  scripts/fbwl-smoke-command-dialog.sh
  scripts/fbwl-smoke-osd.sh
  scripts/fbwl-smoke-idle.sh
  scripts/fbwl-smoke-session-lock.sh
  scripts/fbwl-smoke-shortcuts-inhibit.sh
  scripts/fbwl-smoke-single-pixel-buffer.sh
  scripts/fbwl-smoke-text-input.sh
  scripts/fbwl-smoke-input.sh
  scripts/fbwl-smoke-keys-file.sh
  scripts/fbwl-smoke-config-dir.sh
  scripts/fbwl-smoke-apps-rules.sh
  scripts/fbwl-smoke-apps-rules-xwayland.sh
  scripts/fbwl-smoke-move-resize.sh
  scripts/fbwl-smoke-workspaces.sh
  scripts/fbwl-smoke-maximize-fullscreen.sh
  scripts/fbwl-smoke-minimize-foreign.sh
  scripts/fbwl-smoke-layer-shell.sh
  scripts/fbwl-smoke-multi-output.sh
  scripts/fbwl-smoke-fullscreen-stacking.sh
)

ran=0
skipped=0

for s in "${scripts[@]}"; do
  if [[ ! -f "$s" ]]; then
    echo "error: missing script: $s" >&2
    exit 1
  fi

  missing=()

  while IFS= read -r cmd; do
    if ! have_cmd "$cmd"; then
      missing+=("$cmd")
    fi
  done < <(collect_required_cmds "$s")

  while IFS= read -r exe; do
    if ! have_exe "$exe"; then
      missing+=("$exe")
    fi
  done < <(collect_required_exes "$s")

  if ((${#missing[@]} > 0)); then
    echo "==> $s (skip: missing: ${missing[*]})"
    skipped=$((skipped + 1))
    continue
  fi

  echo "==> $s"
  "$s"
  ran=$((ran + 1))
done

echo "ok: smoke-ci finished (ran=$ran skipped=$skipped)"
