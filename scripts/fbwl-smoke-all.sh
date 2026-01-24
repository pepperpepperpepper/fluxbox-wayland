#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

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

for s in "${scripts[@]}"; do
  echo "==> $s"
  "$s"
done

echo "ok: all Wayland smoke tests passed"
