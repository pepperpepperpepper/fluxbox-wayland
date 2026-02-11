# Fluxbox → Wayland (Compositor) Port Plan

Target: **Fluxbox manages native Wayland apps** (Fluxbox becomes a Wayland compositor, not an X11 WM running under Wayland).

Primary product goal: a daily-usable compositor with the “Fluxbox feel” (menus, key/mouse bindings, workspaces, theming, lightweight), while preserving existing Fluxbox config formats **and variables** (parity) and only adding Wayland-specific keys where unavoidable.

Hard constraint: **SSH/terminal-only development and testing**.
- Do not rely on “seeing” a compositor window, clicking with a real mouse, etc.
- Every acceptance check must be runnable non-interactively and exit `0/1`.
- Keep the build + test commands below up-to-date as we add new features:
  - update the `make ...` line in “Quick start”
  - add new smoke tests to `scripts/fbwl-smoke-all.sh`
  - list new smoke scripts in this document and attach them to the relevant milestone’s Acceptance section

## Status Snapshot (as of 2026-01-26)

- `scripts/fbwl-smoke-all.sh` passes over SSH (headless + Xvfb/X11 backend + XWayland + policy/UI/protocol smoke coverage, including `scripts/fbwl-smoke-xvfb.sh`, `scripts/fbwl-smoke-xvfb-decor-style.sh`, `scripts/fbwl-smoke-xvfb-policy.sh` (also covers input + move/resize grabs on the X11 backend), `scripts/fbwl-smoke-xvfb-protocols.sh`, `scripts/fbwl-smoke-xvfb-tray.sh`, `scripts/fbwl-smoke-xvfb-xwayland.sh`, and portal tests via `scripts/fbwl-smoke-xvfb-portal.sh`).
- CI helper: `scripts/fbwl-smoke-ci.sh` runs the same suite but **skips** individual smoke scripts when required host commands/binaries are missing (parses `need_cmd`, `need_exe`, `-x /path` checks, and `./fbwl-*`/`./fluxbox-wayland` invocations; includes nested smoke script deps).
- Wayland-only build works: `./configure --disable-x11 --enable-wayland` builds `fluxbox-wayland` + Wayland smoke/IPCs without building X11 `fluxbox`/`libFbTk` (verified with `scripts/fbwl-smoke-headless.sh`).
- Protocol logging smoke test passes: `scripts/fbwl-smoke-log-protocol.sh` (runs `fluxbox-wayland --log-protocol` and asserts `WAYLAND REQ/EVT` logs).
- Portal smoke tests pass:
  - Xvfb wrapper: `scripts/fbwl-smoke-xvfb-portal.sh` (runs the portal suite under the wlroots `x11` backend; output auto-detected if `OUTPUT_NAME` is unset).
  - Screenshot: `scripts/fbwl-smoke-portal-wlr.sh` (writes `/tmp/out.png`; auto-isolates `/tmp` via `unshare` when needed to avoid shared `/tmp` collisions).
  - ScreenCast: `scripts/fbwl-smoke-portal-wlr-screencast.sh` (creates a PipeWire node; validated via `pw-cli info`).
  - ScreenCast (front-end): `scripts/fbwl-smoke-xdg-desktop-portal.sh` (runs `xdg-desktop-portal` + `xdg-desktop-portal-wlr` and validates a PipeWire node via `pw-cli info`).
  - Screenshot (front-end): `scripts/fbwl-smoke-xdg-desktop-portal-screenshot.sh` (runs `xdg-desktop-portal` + `xdg-desktop-portal-wlr` and validates a PNG output URI). This currently **skips** when `xdg-desktop-portal-wlr` exposes `org.freedesktop.impl.portal.Screenshot` version `< 2` (because `xdg-desktop-portal` will not export `org.freedesktop.portal.Screenshot` in that case).
- Docs: man pages for `fluxbox-wayland` and `startfluxbox-wayland` exist (`doc/fluxbox-wayland.1.in`, `doc/startfluxbox-wayland.1.in`).

## Current Implementation Status (in this repo)

This repo already contains a wlroots-based compositor MVP plus deterministic headless tests:

- Compositor: `src/wayland/fluxbox_wayland.c` (builds `fluxbox-wayland` with `--enable-wayland`)
  - `xdg-shell` toplevels/popups are rendered via `wlr_scene_*`
  - Protocols: `xdg-activation-v1`, `xdg-decoration-unstable-v1`, `ext-idle-notify-v1`, `idle-inhibit-unstable-v1`, `ext-session-lock-v1`, `xdg-output-unstable-v1`, `wlr-output-management-unstable-v1`, `wlr-output-power-management-unstable-v1`, `wlr-screencopy-unstable-v1`, `wlr-export-dmabuf-unstable-v1`, `ext-image-copy-capture-v1`, `ext-image-capture-source-v1`, `viewporter`, `fractional-scale-v1`, `cursor-shape-v1`, `presentation-time`, `text-input-unstable-v3`, `input-method-unstable-v2`, `primary-selection-unstable-v1`, `wlr-data-control-unstable-v1`, `ext-data-control-v1`, `keyboard-shortcuts-inhibit-unstable-v1`, `single-pixel-buffer-v1`
  - Seat/cursor plumbing, click-to-focus, Alt+drag move/resize
  - Workspace switching + move-to-workspace (see keybindings below)
- Policy core (backend-agnostic): `src/wmcore/fbwm_core.h`, `src/wmcore/fbwm_core.c`
  - Focus MRU, focus cycle, and a minimal workspace model
- In-repo deterministic clients:
  - `util/fbwl-smoke-client.c` (minimal xdg-shell client for smoke tests)
  - `util/fbwl-clipboard-client.c` (Wayland clipboard set/get smoke client via `wl_data_device_manager`)
  - `util/fbwl-cursor-shape-client.c` (cursor-shape set smoke client)
  - `util/fbwl-data-control-client.c` (wlr/ext data-control set/get smoke client)
  - `util/fbwl-dnd-client.c` (Wayland drag-and-drop smoke client via `wl_data_device_manager`)
  - `util/fbwl-presentation-time-client.c` (presentation-time feedback smoke client)
  - `util/fbwl-primary-selection-client.c` (primary selection set/get smoke client)
  - `util/fbwl-relptr-client.c` (relative-pointer + pointer-constraints smoke client)
  - `util/fbwl-screencopy-client.c` (wlr-screencopy smoke client)
  - `util/fbwl-export-dmabuf-client.c` (wlr-export-dmabuf capture smoke client)
  - `util/fbwl-output-management-client.c` (wlr-output-management apply+verify smoke client)
  - `util/fbwl-output-power-client.c` (wlr-output-power-management toggle+verify smoke client)
  - `util/fbwl-xdg-output-client.c` (xdg-output smoke client for output logical position/size/name)
  - `util/fbwl-viewporter-client.c` (viewporter smoke client)
  - `util/fbwl-fractional-scale-client.c` (fractional-scale smoke client)
  - `util/fbwl-text-input-client.c` (text-input-v3 smoke client)
  - `util/fbwl-input-method-client.c` (input-method-v2 smoke client)
  - `util/fbwl-xdg-activation-client.c` (xdg-activation token + activate focus smoke client)
  - `util/fbwl-xdg-decoration-client.c` (xdg-decoration negotiation smoke client)
  - `util/fbwl-idle-client.c` (ext-idle-notify + idle-inhibit smoke client)
  - `util/fbwl-session-lock-client.c` (ext-session-lock smoke client)
  - `util/fbwl-shortcuts-inhibit-client.c` (keyboard-shortcuts-inhibit smoke client)
  - `util/fbwl-single-pixel-buffer-client.c` (single-pixel-buffer smoke client)
  - `util/fbwl-remote.c` (unix-socket IPC client for scripted control; Wayland replacement for `fluxbox-remote`)
  - `util/fbwl-sni-item-client.c` (DBus StatusNotifierItem registration test client for tray/SNI smoke tests)
  - `util/fbwl-xdp-portal-client.c` (DBus `xdg-desktop-portal` ScreenCast (front-end) smoke client)
  - `util/fbwl-input-injector.c` (virtual pointer/keyboard injector)
  - `util/fbwl-foreign-toplevel-client.c` (foreign-toplevel client for minimize/unminimize tests)
  - `util/fbwl-layer-shell-client.c` (layer-shell client for panel/reserved-area tests)
  - `util/fbx11-smoke-client.c` (minimal X11 client for XWayland smoke tests)
- SSH-friendly smoke scripts (exit 0/1):
  - `scripts/fbwl-smoke-all.sh`
  - `scripts/fbwl-smoke-ci.sh`
  - `scripts/fbwl-smoke-headless.sh`
  - `scripts/fbwl-smoke-log-protocol.sh`
  - `scripts/fbwl-smoke-background.sh`
  - `scripts/fbwl-smoke-xvfb.sh`
  - `scripts/fbwl-smoke-xvfb-protocols.sh`
  - `scripts/fbwl-smoke-xvfb-tray.sh`
  - `scripts/fbwl-smoke-xvfb-xwayland.sh`
  - `scripts/fbwl-smoke-xvfb-portal.sh`
  - `scripts/fbwl-smoke-xwayland.sh`
  - `scripts/fbwl-smoke-input.sh`
  - `scripts/fbwl-smoke-keys-file.sh`
  - `scripts/fbwl-smoke-config-dir.sh`
  - `scripts/fbwl-smoke-apps-rules.sh`
  - `scripts/fbwl-smoke-apps-rules-xwayland.sh`
  - `scripts/fbwl-smoke-move-resize.sh`
  - `scripts/fbwl-smoke-workspaces.sh`
  - `scripts/fbwl-smoke-maximize-fullscreen.sh`
  - `scripts/fbwl-smoke-minimize-foreign.sh`
  - `scripts/fbwl-smoke-layer-shell.sh`
  - `scripts/fbwl-smoke-multi-output.sh`
  - `scripts/fbwl-smoke-fullscreen-stacking.sh`
  - `scripts/fbwl-smoke-ipc.sh`
  - `scripts/fbwl-smoke-startfluxbox-wayland.sh`
  - `scripts/fbwl-smoke-fluxbox-remote.sh`
  - `scripts/fbwl-smoke-sni.sh`
  - `scripts/fbwl-smoke-tray.sh`
  - `scripts/fbwl-smoke-tray-iconname.sh`
  - `scripts/fbwl-smoke-tray-icon-theme-path.sh`
  - `scripts/fbwl-smoke-tray-attention.sh`
  - `scripts/fbwl-smoke-tray-overlay.sh`
  - `scripts/fbwl-smoke-tray-passive.sh`
  - `scripts/fbwl-smoke-clipboard.sh`
  - `scripts/fbwl-smoke-data-control.sh`
  - `scripts/fbwl-smoke-dnd.sh`
  - `scripts/fbwl-smoke-cursor-shape.sh`
  - `scripts/fbwl-smoke-presentation-time.sh`
  - `scripts/fbwl-smoke-primary-selection.sh`
  - `scripts/fbwl-smoke-relptr.sh`
  - `scripts/fbwl-smoke-screencopy.sh`
  - `scripts/fbwl-smoke-export-dmabuf.sh`
  - `scripts/fbwl-smoke-portal-wlr.sh`
  - `scripts/fbwl-smoke-portal-wlr-screencast.sh`
  - `scripts/fbwl-smoke-xdg-desktop-portal.sh`
  - `scripts/fbwl-smoke-xdg-desktop-portal-screenshot.sh`
  - `scripts/fbwl-smoke-output-management.sh`
  - `scripts/fbwl-smoke-output-power.sh`
  - `scripts/fbwl-smoke-xdg-output.sh`
  - `scripts/fbwl-smoke-viewporter.sh`
  - `scripts/fbwl-smoke-fractional-scale.sh`
  - `scripts/fbwl-smoke-xdg-activation.sh`
  - `scripts/fbwl-smoke-xdg-decoration.sh`
  - `scripts/fbwl-smoke-ssd.sh`
  - `scripts/fbwl-smoke-style.sh`
  - `scripts/fbwl-smoke-menu.sh`
  - `scripts/fbwl-smoke-window-menu.sh`
  - `scripts/fbwl-smoke-toolbar.sh`
  - `scripts/fbwl-smoke-iconbar.sh`
  - `scripts/fbwl-smoke-command-dialog.sh`
  - `scripts/fbwl-smoke-osd.sh`
  - `scripts/fbwl-smoke-idle.sh`
  - `scripts/fbwl-smoke-session-lock.sh`
  - `scripts/fbwl-smoke-shortcuts-inhibit.sh`
  - `scripts/fbwl-smoke-single-pixel-buffer.sh`
  - `scripts/fbwl-smoke-text-input.sh`

Keybindings (current MVP, subject to change):
- Built-in defaults can be extended/overridden via `--keys FILE` (minimal subset of Fluxbox `~/.fluxbox/keys` syntax; see `scripts/fbwl-smoke-keys-file.sh`).
- Fluxbox apps (“Remember”) rules can be loaded via `--apps FILE` (minimal subset of Fluxbox `~/.fluxbox/apps` syntax; see `scripts/fbwl-smoke-apps-rules.sh`).
- Fluxbox config directory can be loaded via `--config-dir DIR` (minimal subset of `~/.fluxbox/init` plus `keys`/`apps` discovery; see `scripts/fbwl-smoke-config-dir.sh`).
- `Alt+Return`: spawn terminal (`--terminal CMD`, default `weston-terminal`)
- `Alt+Escape`: exit compositor
- `Alt+F1`: cycle to next toplevel
- `Alt+F2`: open command dialog
- `Alt+M`: toggle maximize
- `Alt+F`: toggle fullscreen
- `Alt+I`: toggle minimize
- `Alt+[1-9]`: switch workspace
- `Alt+Ctrl+[1-9]`: move focused view to workspace

---

This plan is written against the upstream-ish Fluxbox codebase, which is heavily X11-centric:
- X event loop: `src/fluxbox.cc` (`Fluxbox::eventLoop()` uses `XPending/XNextEvent`).
- “App” abstraction is Xlib: `src/FbTk/App.hh` (`Display*`, XIM).
- Client model is an X window: `src/WinClient.hh` (`WinClient : Focusable, FbTk::FbWindow`).
- IPC tool is X root-window property based: `util/fluxbox-remote.cc`.
- Build hard-requires X11: `configure.ac` (`PKG_CHECK_MODULES([X11], [ x11 ], ...)`).

Wayland’s architecture is fundamentally different (no reparenting, no global restack operations exposed to clients, different focus and activation semantics). A successful port requires:
1) new compositor plumbing, and
2) a refactor boundary separating **WM policy** from **display-system mechanics**.

---

## 0) Scope, Non-Goals, and Definitions

### 0.1 Scope (what “Wayland compatible” means here)
- We are building a **Wayland compositor** that can:
  - manage `xdg_toplevel` applications (native Wayland GUI apps),
  - provide desktop-like features (keybindings, workspaces, focus/placement rules),
  - optionally run **X11 apps via XWayland**.
- Feature parity means “behaviorally similar where Wayland allows it”, not “identical to X11 hacks”.
- Config parity means existing classic Fluxbox config (`~/.fluxbox/init`, `keys`, `apps`, `menu`, styles/overlay, etc.) should load and behave the same by default; where Wayland has no direct analogue, implement the closest behavior and document it (avoid silent ignore).

### 0.2 Non-goals (explicitly out of scope early)
- Don’t try to “swap Xlib calls for Wayland calls” inside existing window/frame code; it will not map.
- Don’t attempt to port all of `FbTk` rendering primitives first (high risk of stalling).
- Don’t promise X11 dockapps / XEMBED tray parity for Wayland-native clients.

### 0.3 Glossary (terms used throughout)
- **View**: a managed top-level window-like entity (native `xdg_toplevel` or XWayland surface).
- **Surface tree**: a view’s `wl_surface` plus subsurfaces/popups, all of which must be composed correctly.
- **Output**: a monitor (physical output on DRM, or a nested window on the wlroots X11 backend).
- **Seat**: input seat (keyboard/pointer/touch/tablet/etc).
- **Scene**: compositor-side representation of what is rendered; typically a tree supporting stacking, damage, and transforms.

---

## 1) Guiding Decisions (Make Early, Revisit Rarely)

### 1.1 Compositor foundation
Recommendation: **wlroots**.
- Why: proven backends (DRM/libinput, Wayland, X11, headless), renderer integration, many “desktop-grade” protocol helpers, XWayland integration.
- Cost: introduces a substantial dependency; you still write all policy (workspaces, focus, menus, decorations).

Alternative: libweston (more opinionated), or raw libwayland+DRM/libinput (high effort, no advantage here).

### 1.2 Rendering / composition strategy
Recommendation: use the wlroots **scene graph** (e.g. `wlr_scene`) from the start.
- It simplifies:
  - correct stacking order,
  - damage tracking,
  - output rendering loops,
  - transforming/positioning surface trees.
- It also gives a natural “place” to attach decorations and compositor UI.

### 1.3 UI strategy (menus/toolbar/dialogs)
There are two viable approaches; the plan supports a bootstrap path and a parity path:
1) **Bootstrap**: external helper clients (panel/menu/launcher) using `layer-shell`.
   - Fast path to a usable desktop.
   - Moves complexity into separate processes; requires IPC and careful UX integration.
2) **Parity**: internal compositor UI surfaces drawn by Fluxbox itself.
   - Best “Fluxbox feel” and theme compatibility.
   - Requires re-implementing the X-dependent drawing path.

Recommendation: ship a bootstrap UI early to unblock usability, then converge on parity UI.

### 1.4 Compatibility strategy
Enable **XWayland** early.
- Keeps X11 apps working during the transition.
- Preserves ICCCM/EWMH interactions *for X11 clients* (do not force EWMH semantics onto native Wayland apps).

### 1.5 Build strategy
Keep the existing X11 `fluxbox` building as-is while adding a new `fluxbox-wayland` binary.
- Early on, `--enable-wayland` should be opt-in.
- Optional: `--disable-x11` for a Wayland-only build (skip building X11 `fluxbox` + `libFbTk`).

---

## 2) Target Architecture: Split “Policy Core” from “Backend”

### 2.1 The refactor boundary (the most important technical goal)
WM logic must stop calling Xlib directly. Instead, policy should talk to a backend-agnostic API.

Backend-agnostic concepts:
- `View`: title/app-id, state (fullscreen/maximized/sticky/etc), geometry, focus, close.
- `Output`: logical geometry/scale/transform and hotplug/reconfigure events.
- `Seat`: keyboard/pointer focus, modifier state, button/key events.

Backend responsibilities:
- Provide actual windows/surfaces and feed them into `View` objects.
- Implement protocol glue (xdg-shell, data device, decorations, etc).
- Provide rendering + input device plumbing.
- Expose “backend events” to policy (new view, view state requests, input events, output changes).

Policy responsibilities:
- Focus model, workspace model, stacking/layers, placement rules, key/mouse bindings.
- “Fluxbox UX”: menus, toolbar, theming rules, remember/app rules.
- Compositor-visible state not mandated by Wayland (minimize/hide, sticky behavior, “raise” semantics).

### 2.2 Minimal boundary for the first extraction
Start with:
- `View`: `focus()`, `is_mapped()`, `title()`, `app_id()` (already implemented in `src/wmcore/fbwm_core.*`)
- Add next:
  - geometry getters/setters (logical position + size),
  - state toggles (fullscreen/maximize),
  - workspace assignment and visibility toggles,
  - close request.

### 2.3 Minimal Wayland backend object model
Server/core objects (wlroots):
- `wl_display` + socket (`wl_display_add_socket_auto()` or a named socket for deterministic tests)
- `wlr_backend` (from `wlr_backend_autocreate()`)
- `wlr_renderer` + `wlr_allocator` (autocreate against the backend)
- `wlr_compositor`, `wlr_subcompositor`
- `wlr_data_device_manager` (clipboard + DnD foundation)

