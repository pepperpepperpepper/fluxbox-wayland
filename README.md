# fluxbox-wayland

`fluxbox-wayland` is a **wlroots-based Wayland compositor** with Fluxbox-like policy (root menu, key/mouse bindings, workspaces, theming, lightweight).

This repo is an experimental/WIP port of Fluxbox concepts from X11 to Wayland:

- **`fluxbox-wayland` is a compositor**, not an X11 window manager running “on top of” Wayland.
- It manages native Wayland `xdg_toplevel` apps and can optionally run X11 apps via **XWayland**.
- Classic Fluxbox config formats are intentionally reused with an explicit **1:1 parity** target; remaining gaps are tracked in `whatsleft.md`.

Project notes / work tracking:

- `plan.md` (overall port plan + status snapshot)
- `whatsleft.md` (remaining parity tasks)

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
- Smoke tests: `scripts/fbwl-smoke-all.sh`

To regenerate screenshots in `docs/screenshots/`:

- `FBWL_SMOKE_REPORT_DIR=docs/screenshots scripts/fbwl-smoke-left-click-menu.sh`
- `FBWL_SMOKE_REPORT_DIR=docs/screenshots scripts/fbwl-smoke-tabs-ui-click.sh`
- `FBWL_SMOKE_REPORT_DIR=docs/screenshots scripts/fbwl-smoke-style.sh`
- `FBWL_SMOKE_REPORT_DIR=docs/screenshots scripts/fbwl-smoke-style-textures.sh`
- `FBWL_SMOKE_REPORT_DIR=docs/screenshots scripts/fbwl-smoke-window-alpha.sh`
- `FBWL_SMOKE_REPORT_DIR=docs/screenshots scripts/fbwl-smoke-pseudo-transparency.sh`
