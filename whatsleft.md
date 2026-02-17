# What's Left

This file tracks parity work items migrated out of `plan.md`.

The **top section** is the active checklist for reaching **1:1 Fluxbox/X11 config parity** (i.e. classic `~/.fluxbox` configs work unchanged).
Completed items are kept below for historical context.

## Remaining for 1:1 parity

### Styles / Themes (`fluxbox-style(5)`)

The current Wayland theme implementation is intentionally simplified (mostly colors/fonts). True 1:1 parity requires implementing Fluxbox’s texture/pixmap style engine.

- [x] Implement Fluxbox texture parsing + rendering:
  - [x] Texture descriptors (`Flat|Raised|Sunken`, `Solid|Gradient`, gradient directions, `Interlaced`, `Bevel1|Bevel2`, `Invert`, `Tiled`, `ParentRelative`)
  - [x] Pixmap textures (`*.pixmap` keys) for key elements (PNG; XPM when built with `HAVE_XPM`) + search paths (theme dir `pixmaps/`, `~/.fluxbox/pixmaps`, etc)
  - [x] Apply textures to compositor UI backgrounds: menus, toolbar, slit, window titlebar
  - [x] `ParentRelative` per-element for the above (wallpaper-backed underlay), independent of `session.forcePseudoTransparency`
  - [x] Smoke: deterministic validation for gradients/pixmaps/ParentRelative (`scripts/fbwl-smoke-style-textures.sh`)
  - [x] Add caching for rendered textures/pixmap surfaces (avoid re-rendering on every rebuild; honors `session.cacheLife`/`session.cacheMax`)
- [x] Support style font effects: `*.font.effect` (`shadow`/`halo`) + related `*.font.shadow.*` / `*.font.halo.*` keys (best-effort; also accepts legacy `*.effect` forms)
- [x] Refactor `struct fbwl_decor_theme` to carry per-element textures (not just flat colors) and apply them to:
  - [x] Window decorations: split `window.title.*` vs `window.label.*` textures and render both (label overlays titlebar)
  - [x] Window decorations: `window.button.*` textures + per-button pixmap icons (`window.close.pixmap`, etc) + builtin fallback glyphs
  - [x] Window decorations: `window.frame.focusColor/unfocusColor` applied (focused/unfocused border colors)
  - [x] Tabs UI: `window.tab.*` parsing + `window.tab.label.*` textures/text colors + `window.tab.border*` (best-effort)
  - [x] Window decorations: render handle/grips (`window.handle.*`, `window.grip.*`, `window.handleWidth`) incl. grip resize contexts when border is off
  - [x] Window decorations: button pressed visuals (`window.button.pressed` + `*.pressed.pixmap`) on click/toggle where appropriate
  - [x] Tabs UI: honor `window.tab.justify` (left/center/right) in text rendering
  - [x] Menus (title/frame/hilite) — full texture/pixmap parity for menu subcomponents (incl. marks/submenu indicators)
  - [x] Toolbar + slit (and per-tool subcomponents where applicable)
    - [x] Parse/apply `toolbar.borderWidth`, `toolbar.borderColor`, `toolbar.bevelWidth` (with Fluxbox fallbacks)
    - [x] Parse/apply `slit.borderWidth`, `slit.borderColor`, `slit.bevelWidth` (with Fluxbox fallbacks)
    - [x] Slit theme fallback: `slit` texture inherits `toolbar` when unset (Fluxbox behavior)
	    - [x] Render toolbar/slit borders + bevel padding in the Wayland UI
	    - [x] Parse/render per-tool textures (`toolbar.clock`, `toolbar.workspace`/`toolbar.label`, `toolbar.iconbar.*`, `toolbar.button` + `.pressed`, `toolbar.systray`, and legacy `toolbar.windowLabel` fallbacks)
- [x] Add smoke coverage for gradients/pixmaps/parentrelative and update the screenshot gallery once rendering matches X11 styles
- [x] Support style text justification keys:
  - [x] `window.justify`
  - [x] `toolbar.clock.justify`, `toolbar.workspace.justify`, `toolbar.iconbar.focused.justify`, `toolbar.iconbar.unfocused.justify`
  - [x] Smoke: `scripts/fbwl-smoke-style-justify.sh`
- [x] Remaining Fluxbox style keys (still missing on Wayland):
  - [x] `background.color`, `background.colorTo`, `background.pixmap`, `background.modX`, `background.modY` (map to Wayland wallpaper/background; smoke: `scripts/fbwl-smoke-style-background.sh`)
  - [x] `window.bevelWidth` (parse + apply bevel padding semantics like X11; smoke: `scripts/fbwl-smoke-style-window-bevel.sh`)
  - [x] `window.roundCorners` (rounded decoration corners; smoke: `scripts/fbwl-smoke-style-window-round-corners.sh`)
  - [x] `menu.roundCorners` (rounded menu corners; smoke: `scripts/fbwl-smoke-style-menu-round-corners.sh`)
  - [x] `menu.frame.underlineColor` (type-ahead underline for `session.menuSearch` matches; smoke: `scripts/fbwl-smoke-style-menu-underline-color.sh`)
  - [x] `toolbar.shaped`, `toolbar.button.scale` (best-effort; smoke: `scripts/fbwl-smoke-style-toolbar-shaped-scale.sh`)