Outputs + rendering:
- `wlr_output_layout` (global logical coordinate space)
- `wlr_scene` + scene output integration (`wlr_scene_attach_output_layout`)
- Backend `new_output` listener:
  - pick/enable a mode, set scale/transform, commit output state
  - create scene output node and render on `output->events.frame`

Views / surfaces:
- `wlr_xdg_shell` (listen for new `xdg_surface`)
  - for each `xdg_toplevel`, create a `WaylandView`
  - attach its surface tree into the scene (prefer wlroots scene helpers)
- Popup/subsurface handling must follow the toplevel’s surface tree so menus/tooltips render above correctly.

Input:
- `wlr_seat` (seat capabilities + focus routing)
- `wlr_cursor` + `wlr_xcursor_manager`
- Backend `new_input` listener:
  - add pointer devices to the cursor, attach cursor to output layout
  - set up keyboards via `xkbcommon` keymaps, key repeat, modifier tracking

Optional-but-early (keeps you productive during port):
- XWayland (`wlr_xwayland`) so X11 apps work as `View`s.
- Layer-shell (`zwlr_layer_shell_v1`) so a basic panel/menu can exist as a helper client.

---

## 3) Feature Mapping: Fluxbox Features → Wayland Implementations

This is the “research-driven” translation layer: what needs a protocol, what needs new internal behavior, and what can be preserved.

### 3.1 Core window management
- Focus models (click-to-focus, focus-follows-mouse): **policy core** + `wlr_seat` focus routing.
- Raise/lower/stacking layers: **scene graph ordering** (no client-driven restack).
- Move/resize: **compositor-side interactive grabs**, and correct `xdg_toplevel.configure` sequencing for size changes.
- Maximize/fullscreen: `xdg_toplevel` states + output-aware geometry.
- Minimize/iconify: no universal Wayland semantic; implement **internal hidden state** and expose it via foreign-toplevel management for panels.
- Sticky windows: internal policy across workspaces/outputs.
- Window placement strategies: reuse existing placement algorithms once geometry and struts are abstracted.

### 3.2 Desktop UI (decorations, menus, toolbar, dialogs)
- Client-side decorations (CSD): works “for free”; you still manage placement/stacking.
- Server-side decorations (SSD): implement compositor-rendered frames + hit-testing.
  - Negotiate via `xdg-decoration` where possible.
  - Provide per-app rules (“force SSD”, “force CSD”, “no decorations”) similar to Fluxbox `apps` rules.
- Root menu / window menu / command dialog / OSD:
  - Bootstrap option: external layer-shell clients.
  - Parity option: internal UI layer drawn by compositor, themed by Fluxbox styles.

### 3.3 “Tray”, slit, and legacy integrations
- XEMBED tray: not a Wayland concept; replace with **StatusNotifierItem (SNI)** host via DBus.
  - Current implementation: compositor exports `org.kde.StatusNotifierWatcher` over the session bus, logs item register/unregister,
    and shows tray slots in the internal toolbar.
    - Icons: renders `org.kde.StatusNotifierItem.IconPixmap` (ARGB32) when provided; otherwise draws a placeholder.
    - IconName fallback: when `IconPixmap` is missing/errors, query `org.kde.StatusNotifierItem.IconName` and load a PNG from
      the XDG icon theme search path (`$XDG_DATA_HOME`, `$XDG_DATA_DIRS`), optionally preferring `$FBWL_ICON_THEME` if set.
    - IconThemePath: if the item provides `org.kde.StatusNotifierItem.IconThemePath`, include it as an additional icon search root
      for `IconName`-based lookups (useful for app-bundled icons).
    - Status/attention/overlay:
      - respects `Status` (`Passive` hides; `NeedsAttention` uses `AttentionIcon*` when available)
      - composites `OverlayIcon*` onto the primary icon when provided
    - Icon updates: listens for `NewIcon` / `NewAttentionIcon` / `NewOverlayIcon` / `NewStatus` / `PropertiesChanged` and refreshes.
    - Clicks: left click → `Activate`, middle click → `SecondaryActivate`, right click → `ContextMenu`.
    (see `scripts/fbwl-smoke-sni.sh` and `scripts/fbwl-smoke-tray.sh`).
- Slit/dockapps: no native analogue; choose one:
  - support only XWayland dockapps (best-effort), or
  - replace with a Wayland-native “dock area” using layer-shell.
- Wallpaper (`fbsetbg`/`fbsetroot`): root-window background doesn’t exist; choose one:
  - compositor internal background renderer, or
  - ship a tiny layer-shell wallpaper client.
  - Current implementation: compositor-drawn solid background via `--bg-color #RRGGBB[AA]` (see `scripts/fbwl-smoke-background.sh` for terminal-only verification).

### 3.4 Fluxbox utilities and remote control
- `fluxbox-remote`: rewrite to talk to a compositor IPC endpoint (unix socket and/or DBus).
- `fbrun`: can remain a launcher client; under Wayland it should be a Wayland client (or use layer-shell).
- Session startup (`startfluxbox`): add a Wayland variant that sets env (`XDG_SESSION_TYPE=wayland`, etc), ensures a DBus session (e.g., via `dbus-run-session` if needed), and starts the compositor.

### 3.5 Config compatibility (classic Fluxbox parity target)
The fastest path to adoption is: keep formats stable and keep users’ configs working unchanged. Add Wayland-only options only where needed and under a clear namespace.

Suggested compatibility targets:
- `~/.fluxbox/init`: keep; add Wayland-specific keys under a clear namespace:
  - output config defaults (scale, transform), per-output workspace policy,
  - input (xkb layout/variant/options), cursor theme/size,
  - XWayland enable/disable.
  - Current implementation: optional `--config-dir DIR` loader:
    - Loads `DIR/init` if present and understands: `session.screen0.workspaces`, `session.keyFile`, `session.appsFile`, `session.styleFile`, `session.menuFile`.
    - If `session.keyFile`/`session.appsFile` are unset, falls back to `DIR/keys` and `DIR/apps` when present.
    - Precedence: `--workspaces`/`--keys`/`--apps`/`--style` override `init`; no config is auto-loaded unless `--config-dir` is provided (keeps smoke tests deterministic).
- `~/.fluxbox/keys`: keep; reuse parser, but map to compositor key events:
  - “Mod1/Mod4/Control/Shift” come from `xkbcommon` state, not X keycodes.
  - Current implementation: optional `--keys FILE` loader with a minimal subset:
    - `Mod1/Mod4/Control/Shift` modifiers, `KEYSYM` (via `xkb_keysym_from_name`), `:COMMAND ...`
    - Supported commands: `ExecCommand`, `Exit`, `Reconfigure`, `KeyMode`, `NextWindow`/`PrevWindow`, `NextTab`/`PrevTab`/`Tab N`, `Maximize`/`MaximizeHorizontal`/`MaximizeVertical`, `Fullscreen`, `Minimize`/`Iconify`, workspace and move/take variants (`Workspace N`, `Next/PrevWorkspace`, `SendTo*`, `TakeTo*`), stacking/layer (`Raise`/`Lower`/`RaiseLayer`/`LowerLayer`/`SetLayer`), menus (`WindowMenu`/`RootMenu`/`WorkspaceMenu`/`HideMenu(s)`), `StartMoving`/`StartResizing`, and `MacroCmd`.
- `~/.fluxbox/menu`: keep; menu entries are just process spawns.
- `~/.fluxbox/apps` (“Remember” rules): keep; extend matching:
  - Wayland: `app_id` and title
  - XWayland: keep WM_CLASS/WM_NAME matching
  - Current implementation: optional `--apps FILE` loader with a minimal subset:
    - Matches `[app]` entries with `(app_id=...)`/`(title=...)` plus X11-style aliases:
      - `(class=...)` → app_id / XWayland WM_CLASS(class)
      - `(name=...)` → XWayland WM_CLASS(instance) (falls back to `app_id` for native Wayland)
      - bare `(FOO)` defaults to `app_id`; `!=` supported.
    - Patterns use POSIX extended regex (ERE).
    - Supported settings: `[Workspace] {0-N}` (0-based IDs), `[Jump] {yes|no}`, `[Sticky] {yes|no}`, `[Minimized] {yes|no}`, `[Maximized] {yes|no|horz|vert}`, `[Fullscreen] {yes|no}`.
- `~/.fluxbox/styles/*` and `~/.fluxbox/overlay`: keep file formats; port rendering backend.

Docs impact:
- Current implementation: man pages `doc/fluxbox-wayland.1` and `doc/startfluxbox-wayland.1`.

Parity roadmap (requested):
- Expand `--config-dir` from “minimal subset” to “full classic init/resource surface”:
  - Load and apply all `session.*` and `session.screenN.*` resources used by Fluxbox (not just workspaces + file discovery).
  - Support Fluxbox theme overlay semantics (`session.styleFile` + `session.styleOverlay`).
  - Avoid regressions for SSH smoke tests by keeping explicit CLI overrides (`--keys/--apps/--menu/--style/--workspaces`) as highest precedence.
- Make `startfluxbox-wayland` pass `--config-dir ~/.fluxbox` by default (to match user expectations), while keeping the compositor binary deterministic for tests/CI.

---

## 4) Wayland Protocols & Subsystems Checklist

This is the “desktop completeness” checklist. Don’t try to implement all of it up front; use it to avoid missing major user-visible features.

### 4.1 Core / required for “any desktop”
- `xdg-shell`: toplevel windows + popups.
- `wl_seat`, `wl_keyboard`, `wl_pointer`: input.
- `wl_data_device_manager`: clipboard and DnD basics.

### 4.2 Window management / UX protocols
- `xdg-decoration`: negotiate client/server decorations.
- `viewporter`: scaling and viewport cropping for some clients.

### 4.3 Shell ecosystem protocols (integration with panels/pagers/tools)
- `wlr-layer-shell-unstable-v1`: panels, notifications, desktop widgets (if using helper clients).
- `wlr-foreign-toplevel-management-unstable-v1`: taskbars/pagers and “minimized” state reporting.
- `wlr-output-management-unstable-v1` (+ output power management): dynamic output config tools.

### 4.4 Input + “real desktop” expectations
- `pointer-constraints` + `relative-pointer`: games/VMs/remote desktops.
- Drag-and-drop: data device DnD paths (often “just works” once data device is correct, but must be tested).
- Touch/tablet/switches: tablet v2, touch events; optional but increasingly expected.

### 4.5 Screen capture, portals, and privacy
- `wlr-screencopy-unstable-v1`: screenshots/recording and portal support.
- `wlr-export-dmabuf-unstable-v1`: efficient capture for PipeWire/portal stacks.

### 4.6 Text input / IME (often overlooked, highly visible if missing)
- `text-input-v3` and `input-method-v2` (or the wlroots-supported equivalents): required for non-trivial IME use.

### 4.7 Session/locking/idle
- `idle-inhibit`: prevent idle while playing video, etc.
- idle notify + session lock protocols (staging/ext protocols): for screen lockers and power management.

---

## 5) Phased Implementation Plan (Milestones + Acceptance Criteria)

The milestones are ordered to produce a usable compositor early and avoid “rewrite everything first”.
Each milestone includes an acceptance test so progress is measurable.

### Milestone A — Build + run a “hello compositor” (`fluxbox-wayland`)
Deliverables:
- New `fluxbox-wayland` binary builds via `--enable-wayland`.
- Starts a wlroots compositor, adds a Wayland socket, and exits cleanly on SIGINT/SIGTERM.
- Accepts `xdg_toplevel` surfaces and renders them (even undecorated).

Implementation sketch:
- Initialize wlroots logging early.
- Create `wl_display`, backend, renderer, allocator, scene, output layout.
- Set up listeners: `new_output`, `new_input`, `new_xdg_surface`.
- Implement a minimal render loop (scene → output) on frame events.
- Print the chosen `WAYLAND_DISPLAY` socket name on startup (make testing scripts easy).

Build notes:
- wlroots headers are C (not C++) in practice; keep the compositor “plumbing” in C or behind a C wrapper.
- Protocol headers: ensure `xdg-shell` server and client protocol headers are available (vendored or generated).

Acceptance:
- SSH/terminal-only: a scripted smoke test passes (no GUI interaction required).
  - Headless backend smoke: `scripts/fbwl-smoke-headless.sh`.
  - Xvfb + X11 backend smoke: `scripts/fbwl-smoke-xvfb.sh` and `scripts/fbwl-smoke-xvfb-protocols.sh`.
  - (Both use the in-repo `fbwl-smoke-client` to create an `xdg_toplevel` and complete the xdg-shell handshake.)

### Milestone B — Input + focus MVP
Deliverables:
- Pointer focus and keyboard focus work.
- Basic keybindings work (hardcoded first), including quit/reload.

Key tasks:
- Implement `Seat` and `Cursor` plumbing (wlroots cursor + xcursor manager).
- Implement a backend-agnostic internal key event format so `src/Keys.*` parsing can be reused.
- Add a deterministic way to test input over SSH:
  - Preferred: enable wlroots virtual input globals (`wlr_virtual_keyboard_manager_v1_create`,
    `wlr_virtual_pointer_manager_v1_create`) and provide a tiny in-repo client to inject keys/clicks.
  - Alternative: add a small “test control socket”/IPC endpoint which can inject internal key/pointer events.
- Decide on “global shortcut” rules (compositor-reserved combos vs client receives key):
  - e.g. Mod4+… is compositor-only by default; others go to focused client.

Acceptance:
- SSH/terminal-only:
  - Using virtual pointer (or test IPC), focus can be changed between two test clients.
  - Using virtual keyboard (or test IPC), a keybinding triggers an observable action (e.g. spawn a process; verified via logs/`pgrep`).
  - Scripted check: `scripts/fbwl-smoke-input.sh` (spawns two in-repo test toplevels, injects clicks + Alt+Return via `fbwl-input-injector`, and asserts logs + a “spawn marker” file).

### Milestone C — `View` abstraction + minimal policy core extraction
Deliverables:
- A backend-agnostic `View` model exists and is used by the Wayland backend.
- A minimal set of WM policy code (focus/workspace/layer/placement) talks only to `IView`/`IOutput`/`ISeat`.

Key tasks:
- Identify the smallest method set needed by focus/workspace/placement.
- Add adapters so legacy X11 code can eventually satisfy the same interfaces (even if partially).

Acceptance:
- At least one policy feature (e.g., focus cycling or workspace switching) works through the new interfaces.

### Milestone D — Move/resize + compositor-side interactive grabs
Deliverables:
- Mouse-driven move/resize works (including edge/corner semantics matching Fluxbox options).
- Keyboard-driven move/resize (optional) works for parity.

Wayland-specific requirements:
- Implement compositor-side grabs (pointer capture) via wlroots seat events.
- Follow `xdg_toplevel.configure` / `ack_configure` rules for resizes; keep “pending” vs “committed” size clear.

Acceptance:
- SSH/terminal-only:
  - Use virtual pointer (or test IPC) to simulate press/move/release and verify geometry changes via logs/IPC.
  - Ensure `xdg_toplevel.configure`/`ack_configure` discipline remains correct under resize.
  - Scripted check: `scripts/fbwl-smoke-move-resize.sh` (spawns one in-repo test toplevel, injects Alt+drag move and Alt+drag resize via `fbwl-input-injector`, and asserts `Move:`/`Resize:` + `Surface size:` logs).

### Milestone E — Workspaces + output mapping (multi-monitor)
Deliverables:
- Multiple workspaces: switching, moving windows between them, per-workspace focus.
- Output hotplug/update: compositor survives output add/remove/reconfigure.

Key tasks:
- Map Fluxbox “screen/head” concepts onto `wlr_output_layout` logical space.
- Implement struts/reserved areas via layer-shell exclusive zones (or internal panel reserved space).
  - Ensure initial placement and maximize respect output `usable_area` (don’t cover panels).

Acceptance:
- On multi-output setups (or simulated), windows stay on the correct output; workspace switching works.
- SSH/terminal-only:
  - Scripted check: `scripts/fbwl-smoke-workspaces.sh` (spawns two `fbwl-smoke-client` toplevels, moves one to workspace 2 via `fbwl-input-injector key alt-ctrl-2`, switches workspaces via `alt-1/alt-2`, and asserts `Workspace:` + `Policy:` logs).
  - Scripted check: `scripts/fbwl-smoke-multi-output.sh` (simulates 2 outputs with `WLR_HEADLESS_OUTPUTS=2`, places a new client on each output by moving the virtual pointer, then asserts `Place:` logs and per-output `Maximize:`/`Fullscreen:` sizes).
  - Scripted check: `scripts/fbwl-smoke-layer-shell.sh` (creates a top panel with an exclusive zone, asserts the output `usable_area`, and verifies new toplevel placement/maximize uses it).

### Milestone F — Stacking/layers, fullscreen, maximize, minimize-ish
Deliverables:
- Correct stacking across layers (desktop, below, normal, above, dock, fullscreen, overlays).
- Fullscreen/maximize behavior per output.
- Internal minimize/hide state + panel visibility via foreign-toplevel protocol.

Acceptance:
- Fullscreen window truly covers the output and blocks normal windows; returning restores stack.
- SSH/terminal-only:
  - Scripted check: `scripts/fbwl-smoke-maximize-fullscreen.sh` (toggles maximize and fullscreen via `fbwl-input-injector key alt-m` / `alt-f`, and asserts `Maximize:`/`Fullscreen:` + `Surface size:` logs).
  - Scripted check: `scripts/fbwl-smoke-fullscreen-stacking.sh` (makes a window fullscreen, cycles focus to another window, then clicks where the second window would be and asserts `Pointer press ... hit=<fullscreen>` so fullscreen stays top).
  - Scripted check: `scripts/fbwl-smoke-minimize-foreign.sh` (minimize/unminimize via `fbwl-foreign-toplevel-client` and via `Alt+I`, and asserts the minimized view no longer receives pointer hits).

### Milestone G — Decorations & theming (server-side, Fluxbox-like)
Deliverables:
- Fluxbox-like frame: titlebar, buttons, borders, resize handles.
- Theme integration:
  - reuse style parsing where possible,
  - refactor “draw into X pixmap/window” into “draw into buffer/texture”.

Key tasks:
- Introduce a Wayland-capable drawing path (choose one):
  - Cairo/Pango → render to buffers/bitmaps → upload as textures.
  - wlroots renderer primitives + custom text rendering (more work).
- Implement decoration hit-testing and map it to move/resize actions.
- Implement tabbing/grouping if desired (Fluxbox hallmark), noting it becomes compositor-managed grouping, not X reparenting.

Acceptance:
- At least one theme renders correctly; titlebar buttons function; resize handles behave.
- SSH/terminal-only:
  - Scripted check: `scripts/fbwl-smoke-ssd.sh` (uses `fbwl-smoke-client --xdg-decoration` + `fbwl-input-injector` to drag the titlebar/border and click maximize; asserts `Decor: title-render` + `Move:`/`Resize:`/`Maximize:` logs).
  - Scripted check: `scripts/fbwl-smoke-style.sh` (loads a minimal Fluxbox `theme.cfg` subset via `--style` and asserts the configured `window.title.height` and `window.borderWidth` affect decoration hit-testing for move/resize).

### Milestone H — Menus/toolbars/dialogs (Fluxbox UX)
Deliverables:
- Root menu, window menu, toolbar (workspace name/iconbar/clock), OSD and command dialog.

Implementation paths:
1) Bootstrap: ship these as layer-shell helper clients (fast).
2) Parity: implement in-compositor UI surfaces (more work, best theme integration).

