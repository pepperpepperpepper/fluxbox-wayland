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

## CmdLang Parity — Per-Instance Stateful Commands

Goal: match Fluxbox/X11 “command object” semantics for stateful cmdlang commands so that identical `ToggleCmd`/`Delay`
strings in different bindings do **not** share state.

- [x] Scope `ToggleCmd` and `Delay` state per invoker (keys/mouse binding) (not per `(server, args string)`).
- [x] Smoke: prove two different bindings with identical `ToggleCmd {…} {…}` args don’t share state (`scripts/fbwl-smoke-keybinding-cmdlang.sh`).

## Wallpaper Utility Parity — `fbsetbg` on Wayland (Best-effort)

Goal: make classic Fluxbox configs/menu entries that call `fbsetbg …` work under `fluxbox-wayland` by routing to the
compositor’s internal wallpaper support (via `fbwl-remote wallpaper …`) when a Fluxbox-Wayland IPC socket is available.

- [x] `util/fbsetbg`: when `WAYLAND_DISPLAY` is set and the Fluxbox-Wayland IPC socket exists, set wallpaper via
      `fbwl-remote wallpaper <path>` (best-effort), otherwise fall back to classic X11 behavior unchanged.
- [x] Smoke: deterministic headless test proving `fbsetbg <png>` changes the compositor wallpaper (`scripts/fbwl-smoke-fbsetbg-wayland.sh`).

## Wallpaper Mode Parity — `fbsetbg -a/-f` on Wayland

Goal: honor classic `fbsetbg` mode flags on Wayland by letting the compositor render wallpapers in different modes and
ensuring pseudo transparency samples match the visible desktop background.

- [x] IPC: extend `wallpaper` command to accept `--mode <stretch|fill|center|tile>` (default: `stretch`)
- [x] Background: implement `fill` mode (aspect-preserving cover crop) and `center` mode (no scale, centered with background fill)
- [x] Pseudo transparency: `fbwl_pseudo_bg_update()` should compute wallpaper source mapping using the active wallpaper mode
- [x] `util/fbsetbg`: pass `-f/-a/-c/-t` as `wallpaper --mode …` and remember/restore mode for `-l` under Wayland
- [x] Smoke: deterministic headless test proving `-a` (fill) differs from `-f` (stretch)

## Wallpaper Mode Parity — `fbsetbg -t` (tile) on Wayland

Goal: implement true tile mode parity where the wallpaper image repeats unscaled and pseudo transparency samples match the
tiled desktop background (best-effort, multi-output).

- [x] Background: implement `tile` mode (repeat pattern across the output, aligned in global coords across outputs)
- [x] Pseudo transparency: ensure `fbwl_pseudo_bg_update()` matches the visible tiled background (no scaling; repeat)
- [x] Smoke: deterministic headless test proving `--mode tile` repeats (sample pixels beyond the first tile differ as expected)

## Focus Model Parity — StrictMouseFocus vs X11

Goal: match Fluxbox/X11 `MouseFocus` vs `StrictMouseFocus` semantics:
- `MouseFocus`: focus follows the pointer **only on pointer motion** (no focus shifts on window restack/geometry changes under a stationary cursor)
- `StrictMouseFocus`: also shifts focus on restack/geometry changes under a stationary cursor
- Neither model clears focus just because the pointer is on empty desktop (focus stays on last focused window)

- [x] Wayland: stop clearing focus on pointer leaving all views in `StrictMouseFocus` (keep last focused view, like X11)
- [x] Wayland: cancel `autoRaiseDelay` pending raise when the pointer leaves the focused view (even if focus is not cleared)
- [x] Smoke: update `scripts/fbwl-smoke-config-dir.sh` focus expectations accordingly (no `Focus: clear reason=pointer-leave` in StrictMouseFocus)

## Fluxbox-Remote Parity — CmdLang via Wayland IPC

Goal: match Fluxbox/X11 `fluxbox-remote` behavior where arbitrary Fluxbox command lines (the same commands used in
`~/.fluxbox/keys`, menus, etc) can be executed remotely.

- [x] IPC: when a command is not a built-in IPC command, attempt to execute it via the Fluxbox cmdlang parser
      (respect `allowRemoteActions`)
- [x] Smoke: prove a non-IPC command (like `SetAlpha 200 150`) works via `fluxbox-remote --wayland`
