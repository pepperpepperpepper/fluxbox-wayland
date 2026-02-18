#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Smoke scripts assume a mostly clean environment. A stray compositor from a prior debug run
# can create hard-to-debug flakes (XWayland display lock contention, stale IPC sockets, etc).
# By default we fail fast when any `fluxbox-wayland` is already running.
#
# Override via `FBWL_SMOKE_ALLOW_RUNNING_FLUXBOX_WAYLAND=1`.
if [[ "${FBWL_SMOKE_ALLOW_RUNNING_FLUXBOX_WAYLAND:-0}" != "1" ]]; then
  if command -v pgrep >/dev/null 2>&1; then
    if pgrep -a fluxbox-wayland >/dev/null 2>&1; then
      echo "error: fluxbox-wayland already running; stop it before running this suite" >&2
      echo "debug: running instances:" >&2
      pgrep -a fluxbox-wayland >&2 || true
      echo "hint: set FBWL_SMOKE_ALLOW_RUNNING_FLUXBOX_WAYLAND=1 to override" >&2
      exit 1
    fi
  fi
fi

# Many smoke scripts use aggressive `timeout N ...` waits (typically `N=5`) for startup/log
# transitions. When running the full suite sequentially, the system can get momentarily
# loaded and those waits become flaky. To keep the scripts themselves simple/readable, we
# install a small `timeout` wrapper (first in PATH) that scales integer-second durations.
#
# Override via `FBWL_SMOKE_TIMEOUT_FACTOR=<int>` (defaults to 5 for this suite).
FBWL_SMOKE_TIMEOUT_FACTOR="${FBWL_SMOKE_TIMEOUT_FACTOR:-5}"
export FBWL_SMOKE_TIMEOUT_FACTOR

REAL_TIMEOUT="$(command -v timeout || true)"
if [[ -n "$REAL_TIMEOUT" && -x "$REAL_TIMEOUT" ]]; then
  FBWL_SMOKE_TMPBIN="$(mktemp -d /tmp/fbwl-smoke-bin-XXXXXX)"
  trap 'rm -rf "$FBWL_SMOKE_TMPBIN" 2>/dev/null || true' EXIT

  cat >"$FBWL_SMOKE_TMPBIN/timeout" <<EOF
#!/usr/bin/env bash
set -euo pipefail

REAL_TIMEOUT="$REAL_TIMEOUT"
factor="\${FBWL_SMOKE_TIMEOUT_FACTOR:-1}"

if [[ "\$#" -lt 1 ]]; then
  exec "\$REAL_TIMEOUT"
fi

first="\$1"
shift

if [[ "\$first" == -* ]]; then
  # Option form (rare in our suite) - don't try to scale.
  exec "\$REAL_TIMEOUT" "\$first" "\$@"
fi