Acceptance:
- A user can right-click for the root menu, switch workspaces from a toolbar, and run commands.
- SSH/terminal-only:
  - Scripted check: `scripts/fbwl-smoke-menu.sh` (loads a Fluxbox-style `menu` file via `--menu`, opens the root menu via a background right-click, selects an `[exec]` entry, and asserts the exec side effect + `Menu:` logs).
  - Scripted check: `scripts/fbwl-smoke-window-menu.sh` (right-clicks a decorated titlebar, selects `Close`, and asserts `Menu: open-window` + `Menu: window-close` logs and that the client exits).
  - Scripted check: `scripts/fbwl-smoke-toolbar.sh` (clicks the `nextworkspace` tool button in the internal toolbar and asserts `Toolbar: click tool=nextworkspace cmd=nextworkspace` + `Workspace: apply current=2 reason=switch-next` logs).
  - Scripted check: `scripts/fbwl-smoke-iconbar.sh` (spawns two toplevels, clicks the iconbar entry for the non-focused one, and asserts `Toolbar: click iconbar` + `Focus:` logs).
  - Scripted check: `scripts/fbwl-smoke-command-dialog.sh` (opens the internal command dialog via `Alt+F2`, types `touch …`, presses Enter, and asserts `CmdDialog:` logs + marker file creation).
  - Scripted check: `scripts/fbwl-smoke-osd.sh` (switches to workspace 2 and asserts `OSD: show workspace=2` and timer-based hide).

### Milestone I — XWayland integration (do early for usability)
Deliverables:
- X11 apps run and are managed as `View`s.
- Basic ICCCM interactions: map/unmap, title/class hints, basic window types.

Key tasks:
- Integrate wlroots XWayland (`wlr_xwayland`) and wrap XWayland surfaces as `View`s.
- Keep EWMH/ICCCM behavior for XWayland clients; do not attempt to “emulate EWMH” for native Wayland clients.

Acceptance:
- An X11 client launched via XWayland appears and can be moved/resized/focused.
- SSH/terminal-only:
  - Scripted check: `scripts/fbwl-smoke-xwayland.sh` (runs an in-repo X11 client under XWayland, then uses `fbwl-input-injector` to move/resize and asserts `Focus:`/`Move:`/`Resize:`/`Surface size:` logs).
  - Scripted check: `scripts/fbwl-smoke-xvfb-xwayland.sh` (runs `fluxbox-wayland` on the wlroots X11 backend under Xvfb, then runs the same XWayland client move/resize assertions; catches regressions specific to nested X11 backend environments).

### Milestone J — Replace X11-only desktop integrations
Deliverables:
- Tray: implement SNI host (DBus) or provide a supported external tray solution.
- IPC: replace X root-window property IPC with a compositor-native IPC service.
- Utilities:
  - a Wayland-safe wallpaper story,
  - `fluxbox-remote` rewritten,
  - `startfluxbox-wayland` script.

Acceptance:
- SSH/terminal-only:
  - Scripted check: `scripts/fbwl-smoke-sni.sh` (starts a private DBus session, registers a test StatusNotifierItem, and asserts `SNI:` logs for register/unregister).
  - Scripted check: `scripts/fbwl-smoke-tray.sh` (registers a test StatusNotifierItem that exports `IconPixmap` and implements `Activate`/`SecondaryActivate`/`ContextMenu`, verifies the icon renders via `fbwl-screencopy-client --expect-rgb`, triggers an icon update (via `NewIcon`/`PropertiesChanged`) and verifies the pixel changes, clicks its tray slot with left/middle/right click, and asserts the side effects).
  - Scripted check: `scripts/fbwl-smoke-tray-iconname.sh` (registers a test StatusNotifierItem that exports only `IconName`, provides a temporary XDG icon theme dir containing a deterministic PNG icon, and verifies it renders via `fbwl-screencopy-client --expect-rgb`).
  - Scripted check: `scripts/fbwl-smoke-tray-icon-theme-path.sh` (registers a test StatusNotifierItem that exports `IconName` + `IconThemePath`, provides a temporary icon theme dir, and verifies it renders via `fbwl-screencopy-client --expect-rgb` without relying on global XDG icon dirs).
  - Scripted check: `scripts/fbwl-smoke-tray-attention.sh` (registers a test StatusNotifierItem with `Status=NeedsAttention` and `AttentionIconPixmap`, verifies the attention icon is rendered).
  - Scripted check: `scripts/fbwl-smoke-tray-overlay.sh` (registers a test StatusNotifierItem with `OverlayIconPixmap`, verifies the overlay renders on top of the base icon by sampling two pixels).
  - Scripted check: `scripts/fbwl-smoke-tray-passive.sh` (registers a test StatusNotifierItem with `Status=Passive`, verifies it is hidden from the tray).
  - Scripted check: `scripts/fbwl-smoke-ipc.sh` (uses `fbwl-remote` to issue `ping`, `workspace N`, and `quit` over the compositor IPC socket).
  - Scripted check: `scripts/fbwl-smoke-startfluxbox-wayland.sh` (runs `util/startfluxbox-wayland` with a custom startup script, verifies it sets `XDG_SESSION_TYPE=wayland`, ensures a DBus session, and that the compositor can be quit non-interactively).
  - Scripted check: `scripts/fbwl-smoke-fluxbox-remote.sh` (verifies `fluxbox-remote` can talk to the compositor IPC socket in `--wayland` mode for basic commands).
  - Scripted check: `scripts/fbwl-smoke-background.sh` (starts `fluxbox-wayland --bg-color …`, captures output via `fbwl-screencopy-client --expect-rgb`, and verifies a pixel matches the configured background color).
- Remote control can trigger at least a few core commands (e.g., workspace switch, quit).

### Milestone K — Protocol coverage & polish (“modern desktop expectations”)
Deliverables:
- Pointer constraints + relative pointer.
- Screencopy + portal compatibility.
  - Provide `ext-image-copy-capture-v1` + `ext-image-capture-source-v1` (output capture source) so `xdg-desktop-portal-wlr` can run even when `linux-dmabuf` is unavailable (e.g. `WLR_RENDERER=pixman` headless smoke tests).
- Idle inhibit / idle notify, input method/text input.
- Fractional scaling / hiDPI correctness (as supported by protocol set).
- Performance and correctness passes (frame pacing, damage correctness, latency).

Acceptance:
- Common “Wayland correctness” tools and apps (screen sharing, IME, games) behave as expected.
- SSH/terminal-only:
  - Scripted check: `scripts/fbwl-smoke-portal-wlr.sh` (runs `xdg-desktop-portal-wlr` against `fluxbox-wayland` headless, asserts it uses `ext_image_copy_capture`, and calls the backend Screenshot API to produce a deterministic PNG at `/tmp/out.png`; the script will auto-isolate `/tmp` via `unshare` when needed to avoid shared `/tmp` collisions).
  - Scripted check: `scripts/fbwl-smoke-portal-wlr-screencast.sh` (runs `xdg-desktop-portal-wlr` against `fluxbox-wayland` headless, forces non-interactive capture via an `xdg-desktop-portal-wlr` config with `chooser_type=none` + `output_name=HEADLESS-1`, asserts it uses `ext_image_copy_capture`, then drives the backend ScreenCast API and verifies a PipeWire node is created via `pw-cli info NODE_ID`).

---

## 6) Build System Plan (Autotools-Friendly)

Goal: keep `fluxbox` (X11) building as-is while adding `fluxbox-wayland`.

### 6.1 Configure flags
- Add `--enable-wayland` (default off initially) that checks (via `pkg-config`) for:
  - `wlroots`, `wayland-server`, `wayland-protocols`, `xkbcommon`
  - `wayland-client` (for in-repo test clients)
  - `pangocairo` (Cairo/Pango; required for compositor UI text rendering, e.g. root-menu labels)
  - plus renderer/input deps implied by your wlroots build (libinput/udev/drm/gbm/egl, etc)
- Optional:
  - `--disable-x11` to build Wayland-only.

### 6.2 Targets
- Add a new program target in `src/Makemodule.am`:
  - `fluxbox-wayland` with sources under `src/wayland/` and shared policy sources as they become backend-agnostic.
- Add in-repo test utilities under `util/`:
  - `fbwl-smoke-client` (deterministic xdg-shell handshake)
  - `fbwl-input-injector` (virtual pointer/keyboard injector for scripted tests)
- Keep link dependencies clean: X11-only code should not leak into the Wayland binary.

### 6.3 Dependency tiers (make “minimal build” explicit)
Required for a functional Wayland compositor build:
- `wlroots`, `wayland-server`, `wayland-protocols`, `xkbcommon`
- `wayland-client` (required for the in-repo test clients: `fbwl-smoke-client`, `fbwl-input-injector`)
- `pangocairo` (Cairo/Pango; required for compositor UI text rendering, e.g. root-menu labels)

Strongly recommended (often effectively required depending on wlroots build options):
- DRM/libinput stack: `libinput`, `libdrm`, `udev`, GBM/EGL/GLES libraries
- Seat/session management: `libseat`/`seatd` or logind integration (depends on your wlroots configuration)

Optional feature deps:
- XWayland support: XWayland + XCB/X11 libs as required by wlroots
- SNI tray host: DBus libs (current implementation uses `libsystemd` sd-bus)

---

## 7) Testing and Debugging Strategy (Using Xvfb + wlroots backends)

### 7.0 Hard constraint: SSH/terminal-only testing
Assumption: development/testing happens over SSH in a non-interactive terminal session.

Implications:
- Do not rely on “seeing” a compositor window, clicking with a real mouse, etc.
- Every milestone’s “Acceptance” must be runnable as a shell command that exits 0/1.
- Prefer deterministic backends (`headless` and/or Xvfb + `x11`) and log-based assertions.
- Always wrap long-running processes in `timeout` and use `trap` for cleanup.
- For non-XWayland tests, prefer disabling XWayland (`--no-xwayland`) to reduce dependencies and avoid flaky XWayland/GL stacks.
- Ensure `XDG_RUNTIME_DIR` is valid (Wayland sockets live here). If it’s missing:
  ```sh
  export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/tmp/xdg-runtime-$UID}
  mkdir -p "$XDG_RUNTIME_DIR"
  chmod 0700 "$XDG_RUNTIME_DIR"
  ```

Iteration loop (after code changes):
```sh
make -j"$(nproc)" fluxbox-wayland fluxbox-remote fbwl-smoke-client fbwl-clipboard-client fbwl-cursor-shape-client fbwl-data-control-client fbwl-dnd-client fbwl-presentation-time-client fbwl-primary-selection-client fbwl-relptr-client fbwl-screencopy-client fbwl-export-dmabuf-client fbwl-output-management-client fbwl-output-power-client fbwl-xdg-output-client fbwl-viewporter-client fbwl-fractional-scale-client fbwl-text-input-client fbwl-input-method-client fbwl-xdg-activation-client fbwl-xdg-decoration-client fbwl-idle-client fbwl-session-lock-client fbwl-shortcuts-inhibit-client fbwl-single-pixel-buffer-client fbwl-remote fbwl-input-injector fbwl-foreign-toplevel-client fbwl-layer-shell-client fbx11-smoke-client fbx11-xembed-tray-client fbwl-sni-item-client fbwl-xdp-portal-client util/startfluxbox-wayland
scripts/fbwl-smoke-all.sh
```

Fast targeted checks (minimal rebuilds):
```sh
make -j"$(nproc)" fluxbox-wayland fbwl-sni-item-client && scripts/fbwl-smoke-sni.sh
make -j"$(nproc)" fluxbox-wayland fbwl-sni-item-client fbwl-input-injector fbwl-remote fbwl-screencopy-client && scripts/fbwl-smoke-tray.sh
make -j"$(nproc)" fluxbox-wayland fbwl-sni-item-client fbwl-remote fbwl-screencopy-client && scripts/fbwl-smoke-tray-iconname.sh
make -j"$(nproc)" fluxbox-wayland fbwl-sni-item-client fbwl-remote fbwl-screencopy-client && scripts/fbwl-smoke-tray-icon-theme-path.sh
make -j"$(nproc)" fluxbox-wayland fbwl-sni-item-client fbwl-remote fbwl-screencopy-client && scripts/fbwl-smoke-tray-attention.sh
make -j"$(nproc)" fluxbox-wayland fbwl-sni-item-client fbwl-remote fbwl-screencopy-client && scripts/fbwl-smoke-tray-overlay.sh
make -j"$(nproc)" fluxbox-wayland fbwl-sni-item-client fbwl-remote && scripts/fbwl-smoke-tray-passive.sh
make -j"$(nproc)" fluxbox-wayland && scripts/fbwl-smoke-portal-wlr.sh
make -j"$(nproc)" fluxbox-wayland && scripts/fbwl-smoke-portal-wlr-screencast.sh
make -j"$(nproc)" fluxbox-wayland fbwl-xdp-portal-client && scripts/fbwl-smoke-xdg-desktop-portal.sh
make -j"$(nproc)" fluxbox-wayland fbwl-xdp-portal-client && scripts/fbwl-smoke-xdg-desktop-portal-screenshot.sh
scripts/fbwl-smoke-all.sh
```

Quick start (build + run the scripted smoke tests):
```sh
sudo pacman -S --needed base-devel git pkgconf autoconf automake libtool gettext \
  wlroots wayland pixman libxkbcommon libxcb xcb-util-wm pango cairo systemd systemd-libs dbus \
  pipewire wireplumber xdg-desktop-portal xdg-desktop-portal-wlr file python \
  xorg-server-xvfb xorg-xwayland ripgrep coreutils grim
```

```sh
./autogen.sh
./configure --enable-wayland
make -j"$(nproc)" fluxbox-wayland fluxbox-remote fbwl-smoke-client fbwl-clipboard-client fbwl-cursor-shape-client fbwl-data-control-client fbwl-dnd-client fbwl-presentation-time-client fbwl-primary-selection-client fbwl-relptr-client fbwl-screencopy-client fbwl-export-dmabuf-client fbwl-output-management-client fbwl-output-power-client fbwl-xdg-output-client fbwl-viewporter-client fbwl-fractional-scale-client fbwl-text-input-client fbwl-input-method-client fbwl-xdg-activation-client fbwl-xdg-decoration-client fbwl-idle-client fbwl-session-lock-client fbwl-shortcuts-inhibit-client fbwl-single-pixel-buffer-client fbwl-remote fbwl-input-injector fbwl-foreign-toplevel-client fbwl-layer-shell-client fbx11-smoke-client fbx11-xembed-tray-client fbwl-sni-item-client fbwl-xdp-portal-client util/startfluxbox-wayland

scripts/fbwl-smoke-all.sh

# Or run individual smoke tests:
scripts/fbwl-smoke-headless.sh
scripts/fbwl-smoke-background.sh
scripts/fbwl-smoke-xvfb.sh
scripts/fbwl-smoke-xvfb-decor-style.sh
scripts/fbwl-smoke-xvfb-policy.sh
scripts/fbwl-smoke-xvfb-protocols.sh
scripts/fbwl-smoke-xvfb-tray.sh
scripts/fbwl-smoke-xvfb-xwayland.sh
scripts/fbwl-smoke-xvfb-portal.sh
scripts/fbwl-smoke-xwayland.sh
scripts/fbwl-smoke-xembed-tray.sh
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
scripts/fbwl-smoke-cursor-shape.sh
scripts/fbwl-smoke-dnd.sh
scripts/fbwl-smoke-presentation-time.sh
scripts/fbwl-smoke-primary-selection.sh
scripts/fbwl-smoke-relptr.sh
scripts/fbwl-smoke-screencopy.sh
scripts/fbwl-smoke-export-dmabuf.sh
scripts/fbwl-smoke-portal-wlr.sh
scripts/fbwl-smoke-portal-wlr-screencast.sh
scripts/fbwl-smoke-xdg-desktop-portal.sh
scripts/fbwl-smoke-xdg-desktop-portal-screenshot.sh
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
scripts/fbwl-smoke-clientmenu-usepixmap.sh
scripts/fbwl-smoke-toolbar.sh
scripts/fbwl-smoke-iconbar.sh
scripts/fbwl-smoke-iconbar-resources.sh
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
scripts/fbwl-smoke-maximize-axis-toggle.sh
scripts/fbwl-smoke-maximize-fullscreen.sh
scripts/fbwl-smoke-minimize-foreign.sh
scripts/fbwl-smoke-layer-shell.sh
scripts/fbwl-smoke-multi-output.sh
scripts/fbwl-smoke-fullscreen-stacking.sh
```

### 7.1 Primary automated smoke test: wlroots headless backend
Why: no X server required, fully headless, ideal for CI and SSH-only bring-up.

Preferred (scripted, non-interactive):
```sh
scripts/fbwl-smoke-headless.sh
```

Manual equivalent:
```sh
./autogen.sh
./configure --enable-wayland
make -j"$(nproc)" fluxbox-wayland fbwl-smoke-client

SOCKET=wayland-fluxbox-test
LOG=/tmp/fluxbox-wayland.log
: >"$LOG"

cleanup() {
  [ -n "${PID:-}" ] && kill "$PID" 2>/dev/null || true
}

WLR_BACKENDS=headless WLR_RENDERER=pixman ./fluxbox-wayland --socket "$SOCKET" >"$LOG" 2>&1 &
PID=$!
trap cleanup EXIT

timeout 5 sh -c "until rg -q 'Running fluxbox-wayland' '$LOG'; do sleep 0.05; done"

./fbwl-smoke-client --socket "$SOCKET" --timeout-ms 2000
```

In-repo client used for deterministic smoke tests:
- `fbwl-smoke-client`: creates an `xdg_toplevel`, waits for the first `configure`, acks it, commits once.
- Useful flags:
  - `--title TITLE` (distinguish multiple clients in logs)
  - `--app-id APPID` (set `xdg_toplevel` app_id for apps-rules tests)
  - `--stay-ms MS` (keep the surface alive long enough for input tests)
  - `--xdg-decoration` (request server-side xdg-decoration; enables compositor-side decorations tests)

### 7.2 Automated integration test: Xvfb + wlroots X11 backend
Why: coverage for the X11 backend path; still fully headless over SSH.

Preferred (scripted, non-interactive):
```sh
scripts/fbwl-smoke-xvfb.sh
scripts/fbwl-smoke-xvfb-decor-style.sh
scripts/fbwl-smoke-xvfb-policy.sh
scripts/fbwl-smoke-xvfb-protocols.sh
scripts/fbwl-smoke-xvfb-tray.sh
scripts/fbwl-smoke-xvfb-xwayland.sh
scripts/fbwl-smoke-xvfb-portal.sh
```

Notes:
- wlroots’ X11 backend requires a 24-bit TrueColor visual; make sure Xvfb is 24-bit.
- Some environments behave better with `-extension GLX` (avoids GLX/EGL edge cases).
- `scripts/fbwl-smoke-xvfb-policy.sh` runs policy/input smoke scripts on the X11 backend (including `scripts/fbwl-smoke-input.sh` and `scripts/fbwl-smoke-move-resize.sh`).

### 7.3 Automated input test: virtual pointer + virtual keyboard (headless)
Why: verifies “real compositor” plumbing (seat/cursor focus + keybindings) without a GUI.

```sh
scripts/fbwl-smoke-input.sh
```

Also (nested, catches X11-backend-specific regressions):
```sh
scripts/fbwl-smoke-xvfb-policy.sh
```

### 7.3b Automated move/resize test: interactive grabs (headless)
Why: verifies compositor-side pointer grabs and xdg-shell resize discipline without needing a GUI.

```sh
scripts/fbwl-smoke-move-resize.sh
```

Also (nested, catches X11-backend-specific regressions):
```sh
scripts/fbwl-smoke-xvfb-policy.sh
```

### 7.3c Automated workspaces test: switch + move-to-workspace (headless)
Why: verifies workspace visibility rules and focus handoff without needing a GUI.

