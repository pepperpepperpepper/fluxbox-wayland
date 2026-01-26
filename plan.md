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
    - Supported commands: `ExecCommand`, `Exit`, `NextWindow`, `Maximize`, `Fullscreen`, `Minimize`/`Iconify`, `Workspace N`, `SendToWorkspace N`, `TakeToWorkspace N`
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
  - Scripted check: `scripts/fbwl-smoke-toolbar.sh` (clicks workspace 2 in the internal toolbar and asserts `Toolbar: click workspace=2` + `Workspace: apply current=2 reason=toolbar` logs).
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
make -j"$(nproc)" fluxbox-wayland fluxbox-remote fbwl-smoke-client fbwl-clipboard-client fbwl-cursor-shape-client fbwl-data-control-client fbwl-dnd-client fbwl-presentation-time-client fbwl-primary-selection-client fbwl-relptr-client fbwl-screencopy-client fbwl-export-dmabuf-client fbwl-output-management-client fbwl-output-power-client fbwl-xdg-output-client fbwl-viewporter-client fbwl-fractional-scale-client fbwl-text-input-client fbwl-input-method-client fbwl-xdg-activation-client fbwl-xdg-decoration-client fbwl-idle-client fbwl-session-lock-client fbwl-shortcuts-inhibit-client fbwl-single-pixel-buffer-client fbwl-remote fbwl-input-injector fbwl-foreign-toplevel-client fbwl-layer-shell-client fbx11-smoke-client fbwl-sni-item-client fbwl-xdp-portal-client util/startfluxbox-wayland
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
make -j"$(nproc)" fluxbox-wayland fluxbox-remote fbwl-smoke-client fbwl-clipboard-client fbwl-cursor-shape-client fbwl-data-control-client fbwl-dnd-client fbwl-presentation-time-client fbwl-primary-selection-client fbwl-relptr-client fbwl-screencopy-client fbwl-export-dmabuf-client fbwl-output-management-client fbwl-output-power-client fbwl-xdg-output-client fbwl-viewporter-client fbwl-fractional-scale-client fbwl-text-input-client fbwl-input-method-client fbwl-xdg-activation-client fbwl-xdg-decoration-client fbwl-idle-client fbwl-session-lock-client fbwl-shortcuts-inhibit-client fbwl-single-pixel-buffer-client fbwl-remote fbwl-input-injector fbwl-foreign-toplevel-client fbwl-layer-shell-client fbx11-smoke-client fbwl-sni-item-client fbwl-xdp-portal-client util/startfluxbox-wayland

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
- [x] Published report (latest, uses `weston-terminal`): https://tmp.uh-oh.wtf/fluxbox-wayland/smoke-report/94b1f2b5/20260125-203918/index.html
- [x] Published report (older example): https://tmp.uh-oh.wtf/fluxbox-wayland/smoke-report/d6b6fc11/20260125-092055/index.html

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
- [ ] Apply the full resource set to Wayland compositor behavior:
  - [x] Focus/raise/click behavior:
    - `session.screen0.focusModel` (ClickToFocus/MouseFocus/StrictMouseFocus; StrictMouseFocus currently behaves like MouseFocus).
    - `session.screen0.autoRaise` + `session.autoRaiseDelay` (auto-raise with delay on mouse-focus).
    - `session.screen0.clickRaises` (raise on click even when focus doesn't change; decor-only when disabled).
    - `session.screen0.focusNewWindows` (controls focusing newly-mapped views).
  - [ ] Placement policy, workspace names/behavior, toolbar/menu behavior, tab-related policy, etc.:
    - [x] Workspace names: `session.screen0.workspaceNames` (drives toolbar workspace labels + workspace OSD; numbers remain the fallback).
    - [ ] Window placement strategy: `session.screen0.windowPlacement` + row/col direction resources.
    - [ ] Toolbar behavior: `session.screen0.toolbar.*` (visible/placement/autoHide/autoRaise/width/height/tools).
    - [ ] Menu behavior: `session.screen0.menuDelay` (+ other menu resources as needed).
    - [ ] Tab-related policy: `session.screen0.tabs.*` (where applicable for Wayland UI).
  - Output mapping strategy for `screenN` resources (define how multiple Wayland outputs map to classic `screen0` semantics).
- [ ] Extend `keys` support to full classic syntax and command set:
  - Include mouse bindings and the commonly used commands beyond the current subset.
  - Add smoke coverage for representative bindings and “reload config” behavior.
- [ ] Extend `apps` rules toward full “Remember” parity:
  - Expand supported settings and matching (Wayland app_id/title, XWayland WM_CLASS/WM_NAME, groups where applicable).
  - Add tests that assert multiple attributes apply deterministically.
- [ ] Extend menu/style parity:
  - Menu: support the common Fluxbox menu tags used in real configs (includes, dynamic submenus, etc.).
  - Style/overlay: expand theme parsing until Fluxbox-like UI can be driven by real styles.
- [ ] Add introspection + reload:
  - Provide an IPC/CLI hook to dump effective config and a `reconfigure` command to reload `init`/keys/apps/menu/style at runtime.
  - Add smoke tests that change config on disk and assert the compositor reacts (no restart required).