### Apps file (`fluxbox-apps(5)`)

- [x] `[Deco] {value}` parity:
  - Accept the full preset set: `NORMAL`, `NONE`, `BORDER`, `TAB`, `TINY`, `TOOL`
  - Support bitmask form and map flags onto the Wayland decoration implementation (titlebar/handle/border/buttons/tabs)
  - Ensure `apps` save/round-trip preserves unknown/bitmask values (saved as `0x...`)
  - Smoke: `scripts/fbwl-smoke-apps-deco-mask.sh`
- [x] ClientPattern parity (match terms inside `( … )`):
  - [x] Default property is `Name` (instance) when no key is given (Fluxbox semantics)
  - [x] Support `(role=...)` for XWayland windows (`WM_WINDOW_ROLE`) (listen `set_role` + X11 property fallback query for deterministic matching)
  - [x] Smoke: `scripts/fbwl-smoke-apps-rules-xwayland.sh` (instance default + role match)

### Keys / CmdLang (`fluxbox-keys(5)`)

- [x] Implement `BindKey` command parity (appends to `session.keyFile` and applies immediately; smoke-covered)
- [x] Implement `KeyMode <mode> [return-keybinding]` optional second arg (return binding now works; smoke-covered)
- [x] Add missing common aliases:
  - `MinimizeWindow` alias for `Minimize`
  - `MaximizeWindow` alias for `Maximize`
- [x] `SetDecor <decor>` parity: accept full apps-style `[Deco]` values/masks and apply them to the active window’s decoration mask
  - Smoke: `scripts/fbwl-smoke-setdecor.sh`
- [x] `Tab <number>` parity: support negative indices (`Tab -1` = last tab, `Tab -2` = next-to-last, etc)
  - Smoke: extended `scripts/fbwl-smoke-tabs-commands.sh` to cover negative tab selection
- [x] `StartResizing <corner>` parity: support all `fluxbox-keys(5)` modes (`NearestEdge`, `NearestCornerOrEdge` + size args, `Center`, single-edge `Top/Left/Right/Bottom`, and explicit corners)
  - Smoke: `scripts/fbwl-smoke-startresizing-args.sh`
- [x] Keys/mousebindings parser parity: accept fluxconf-mangled mouse tokens (e.g. `Mouse1ow`, `Mouse1top`, `Mouse1ebar`) as context synonyms for `OnWindow/OnDesktop/OnTitlebar Mouse1`
  - Smoke: `scripts/fbwl-smoke-mousebindings-fluxconf-mangled.sh`
- [x] Mousebinding context parity: support window button contexts (`OnWinButton`, `OnMinButton`, `OnMaxButton`) for titlebar buttons (and allow bindings to override default button actions)
  - Smoke: `scripts/fbwl-smoke-mousebindings-winbutton-contexts.sh`
- [x] Mousebinding precedence parity: `OnToolbar ...` bindings should override toolbar tool clicks (bindings handled before toolbar UI click handling)
  - Smoke: `scripts/fbwl-smoke-mousebindings-ontoolbar-precedence.sh`

### Menus / WindowMenu (`fluxbox-menu(5)`)

- [x] `~/.fluxbox/windowmenu` parity:
  - Support root-menu tags in windowmenu files (`[exec]`, `[submenu]`, `[include]`, etc)
  - Implement key windowmenu tags/semantics: `[maximize]` Btn1/2/3 mapping, `[sendto]` middle-click “send+follow”, `[extramenus]` → Remember… submenu (writes `apps`)
  - `[kill]` is forceful (disconnect Wayland client / SIGKILL XWayland pid) and distinct from `[close]` (xdg close)
  - Smoke: `scripts/fbwl-smoke-window-menu.sh`

### Session startup parity

- [x] `util/startfluxbox-wayland`: default to classic `~/.fluxbox/startup`; fall back to `startup-wayland` only for backward compatibility

### Final audit (once above lands)

- [x] Re-audit against upstream manpages (`fluxbox(1)`, `fluxbox-keys(5)`, `fluxbox-apps(5)`, `fluxbox-menu(5)`, `fluxbox-style(5)`) and add smokes for any newly found gaps
  - Menu syntax parity gap found: `fluxbox-menu(5)` escape semantics (`\\` escapes chars inside `(label)` / `{command}` / `<icon>`) were not fully implemented for parentheses/angle (and had an even/odd backslash bug for braces).
  - Fix: implement proper escaped-delimiter matching + unescaping for all menu fields.
  - Smoke: `scripts/fbwl-smoke-menu-escaping.sh` (escaped `\\)` in menu labels is searchable/selectable)

## XWayland Parity — XEmbed Tray Icons (Best-effort)

Goal: support legacy X11 XEmbed/system-tray icons under XWayland by translating XEmbed dock requests into StatusNotifierItems rendered by the toolbar tray.

- [x] Provide an in-repo proxy (`xembed-sni-proxy`) and auto-start it when XWayland is ready
  - Uses local `./xembed-sni-proxy` (built from this repo) or falls back to system `xembed-sni-proxy` / `xembedsniproxy` / `snixembed`.
  - Built when Wayland + `libsystemd` + `xcb-composite` are available.
  - Controlled via `FBWL_XEMBED_SNI_PROXY` (default: `auto`).
  - Smoke: `scripts/fbwl-smoke-xembed-tray.sh`

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