```sh
scripts/fbwl-smoke-workspaces.sh
```

### 7.3d Automated maximize/fullscreen test: xdg state toggles (headless)
Why: verifies maximize/fullscreen state changes and resize/ack discipline without needing a GUI.

```sh
scripts/fbwl-smoke-maximize-fullscreen.sh
```

### 7.3e Automated multi-output test: placement + per-output states (headless)
Why: verifies multi-output survival and that new views are placed on the intended output.

```sh
scripts/fbwl-smoke-multi-output.sh
```

### 7.3f Automated fullscreen stacking test (headless)
Why: verifies fullscreen windows stay on top even if focus changes.

```sh
scripts/fbwl-smoke-fullscreen-stacking.sh
```

### 7.3g Automated minimize + foreign-toplevel test (headless)
Why: verifies minimized views are hidden and can be restored via foreign-toplevel.

```sh
scripts/fbwl-smoke-minimize-foreign.sh
```

### 7.3h Automated layer-shell test (headless)
Why: verifies layer-shell surfaces configure and reserve usable area, and that new toplevel placement/maximize respects it.

```sh
scripts/fbwl-smoke-layer-shell.sh
```

### 7.3i Automated XWayland smoke test (headless)
Why: verifies XWayland integration and that X11 clients map and can be focused/moved/resized.

```sh
scripts/fbwl-smoke-xwayland.sh
```

Also (nested, catches X11-backend-specific regressions):
```sh
scripts/fbwl-smoke-xvfb-xwayland.sh
```

### 7.3j Automated relative-pointer + pointer-constraints test (headless)
Why: verifies `pointer-constraints` activation and that `relative-pointer` delivers motion while locked.

```sh
scripts/fbwl-smoke-relptr.sh
```

### 7.3k Automated screencopy test (headless)
Why: verifies `wlr-screencopy-unstable-v1` is advertised and clients can capture a frame.

```sh
scripts/fbwl-smoke-screencopy.sh
```

### 7.3k2 Automated export-dmabuf test (headless)
Why: verifies `wlr-export-dmabuf-unstable-v1` is advertised and responds to a capture request.

Note: under `WLR_RENDERER=pixman` the capture is expected to `cancel`; the smoke test treats `cancel` as success to verify the protocol path.

```sh
scripts/fbwl-smoke-export-dmabuf.sh
```

### 7.3k3 Automated portal-wlr smoke test (headless)
Why: verifies `xdg-desktop-portal-wlr` can connect to `fluxbox-wayland` over SSH and that the compositor advertises `ext-image-copy-capture-v1` + `ext-image-capture-source-v1` (output) for portal stacks.

```sh
scripts/fbwl-smoke-portal-wlr.sh
```

### 7.3k4 Automated portal-wlr screencast smoke test (headless)
Why: verifies `xdg-desktop-portal-wlr` ScreenCast can be driven non-interactively over SSH and that it creates a PipeWire node while capturing from `fluxbox-wayland` using the `ext_image_copy_capture` path.

```sh
scripts/fbwl-smoke-portal-wlr-screencast.sh
```

### 7.3l Automated xdg-activation test (headless)
Why: verifies `xdg-activation-v1` tokens can transfer focus to another toplevel.

```sh
scripts/fbwl-smoke-xdg-activation.sh
```

### 7.3m Automated xdg-decoration negotiation test (headless)
Why: verifies `xdg-decoration-unstable-v1` is advertised and negotiates client-side decorations (for now).

```sh
scripts/fbwl-smoke-xdg-decoration.sh
```

### 7.3n Automated idle-inhibit + ext-idle-notify test (headless)
Why: verifies `idle-inhibit` disables idle notifications and that ext-idle-notify resumes after uninhibit.

```sh
scripts/fbwl-smoke-idle.sh
```

### 7.3n2 Automated session-lock test (headless)
Why: verifies `ext-session-lock-v1` can lock/unlock and that lock surfaces block pointer hits to normal views.

```sh
scripts/fbwl-smoke-session-lock.sh
```

### 7.3o Automated xdg-output test (headless)
Why: verifies `xdg-output-unstable-v1` is advertised and returns output logical position/size/name.

```sh
scripts/fbwl-smoke-xdg-output.sh
```

### 7.3p Automated viewporter test (headless)
Why: verifies `viewporter` is advertised and clients can create a `wp_viewport` and set a destination.

```sh
scripts/fbwl-smoke-viewporter.sh
```

### 7.3q Automated fractional-scale test (headless)
Why: verifies `fractional-scale-v1` is advertised and clients receive `preferred_scale`.

```sh
scripts/fbwl-smoke-fractional-scale.sh
```

### 7.3r Automated wlr-output-management test (headless)
Why: verifies `wlr-output-management-unstable-v1` is advertised and can apply+verify an output position change.

```sh
scripts/fbwl-smoke-output-management.sh
```

### 7.3t Automated wlr-output-power-management test (headless)
Why: verifies `wlr-output-power-management-unstable-v1` is advertised and can toggle an output off/on.

```sh
scripts/fbwl-smoke-output-power.sh
```

### 7.3s Automated text-input + input-method test (headless)
Why: verifies `text-input-unstable-v3` and `input-method-unstable-v2` round-trip an IME commit into a focused client.

```sh
scripts/fbwl-smoke-text-input.sh
```

### 7.3u Automated data-control test (headless)
Why: verifies `wlr-data-control-unstable-v1` and `ext-data-control-v1` are advertised and can set/get the clipboard selection without a serial.

```sh
scripts/fbwl-smoke-data-control.sh
```

### 7.3v Automated primary selection test (headless)
Why: verifies `primary-selection-unstable-v1` is advertised and primary selection can be set/get (requires a virtual keyboard for serials).

```sh
scripts/fbwl-smoke-primary-selection.sh
```

### 7.3w Automated cursor-shape test (headless)
Why: verifies `cursor-shape-v1` is advertised and that a client can request a cursor shape via `wp_cursor_shape_device_v1.set_shape`.

```sh
scripts/fbwl-smoke-cursor-shape.sh
```

### 7.3x Automated presentation-time test (headless)
Why: verifies `presentation-time` is advertised and that a client can request `wp_presentation.feedback` and receive `presented`/`discarded`.

```sh
scripts/fbwl-smoke-presentation-time.sh
```

### 7.3y Automated keyboard-shortcuts-inhibit test (headless)
Why: verifies `keyboard-shortcuts-inhibit-unstable-v1` is advertised and that compositor keybindings are suppressed while inhibition is active.

```sh
scripts/fbwl-smoke-shortcuts-inhibit.sh
```

### 7.3z Automated single-pixel-buffer test (headless)
Why: verifies `single-pixel-buffer-v1` is advertised and that a client can create and commit a 1×1 buffer.

```sh
scripts/fbwl-smoke-single-pixel-buffer.sh
```

### 7.3aa Automated Fluxbox `keys` file test (headless)
Why: verifies `--keys FILE` loads a minimal subset of Fluxbox `~/.fluxbox/keys` syntax and can override the built-in defaults.

```sh
scripts/fbwl-smoke-keys-file.sh
```

### 7.3aa2 Automated Fluxbox `--config-dir` / `init` test (headless)
Why: verifies `--config-dir DIR` loads a minimal subset of Fluxbox `init` (`session.screen0.workspaces`, `session.keyFile`, `session.appsFile`, `session.styleFile`, `session.menuFile`) and can discover `keys`/`apps` files without `--keys/--apps` flags.

```sh
scripts/fbwl-smoke-config-dir.sh
```

### 7.3ab Automated Fluxbox `apps` (“Remember”) rules test (headless)
Why: verifies `--apps FILE` loads a minimal subset of Fluxbox `~/.fluxbox/apps` syntax and can apply workspace/jump/sticky rules deterministically.

```sh
scripts/fbwl-smoke-apps-rules.sh
```

### 7.3ab2 Automated Fluxbox `apps` rules test for XWayland instance matching (headless)
Why: verifies `(name=...)` matches XWayland WM_CLASS(instance) and that rules can be applied to X11 apps running under XWayland.

```sh
scripts/fbwl-smoke-apps-rules-xwayland.sh
```

### 7.3ac Automated `startfluxbox-wayland` script test (headless)
Why: verifies a Wayland-safe session start wrapper ensures a DBus session, can run a startup script, and can launch/quit the compositor non-interactively over SSH.

```sh
scripts/fbwl-smoke-startfluxbox-wayland.sh
```

### 7.3ad Automated `fluxbox-remote` (Wayland IPC mode) test (headless)
Why: keeps the classic `fluxbox-remote` command useful by supporting compositor IPC when managing Wayland apps.

```sh
scripts/fbwl-smoke-fluxbox-remote.sh
```

### 7.3ae Automated SNI (“system tray”) watcher test (headless)
Why: verifies a DBus StatusNotifierWatcher is available so tray icons from Wayland/XWayland apps can register.

```sh
scripts/fbwl-smoke-sni.sh
```

### 7.4 What to test early (catch architectural bugs)
- Multiple toplevels + popups (menus, tooltips) correctness.
- Configure/ack correctness on resize.
- Clipboard + DnD.
- HiDPI scaling correctness (scale != 1).
- Output hotplug/reconfigure.

### 7.5 Logging knobs (make debugging survivable)
- `fluxbox-wayland --log-level silent|error|info|debug` (wlroots log verbosity).
- `fluxbox-wayland --log-protocol` (very verbose Wayland protocol request/event log to stderr).
  - Scripted check: `scripts/fbwl-smoke-log-protocol.sh`
- Ensure wlroots log output is surfaced (e.g. via `WLR_LOG` env).

---

## 8) Major Risks & Mitigations

### 8.1 “FbTk is a toolkit but it’s X11”
FbTk currently wraps X windows (`FbTk::FbWindow`) and renders via X primitives.
Risk: trying to “incrementally port” FbTk drawing will stall the project.
Mitigation:
- Treat UI rendering as a new subsystem (`src/ui/`), reuse only config/theme formats initially.
- Bootstrap with external helper clients if needed to keep momentum.

### 8.2 Semantic mismatches: Wayland can’t be forced like X11
Wayland doesn’t allow:
- arbitrary client moving/resizing without protocol cooperation,
- global window tree introspection,
- global keylogging-style grabs.
Mitigation:
- be strict about xdg-shell rules (configure/ack discipline),
- implement compositor-side policies (interactive grabs, internal hidden/minimized state),
- explicitly document behavioral differences from X11 Fluxbox where unavoidable.

### 8.3 “Desktop plumbing” is bigger than window management
Users expect screen capture, IME, tray, output management, etc.
Mitigation:
- maintain a protocol checklist (Section 4),
- stage features by “visibility” (IME/tray/capture are highly visible missing pieces),
- ship sensible defaults and document external alternatives where appropriate.

---

## 9) Concrete Next Steps (Suggested First PR Sequence)

- [x] Treat “SSH/terminal-only” as a hard requirement: every milestone must have a scriptable acceptance check.
- [x] Keep Milestone A green via scripts (`scripts/fbwl-smoke-headless.sh`, `scripts/fbwl-smoke-xvfb.sh`).
- [x] Keep Milestone B green via scripted input (`scripts/fbwl-smoke-input.sh`).
- [x] Keep Milestone D green via scripted move/resize (`scripts/fbwl-smoke-move-resize.sh`).
- [x] Introduce backend-agnostic view/policy boundary (Milestone C; see `src/wmcore/fbwm_core.*`).
- [x] Implement Milestone E workspaces (`scripts/fbwl-smoke-workspaces.sh`).
- [x] Implement Milestone E output mapping (`scripts/fbwl-smoke-multi-output.sh`).
- [x] Implement Milestone F fullscreen/maximize behavior (`scripts/fbwl-smoke-maximize-fullscreen.sh`).
- [x] Add a CI-friendly runner that skips missing host deps (`scripts/fbwl-smoke-ci.sh`).
- [x] Add Xvfb tray smoke coverage (`scripts/fbwl-smoke-xvfb-tray.sh`).
- [x] Make `smoke-ci` dependency skipping robust (parse `need_cmd`, `need_exe`, `-x /path` checks, and `./fbwl-*`/`./fluxbox-wayland` invocations; include nested smoke script deps).
- [x] Add Xvfb decorations/style smoke coverage (`scripts/fbwl-smoke-xvfb-decor-style.sh`) and allow `fbwl-smoke-ssd.sh`/`fbwl-smoke-style.sh` to run on non-headless backends via `WLR_BACKENDS` overrides.
- [x] Add Xvfb policy smoke coverage (`scripts/fbwl-smoke-xvfb-policy.sh`) and allow key policy/UI smoke scripts to run on non-headless backends via `WLR_BACKENDS` overrides (`scripts/fbwl-smoke-workspaces.sh`, `scripts/fbwl-smoke-maximize-fullscreen.sh`, `scripts/fbwl-smoke-minimize-foreign.sh`, `scripts/fbwl-smoke-layer-shell.sh`, `scripts/fbwl-smoke-fullscreen-stacking.sh`, `scripts/fbwl-smoke-window-menu.sh`).
- [x] Remove `seq` dependency from Xvfb smoke wrappers (use bash loops) to reduce hidden host deps and make `smoke-ci` skipping more accurate.
- [x] Run input + move/resize smoke tests on Xvfb by making them backend-override friendly and including them in `scripts/fbwl-smoke-xvfb-policy.sh`.
- [x] Harden remaining smoke scripts by removing hardcoded input coordinates (prefer parsing `Place:` lines for click/drag targets), especially `scripts/fbwl-smoke-xwayland.sh`, `scripts/fbwl-smoke-xvfb-xwayland.sh`, `scripts/fbwl-smoke-minimize-foreign.sh`, `scripts/fbwl-smoke-session-lock.sh`, `scripts/fbwl-smoke-relptr.sh`, and `scripts/fbwl-smoke-cursor-shape.sh`.
- [x] Remove remaining hardcoded menu/protocol coordinates (prefer parsing `Menu: open` / `Place:` logs) to reduce flake risk under both headless and Xvfb runs.

---

## 10) Smoke Tests — Expand Xvfb Coverage (Next)

- [x] Run apps-rules smoke on Xvfb by making `scripts/fbwl-smoke-apps-rules.sh` backend-override-friendly and adding it to `scripts/fbwl-smoke-xvfb-policy.sh`.
- [x] Run apps-rules (XWayland) smoke on Xvfb by making `scripts/fbwl-smoke-apps-rules-xwayland.sh` backend-override-friendly and chaining it from `scripts/fbwl-smoke-xvfb-xwayland.sh`.
- [x] Audit remaining headless-only smoke scripts and decide which should be Xvfb-capable (`WLR_BACKENDS`/`WLR_RENDERER` overrides) vs intentionally headless-only (added Xvfb output coverage via `scripts/fbwl-smoke-xvfb-outputs.sh` and made output tests pick `WLR_HEADLESS_OUTPUTS` vs `WLR_X11_OUTPUTS` based on backend).
- [x] Add a single-session Xvfb “kitchen sink” smoke run that exercises most UX features without restarting the compositor between checks (added `scripts/fbwl-smoke-xvfb-kitchen-sink.sh` and wired it into `scripts/fbwl-smoke-all.sh`/`scripts/fbwl-smoke-ci.sh`).

---

## 11) Smoke Tests — Expand Xvfb Protocol Coverage (Next)

- [x] Expand `scripts/fbwl-smoke-xvfb-protocols.sh` to run with 2 outputs and cover `xdg-output`, `output-management`, and `output-power` in the same single-session run (note: `presentation-time` feedback currently times out on the wlroots `x11` backend under Xvfb, so keep it covered via headless `scripts/fbwl-smoke-presentation-time.sh`).
- [x] Make `scripts/fbwl-smoke-presentation-time.sh` backend-override friendly (note: it still fails on the wlroots `x11` backend due to missing `presentation-time` feedback).
- [x] Re-run `scripts/fbwl-smoke-ci.sh` to validate.
- [x] Push changes.

---

## 12) Smoke Tests — Fix Presentation-Time on Xvfb (Next)

- [x] Make the X11 backend emit synthetic `output.present` events when wlroots can’t (e.g., Xvfb without the X11 Present extension), so `wp_presentation` feedback completes.
- [x] Re-enable `fbwl-presentation-time-client` coverage in `scripts/fbwl-smoke-xvfb-protocols.sh`.
- [x] Re-run `scripts/fbwl-smoke-ci.sh` to validate.
- [x] Push changes.

---

## 13) Smoke Tests — Expand IPC Coverage (Next)

- [x] Expand `scripts/fbwl-smoke-ipc.sh` to cover `nextworkspace`/`prevworkspace` wraparound and `focus-next` with multiple views.
- [x] Mirror the same coverage via `scripts/fbwl-smoke-fluxbox-remote.sh` (Wayland IPC via `fluxbox-remote --wayland`).
- [x] Re-run `scripts/fbwl-smoke-ci.sh` to validate.
- [x] Push changes.

---

## 14) Smoke Tests — IPC Error Paths (Next)

- [x] Expand `scripts/fbwl-smoke-ipc.sh` to validate IPC error responses (missing args, invalid workspace numbers, out-of-range, unknown commands) and command aliases (`nextwindow`, `exit`).
- [x] Mirror the same coverage via `scripts/fbwl-smoke-fluxbox-remote.sh` (Wayland IPC via `fluxbox-remote --wayland`).
- [x] Re-run `scripts/fbwl-smoke-ci.sh` to validate.
- [x] Push changes.

---

## 15) Smoke Tests — Xvfb Regression Loops (Next)

- [x] Add small deterministic stress loops in `scripts/fbwl-smoke-xvfb-kitchen-sink.sh` for IPC workspace switching and focus cycling (X11 backend / Xvfb).
- [x] Re-run `scripts/fbwl-smoke-ci.sh` to validate.
- [x] Push changes.

---

## 16) Smoke Tests — Xvfb Failure Diagnostics (Next)

- [x] Add standard failure diagnostics (log tails + optional `xwd` screenshot) to key Xvfb smoke scripts:
  `scripts/fbwl-smoke-xvfb.sh`, `scripts/fbwl-smoke-xvfb-kitchen-sink.sh`, `scripts/fbwl-smoke-xvfb-protocols.sh`, `scripts/fbwl-smoke-xvfb-xwayland.sh`.
- [x] Harden XWayland Xvfb smoke by adding timeouts around move/resize log assertions (`scripts/fbwl-smoke-xvfb-xwayland.sh`).
- [x] Re-run `scripts/fbwl-smoke-ci.sh` to validate.
- [x] Push changes.

---

## 17) Smoke Tests — Publish Screenshot Report (Next)

- [x] Add a `scripts/publish_screenshots.sh` helper that uploads screenshots + an `index.html` gallery via `wtf-upload`.
- [x] Add a simple `index.html` template and a `manifest.tsv` format (file + caption) for reports.
- [x] Start capturing screenshots for key UX features during the Xvfb “kitchen sink” run (`scripts/fbwl-smoke-xvfb-kitchen-sink.sh`).
- [x] Re-run `scripts/fbwl-smoke-ci.sh` to validate.
- [x] Push changes.
- [x] Published report (latest, 2026-02-01, uses `weston-terminal`): https://tmp.uh-oh.wtf/fluxbox-wayland/smoke-report/4691e2dc/20260201-093944/index.html
- [x] Published report (previous, 2026-02-01, uses `weston-terminal`): https://tmp.uh-oh.wtf/fluxbox-wayland/smoke-report/4691e2dc/20260201-005222/index.html
- [x] Published report (previous, 2026-01-30, uses `weston-terminal`): https://tmp.uh-oh.wtf/fluxbox-wayland/smoke-report/4691e2dc/20260130-173952/index.html
- [x] Published report (older, 2026-01-25, uses `weston-terminal`): https://tmp.uh-oh.wtf/fluxbox-wayland/smoke-report/94b1f2b5/20260125-203918/index.html
- [x] Published report (older example, 2026-01-25): https://tmp.uh-oh.wtf/fluxbox-wayland/smoke-report/d6b6fc11/20260125-092055/index.html

