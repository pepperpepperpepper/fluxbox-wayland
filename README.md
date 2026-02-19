# fluxbox-wayland

`fluxbox-wayland` is a **wlroots-based Wayland compositor** with Fluxbox-like policy (root menu, key/mouse bindings, workspaces, theming, lightweight).

This repo is **BETA**: full Wayland compatibility with an explicit **1:1 Fluxbox/X11 config parity** target (still stabilizing).

- **`fluxbox-wayland` is a compositor**, not an X11 window manager running “on top of” Wayland.
- It manages native Wayland `xdg_toplevel` apps and can optionally run X11 apps via **XWayland**.
- Classic Fluxbox config formats are intentionally reused with an explicit **1:1 parity** target (see “Fluxbox/X11 parity” below).

## Fluxbox/X11 parity (implemented)

The core goal is that classic `~/.fluxbox/` configs “just work” under Wayland, with Fluxbox-like behavior and command
semantics.

Parity coverage includes:

- **Startup / CLI**: Fluxbox-style `-rc <init-file>` semantics (including relative path resolution), `-no-toolbar`, `-no-slit`.
- **Bindings + cmdlang**: Fluxbox key/mouse binding formats and cmdlang parsing/semantics (including `If`/`ForEach`, and per-binding `ToggleCmd`/`Delay` statefulness).
- **Menus**: root menu + client menu behavior (including escaping, icons, and menu search/type-ahead).
- **Workspaces**: add/remove/name, navigation, and workspace warping behavior.
- **Focus / stacking policy**: click-raise/click-focus, focus models, directional focus, MRU cycling, auto-raise delay behavior.
- **Move/resize/maximize**: keyboard and pointer move/resize; maximize/fullscreen; common edge-snap behaviors.
- **Apps rules**: Fluxbox `apps` file rules (matching + placement + decoration flags + tabbing-related rules).
- **Tabs UI**: attach areas, mouse focus behavior, and tab command parity.
- **Toolbar**: tools ordering and theming; `autoHide`, `autoRaise`, `maxOver`, `layer`, per-screen overrides.
- **Slit**: direction + ordering + autosave; `autoHide`, `autoRaise`, `maxOver`; menu behavior; KDE dockapp handling (best-effort under XWayland).
- **Styles / themes**: Fluxbox texture engine (gradients/pixmaps/`ParentRelative`), per-element theming for menus/toolbar/slit/window decorations, font effects, and common shape keys (bevel/rounded corners) on supported UI components.

Docs:

- Man pages: `doc/fluxbox-wayland.1.in`, `doc/startfluxbox-wayland.1.in`
- Smoke tests (deterministic, non-interactive): `scripts/fbwl-smoke-all.sh`

## Screenshots

All screenshots below are tracked in git under `docs/screenshots/` so GitHub can render them directly.
There is also a standalone HTML gallery at `docs/screenshots/index.html` (works locally; can be served via GitHub Pages).

<table>
  <tr>
    <td align="center">
      <a href="docs/screenshots/left-click-root-menu.png">
        <img src="docs/screenshots/left-click-root-menu.png" width="320" />
      </a>
      <br />
      <sub>Root menu via left-click (common apps)</sub>
    </td>
    <td align="center">
      <a href="docs/screenshots/tabs-ui.png">
        <img src="docs/screenshots/tabs-ui.png" width="320" />
      </a>
      <br />
      <sub>Tabs UI (autotab placement)</sub>
    </td>
    <td align="center">
      <a href="docs/screenshots/style.png">
        <img src="docs/screenshots/style.png" width="320" />
      </a>
      <br />
      <sub>Style/theme parsing (thick border + tall titlebar)</sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <a href="docs/screenshots/window-alpha.png">
        <img src="docs/screenshots/window-alpha.png" width="320" />
      </a>
      <br />
      <sub>Per-window alpha over a generated spiral wallpaper</sub>
    </td>
    <td align="center">
      <a href="docs/screenshots/pseudo-transparency-compositing.png">
        <img src="docs/screenshots/pseudo-transparency-compositing.png" width="320" />
      </a>
      <br />
      <sub>Transparency with compositing (alpha shows background color)</sub>
    </td>
    <td align="center">
      <a href="docs/screenshots/pseudo-transparency-pseudo.png">
        <img src="docs/screenshots/pseudo-transparency-pseudo.png" width="320" />
      </a>
      <br />
      <sub>Forced pseudo-transparency (alpha samples wallpaper)</sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <a href="docs/screenshots/style-texture-gradient.png">
        <img src="docs/screenshots/style-texture-gradient.png" width="320" />
      </a>
      <br />
      <sub>Style texture: gradient</sub>
    </td>
    <td align="center">
      <a href="docs/screenshots/style-texture-pixmap-tiled.png">
        <img src="docs/screenshots/style-texture-pixmap-tiled.png" width="320" />
      </a>
      <br />
      <sub>Style texture: tiled pixmap</sub>
    </td>
    <td align="center">
      <a href="docs/screenshots/style-texture-parentrelative.png">
        <img src="docs/screenshots/style-texture-parentrelative.png" width="320" />
      </a>
      <br />
      <sub>Style texture: ParentRelative</sub>
    </td>
  </tr>
</table>

## Build / Run (quick notes)

- Build: `./autogen.sh && ./configure --enable-wayland && make -j` (or Wayland-only: `./configure --disable-x11 --enable-wayland`)
- Run (recommended): `util/startfluxbox-wayland`
- Smoke tests:
  - Main suite: `scripts/fbwl-smoke-all.sh` (SSH/headless-friendly; includes Xvfb/XWayland coverage where available)
  - CI helper: `scripts/fbwl-smoke-ci.sh` (same suite, but skips individual tests when host deps are missing)
  - Toolbar parity: `scripts/fbwl-smoke-toolbar-autohide.sh`, `scripts/fbwl-smoke-toolbar-autoraise.sh`, `scripts/fbwl-smoke-toolbar-maxover.sh`

To regenerate screenshots in `docs/screenshots/`:

- `FBWL_SMOKE_REPORT_DIR=docs/screenshots scripts/fbwl-smoke-left-click-menu.sh`
- `FBWL_SMOKE_REPORT_DIR=docs/screenshots scripts/fbwl-smoke-tabs-ui-click.sh`
- `FBWL_SMOKE_REPORT_DIR=docs/screenshots scripts/fbwl-smoke-style.sh`
- `FBWL_SMOKE_REPORT_DIR=docs/screenshots scripts/fbwl-smoke-style-textures.sh`
- `FBWL_SMOKE_REPORT_DIR=docs/screenshots scripts/fbwl-smoke-window-alpha.sh`
- `FBWL_SMOKE_REPORT_DIR=docs/screenshots scripts/fbwl-smoke-pseudo-transparency.sh`