if [[ "\$factor" =~ ^[0-9]+$ ]] && [[ "\$factor" -gt 1 ]]; then
  if [[ "\$first" =~ ^([0-9]+)([smhd]?)\$ ]]; then
    n="\${BASH_REMATCH[1]}"
    suf="\${BASH_REMATCH[2]}"
    if [[ "\$suf" == \"\" || "\$suf" == \"s\" ]]; then
      first="\$((n * factor))\${suf}"
    fi
  fi
fi

exec "\$REAL_TIMEOUT" "\$first" "\$@"
EOF
  chmod +x "$FBWL_SMOKE_TMPBIN/timeout"
  export PATH="$FBWL_SMOKE_TMPBIN:$PATH"
fi

scripts=(
  scripts/fbwl-check-wayland-loc.sh
  scripts/fbwl-smoke-headless.sh
  scripts/fbwl-smoke-log-protocol.sh
  scripts/fbwl-smoke-background.sh
  scripts/fbwl-smoke-wallpaper.sh
  scripts/fbwl-smoke-fbsetbg-wayland.sh
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
  scripts/fbwl-smoke-xwayland-window-attention.sh
  scripts/fbwl-smoke-focushidden.sh
  scripts/fbwl-smoke-xembed-tray.sh
  scripts/fbwl-smoke-xwayland-net-wm-icon.sh
  scripts/fbwl-smoke-xwayland-xprop-clientpattern.sh
  scripts/fbwl-smoke-xwayland-max-ignore-increment.sh
  scripts/fbwl-smoke-ipc.sh
  scripts/fbwl-smoke-restart.sh
  scripts/fbwl-smoke-startfluxbox-wayland.sh
  scripts/fbwl-smoke-fluxbox-remote.sh
  scripts/fbwl-smoke-clientpatterntest.sh
  scripts/fbwl-smoke-sni.sh
  scripts/fbwl-smoke-tray.sh
  scripts/fbwl-smoke-tray-iconname.sh
  scripts/fbwl-smoke-tray-icon-theme-path.sh
  scripts/fbwl-smoke-tray-attention.sh
  scripts/fbwl-smoke-tray-overlay.sh
  scripts/fbwl-smoke-tray-passive.sh
  scripts/fbwl-smoke-tray-pin.sh
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
  scripts/fbwl-smoke-default-deco.sh
  scripts/fbwl-smoke-setdecor.sh
  scripts/fbwl-smoke-style.sh
  scripts/fbwl-smoke-style-background.sh
  scripts/fbwl-smoke-style-textures.sh
  scripts/fbwl-smoke-style-justify.sh
  scripts/fbwl-smoke-style-window-bevel.sh
  scripts/fbwl-smoke-style-window-round-corners.sh
  scripts/fbwl-smoke-style-menu-round-corners.sh
  scripts/fbwl-smoke-style-menu-underline-color.sh
  scripts/fbwl-smoke-style-toolbar-shaped-scale.sh
  scripts/fbwl-smoke-menu.sh
  scripts/fbwl-smoke-menu-icons.sh
  scripts/fbwl-smoke-menu-search.sh
  scripts/fbwl-smoke-menu-escaping.sh
  scripts/fbwl-smoke-window-menu.sh
  scripts/fbwl-smoke-titlebar-buttons.sh
  scripts/fbwl-smoke-window-alpha.sh
  scripts/fbwl-smoke-pseudo-transparency.sh
  scripts/fbwl-smoke-toolbar.sh
  scripts/fbwl-smoke-toolbar-buttons.sh
  scripts/fbwl-smoke-toolbar-tools-order.sh
  scripts/fbwl-smoke-toolbar-onhead.sh
  scripts/fbwl-smoke-toolbar-layer.sh
  scripts/fbwl-smoke-screen1-toolbar-overrides.sh
  scripts/fbwl-smoke-screen1-menu-overrides.sh
  scripts/fbwl-smoke-strftime-format.sh
	  scripts/fbwl-smoke-alpha.sh
		  scripts/fbwl-smoke-iconbar.sh
		  scripts/fbwl-smoke-iconbar-resources.sh
		  scripts/fbwl-smoke-clientmenu-usepixmap.sh
		  scripts/fbwl-smoke-custommenu-clientmenu-pattern.sh
		  scripts/fbwl-smoke-tooltip-delay.sh
		  scripts/fbwl-smoke-command-dialog.sh
		  scripts/fbwl-smoke-osd.sh
  scripts/fbwl-smoke-idle.sh
  scripts/fbwl-smoke-session-lock.sh
  scripts/fbwl-smoke-shortcuts-inhibit.sh
  scripts/fbwl-smoke-single-pixel-buffer.sh
  scripts/fbwl-smoke-text-input.sh
	  scripts/fbwl-smoke-input.sh
	  scripts/fbwl-smoke-focusnewwindows.sh
	  scripts/fbwl-smoke-clickraises.sh
	  scripts/fbwl-smoke-autoraise.sh
	  scripts/fbwl-smoke-keybinding-commands.sh
	  scripts/fbwl-smoke-keybinding-parity-commands.sh
  scripts/fbwl-smoke-layer-ui-toggle-cmds.sh
  scripts/fbwl-smoke-keybinding-cmdlang.sh
  scripts/fbwl-smoke-cmdlang-if-foreach.sh
  scripts/fbwl-smoke-macrocmd-retarget.sh
  scripts/fbwl-smoke-keybinding-head-mark.sh
  scripts/fbwl-smoke-resource-style-cmds.sh
  scripts/fbwl-smoke-cache-resources.sh
  scripts/fbwl-smoke-keys-file.sh
  scripts/fbwl-smoke-bindkey.sh
  scripts/fbwl-smoke-keys-chaining.sh
  scripts/fbwl-smoke-changeworkspace-event.sh
  scripts/fbwl-smoke-setenv-export.sh
  scripts/fbwl-smoke-activate-focus-pattern.sh
  scripts/fbwl-smoke-focus-directional.sh
  scripts/fbwl-smoke-nextwindow-clientpattern.sh
  scripts/fbwl-smoke-clientpattern-regex-quirk.sh
  scripts/fbwl-smoke-focusmodel-aliases.sh
  scripts/fbwl-smoke-doubleclick.sh
	  scripts/fbwl-smoke-mousebindings-click-move.sh
	  scripts/fbwl-smoke-mousebindings-fluxconf-mangled.sh
	  scripts/fbwl-smoke-mousebindings-winbutton-contexts.sh
	  scripts/fbwl-smoke-mousebindings-ontoolbar-precedence.sh
	  scripts/fbwl-smoke-mousebindings-wheel-click.sh
	  scripts/fbwl-smoke-ignore-border.sh
	  scripts/fbwl-smoke-grips.sh
	  scripts/fbwl-smoke-cli-rc.sh
	  scripts/fbwl-smoke-config-dir.sh
  scripts/fbwl-smoke-allow-remote-actions.sh
  scripts/fbwl-smoke-no-focus-while-typing.sh
  scripts/fbwl-smoke-apps-rules.sh
  scripts/fbwl-smoke-apps-deco-mask.sh
  scripts/fbwl-smoke-apps-hidden.sh
  scripts/fbwl-smoke-apps-tab.sh
  scripts/fbwl-smoke-apps-matchlimit.sh
  scripts/fbwl-smoke-apps-rules-xwayland.sh
  scripts/fbwl-smoke-apps-ignore-size-hints.sh
  scripts/fbwl-smoke-edge-snap.sh
  scripts/fbwl-smoke-move-resize.sh
  scripts/fbwl-smoke-keyboard-move-resize.sh
  scripts/fbwl-smoke-startresizing-args.sh
  scripts/fbwl-smoke-geometry-cmds.sh
  scripts/fbwl-smoke-opaque-resize.sh
	  scripts/fbwl-smoke-showwindowposition.sh
	  scripts/fbwl-smoke-workspaces.sh
	  scripts/fbwl-smoke-workspace-add-remove-name.sh
	  scripts/fbwl-smoke-workspace-nav-offset-toggle.sh
	  scripts/fbwl-smoke-workspace-warping.sh
	  scripts/fbwl-smoke-struts.sh
  scripts/fbwl-smoke-full-maximization.sh
	  scripts/fbwl-smoke-slit-maxover.sh
	  scripts/fbwl-smoke-slit-ordering.sh
	  scripts/fbwl-smoke-slit-kde-dockapps.sh
	  scripts/fbwl-smoke-slit-alpha-input.sh
	  scripts/fbwl-smoke-slit-menu.sh
	  scripts/fbwl-smoke-slit-autoraise.sh
	  scripts/fbwl-smoke-slit-autosave.sh
	  scripts/fbwl-smoke-tabs-ui-click.sh
	  scripts/fbwl-smoke-mousebindings-ontab.sh
	  scripts/fbwl-smoke-tabs-ui-mousefocus.sh
	  scripts/fbwl-smoke-tabs-attach-area.sh
	  scripts/fbwl-smoke-tabs-commands.sh
	  scripts/fbwl-smoke-tabs-maxover.sh
	  scripts/fbwl-smoke-maximize-axis-toggle.sh
  scripts/fbwl-smoke-maximize-fullscreen.sh
  scripts/fbwl-smoke-max-disable-move-resize.sh
  scripts/fbwl-smoke-minimize-foreign.sh
  scripts/fbwl-smoke-layer-shell.sh
  scripts/fbwl-smoke-multi-output.sh
  scripts/fbwl-smoke-fullscreen-stacking.sh
  scripts/fbwl-smoke-mousefocus-geometry.sh
  scripts/fbwl-smoke-strict-mousefocus-stacking.sh
  scripts/fbwl-smoke-strict-mousefocus-layer.sh
  scripts/fbwl-smoke-strict-mousefocus-geometry.sh
  scripts/fbwl-smoke-focus-same-head.sh
)

for s in "${scripts[@]}"; do
  echo "==> $s"
  "$s"
done

echo "ok: all Wayland smoke tests passed"