---

## 18) Config Parity — Full classic Fluxbox config surface (Next)

Goal: users can drop in their existing `~/.fluxbox` directory and get the same behavior (as close as Wayland allows) without hand-editing files.

- [x] Inventory Fluxbox config “surface area”:
  - Extracted `init` resources (`session.*`, `session.screenN.*`) from `doc/fluxbox.1` plus code-only keys from `src/` (e.g., `ScreenResource.cc` / `screen.name() + ".foo"` patterns).
  - Recorded the inventory in `doc/fluxbox-init-resources.tsv` (generated by `scripts/fbwl-extract-fluxbox-init-resources.py`).
- [x] Implement a general Fluxbox-style resource loader for `init`:
  - Added a generic `init` parser + key/value store with typed getters (bool/int/color) and case-insensitive lookups (`src/wayland/fbwl_server_config.c`).
  - Added Fluxbox-compatible path resolution + discovery helpers (`session.keyFile/appsFile/menuFile/styleFile`), and switched `--config-dir` to use them (`src/wayland/fbwl_server_bootstrap.c`).
  - Kept parsing X11-free (no Xrm/X11 deps).
- [x] Apply the full resource set to Wayland compositor behavior:
  - [x] Focus/raise/click behavior:
    - `session.screen0.focusModel` (ClickToFocus/MouseFocus/StrictMouseFocus).
      - X11 semantics: `MouseFocus` focuses on “real” pointer entry/motion and deliberately ignores synthetic EnterNotify that happen without pointer motion (via `FocusControl::ignoreAtPointer()` / `isIgnored()` used on unmap/workspace switch/move-resize/menu close). `StrictMouseFocus` does not apply that ignore behavior unless forced, so focus can change even when the pointer is stationary and a window appears/disappears under it.
      - Wayland status: MouseFocus focuses only on pointer motion. StrictMouseFocus also clears focus when the pointer moves over no view, and re-evaluates focus on workspace-visibility changes (map/unmap/workspace switch/minimize) and on Raise/Lower restacks, so stationary-pointer transitions are handled for those cases (`src/wayland/fbwl_server_policy_input.c`, `scripts/fbwl-smoke-strict-mousefocus-stacking.sh`).
	      - Parity note (answers “does this match X11 exactly?”):
	        - For `MouseFocus` behavior: yes (focus changes only on pointer motion/entry; stationary view-tree changes do not shift focus; leaving all windows does not clear focus), matching Fluxbox/X11’s “ignoreAtPointer” intent. The implementation is necessarily different because Wayland has no synthetic EnterNotify stream; we simply avoid re-evaluating focus on stationary scene changes for this model.
	        - For `StrictMouseFocus`: yes for the implemented WM operations. We explicitly re-check view-under-cursor not only on pointer motion, but also on workspace visibility changes (map/unmap/switch/minimize), compositor-driven restacks (Raise/Lower, layer changes, fullscreen reparenting), and geometry-only view changes (keyboard move/resize and `MoveTo`/`Move`/`ResizeTo`/`Resize*`, plus async XDG/XWayland commit resizes), so focus can change without pointer motion like Fluxbox/X11.
    - `session.screen0.autoRaise` + `session.autoRaiseDelay` (auto-raise with delay on mouse-focus).
    - `session.screen0.clickRaises` (raise on click even when focus doesn't change; decor-only when disabled).
    - `session.screen0.focusNewWindows` (controls focusing newly-mapped views).
  - [x] Placement policy, workspace names/behavior, toolbar/menu behavior, tab-related policy, etc.:
    - [x] Workspace names: `session.screen0.workspaceNames` (drives toolbar workspace labels + workspace OSD; numbers remain the fallback).
    - [x] Window placement strategy: `session.screen0.windowPlacement` + row/col direction resources.
      - Supports Row/Col Smart + MinOverlap + Cascade + UnderMouse; Autotab tabs new windows to the currently focused one.
	    - [x] Toolbar behavior: `session.screen0.toolbar.*` (visible/placement/autoHide/autoRaise/width/height/tools).
	      - Implemented: `visible`, `placement`, `widthPercent`, `height`, `tools` (workspace/iconbar/systemtray/clock), `autoHide`, `autoRaise` (uses `session.autoRaiseDelay`).
	      - Note: Left/Right placements use a real vertical toolbar layout (covered by `scripts/fbwl-smoke-config-dir.sh`).
	      - Smoke: extended `scripts/fbwl-smoke-config-dir.sh` to validate placement/size/tools plus autoHide/autoRaise logs.
	    - [x] Menu behavior: `session.screen0.menuDelay` (hover-to-open submenus after delay).
	      - Smoke: extended `scripts/fbwl-smoke-config-dir.sh` to validate delayed submenu open (via `fbwl-input-injector motion`).
		    - [x] Tab-related policy: `session.screen0.tabs.*` + `session.screen0.tab.*` + `session.tabsAttachArea` + `session.tabPadding` (MVP).
		      - Implemented: AutotabPlacement attaches new windows to the currently focused one and keeps the group together (workspace/sticky + move/resize/maximize sync).
		      - Init parsing/logging: `tabs.intitlebar/maxOver/usePixmap`, `tab.placement/width`, `tabFocusModel`, `tabsAttachArea`, `tabPadding`.
		      - Smoke: extended `scripts/fbwl-smoke-config-dir.sh` to assert init parsing + `Tabs: attach reason=autotab`.
    - [x] Output mapping strategy for `screenN` resources (Wayland outputs → Fluxbox screens).
      - Map outputs to screen indices by sorting output layout boxes by `(x, y, name)`.
      - Treat Wayland outputs as Fluxbox “screens”: `session.screenN.*` keys apply per output index from `ScreenMap`, and fall back to `screen0` when unset (so single-screen configs apply everywhere unless overridden).
      - Toolbar mapping: `session.screen0.toolbar.onhead` selects which `ScreenMap` index the (single) toolbar is placed on; toolbar appearance/behavior reads from `session.screen(toolbar_screen).*`.
      - Smokes:
        - `scripts/fbwl-smoke-multi-output.sh` asserts toolbar centers on `screen0` with 2 outputs.
        - `scripts/fbwl-smoke-screen1-toolbar-overrides.sh` asserts `session.screen1.toolbar.*` overrides apply when the toolbar is on head 2.
    - [x] Extend `keys` support to full classic syntax and command set:
	  - [x] Expand command set: Close/Kill, WindowMenu/RootMenu/HideMenus, Prev/NextWindow, Prev/NextWorkspace, Send/Take to (prev|next|N), Reconfigure.
	  - [x] Support numeric keycodes (X-style) in `keys`.
	  - [x] Support mouse bindings: OnDesktop/OnWindow/OnTitlebar/OnToolbar + Mouse1-5 (Mouse4/5 via scroll axis).
	  - [x] Support classic Fluxbox command language in `keys`/menus (nested `MacroCmd`, `If/Cond`, `ForEach/Map`, `Delay`, `ToggleCmd`; see §27 + `scripts/fbwl-smoke-keybinding-cmdlang.sh`).
	  - [x] Smoke: extend `scripts/fbwl-smoke-keys-file.sh` to validate key+mouse bindings and `Reconfigure` reload.
	  - [x] TODO:
	    - [x] NextTab/PrevTab/Tab N.
	    - [x] Client patterns (workspace=[current], etc).
	      - Parse `{static groups}` options and `(...)` client-pattern terms from `keys` for Next/PrevWindow.
	      - Match subset: `workspace=[current]|N`, `class|app_id`, `title`, `minimized|maximized|fullscreen`, `stuck`.
	      - Note: `static` is parsed but currently has no effect (ordering remains MRU-ish).
	    - [x] Key modes.
	      - Parse `ModeName:` prefix and scope bindings (keys + mouse) to the active KeyMode.
	      - Implement `:KeyMode <mode> [...]` command (only the first token is used as mode name; additional args are currently ignored).
	      - Reconfigure resets active KeyMode back to `default`.
	    - [x] WorkspaceMenu.
	      - Implement `:WorkspaceMenu` as a real workspace switcher menu (not RootMenu) with workspace-name labels.
	      - Selecting a workspace switches + reapplies workspace visibility.
	      - Smoke: extended `scripts/fbwl-smoke-keys-file.sh` to open WorkspaceMenu via `OnDesktop Mouse2` and click workspace 2.
- [x] Extend `apps` rules toward full “Remember” parity:
  - [x] Match semantics: patterns are full-match anchored (Fluxbox-like `^(...)$` behavior).
  - [x] Support `[group]` blocks (tab-group attach on map).
  - [x] Support `[Deco] {none}` and `[Layer] {Menu/Top/Normal/Bottom/Desktop}` (maps to scene layers).
  - [x] Smoke: extend `scripts/fbwl-smoke-apps-rules.sh` to validate group/deco/layer behavior.
  - [x] TODO: Fill out “Remember” surface area:
    - [x] Position: `[Position] (anchor) {X[%] Y[%]}` (best-effort compositor placement).
    - [x] Dimensions: `[Dimensions] {width[%] height[%]}` (best-effort size request).
    - [x] Head: `[Head] {number}` (Wayland outputs via ScreenMap).
	    - [x] Shaded
	    - [x] Alpha
	    - [x] FocusProtection
    - [x] SaveOnClose (write-back)
		- [x] Extend menu/style parity:
		  - [x] Menu: support `[include]` (file/dir), `[separator]`, `[nop]`.
		  - [x] Menu: dynamic submenu/tag `[workspaces]`.
		  - [x] Dynamic menus/tags: `[config]`, `[reconfig]`, `[style]`, `[stylesmenu]`, `[stylesdir]`, `[wallpapers]` (plus themes aliases).
			  - [x] TODO: menu icons + `[encoding]` blocks.
		  - [x] Style/overlay: expand theme parsing until Fluxbox-like UI can be driven by real styles.
- [x] Add introspection + reload:
  - [x] IPC/CLI: `fbwl-remote dump-config` and `fbwl-remote reconfigure` (reloads `init` when `--config-dir` is set, plus keys/apps/menu/style).
  - [x] Smoke: extend `scripts/fbwl-smoke-ipc.sh` to validate dump-config + reconfigure reload logs.
  - [x] Reconfigure: re-load `init` resources and apply updated settings live (incl. `session.*File` repointing under `--config-dir`).
  - [x] Smoke: extend `scripts/fbwl-smoke-config-dir.sh` to validate `init` reload (keyFile swap + toolbar resize).

---

## 19) Post-Parity Polish — Close Known Gaps (Next)

Goal: eliminate the remaining documented behavior gaps that block “daily usable” expectations, while keeping everything SSH-friendly and covered by deterministic smoke tests.

- [x] Implement true `StrictMouseFocus` semantics:
  - When the pointer leaves all focusable views, clear keyboard focus (or focus the background) rather than keeping the last-focused view.
  - Ensure this interacts correctly with `session.screen0.clickRaises`, `session.screen0.autoRaise`, and `session.autoRaiseDelay`.
  - Smoke: extend `scripts/fbwl-smoke-config-dir.sh` to set `session.screen0.focusModel: StrictMouseFocus` and assert focus clears on pointer leave (via `fbwl-input-injector motion` to empty area + log assertion).
  - Note (X11 parity): Fluxbox/X11 `StrictMouseFocus` means “focus always follows mouse, even when stationary” (window raise/map/unmap/geometry changes under a stationary pointer can change focus via synthetic Enter/Leave). Our Wayland `StrictMouseFocus` explicitly re-checks view-under-cursor on workspace-visibility changes, compositor-driven restacks, and geometry-only view changes (keyboard move/resize and `MoveTo`/`Move`/`ResizeTo`/`Resize*`), plus async XDG/XWayland commit resizes.
- [x] Implement true vertical toolbar for Left/Right placements:
  - Left/Right placements now use a real vertical layout (tool buttons, iconbar, tray, clock stacked vertically); `autoHide` slides along X for Left/Right.
  - Strut: reserves toolbar thickness when `autoHide=false`; when `autoHide=true`, reserve no usable-area strut (Fluxbox/X11 semantics).
  - Smoke: extended `scripts/fbwl-smoke-config-dir.sh` to validate left/right toolbar geometry + strut; updated maximize/layer-shell expectations; added optional left/right toolbar screenshots in `scripts/fbwl-smoke-xvfb-kitchen-sink.sh` (gated by `FBWL_REPORT_DIR`).
- [x] Make `keys` “static groups” affect Next/PrevWindow ordering:
  - Policy: when `{static}` is present, cycle candidates in deterministic “creation order” (monotonic per-view `create_seq`), stepping next/prev relative to the currently focused view and wrapping at the ends.
  - Smoke: extended `scripts/fbwl-smoke-keys-file.sh` to spawn multiple matching windows and assert `{static groups}` Next/PrevWindow traverses in stable order.
- [x] Menu parser polish: menu icons + `[encoding]` blocks:
  - Icons: menu entries accept per-item `<icon>` paths; menu UI now reserves an icon column when any icon is present so labels stay aligned even when some items have no icon or a missing icon path.
  - Encoding: `[encoding] {…}` / `[endencoding]` blocks are supported to decode legacy menu files (default remains UTF-8 when unspecified).
  - Smoke: `scripts/fbwl-smoke-menu.sh` covers icon + encoding parsing (incl. a missing icon path); `scripts/fbwl-smoke-menu-icons.sh` exercises icon/no-icon/missing-icon rendering and captures a screenshot when `FBWL_REPORT_DIR` is set.
- [x] Style/theme parity: expand parsing until real Fluxbox styles can drive the UI:
  - Fill missing style keys that affect menus/toolbars/decorations (fonts, colors, bevel/border/gradients as needed).
  - Keep parsing tolerant (unknown keys warn once, not fatal) to support real-world style files.
  - Progress: add `menu.hilite.textColor` (selected item fg), `menu.frame.disableColor` (NOP/disabled fg), `toolbar.{textColor,label.textColor,windowLabel.textColor}` (with “specific overrides generic” precedence), `window.label.{focus,unfocus}.color` (mapped to our titlebar colors), plus `borderWidth`/`handleWidth`/`borderColor` aliases, `*.colorTo` “gradient blend” support for key surfaces (menus/toolbars/title/buttons), and basic font keys (`*.font`, `window.font`, `menu.*.font`, `toolbar.*.font`) wired into UI text rendering; log unique unknown keys (deduped, capped).
  - Smoke: `scripts/fbwl-smoke-style.sh` now uses a representative style fragment (BlueNight-derived) and asserts key parsing (including `*.colorTo` and font keys); `scripts/fbwl-smoke-xvfb-decor-style.sh` runs it under Xvfb.
- [x] Re-publish a screenshot report after completing the above:
  - Generate a fresh report via the Xvfb “kitchen sink” run and publish with `scripts/publish_screenshots.sh`.
  - Record the new “latest” URL in section 17.

---

## 20) Focus Parity — StrictMouseFocus stacking changes (Next)

Goal: close the remaining X11-vs-Wayland semantic gap for `StrictMouseFocus`: in Fluxbox/X11, stack changes can trigger synthetic Enter/Leave without pointer motion, which can shift focus when the pointer is stationary. Our Wayland implementation currently re-evaluates focus on pointer motion and on workspace-visibility changes (map/unmap/switch/minimize), but not on pure stacking operations like Raise/Lower.

- [x] `StrictMouseFocus`: when a restack operation changes which view is topmost under the pointer (without pointer motion), update keyboard focus to follow the pointer.
  - Implementation: capture `fbwl_view_at()` before and after Raise/Lower; if the topmost view under the cursor changed, call the existing pointer-focus re-evaluation path (StrictMouseFocus only).
  - Constraint: keep all `src/wayland/*.c|*.h` files under the 1000-LOC gate.
- [x] Smoke: add a deterministic stacking test that proves focus can change without pointer motion in `StrictMouseFocus`:
  - Script: `scripts/fbwl-smoke-strict-mousefocus-stacking.sh` (binds `Mod1 F2 :Lower` because `fbwl-input-injector` only supports a fixed set of key specs).
  - Config: `session.screen0.focusModel: StrictMouseFocus`, `session.screen0.windowPlacement: UnderMousePlacement`.
  - Scenario: spawn two overlapping clients under the cursor, then invoke `Lower` twice and assert focus flips between them without any `motion` injection.
  - Wired into: `scripts/fbwl-smoke-all.sh`, `scripts/fbwl-smoke-ci.sh`.
- [x] `StrictMouseFocus`: treat layer changes/reparenting as restacks
  - Gap: X11 can generate EnterNotify on non-Raise/Lower stack changes (e.g. `SetLayer`/`RaiseLayer`/`LowerLayer`, fullscreen reparenting), which can shift focus when the pointer is stationary.
  - Wayland: added a shared StrictMouseFocus restack helper (`server_strict_mousefocus_view_under_cursor` + `server_strict_mousefocus_recheck_after_restack`) and invoked it for `SetLayer` (keybindings + window menu) and fullscreen reparenting, so focus follows the topmost view under a stationary cursor.
  - Smoke: `scripts/fbwl-smoke-strict-mousefocus-layer.sh` (binds `Mod1 F2 :SetLayer Bottom` and asserts focus flips without any `motion` injection).

---

## 21) Parity Audit — Remaining X11 config/UX gaps (Next)

Goal: enumerate and close the remaining “classic Fluxbox/X11 config directory behaves the same” gaps that are still present in the Wayland backend.

This is primarily “wire the remaining `init` resources” work (see `doc/fluxbox-init-resources.tsv`), plus a few UX features that are core to Fluxbox daily use but currently missing or simplified on Wayland.

### 21.1 `init` resource parity (currently parsed but not fully applied)

Note: the Wayland backend now supports `session.screenN.*` resources for most policy/UI behavior via `ScreenMap` (Wayland outputs → Fluxbox “screens”), with `screenN` → `screen0` fallback semantics for unset keys. Some resources are still global or `screen0`-only by design (workspaces/workspaceNames/defaultDeco), and are called out below.

- [x] Generalize `session.screenN.*` application beyond `screen0`
  - Mapping: Wayland outputs are treated as Fluxbox “screens” via `ScreenMap` ordering.
  - Fallback: `session.screenN.foo` falls back to `session.screen0.foo` when unset (so existing single-screen configs apply everywhere unless overridden).
  - Runtime selection: screen config is chosen by either:
    - cursor position (`screen(cursor)`) for pointer-driven policy (focus model, clickRaises/autoRaise, move/resize thresholds, warping), or
    - view output (`screen(view)`) for per-view policy (maximize/fullscreen sizing, maxIgnoreIncrement, tabs maxOver).
  - Parity notes:
    - Workspaces: workspace **count/names remain global**, but the **current workspace is per-head** (switching targets the pointer head).
    - Menu behavior: `menuDelay` / `menu.alpha` apply at menu-open time (menus open at cursor; uses `screen(cursor)`).
    - Remote actions: Fluxbox/X11 is screen-scoped, but Wayland IPC is global; treat `allowRemoteActions` as a global gate (from `screen0`).
  - Smoke:
    - `scripts/fbwl-smoke-screen1-toolbar-overrides.sh` (toolbar on head 2 uses `session.screen1.toolbar.{placement,widthPercent,height}` overrides)
    - `scripts/fbwl-smoke-screen1-menu-overrides.sh` (root menu alpha+delay uses `session.screenN.menu.*` from `screen(cursor)`)

- [x] Expand `focusModel` enum compatibility to accept classic Fluxbox values:
  - Map `SloppyFocus` / `SemiSloppyFocus` (and any other upstream names) onto the closest Wayland policy, and add a smoke proving the mapping.
  - Smoke: `scripts/fbwl-smoke-focusmodel-aliases.sh`
- [x] Apply remaining global `session.*` resources (Wayland equivalents or documented no-ops):
  - `session.ignoreBorder`: implemented (when true, ignore `StartMoving` bindings on `OnWindowBorder` so borders don’t initiate move)
    - Smoke: `scripts/fbwl-smoke-ignore-border.sh`
  - `session.cacheLife` / `session.cacheMax`: applied to the internal icon buffer cache (menus/iconbar/window icons)
  - `session.colorsPerChannel`: parsed + stored; ignored on Wayland (kept for config compatibility)
  - `session.forcePseudoTransparency`: implemented (Wayland pseudo transparency; alpha blends against the desktop background only)
    - Smoke: `scripts/fbwl-smoke-pseudo-transparency.sh`
  - `session.groupFile`: parsed + logged (deprecated; grouping uses the `apps` file)
  - `session.configVersion`: parsed + stored (currently informational; no auto-migration yet)
- [x] Add `session.doubleClickInterval` and implement true “Double” mouse bindings in `keys`:
  - Parse the `Double` token into mouse bindings and dispatch based on `session.doubleClickInterval`.
  - Smoke: `scripts/fbwl-smoke-doubleclick.sh`
- [x] Implement the remaining screen-level focus / attention resources:
  - [x] `session.screenN.noFocusWhileTypingDelay`
    - Parse/apply from `init` and block focus-steal on map/activate when the currently focused view has received recent “typing” keypresses (approximate X11 `FluxboxWindow::isTyping()` semantics; `Enter` resets typing state).
    - Implementation: track `last_typing_time_msec` on the focused view from the seat keyboard path (`src/wayland/fbwl_server_seat_glue.c`), and gate map/activate focus via `server_focus_request_allowed()` (`src/wayland/fbwl_server_policy.c`).
    - Smoke: `scripts/fbwl-smoke-no-focus-while-typing.sh` (focusNew + typing delay blocks focus; attention starts; `Enter` allows focus again).
  - [x] `session.screenN.demandsAttentionTimeout`
    - Implemented as a Wayland-side “attention” timer that blinks the view’s decoration active state until it is focused (or destroyed), repeating at the configured interval.
    - Smoke: covered by `scripts/fbwl-smoke-no-focus-while-typing.sh` (asserts attention start + at least one toggle).
    - Attention UX: iconbar highlights urgent views and an attention OSD is shown on request (X11 has a dedicated attention state).
  - [x] `session.screenN.allowRemoteActions`
    - Gate IPC commands when disabled (Fluxbox/X11 default is false when driven by `init`).
    - Behavior: `ping` still works, other commands return `err remote_actions_disabled`.
    - Smoke: `scripts/fbwl-smoke-allow-remote-actions.sh`.
  - [x] `session.screenN.focusSameHead`
    - Implemented for both focus-cycle (Next/PrevWindow) and “revert focus” (when a focused view becomes invisible/unmaps/workspace changes): restrict candidate selection to the output under the pointer (Wayland analogue of X11 `getCurrHead()`).
    - Parity: disable the restriction while moving a window (Wayland: `server->grab.mode == FBWL_CURSOR_MOVE`, matching Fluxbox/X11’s `focusedFbWindow()->isMoving()` exception).
    - Parity: if there are no focusable windows on the current head, revert-focus clears focus (Wayland: `clear_keyboard_focus()` paths), matching Fluxbox/X11’s revert-focus fallback to focusing the root/pointer.
    - Smoke: `scripts/fbwl-smoke-focus-same-head.sh` (two outputs; make MRU on other head; close focused window; assert focus stays on pointer head).
- [x] Implement the remaining move/resize/placement/maximize resources:
  - [x] `session.screenN.edgeSnapThreshold` / `session.screenN.edgeResizeSnapThreshold` (snap-to-edge during move/resize)
    - Implemented: snap-to-output-edge for interactive move/resize (uses output usable area; SSD-aware when server-side decorations are enabled).
    - Defaults match Fluxbox/X11: `edgeSnapThreshold=10`, `edgeResizeSnapThreshold=0`.
    - Smoke: `scripts/fbwl-smoke-edge-snap.sh`.
  - [x] `session.screenN.opaqueMove` / `session.screenN.opaqueResize` / `session.screenN.opaqueResizeDelay`
    - Defaults match Fluxbox/X11: `opaqueMove=True`, `opaqueResize=False`, `opaqueResizeDelay=50`.
    - Implemented:
      - `opaqueMove`: controls whether interactive move is “live” (moves the view each motion) vs an outline.
      - `opaqueResize`: controls whether interactive resize is “live” vs an outline that applies on release.
      - `opaqueResizeDelay`: when `opaqueResize=True`, debounces live-resize applies (matches Fluxbox/X11 timer semantics).
    - Smoke: `scripts/fbwl-smoke-opaque-resize.sh` (covers outline vs `opaqueResize=True` + delay; asserts `Resize: apply-delay` happens before the release log).
  - [x] `session.screenN.showwindowposition` (OSD during move/resize)
    - Implemented: when enabled, shows an OSD during interactive move (frame x/y) and resize (client w/h).
    - Note: uses a separate OSD instance from the workspace OSD to avoid message clobbering.
    - Smoke: `scripts/fbwl-smoke-showwindowposition.sh`.
  - [x] `session.screenN.workspacewarping` + related resources (drag a window across output edge to switch workspace):
    - Implemented: while moving, entering the edge “warp pad” switches workspaces and warps the pointer to the opposite edge (Fluxbox/X11-style).
    - Uses `edgeSnapThreshold` as the warp pad width (Fluxbox/X11 behavior).
    - If `opaqueMove=True`, the dragged window is moved to the new workspace (send + switch); otherwise only the workspace switches.
    - Resources:
      - `session.screenN.workspacewarpinghorizontal` / `session.screenN.workspacewarpingvertical`
      - `session.screenN.workspacewarpinghorizontaloffset` / `session.screenN.workspacewarpingverticaloffset`
    - Smoke: `scripts/fbwl-smoke-workspace-warping.sh`.
  - [x] `session.screenN.maxIgnoreIncrement` / `session.screenN.maxDisableMove` / `session.screenN.maxDisableResize`
    - Defaults match Fluxbox/X11: `maxIgnoreIncrement=True`, `maxDisableMove=False`, `maxDisableResize=False`.
    - `maxIgnoreIncrement`: for XWayland, when false, maximize snaps to WM_NORMAL_HINTS resize increments (Fluxbox/X11 `applySizeHints` path); when true, maximize uses the full target area.
    - `maxDisableMove` / `maxDisableResize`: when true, block starting move/resize grabs on fully-maximized/fullscreen views; resize (when allowed) restores/unmaximizes before starting (Fluxbox/X11 `startResizing()` behavior).
    - Smokes:
      - `scripts/fbwl-smoke-xwayland-max-ignore-increment.sh` (XWayland size hints increments)
      - `scripts/fbwl-smoke-max-disable-move-resize.sh` (blocked grabs while maximized)
  - [x] `session.screenN.fullMaximization`
    - Implemented: maximize (full + horizontal + vertical) uses the full output box (ignores usable-area/struts), matching Fluxbox/X11 `fullMaximization`.
    - Smoke: `scripts/fbwl-smoke-full-maximization.sh` (`fullmax` case).
  - [x] `session.screenN.toolbar.maxOver`
    - Implemented: when true, the toolbar does not reserve a strut in the usable area (Fluxbox/X11 toolbar strut behavior).
    - Smoke: `scripts/fbwl-smoke-full-maximization.sh` (`toolbarmaxover` case).
  - [x] `session.screenN.tabs.maxOver`
    - Implemented (external tabs only): when `tabs.intitlebar=false` and a window is tabbed, maximize (full + horizontal + vertical) either reserves space for the external tab strip (`tabs.maxOver=false`) or ignores it so the tab strip can extend off-screen (`tabs.maxOver=true`), matching Fluxbox/X11 `max_over_tabs` behavior.
    - Smoke: `scripts/fbwl-smoke-tabs-maxover.sh` (TopCenter placement; size difference is `titleHeight + borderWidth`).
  - [x] `session.screenN.slit.maxOver`
    - Implemented: when `slit.maxOver=false`, the slit reserves its thickness in the output usable-area box; when `slit.maxOver=true`, maximize/placement ignore the slit and can extend under it (Fluxbox/X11 semantics).
    - Smoke: `scripts/fbwl-smoke-slit-maxover.sh` (DOCK XWayland client + usable-area difference).
- [x] Implement the remaining toolbar/menu/iconbar resources:
  - [x] `session.screenN.strftimeFormat` (clock format)
    - Implemented: toolbar clock now uses the configured strftime format (default remains `%H:%M`).
    - Smoke: `scripts/fbwl-smoke-strftime-format.sh` (sets format to `FMTTEST` and asserts toolbar log).
  - [x] `session.screenN.menu.alpha` / `session.screenN.toolbar.alpha` (alpha for compositor-drawn UI)
    - Implemented: apply classic 0–255 alpha to compositor-drawn menu + toolbar surfaces (background/highlight/labels).
    - Smoke: `scripts/fbwl-smoke-alpha.sh` (asserts `Toolbar: position ... alpha=` and `Menu: open ... alpha=` logs from a config-dir init).
  - [x] `session.screenN.tooltipDelay`
    - Implemented (toolbar iconbar): when hovering an iconbar item whose title is ellipsized, show a tooltip after `tooltipDelay` ms (`0` = immediate, `<0` = disabled), matching Fluxbox/X11 behavior.
    - Smoke: `scripts/fbwl-smoke-tooltip-delay.sh`.
  - [x] `session.menuSearch` (menu typeahead/search behavior)
    - Implemented: classic `nowhere|itemstart|somewhere` modes; type-to-select works while a menu is open.
    - Smoke: `scripts/fbwl-smoke-menu-search.sh` (execs a typed match in both `itemstart` and `somewhere` modes).
  - [x] `session.screenN.iconbar.*` (mode/alignment/icon sizing/text padding/iconifiedPattern/usePixmap)
    - Implemented: `mode`, `alignment`, `iconWidth`, `iconTextPadding`, `usePixmap`, `iconifiedPattern`.
    - Icons: Wayland `app_id` → icon theme lookup (and absolute path support) via `src/wayland/fbwl_icon_theme.c`.
    - Smokes: `scripts/fbwl-smoke-iconbar.sh`, `scripts/fbwl-smoke-iconbar-resources.sh`.
    - Parity note: `iconbar.mode` supports a subset of Fluxbox ClientPattern terms; extend as needed.
  - [x] `session.screenN.clientMenu.usePixmap`
    - Implemented: `ClientMenu` keybinding command opens a per-workspace client list menu. When enabled, entries resolve icons from `app_id` via icon theme lookup (and absolute path support).
    - Smoke: `scripts/fbwl-smoke-clientmenu-usepixmap.sh`.
  - [x] `session.screenN.systray.pinLeft` / `session.screenN.systray.pinRight` (and `session.screenN.pinLeft` / `session.screenN.pinRight` alias from docs)
    - Implemented: Fluxbox-style left/right pin ordering for SNI tray icons; matches tokens against StatusNotifierItem `Id` (case-insensitive).
    - Smoke: `scripts/fbwl-smoke-tray-pin.sh` (covers both `systray.pin*` and alias `pin*` keys).
  - [x] `session.screenN.toolbar.layer` / `session.screenN.toolbar.onhead`
    - Implemented:
      - `session.screen0.toolbar.onhead` (Fluxbox/X11-style 1-based head index) selects which Wayland “screen” (output index from `ScreenMap`) the (single) toolbar is placed on.
      - Toolbar config is sourced from `session.screen(toolbar_screen).*` so per-screen overrides apply when the toolbar is moved to another output (e.g. `session.screen1.toolbar.placement`).
      - `toolbar.layer` maps Fluxbox layer names/numbers to compositor scene layers (overlay/top/normal/bottom/background).
      - Input parity: toolbar click/hover only triggers when the toolbar is the topmost scene node under the pointer (so lower layers don’t steal clicks from windows).
    - Smokes:
      - `scripts/fbwl-smoke-toolbar-onhead.sh` (placement + strut applies only on the toolbar output)
      - `scripts/fbwl-smoke-toolbar-layer.sh` (Top vs Bottom layer click routing)
      - `scripts/fbwl-smoke-screen1-toolbar-overrides.sh` (screen1 toolbar overrides apply when on head 2)
  - [x] `session.screenN.toolbar.button.<name>.{label,commands}` + `session.screenN.toolbar.tools` `button.<name>` entries
    - Implemented: `button.<name>` tokens in `toolbar.tools` are rendered as clickable tool buttons; `toolbar.button.<name>.commands` is parsed as a colon-delimited button1..button5 command list (Fluxbox key commands).
    - Execution: commands are resolved via `fbwl_fluxbox_cmd_resolve()` and run through the keybinding action path (so RootMenu/Workspace/etc behave like keybindings).
    - Note: we also route vertical scroll wheel events over toolbar buttons to button4/button5 commands (Wayland axis → Fluxbox mouse button mapping).
    - Smoke: `scripts/fbwl-smoke-toolbar-buttons.sh`.
  - [x] Toolbar tool ordering + per-tool widgets parity (`session.screenN.toolbar.tools`):
    - Implemented: tools are laid out in the exact order given by `toolbar.tools` (ordered list), including per-tool widgets like `workspacename`, `prevworkspace`, `nextworkspace`, `prevwindow`, `nextwindow`, plus `button.<name>` tools.
    - Notes:
      - The built-in “segment” tools `iconbar` / `systemtray` / `clock` are treated as singletons; the first occurrence wins.
    - Smokes: `scripts/fbwl-smoke-toolbar.sh`, `scripts/fbwl-smoke-toolbar-tools-order.sh`.
  - [x] Implement the remaining titlebar + per-window defaults resources:
  - [x] `session.titlebar.left` / `session.titlebar.right` and `session.screenN.titlebar.{left,right}` (button list + order)
    - Implemented for `screen0`: init parsing supports both global and per-screen overrides; used for compositor-drawn titlebar button layout.
    - Smoke: `scripts/fbwl-smoke-titlebar-buttons.sh`.
  - [x] `session.screenN.defaultDeco` (default decoration set semantics; Wayland equivalent maps to SSD on/off)
    - Implemented: treat `NONE` as “no SSD” (we still request xdg-decoration server-side; we simply don’t draw).
    - Applies to XWayland too (no WM decorations when `NONE`).
    - Reconfigure: changing the default updates existing non-forced windows.
    - Smoke: `scripts/fbwl-smoke-default-deco.sh`.
  - [x] `session.screenN.window.{focus,unfocus}.alpha` (default per-view alpha, overridden by apps rules)
    - Implemented: parse `session.screen0.window.{focus,unfocus}.alpha` from `init` and apply as the default alpha for views that don’t already have `[Alpha]` set via `apps` (or via the window menu).
    - Reconfigure: changing the defaults updates existing windows that are still following the defaults (and does not override `apps` alpha).
    - Smoke: `scripts/fbwl-smoke-window-alpha.sh`.
  - [x] `session.screenN.windowMenu` (load the `windowmenu` file and use it for the window menu structure)
    - Implemented: `session.screen0.windowMenu` (plus `session.windowMenu` fallback) and `windowmenu` fallback file discovery.
    - Smoke: `scripts/fbwl-smoke-window-menu.sh`.
  - [x] Tabs UI parity (`session.screenN.tabs.*` + `session.screenN.tab.*` UX)
    - Implemented: visible tab strip UI for tab groups (intitlebar + external placement via `tab.placement`), including click-to-activate and mouse-tab-focus (`tabFocusModel`).
    - Sizing/padding: honors `tab.width` + `session.tabPadding`.
    - Pixmap icons: honors `tabs.usePixmap` (XWayland prefers `_NET_WM_ICON`, Wayland uses icon theme lookup from `app_id`).
    - Drag-to-tab: honors `session.tabsAttachArea` on move-grab release (Titlebar-only vs Window).
    - Smokes:
      - `scripts/fbwl-smoke-tabs-ui-click.sh` (click-to-activate)
      - `scripts/fbwl-smoke-tabs-ui-mousefocus.sh` (MouseTabFocus hover activation)
      - `scripts/fbwl-smoke-tabs-attach-area.sh` (Titlebar-only attach enforcement)
      - `scripts/fbwl-smoke-tabs-maxover.sh` (external maximize offsets)
- [x] Implement remaining “struts” resources (Wayland mapping):
  - `session.screenN.struts` sets extra reserved pixels on output N (applies to placement + maximize).
  - Compatibility: `session.screen0.struts.<ai>` (Fluxbox/X11 “head” struts) maps `<ai>` (1-based) to Wayland outputs from `ScreenMap`.
  - Precedence: `screen0.struts` base → `screen0.struts.<ai>` per-output → `screenN.struts` explicit per-output override (for N>0).
  - Smoke: `scripts/fbwl-smoke-struts.sh` (2 outputs; asserts both Place usable box and Maximize size respect struts + override precedence).

### 21.2 Core UX parity still missing (beyond `init`)

- [x] Mousebinding context parity (grips):
  - Distinguish `OnWindowBorder` vs `OnLeftGrip` vs `OnRightGrip` (previously `OnLeftGrip`/`OnRightGrip` collapsed to `OnWindowBorder`, breaking the stock `data/keys` bindings).
  - Implementation: added explicit contexts + detect left/right grips from `fbwl_view_decor_hit_test().edges` (bottom-left/bottom-right), and covered with `scripts/fbwl-smoke-grips.sh`.
- [x] Titlebar buttons parity:
  - Implemented the classic Fluxbox button set: `MenuIcon`, `Shade`, `Stick`, `Minimize`, `Maximize`, `Close`, `LHalf`, `RHalf`.
  - Layout is driven by `session.titlebar.{left,right}` with `session.screen0.titlebar.{left,right}` overrides.
  - Behavior:
    - `MenuIcon` opens the window menu.
    - `Shade` toggles shade.
    - `Stick` toggles sticky and reapplies workspace visibility.
    - `LHalf`/`RHalf` tile the window into the left/right half of the current output’s usable area.
    - Maximize/tiling now accounts for the SSD frame (border/title) so the titlebar stays on-screen/clickable.
  - Theming: new button colors are wired to the existing `window.button.focus.color` / `window.button.focus.colorTo` keys (applies to the full button set).
  - Smoke: `scripts/fbwl-smoke-titlebar-buttons.sh`.
- [x] Window menu parity:
  - Implemented `windowmenu` file parsing + fallback default matching `data/windowmenu` ordering.
  - Added classic window actions: Shade, Stick, Raise/Lower, SendTo (workspace submenu), Layer submenu, Alpha submenu.
  - Note: `SetTitleDialog` is implemented as a compositor-local title override (decorations/iconbar/foreign-toplevel only). It does **not** change the app-provided title, and the override is cleared if the client later updates its title.
  - Smoke: `scripts/fbwl-smoke-window-menu.sh`.
- [x] Keybinding command parity (fluxbox-keys):
  - Implemented: `MaximizeHorizontal`, `MaximizeVertical`, `RaiseLayer`, `LowerLayer`, `SetLayer <layer>` (accepts both classic layer names like `Top`/`Normal`/`Bottom`/`Desktop`/`Menu` and numeric values).
  - Parity note: `MaximizeHorizontal`/`MaximizeVertical` preserve the other axis when toggling from fully-maximized (modeled as independent `{horz,vert}` bits + a single restore geometry).
  - Smokes: `scripts/fbwl-smoke-keybinding-commands.sh`, `scripts/fbwl-smoke-maximize-axis-toggle.sh`.
- [x] Iconbar parity:
  - [x] Wayland icons: derive from `app_id` via icon theme lookup (and absolute path support).
  - [x] XWayland icons: derive from WM hints/NET_WM_ICON.
  - [x] Apply `iconbar.*` resources (filtering, ordering, layout) for X11-like defaults.
    - Parity note: `iconbar.mode` supports a subset of ClientPattern.
  - Smokes: `scripts/fbwl-smoke-iconbar.sh`, `scripts/fbwl-smoke-iconbar-resources.sh`, `scripts/fbwl-smoke-xwayland-net-wm-icon.sh`.
- [x] Slit/dockapps parity:
  - [x] Strategy: XWayland DOCK windows-only “slit” (best-effort).
  - [x] Implement compositor-managed slit container:
    - Manages XWayland surfaces with `_NET_WM_WINDOW_TYPE_DOCK` (excluding surfaces that set `strut_partial`, i.e. panels).
    - Config: `session.screenN.slit.{placement,onhead,layer,autoHide,autoRaise,maxOver,alpha,direction,acceptKdeDockapps}`.
    - Usable-area behavior: reserves `slit.thickness` unless `slit.maxOver=true` or `slit.autoHide=true`.
  - [x] Smoke: `scripts/fbwl-smoke-slit-maxover.sh` (DOCK XWayland client + `slit.maxOver` usable-area effect).
  - [x] `session.slitlistFile` ordering (match Fluxbox/X11 slitlist WM_CLASS ordering).
    - Smoke: `scripts/fbwl-smoke-slit-ordering.sh` (spawn DOCK clients in reverse order; assert slit ordering matches slitlist).
  - [x] `slit.acceptKdeDockapps` parity (KDE dockapp property detection: `KWM_DOCKWINDOW` / `_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR`).
    - Smoke: `scripts/fbwl-smoke-slit-kde-dockapps.sh` (assert accept=false behaves like normal XWayland; accept=true routes into slit).
  - [x] Slit menu parity (Fluxbox/X11 “Slit” right-click menu + Clients submenu + “Save SlitList” write-back to `session.slitlistFile`):
    - Opens on right-click within the slit; left-click raises the slit but still forwards the click to the dockapp (Fluxbox/X11 uses `ReplayPointer` + raises the slit window on button1).
    - Menu items: Placement, Layer, On Head, Auto hide, Auto raise, Maximize Over, Alpha (presets), Clients submenu.
    - Clients submenu: Cycle Up/Down, per-client visibility toggles, “Save SlitList” write-back to `session.slitlistFile` (match name preference: XWayland instance → class → app_id → title).
    - Smoke: `scripts/fbwl-smoke-slit-menu.sh` (cycle down, save, assert `slitlist` contents).
    - Parity notes:
      - Alpha is a preset percentage submenu (not Fluxbox/X11’s 0–255 integer menu item).
      - Per-client move up/down via wheel/middle/right click isn’t implemented; menu currently closes on click (no “stay open” toggle/radio semantics).
  - [x] Slit parity: auto-save `session.slitlistFile` on DOCK attach + shutdown (Fluxbox/X11 writes `slitlist` in `addClient()` and `shutdown()`).
    - Smoke: `scripts/fbwl-smoke-slit-autosave.sh` (assert file is created/updated on attach + updated on shutdown after cycle-down without manual “Save SlitList”).
  - [x] Legacy tray parity for X11 apps (XEmbed):
    - Implemented: optional XEmbed → SNI bridge via an external proxy auto-started on XWayland ready:
      - Default: auto (prefers `xembedsniproxy`, then `snixembed`, then `xembed-sni-proxy` if found in `PATH`).
      - Override: set `FBWL_XEMBED_SNI_PROXY=0/false/off` to disable, or set it to a custom shell command string (supports args).
      - Lifecycle: proxy PID is tracked and terminated on compositor shutdown.
    - Smoke: `scripts/fbwl-smoke-xembed-tray.sh` (skips if proxy binary missing; runs `./fbx11-xembed-tray-client` under XWayland and validates the tray icon render via `fbwl-screencopy-client --expect-rgb`).
  - [x] Wallpaper parity:
    - Implement compositor-native wallpaper rendering (PNG) via a scene buffer per output (keeps solid-color fallback).
    - Menu tag: `[wallpapers]` with no `{command}` now sets wallpaper via a server action (no PATH dependency); custom `{command}` still executes as before.
    - IPC: `fbwl-remote wallpaper <path>` sets wallpaper; `fbwl-remote wallpaper none` clears.
    - Smoke: `scripts/fbwl-smoke-wallpaper.sh` (generates a 64x64 PNG, sets wallpaper, validates via `fbwl-screencopy-client --expect-rgb`).

### 21.3 Engineering constraint (unblocks further parity work)

- [x] Refactor/split `src/wayland/fbwl_server_policy_input.c` to regain LOC headroom:
  - Current state: file peaked at 1041 LOC after per-head workspace + keybinding hook work and must remain < 1000 (enforced by `scripts/fbwl-check-wayland-loc.sh`). Now ~965 LOC.
  - Goal: make future parity patches safe without “whitespace surgery”.
  - Implementation: moved workspace visibility + head-aware workspace switching helpers (`apply_workspace_visibility()`, `server_workspace_switch_on_head()`) into `src/wayland/fbwl_server_ui.c` (which is now ~977 LOC) and switched StrictMouseFocus follow-up to use the existing `server_strict_mousefocus_recheck()` helper.
- [x] Refactor/split `src/wayland/fbwl_keybindings.c` to regain LOC headroom:
  - Current state: file sat at 999 LOC; cycle-pattern parsing + candidate selection moved into `src/wayland/fbwl_keybindings_cycle.c` so we can keep iterating on keybinding parity safely.
- [x] Refactor/split `src/wayland/fbwl_server_bootstrap.c` to regain LOC headroom:
  - Current state: was 999 LOC; moved shared init parsing helpers into `src/wayland/fbwl_server_config.c` (bootstrap now ~677 LOC).
- [x] Restore Next/PrevWindow “cycle” focus logging after keybindings refactor:
  - Why: `scripts/fbwl-smoke-input.sh` asserts `Policy: focus (cycle)` for `NextWindow` (Alt+F1).
  - Implementation: added `fbwm_core_focus_view_with_reason()` and use it for keybinding-driven cycle focus.
- [x] Refactor/split `src/wayland/fbwl_view.c` to regain LOC headroom:
  - Current state: file sat at 998 LOC and must remain < 1000 (enforced by `scripts/fbwl-check-wayland-loc.sh`).
  - Implementation: moved decor/alpha/shade helpers into `src/wayland/fbwl_view_decor.c` and updated `src/Makemodule.am` (view.c now ~630 LOC).
  - Goal: unblock parity work that needs to touch view placement/maximize/output-usable logic (e.g. struts).
- [x] Refactor/split `src/wayland/fbwl_ui_menu.c` to regain LOC headroom:
  - Current state: `src/wayland/fbwl_ui_menu.c` now ~702 LOC; icon loading moved into `src/wayland/fbwl_ui_menu_icon.c`.
  - Goal: unblock menu pixmap + tooltipDelay parity work without risking the LOC gate.
- [x] Refactor/split `src/wayland/fbwl_ui_toolbar.c` to regain LOC headroom:
  - Implementation: extracted iconbar + tray build logic into `src/wayland/fbwl_ui_toolbar_iconbar.c` and `src/wayland/fbwl_ui_toolbar_tray.c` (internal API in `src/wayland/fbwl_ui_toolbar_build.h`), and updated `src/Makemodule.am`.
  - Current state: `fbwl_ui_toolbar.c` now ~821 LOC so we can safely implement `tooltipDelay`, toolbar buttons, and remaining iconbar resources without tripping the LOC gate.
- [x] Refactor/split `src/wayland/fbwl_sni_item.c` to regain LOC headroom:
  - Current state: file sat at ~981 LOC and must remain < 1000.
  - Goal: unblock further SNI/tray parity work without risking the LOC gate.
  - Implementation: moved request/reply logic into `src/wayland/fbwl_sni_item_requests.c` and updated `src/Makemodule.am` (`fbwl_sni_item.c` now ~161 LOC).
- [x] Refactor/split `src/wayland/fbwl_apps_rules.c` to regain LOC headroom:
  - Current state: file sits at ~989 LOC and must remain < 1000.
  - Goal: unblock further `apps` parity work without risking the LOC gate.
  - Implementation: split into `src/wayland/fbwl_apps_rules_load_helpers.c`, `src/wayland/fbwl_apps_rules_load.c`, and `src/wayland/fbwl_apps_rules_match.c`, with `src/wayland/fbwl_apps_rules.c` reduced to the `free()` API; updated `src/Makemodule.am`.

---

## 22) Focus Parity — StrictMouseFocus geometry-only changes (Done)

Goal: close the remaining X11-vs-Wayland semantic gap for `StrictMouseFocus`: in Fluxbox/X11, pure geometry changes can trigger synthetic Enter/Leave without pointer motion, which can shift focus when the pointer is stationary.

- [x] `StrictMouseFocus`: after geometry-only WM operations that can move a view under/away from a stationary cursor, re-evaluate view-under-cursor and update keyboard focus accordingly.
	  - Implemented:
	    - Immediate view-tree changes: call `server_strict_mousefocus_recheck_after_restack(...)` after maximize/restore (`fbwl_view_set_maximized()`), shade/unshade (`fbwl_view_set_shaded()`), tiling (`LHalf`/`RHalf`), and maximize-h/maximize-v keybinding actions.
	    - Commit-time resize parity: on XDG/XWayland surface size commits, if the cursor crosses the view’s frame bounds (old size vs new size), call `server_strict_mousefocus_recheck(...)` so focus can change *without* pointer motion even when the resize is async (XDG configure/commit).
	    - Keybinding geometry ops: `MoveTo`/`Move`/`ResizeTo`/`Resize*` and keyboard-driven `StartMoving`/`StartResizing` call `server_strict_mousefocus_recheck(...)` so focus can change without pointer motion like Fluxbox/X11.
	    - Focus no-op guard: `focus_view()` only short-circuits when both the compositor policy focus (`server->focused_view`) *and* the seat focus (`seat->keyboard_state.focused_surface`) already match the target, so StrictMouseFocus rechecks still work even when keyboards come/go (headless `fbwl-input-injector`).
- [x] Smoke: deterministic headless geometry test proving focus can change without pointer motion in `StrictMouseFocus`.
  - Smoke: `scripts/fbwl-smoke-strict-mousefocus-geometry.sh` (maximize → move cursor over another view → unmaximize; also MoveTo a window away; asserts focus flips with no pointer motion between the geometry change and focus change).
- [x] Parity guard: ensure `MouseFocus` continues to *only* change focus on pointer motion/entry (i.e., geometry-only changes should not shift focus in `MouseFocus`, matching Fluxbox/X11’s “ignoreAtPointer” intent).
  - Smoke: `scripts/fbwl-smoke-mousefocus-geometry.sh` (same scenario; asserts *no* focus flip after unmaximize without motion).

---

## 23) Parity Nits — Remaining “Not Quite X11” Items (Done)

Goal: capture the remaining known deviations where Wayland allows closer behavior, so “total parity” has an explicit checklist.

- [x] MaximizeHorizontal/MaximizeVertical axis-toggle parity
  - Fixed: maximize is modeled as independent `{horz, vert}` bits + a single restore geometry, so toggling one axis from fully-maximized preserves the other axis (Fluxbox/X11 behavior).
  - Smoke: `scripts/fbwl-smoke-maximize-axis-toggle.sh`
- [x] Iconbar `RelativeSmart` alignment semantics
  - Fixed: `RelativeSmart` varies item widths based on preferred title width (Fluxbox/X11-like behavior).
  - Smoke: `scripts/fbwl-smoke-iconbar-resources.sh` (`RelativeSmart` case).
- [x] MouseFocus / StrictMouseFocus semantics parity (X11)
  - Summary: `MouseFocus` changes focus only on real pointer motion; `StrictMouseFocus` also re-evaluates view-under-cursor on compositor-driven restacks and geometry/workspace visibility changes so focus can change with a stationary pointer (Fluxbox/X11 behavior).
  - Smokes: `scripts/fbwl-smoke-mousefocus-geometry.sh`, `scripts/fbwl-smoke-strict-mousefocus-geometry.sh`, `scripts/fbwl-smoke-strict-mousefocus-stacking.sh`.
- [x] Iconbar `mode` ClientPattern completeness
  - Fixed: `iconbar.mode` now supports the documented Fluxbox ClientPattern term *names* used by iconbar mode, with explicit Wayland/implementation limitations.
    - Implemented: `name/class/title/role`, `transient`, `workspace/workspacename`, `head` (incl. `[mouse]`), `layer`, `screen`, plus state terms (`minimized/maximized/maximizedhorizontal/maximizedvertical/fullscreen/shaded/stuck/urgent`).
    - Parity: `focushidden` now maps to “hidden from focus-cycling” for XWayland toplevels with `_NET_WM_WINDOW_TYPE` in `{DOCK, DESKTOP, SPLASH}` (and slit-managed docks). XDG toplevels have no standard window-type, so `focushidden` is always `no` for them.
    - Parity note: Fluxbox/X11 iconbar always appends `(iconhidden=no)`; Wayland does the same. In Wayland, `iconhidden=yes` covers XWayland `_NET_WM_STATE_SKIP_TASKBAR` **and** XWayland `_NET_WM_WINDOW_TYPE` in `{DOCK, DESKTOP, SPLASH, TOOLBAR, MENU}` (plus slit-managed docks), matching Fluxbox’s defaults.
    - Parity: string terms use anchored POSIX regex matching (Fluxbox-like whole-string match; substring via `.*`). Known limitation: `[current]` is only supported for `workspace` (and `[mouse]` for `head`).
  - Smoke: `scripts/fbwl-smoke-iconbar-resources.sh` (maximizedhorizontal + workspacename cases).
- [x] Slit menu “stay open” + per-client reorder shortcuts
  - Fixed: slit toggle/radio items and slit client items are “closeOnClick=false” (Fluxbox/X11 behavior), so you can toggle/reorder multiple entries without the menu closing.
  - Fixed: per-client reorder shortcuts now match Fluxbox/X11’s `SlitClientMenuItem::click()` mapping:
    - Move up: wheel-up (Wayland vertical scroll) or middle click.
    - Move down: wheel-down (Wayland vertical scroll) or right click.
    - Toggle visibility: left click / Enter.
  - Smoke: `scripts/fbwl-smoke-slit-menu.sh` (opens Clients submenu, right-clicks dock-a to move it down, then saves without reopening; asserts no menu close + slitlist order).
- [x] Slit menu alpha input parity (0–255)
  - Fixed: slit Alpha menu now includes a “Set Alpha…” integer-entry prompt (0–255), matching Fluxbox/X11’s `IntMenuItem` semantics.
    - Note: we keep the preset % entries as a convenience (they’re still “stay open” like radio items).
  - Smoke: `scripts/fbwl-smoke-slit-alpha-input.sh` (opens Alpha submenu → Set Alpha… → types `128` → asserts `Slit: set-alpha 128 (prompt)` + layout alpha=128).
- [x] DemandsAttention parity in iconbar/OSD
  - Fixed: urgent views now use “focused” iconbar styling (Fluxbox/X11 `FocusableTheme` behavior: focused OR attention state → focused theme).
  - Fixed: OSD now shows a short “Attention: …” message when attention starts.
  - Smoke: `scripts/fbwl-smoke-no-focus-while-typing.sh` (asserts `OSD: show attention ...` + `Toolbar: iconbar attention ...` on `Attention: start`).
- [x] Multi-screen workspace semantics (per-head / Xinerama parity)
  - Implemented: per-head current workspace; workspace switching acts on the pointer head; visibility is `sticky || view.workspace == head_current(view_head)`.
  - UI/IPC: toolbar `workspacename` uses the toolbar’s head; workspace menu selects/switches on the menu’s head; IPC supports optional head targeting.
  - Smoke: `scripts/fbwl-smoke-multi-output.sh` (switches head 0 and head 1 independently; asserts per-head visibility isolation).

---

## 24) Parity Gaps — Remaining “Not Quite X11” Items (Next)

Goal: capture the remaining known gaps where either (a) we can match Fluxbox/X11 more closely, or (b) Wayland protocol limits mean we need explicit “best-effort” semantics + documentation.

- [x] ClientPattern regex parity (iconbar + keybinding patterns)
  - Fixed: string terms use anchored POSIX regex semantics (whole-string match; substring via `.*`).
  - Parity quirk: like Fluxbox/X11, anchoring uses `^PATTERN$` (no extra parens), so `|` precedence matches X11 (e.g. `foo|bar` matches “starts with foo” OR “ends with bar”).
  - Smokes: `scripts/fbwl-smoke-iconbar-resources.sh`, `scripts/fbwl-smoke-keys-file.sh`, `scripts/fbwl-smoke-clientpattern-regex-quirk.sh`.

- [x] `focushidden` ClientPattern semantics
  - Fixed: `focushidden` now has concrete Wayland-side meaning for XWayland toplevels: `_NET_WM_WINDOW_TYPE` in `{DOCK, DESKTOP, SPLASH}` (and slit-managed docks) → `yes`, otherwise `no`.
  - Fixed: focus cycling (`NextWindow`/`PrevWindow`) skips focushidden views (Fluxbox/X11 behavior).
  - Smoke: `scripts/fbwl-smoke-focushidden.sh` (spawns an XWayland desktop-type view and asserts `NextWindow` skips it).
  - Note: there is no standard XDG/Wayland analogue for `_NET_WM_WINDOW_TYPE`, so pure Wayland toplevels can’t self-declare `focushidden`; use apps rules `[FocusHidden] {yes}` / `[Hidden] {yes}` to force it when needed.

- [x] Keyboard-driven move/resize parity
  - Fixed: `:StartMoving` / `:StartResizing` from keybindings now create a keyboard grab: arrow keys step; Enter commits; Escape cancels (Fluxbox/X11 behavior).
  - Detail: step size is 10px (Ctrl=1px, Shift=50px). StrictMouseFocus recheck runs on commit/cancel and after opaque move/resize steps that change geometry.
  - Smoke: `scripts/fbwl-smoke-keyboard-move-resize.sh`

- [x] Xinerama per-head workspaces
  - Implemented: per-head `workspace_current` in WM-core; workspace switching targets the pointer head; visibility is head-aware (`sticky || ws == current(head)`).
  - UI: toolbar/workspace menu are head-aware.
  - IPC: `get-workspace [head]`, `workspace <n> [head]`, `nextworkspace [head]`, `prevworkspace [head]` (head is 1-based; default head 1).
  - Smoke: `scripts/fbwl-smoke-multi-output.sh`

- [x] Pixmap/theme caching resources (`session.cacheLife` / `session.cacheMax` / `session.colorsPerChannel`)
  - Implemented: disk-loaded menu/iconbar/titlebar icons are now cached (decoded+scaled Cairo surfaces keyed by `(path, icon_px)`), honoring `session.cacheLife` (minutes) + `session.cacheMax` (kB). Setting either to `0` disables caching and clears the cache.
  - Parity note: `session.colorsPerChannel` remains a no-op in Wayland (ARGB32 rendering).

---

## 25) Parity Audit — Newly Found X11 Gaps (Next)

Goal: capture remaining Fluxbox/X11 behaviors not yet mirrored in the Wayland backend, so “total parity” stays an explicit checklist.

- [x] Apps (“Remember”) hidden-state parity: `[Hidden]` / `[FocusHidden]` / `[IconHidden]`
  - Implemented: parse/apply these keys from the `apps` file.
    - `[Hidden] {yes|no}` sets both `FocusHidden` + `IconHidden`.
    - `[FocusHidden] {yes|no}` hides the view from focus cycling (Alt-Tab / `NextWindow` / `PrevWindow`).
    - `[IconHidden] {yes|no}` hides the view from the toolbar iconbar.
  - Parity: `FocusHidden` only affects “focus list” semantics (not click/mouse focusing), matching Fluxbox/X11’s use of focus-hidden state.
  - Smoke: `scripts/fbwl-smoke-apps-hidden.sh`.

- [x] Apps (“Remember”) maximize-mode parity: `[Maximized] {horz|vert}` applies axis-only maximize (not full maximize)
  - Fixed: apps parsing now preserves the axis form:
    - `[Maximized] {yes}` → `h=1 v=1`
    - `[Maximized] {horz}` → `h=1 v=0`
    - `[Maximized] {vert}` → `h=0 v=1`
    - `[Maximized] {no}` → `h=0 v=0`
  - Applied: post-map uses `fbwl_view_set_maximized_axes()` so XDG and XWayland both get correct geometry + state.
  - Smoke: `scripts/fbwl-smoke-apps-rules.sh` + `scripts/fbwl-smoke-apps-rules-xwayland.sh` (covers horz/vert remember on map).

- [x] Apps (“Remember”) `[IgnoreSizeHints] {yes|no}` parity (XWayland-only)
  - Implemented: per-view override that controls whether XWayland maximize applies WM_NORMAL_HINTS (min/max/base/inc).
    - Applied via apps remember pre-map (`fbwl_apps_remember_apply_pre_map()`).
    - Used in full-maximize (`fbwl_view_set_maximized()`): when `IgnoreSizeHints=yes`, skip size-hint snapping regardless of `session.screenN.maxIgnoreIncrement`.
  - Smoke: `scripts/fbwl-smoke-apps-ignore-size-hints.sh` (global `maxIgnoreIncrement=false` + two XWayland windows: one snaps to increments, one ignores via apps rule).

- [x] Apps (“Remember”) `[Tab] {yes|no}` parity (mapping to our tabs model)
  - Semantics: `Tab=no` means “this window is not eligible for tabbing” (Fluxbox/X11: “tabs enabled” toggle).
  - Implemented:
    - Per-view override `view->tabs_enabled_override_set/value` is applied on map *before* any autotab/apps-group attach can run.
    - `fbwl_tabs_attach()` refuses to attach if either the view or the anchor has tabs disabled, so this blocks apps-group + placement autotab + interactive attaches.
    - `Tab` is parsed/inherited/saved in the `apps` rules layer.
  - Smoke: `scripts/fbwl-smoke-apps-tab.sh` (proves `Tab=yes` still attaches and `Tab=no` prevents apps-group attach).

- [x] Keybinding ClientPattern completeness for `NextWindow` / `PrevWindow`
  - Implemented: cycle patterns now reuse the full Fluxbox-style ClientPattern surface area supported by the iconbar matcher:
    - `name`, `class/app_id`, `title`, `role`
    - `workspace` (incl. shorthand `(workspace)`), `workspacename`
    - `head` (incl. `head=[mouse]`), `layer`, `screen`
    - `minimized`, `maximized`, `maximizedhorizontal`, `maximizedvertical`, `fullscreen`, `shaded`, `stuck`, `transient`, `urgent`, `iconhidden`, `focushidden`
  - Detail: `FocusHidden` views are still excluded from cycling (Fluxbox/X11 focus list semantics), regardless of ClientPattern terms.
  - Smoke: `scripts/fbwl-smoke-nextwindow-clientpattern.sh` (layer/head/maximize-axis terms via `NextWindow` patterns).

---

## 26) Parity Audit — ClientPattern advanced semantics (Done)

Goal: finish Fluxbox/X11 ClientPattern semantics so keybindings, iconbar patterns, and apps rules behave like classic Fluxbox.

- [x] ClientPattern special value `[current]` parity (beyond `workspace=[current]`)
  - Implemented: `name|class|title|role|maximized*|fullscreen|shaded|stuck|transient|urgent|iconhidden|focushidden|head|layer|screen` accept `=[current]` to compare against the currently focused view’s value.
  - Implemented: shorthand `(Name)` / `(Class)` / `(Layer)` etc maps to `(...=[current])`, matching Fluxbox/X11 `ClientPattern` parsing rules.
  - Smoke: extended `scripts/fbwl-smoke-nextwindow-clientpattern.sh` to cover `class=[current]`, `(class)`, and `layer!=[current]`.

- [x] ClientPattern `@XPROP` parity (XWayland-only)
  - Implemented: parse `(@FOO=regex)` terms (and `!=`) for keybinding/iconbar patterns.
  - Implemented: match against X11 window properties via XCB on the XWayland connection (tries both text property and CARDINAL-as-string, mirroring Fluxbox/X11 behavior).
  - Implemented: `SetXProp` keybinding command for XWayland windows (sets `UTF8_STRING`).
  - Smoke: `scripts/fbwl-smoke-xwayland-xprop-clientpattern.sh` (covers SetXProp + `NextWindow (@FOO=...)` for both text + CARDINAL properties).

- [x] Apps rules `{N}` match-limit parity
  - Implemented: parse and persist `{N}` suffix on `[app] (...)` and enforce it when matching apps rules (Fluxbox/X11 `ClientPattern::m_matchlimit` semantics).
  - Implemented: track active match counts per rule and decrement on view destroy so slots free up when matching clients exit.
  - Smoke: `scripts/fbwl-smoke-apps-matchlimit.sh` (spawns 3 matching clients with `{2}`, asserts the third does not match, then closes one and asserts a new client matches).

- [x] Remaining keybinding command parity (selected high-impact)
  - Implemented: `ShowDesktop`, `ArrangeWindows*`, `Unclutter`, `GotoWindow`, `NextGroup/PrevGroup`, `Attach`.
    - `Attach`: uses Fluxbox-style ClientPattern matching to attach matching views to the most-recently-focused matching anchor.
      - Note: string ClientPattern terms are whole-string anchored regex (Fluxbox/X11 semantics), so substring matches require `.*`.
    - `GotoWindow {groups}` parity: counts active tab-groups as one slot, matching Fluxbox `FocusControl::goToWindowNumber`.
    - `NextGroup/PrevGroup` parity: cycle focus by group instead of by window.
    - `ShowDesktop` parity: minimize/restore windows on the current workspace; when restoring (unminimize) avoid re-focusing the restored view (Fluxbox/X11 `deiconify(false)` behavior).
  - Smoke: `scripts/fbwl-smoke-keybinding-parity-commands.sh`.

---

## 27) Parity Audit — Remaining Fluxbox command surface (Next)

Goal: close the remaining gaps between Fluxbox/X11’s command parser + command set and the Wayland backend, so existing `keys` and menu command usage works unchanged (or has explicit, documented limitations).

- [x] Keybinding command parity (core daily-use window ops)
  - Implemented: `Shade`, `ShadeOn`, `ShadeOff`, `ShadeWindow`; `Stick`, `StickOn`, `StickOff`, `StickWindow`.
  - Implemented: `SetAlpha`, `SetTitle`, `SetTitleDialog`, `ToggleDecor`, `SetDecor`.
  - Implemented: `Deiconify`, `CloseAllWindows` (+ `KillWindow` alias for `Kill`).
  - Smoke: extended `scripts/fbwl-smoke-keybinding-parity-commands.sh` to bind and assert logs/states for these commands (using only injector-supported key sequences).

- [x] Command language parity for keys/menu (classic Fluxbox parser)
  - Implemented: nested `{...}` parsing with `\}` escape handling for `MacroCmd`, `If/Cond`, `ForEach/Map`, `Delay`, `ToggleCmd`.
  - Implemented boolean terms: `Matches`, `Some`, `Every`, `Not`, `And`, `Or`, `Xor`.
  - Wiring: `fbwl_fluxbox_cmd_resolve()` recognizes these commands; `fbwl_keybindings_execute_action()` executes them via `src/wayland/fbwl_cmdlang*.c`.
  - Menu parsing: brace-delimited command strings now support nested braces so menu items can contain `MacroCmd`/`If`/`ForEach` without truncation.
  - Smoke: `scripts/fbwl-smoke-keybinding-cmdlang.sh`.
  - Parity notes:
    - `ToggleCmd` and `Delay` state is keyed by `(server/userdata, full args string)`; identical strings across bindings share state, unlike Fluxbox/X11’s per-command-instance state.

- [x] Head + marking commands parity
  - Heads: `SendToNextHead`, `SendToPrevHead`, `SetHead` (moves focused view between screens/outputs; preserves relative frame position and reapplies maximized/fullscreen geometry on the target head).
  - Marking: `MarkWindow`, `GotoMarkedWindow` (via `Arg` key placeholder; stores a keycode→window mapping and focuses/raises the marked window).
  - Smoke: `scripts/fbwl-smoke-keybinding-head-mark.sh`.
  - Parity notes:
    - Fluxbox/X11’s placeholder `Arg` is used in key-chains; fbwl currently supports `Arg` as a direct binding key to enable `MarkWindow`/`GotoMarkedWindow` without implementing key-chains.

- [x] Resource/style mutation commands parity
  - Implemented: `ReloadStyle`, `SetStyle`, `SaveRC`, `SetResourceValue`, `SetResourceValueDialog`.
  - Style-path parity: `session.styleFile` / `SetStyle` can now point at a *directory* (loads `theme.cfg` first, then `style.cfg`), matching Fluxbox/X11 `ThemeManager::load` behavior.
  - Save semantics: like Fluxbox/X11, `SetStyle`, `ReloadStyle`, and `SetResourceValue` trigger `SaveRC` after a successful apply.
  - Wayland note: `SetResourceValue` also runs a `Reconfigure` pass so the new value takes effect immediately (we don’t have a live Xrm resource manager like X11).
  - Smoke: `scripts/fbwl-smoke-resource-style-cmds.sh` (covers style directory load + ReloadStyle + SetStyle + SetResourceValue + SetResourceValueDialog + SaveRC).

- [x] Attention UI parity
  - Implemented: track XWayland urgency/demands-attention changes and trigger the existing attention UI (OSD + decor blink + iconbar highlight) when a background window becomes urgent.
  - Wiring: listen for XWayland `request_demands_attention` and `set_hints` events and translate into `fbwl_view_attention_request()` / `fbwl_view_attention_clear()`.
  - Smoke: `scripts/fbwl-smoke-xwayland-window-attention.sh` (spawns two XWayland clients, focuses one, then sets urgency on the other and asserts `Attention: start`).

- [x] ClientPattern regex quirk parity
  - Fixed: anchored string-term regex compilation uses `^PATTERN$` (no extra parens), matching Fluxbox/X11 `|` precedence.
  - Applies to: ClientPattern string terms (keybindings/iconbar) and `apps` match terms.
  - Smoke: `scripts/fbwl-smoke-clientpattern-regex-quirk.sh`.

---

## 28) Parity Audit — Newly Found X11/Fluxbox Gaps (Next)

Goal: capture any remaining Fluxbox/X11 behaviors that are missing in the Wayland backend (or need explicit Wayland-side “best effort” semantics), so “total parity” stays an explicit checklist.

Additional command parity targets from `fluxbox-keys(5)`:
- [x] Workspace commands parity (dynamic workspaces + naming)
  - Implemented: `AddWorkspace`, `RemoveLastWorkspace`.
  - Implemented: `SetWorkspaceName`, `SetWorkspaceNameDialog` (updates/persists `session.screen0.workspaceNames`).
  - Smoke: `scripts/fbwl-smoke-workspace-add-remove-name.sh`.
  - Parity notes:
    - Like Fluxbox/X11, workspace names are optional. We preserve the “numbers fallback” when `workspaceNames` is unset/empty.
    - Once naming is used, we keep the CSV index-stable by filling missing entries with `Workspace N` defaults.

- [x] Workspace navigation parity (offset args + non-wrapping variants)
  - [x] Implement offsets for: `NextWorkspace N`, `PrevWorkspace N`, `SendToNextWorkspace N`, `SendToPrevWorkspace N`, `TakeToNextWorkspace N`, `TakeToPrevWorkspace N` (smoke: `scripts/fbwl-smoke-workspace-nav-offset-toggle.sh`).
  - [x] Implement the `NextWorkspace 0` / `PrevWorkspace 0` toggle-to-previous-workspace semantics (smoke: `scripts/fbwl-smoke-workspace-nav-offset-toggle.sh`).
  - [x] Implement non-wrapping variants: `RightWorkspace N`, `LeftWorkspace N` (smoke: `scripts/fbwl-smoke-workspace-nav-offset-toggle.sh`).

- [x] Geometry command parity (non-interactive move/resize)
  - Implemented: `MoveTo`, `Move`, `MoveRight/Left/Up/Down`.
  - Implemented: `ResizeTo`, `Resize`, `ResizeHorizontal`, `ResizeVertical`.
  - Parity: Fluxbox/X11 `StrictMouseFocus` shifts focus even when the pointer is stationary (because Enter/Leave can fire on geometry/stacking changes); Wayland rechecks view-under-cursor after geometry-only commands and on async commit resizes.
  - Smoke: `scripts/fbwl-smoke-geometry-cmds.sh` (+ strict focus coverage in `scripts/fbwl-smoke-strict-mousefocus-geometry.sh`).

- [x] Focus command parity (directional + pattern forms)
  - Implemented directional focus: `FocusLeft` / `FocusRight` / `FocusUp` / `FocusDown` (Fluxbox/X11 `FocusControl::dirFocus` heuristic).
    - Tabs parity: directional focus operates at the tab-group/window level by targeting only the active tab per group (matching X11’s `Workspace::windowList()` being group-based).
    - Tie-break parity: when weights/exposure match, we pick the oldest window (stable creation-order), matching X11’s first-match wins.
  - [x] Implement pattern forms: `Activate [pattern]` / `Focus [pattern]` behave like `GotoWindow 1 [pattern]` (smoke: `scripts/fbwl-smoke-activate-focus-pattern.sh`).
  - Smoke: `scripts/fbwl-smoke-focus-directional.sh`.

- [x] MacroCmd target-window retargeting parity
  - Fixed: in Fluxbox/X11, `MacroCmd` executes commands against the *current* window as it changes (so `GotoWindow ...` followed by `MoveTo/ResizeTo` affects the newly focused window). fbwl now matches this when `MacroCmd` is invoked without an explicit target view.
    - Implementation detail: `MacroCmd` now preserves the original `target_view` pointer (NULL for top-level keybindings), so each nested command resolves the target at execution time instead of “locking” to the macro-entry view.
  - Smoke: `scripts/fbwl-smoke-macrocmd-retarget.sh`.

- [x] Tabs command parity (mouse-only + group ops)
  - Implemented mouse-only: `StartTabbing`, `ActivateTab`.
  - Implemented group ops: `DetachClient`, `MoveTabLeft`, `MoveTabRight`.
  - Parity: tab-attach by drag is only enabled for drags started via `StartTabbing` (prevents accidental attaches during normal move/resize drags).
  - Smoke: `scripts/fbwl-smoke-tabs-commands.sh`, `scripts/fbwl-smoke-tabs-attach-area.sh`.

- [x] Menu command parity (custom + filtered client menu)
  - Implemented: `CustomMenu <path>`.
  - Implemented: `ClientMenu [pattern]` (pattern-filtered list).
  - Smoke: `scripts/fbwl-smoke-custommenu-clientmenu-pattern.sh`, `scripts/fbwl-smoke-clientmenu-usepixmap.sh`.

- [x] Exec environment command parity
  - Implemented: `SetEnv name value` / `Export name=value` (affects future `ExecCommand`).
  - Smoke: `scripts/fbwl-smoke-setenv-export.sh`.

- [x] Layer/UI toggle command parity
  - [x] Implement offsets for: `RaiseLayer [offset]` / `LowerLayer [offset]` (covered by `scripts/fbwl-smoke-keybinding-commands.sh`).
  - [x] Implement: `ToggleToolbarHidden` / `ToggleToolbarAbove`.
  - [x] Implement: `ToggleSlitHidden` / `ToggleSlitAbove`.
  - Parity: `Toggle{Toolbar,Slit}Hidden` affects hidden-offset even when `autoHide=false` (matches Fluxbox/X11’s `toggleHidden()` behavior).
  - Smoke: `scripts/fbwl-smoke-layer-ui-toggle-cmds.sh`.

- [x] `Restart [path]` command parity (Wayland semantics)
  - Implemented: `Restart [cmd...]` resolves from keys/menu and re-execs the compositor after a clean shutdown.
    - With args: `exec $SHELL -c "<cmd...>"` (Fluxbox/X11 style).
    - Without args: re-`execvp(argv[0], argv)` best-effort.
  - IPC: `fbwl-remote restart [cmd...]` returns `ok restarting` then triggers the same restart path.
  - Smoke: `scripts/fbwl-smoke-restart.sh`.

- [x] Keys file “chaining” parity
  - Implemented: Emacs-style multi-step chains in classic `keys` syntax (e.g. `Mod1 F1 Mod1 F2 :ExecCommand ...`).
  - Runtime: `<Escape>` abort restores the pre-chain mode; non-matching keys abort and are forwarded; timeout restores after 5s.
  - Smoke: `scripts/fbwl-smoke-keys-chaining.sh`.

- [x] Keys file `ChangeWorkspace` special-event parity
  - Implemented: `ChangeWorkspace :<command...>` pseudo-event triggers after workspace switches.
  - Guardrail: recursion guard prevents obvious self-trigger loops (as warned in `fluxbox-keys(5)`).
  - Smoke: `scripts/fbwl-smoke-changeworkspace-event.sh`.

- [x] Mouse binding syntax parity (`fluxbox-keys(5)`)
  - Contexts: added `OnSlit`, `OnTab` (and `OnTitlebar` matches `OnTab` for existing Fluxbox configs).
  - Keys:
    - `MouseN`: press semantics (supports `Double`).
    - `ClickN`: fires on release if the pointer stayed within the click threshold.
    - `MoveN`: fires on motion once the click threshold is exceeded (and if it starts a grab, the grab uses the original press point so deltas match Fluxbox/X11).
  - Smokes:
    - `scripts/fbwl-smoke-mousebindings-click-move.sh` (ClickN vs MoveN threshold + mutual exclusion).
    - `scripts/fbwl-smoke-mousebindings-ontab.sh` (OnTab context).
    - `scripts/fbwl-smoke-slit-menu.sh` (OnSlit context).
    - Existing Move-context coverage remains in `scripts/fbwl-smoke-grips.sh` + `scripts/fbwl-smoke-ignore-border.sh`.

---

## 29) Parity Checkpoint — 2026-02-09

- [x] Fixed: `StrictMouseFocus` geometry recheck could no-op incorrectly when seat focus was stale (e.g. headless tests where virtual keyboards come/go), blocking focus flips after `MoveTo`/unmaximize without pointer motion.
  - Fix: `focus_view()` only short-circuits when both the seat focus and compositor policy focus already match the target (`src/wayland/fbwl_server_policy.c`).
  - Regression guard: `scripts/fbwl-smoke-strict-mousefocus-geometry.sh`.
- [x] Verified: `scripts/fbwl-smoke-ci.sh` passes end-to-end (ran=133, skipped=0).

---

## 30) Parity Feature — Pseudo Transparency on Wayland

Implemented: Fluxbox/X11 `session.forcePseudoTransparency` semantics on Wayland: translucent menus/toolbars/slit/windows blend against the desktop background (wallpaper / background color) rather than the live scene (other windows), matching X11 “pseudo transparency”.

Smoke: `scripts/fbwl-smoke-pseudo-transparency.sh`.
