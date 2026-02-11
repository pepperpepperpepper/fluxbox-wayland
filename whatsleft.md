# What's Left

This file tracks the remaining parity work items migrated out of `plan.md`.

## Pseudo Transparency on Wayland

Goal: implement Fluxbox/X11 `session.forcePseudoTransparency` semantics on Wayland: translucent menus/toolbars/slit/windows blend against the desktop background (wallpaper / background color) rather than the live scene (other windows), matching X11 “pseudo transparency”.

- [x] Config: honor `session.forcePseudoTransparency` (global) in the Wayland backend
  - When false (default): current behavior (true compositing) — alpha blends against whatever is behind.
  - When true: force pseudo transparency — alpha blends against background only (wallpaper or background color), never showing other windows through.

- [x] Implement wallpaper-backed underlays (“pseudo background”) for all alpha-rendered components:
  - Menus (`session.screenN.menu.alpha`)
  - Toolbar/slit/tooltip UI (toolbar alpha, slit alpha, etc)
  - Window alpha (`window.focus.alpha` / `window.unfocus.alpha` defaults + apps `[Alpha]` + `SetAlpha`)
  - Underlay content: use `server->wallpaper_buf` if set, else a solid rect with `server->background_color`
  - Underlay is **opaque** and sits directly behind the translucent element, so underlying windows are occluded

- [x] Window implementation details (alpha windows):
  - Add a per-view scene node (e.g. `view->pseudo_bg`) under `view->scene_tree` at `(0, 0)` sized to the client content (optionally the full frame area if we later apply window alpha to decorations too).
  - Use `wlr_scene_buffer_set_source_box()` + `wlr_scene_buffer_set_dest_size()` to crop/scale the wallpaper buffer so the underlay matches the desktop background region under the view.
  - Update the underlay on view move/resize/maximize/fullscreen/output-changes and on wallpaper changes.
  - Ensure `fbwl_view_alpha_apply()` does **not** apply opacity to the underlay buffer (it must stay opaque).

- [x] Multi-output semantics:
  - Pick the output under the view’s center (or the view’s primary output) to compute wallpaper mapping.
  - Document/accept the seam when a view spans multiple outputs (best-effort; matches our “per-output wallpaper” model).

- [x] Smoke: deterministic headless screencopy test proving pseudo transparency hides windows behind alpha windows
  - Script: `scripts/fbwl-smoke-pseudo-transparency.sh`
  - Setup: set a solid-color wallpaper (like `scripts/fbwl-smoke-wallpaper.sh`), spawn two overlapping clients, apply `SetAlpha 0` (or a low alpha) to the top window.
  - Assert via `fbwl-screencopy-client --expect-rgb` that a sampled pixel inside the top window area matches the wallpaper color (pseudo) and not the underlying window color.
  - Include a control assertion when pseudo is disabled (pixel matches underlying window), or run the scenario twice (`forcePseudoTransparency=false/true`).

## Theme/Pixmap Caching (Best-effort)

- [x] `session.cacheLife` / `session.cacheMax`: applied to the internal icon buffer cache used by menus/iconbar/window icons (`src/wayland/fbwl_ui_menu_icon.c`)
- [x] `session.colorsPerChannel`: parsed + stored; currently ignored on Wayland (kept for config compatibility)
