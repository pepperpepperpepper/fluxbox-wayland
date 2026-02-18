# Wayland monolithic-file taxonomy + refactor plan (physical LOC)

Updated: 2026-02-18

This section is **Wayland implementation only** (Fluxbox Wayland compositor + its Wayland tooling).
It is intentionally **not** a “do this refactor now” doc; it’s a **taxonomy + mechanical extraction
plan** to use when/if files cross a hard size threshold.

Note: `src/wayland/**` is hard-capped at **< 1000 physical LOC** by `scripts/fbwl-check-wayland-loc.sh`
(excluding `src/wayland/protocol/**`).

## Scope (Wayland implementation only)
Included:
- compositor/server: `src/wayland/**` (excluding `src/wayland/protocol/**`, which is generated)
- shared WM core used by the Wayland server: `src/wmcore/**`
- Wayland utilities / protocol test clients: `util/fbwl-*.c`

Excluded:
- X11-only code (`src/*.cc`, `util/fbx11-*.c`, etc.)
- generated protocol stubs (`src/wayland/protocol/**`)
- build outputs (`*.o`, `build-*`, binaries at repo root)

## Measurement
This section uses **physical LOC** (roughly `wc -l`). It’s intentionally simple and “obvious”.
Repo-wide, we already have a stricter **code-SLOC** guardrail (see the older plan below).

To reproduce the counts used here (prints top files + max; adjust thresholds as desired):
```sh
python3 - <<'PY'
from pathlib import Path

paths = []
paths += list(Path('src/wayland').glob('*.c'))
paths += list(Path('src/wayland').glob('*.h'))
paths += list(Path('src/wmcore').glob('*.c'))
paths += list(Path('src/wmcore').glob('*.h'))
paths += list(Path('util').glob('fbwl-*.c'))

def loc(path: Path) -> int:
    data = path.read_bytes()
    n = data.count(b'\\n')
    if data and not data.endswith(b'\\n'):
        n += 1
    return n

counts = [(loc(p), p.as_posix()) for p in paths if p.is_file() and 'src/wayland/protocol' not in p.as_posix()]
counts.sort(reverse=True)

print('Top 40 Wayland-scope files by physical LOC:')
for n, p in counts[:40]:
    print(f'{n:5d}  {p}')
print('\\nMax LOC:', counts[0][0] if counts else 0)
print('Count > 1400:', sum(1 for n,_ in counts if n > 1400))
PY
```

## Status (as of 2026-02-18)
- **Hard threshold (>1400 LOC):** none
- **Max LOC (in scope):** `util/fbwl-input-injector.c` (1242 LOC)
- **Watchlist (>=900 LOC):** these are the files most likely to become the next monoliths

| LOC | File |
| ---: | --- |
| 1242 | `util/fbwl-input-injector.c` |
| 1080 | `util/fbwl-dnd-client.c` |
| 999 | `src/wayland/fbwl_server_ui.c` |
| 999 | `src/wayland/fbwl_fluxbox_cmd.c` |
| 998 | `src/wayland/fbwl_server_key_mode.c` |
| 997 | `src/wayland/fbwl_view.c` |
| 996 | `src/wayland/fbwl_server_keybinding_actions.c` |
| 993 | `src/wayland/fbwl_server_policy.c` |
| 982 | `src/wayland/fbwl_style_parse.c` |
| 982 | `src/wayland/fbwl_server_policy_input.c` |
| 982 | `src/wayland/fbwl_server_bootstrap.c` |
| 978 | `src/wayland/fbwl_ui_toolbar.c` |
| 977 | `src/wayland/fbwl_ui_toolbar_iconbar_pattern.c` |
| 969 | `src/wayland/fbwl_server_menu.c` |
| 969 | `src/wayland/fbwl_server_config.c` |
| 964 | `src/wayland/fbwl_server_menu_actions.c` |
| 956 | `src/wayland/fbwl_keybindings_execute.c` |
| 940 | `src/wayland/fbwl_server_keybinding_actions_windows.c` |
| 938 | `util/fbwl-clipboard-client.c` |
| 925 | `src/wmcore/fbwm_core.c` |
| 913 | `src/wayland/fbwl_ui_slit.c` |

## Taxonomy (current “large/monolithic” candidates, >=600 LOC)
The goal here is to make it obvious which files are “doing too much” so splitting can be targeted.

### Server lifecycle & configuration
- `src/wayland/fbwl_server_bootstrap.c` (982) — wlroots init + protocol managers + subsystem wiring
- `src/wayland/fbwl_server_config.c` (969) — resource/defaults loading + config parsing + apply-to-server glue
- `src/wayland/fbwl_server_reconfigure.c` (667) — live reconfigure pipeline
- `src/wayland/fbwl_server_outputs.c` (773) — output add/remove + output layout + per-output glue
- `src/wayland/fbwl_screen_config.c` (637) — per-screen/head configuration parsing + defaults
- `src/wayland/fbwl_server_internal.h` (643) — “everything server” internal types; tends to grow as a dumping ground

### Policy / focus / workspace / grabs
- `src/wayland/fbwl_server_policy.c` (993) — focus policy + raise/lower + snap/opaque move/resize + apps-rule policy hooks
- `src/wayland/fbwl_server_policy_input.c` (982) — pointer/keyboard input policy glue (when events map to policy actions)
- `src/wmcore/fbwm_core.c` (925) — workspace + focus + stacking model used by the Wayland compositor

### Views & decoration (window model)
- `src/wayland/fbwl_view.c` (997) — view lifecycle + xdg/xwayland glue + geometry/state helpers
- `src/wayland/fbwl_view_decor.c` (755) — decorations layout + hit-testing + scene-tree glue for frame/titlebar
- `src/wayland/fbwl_server_xdg_xwayland.c` (749) — creation/teardown glue between XDG/XWayland view backends
- `src/wayland/fbwl_xwayland.c` (773) — XWayland integration details (ICCCM/EWMH-ish, mapping, quirks)

### Input & keybindings
- `src/wayland/fbwl_keybindings_execute.c` (956) — execute/bind key actions (shelling out + internal command dispatch)
- `src/wayland/fbwl_server_key_mode.c` (998) — key mode state machine (multi-key sequences, mode switching)
- `src/wayland/fbwl_server_keybinding_actions.c` (996) — core action dispatch + action registry (already partially split)
- `src/wayland/fbwl_server_keybinding_actions_windows.c` (940) — window-focused action implementations
- `src/wayland/fbwl_server_keybinding_actions_resources_style.c` (801) — style/resource-related actions
- `src/wayland/fbwl_server_keybinding_actions_heads_mark.c` (695) — head movement + mark/goto-marked actions

### UI (root menu / toolbar / slit)
- `src/wayland/fbwl_server_ui.c` (999) — UI glue + workspace visibility + focus repair + “strict mouse focus” checks
- `src/wayland/fbwl_server_menu.c` (969) — menu state + open/close + input routing
- `src/wayland/fbwl_server_menu_actions.c` (964) — menu-driven actions (root menu + window menu glue)
- `src/wayland/fbwl_ui_menu.c` (895) — menu rendering + input handling + menu model glue
- `src/wayland/fbwl_menu_parse.c` (733) — menu file parser (fluxbox menu syntax)
- `src/wayland/fbwl_ui_toolbar.c` (978) — toolbar model + layout + rebuild pipeline
- `src/wayland/fbwl_ui_toolbar_iconbar_pattern.c` (977) — iconbar pattern parsing + matching + update glue
- `src/wayland/fbwl_ui_slit.c` (913) — slit layout + input + reconfigure glue

### Commands / parsing / rules
- `src/wayland/fbwl_fluxbox_cmd.c` (999) — Fluxbox command surface for Wayland (maps commands to policy/UI actions)
- `src/wayland/fbwl_cmdlang.c` (723) — command language parsing + evaluation
- `src/wayland/fbwl_style_parse.c` (982) — style/theme parsing (Wayland-side)
- `src/wayland/fbwl_apps_rules_load.c` (608) — apps rules parsing + normalization
- `src/wayland/fbwl_util.c` (845) — “kitchen sink” helpers (string, IO, small algorithms); high churn risk

### Tray / SNI
- `src/wayland/fbwl_sni_item_requests.c` (838) — SNI item request/response plumbing + state machine glue

### Wayland utilities / protocol test clients
These tend to be monolithic because they mix:
CLI parsing + Wayland protocol binding + event-loop glue + “demo logic”.
- `util/fbwl-input-injector.c` (1242) — virtual keyboard + virtual pointer injector
- `util/fbwl-dnd-client.c` (1080) — drag-and-drop test client
- `util/fbwl-clipboard-client.c` (938) — clipboard test client
- `util/fbwl-primary-selection-client.c` (871) — primary selection test client
- `util/fbwl-data-control-client.c` (852) — data-control test client
- `util/fbwl-xdp-portal-client.c` (849) — xdg-desktop-portal interaction test client
- `util/fbwl-sni-item-client.c` (806) — SNI test item client
- `util/fbwl-xdg-activation-client.c` (697) — xdg-activation test client
- `util/fbwl-relptr-client.c` (676) — relative-pointer/pointer-constraints test client

## Refactor plan (trigger: >1400 LOC)
There are currently **no** Wayland-scope files above 1400 LOC, so nothing is mandatory under this plan.
If/when a file crosses 1400 LOC:

### Principles (mechanical extraction first)
- Prefer **extracting whole functions** into new translation units, keeping signatures stable.
- Keep public headers stable; use small `*_internal.h` headers only when multiple `*.c` need shared private types.
- Avoid behavior changes while splitting; do follow-up cleanups after the code is split and tested.
- Keep generated protocol code untouched.

### Checklist
1. Identify 2–4 “seams” (clusters of functions that share data but not everything).
2. Add new `*.c` files with a narrow purpose; move functions verbatim.
3. Introduce minimal private headers only if needed (avoid widening public headers).
4. Update build lists (Wayland server sources are listed in `src/Makemodule.am` under `fluxbox_wayland_SOURCES`).
5. Build + smoke:
   - `make -j\"$(nproc)\"`
   - `scripts/fbwl-smoke-xvfb.sh` (or your preferred subset)

### Preemptive “seams” for the watchlist (>=900 LOC)
Not required yet, but these are the most obvious extraction boundaries if any of these files grows:

- `src/wayland/fbwl_server_policy.c`
  - split into `fbwl_server_policy_focus.c` (focus/raise/activate), `fbwl_server_policy_grab.c` (move/resize/edge snap),
    `fbwl_server_policy_rules.c` (apps-rule policy hooks / layer/workspace jumps).
- `src/wayland/fbwl_server_ui.c`
  - split into `fbwl_server_workspaces.c` (apply visibility + workspace switching), `fbwl_server_focus_ui.c`
    (focus repair + strict mousefocus recheck), `fbwl_server_ui_glue.c` (UI init + rebuild orchestration).
- `src/wayland/fbwl_view.c`
  - split into `fbwl_view_lifecycle.c` (create/destroy/map/unmap), `fbwl_view_state.c` (geometry/state helpers),
    `fbwl_view_xdg.c` / `fbwl_view_xwayland.c` (type-specific glue) if the type branches keep growing.
- `src/wayland/fbwl_menu_parse.c`
  - split into `fbwl_menu_lex.c` (tokenization), `fbwl_menu_parse_rules.c` (grammar), `fbwl_menu_parse_build.c`
    (construct menu model + post-processing).
- `src/wayland/fbwl_fluxbox_cmd.c`
  - split by command domain: `fbwl_fluxbox_cmd_windows.c`, `fbwl_fluxbox_cmd_workspaces.c`, `fbwl_fluxbox_cmd_ui.c`,
    keeping a thin `fbwl_fluxbox_cmd.c` as the registry/dispatcher.
- `src/wayland/fbwl_server_keybinding_actions.c`
  - keep as registry/dispatcher; extract more action groups:
    `fbwl_server_keybinding_actions_focus.c`, `fbwl_server_keybinding_actions_workspaces.c`,
    `fbwl_server_keybinding_actions_ui.c` (menus/toolbars/slit).
- `src/wayland/fbwl_keybindings_execute.c`
  - split into `fbwl_keybindings_execute_spawn.c` (exec/fork/argv handling), `fbwl_keybindings_execute_cmd.c`
    (Fluxbox command/cmdlang evaluation), `fbwl_keybindings_execute_dispatch.c` (glue from key event → action call).
- `src/wayland/fbwl_server_config.c`
  - split into `fbwl_server_config_load.c` (file IO + parsing), `fbwl_server_config_defaults.c` (defaults/resources),
    `fbwl_server_config_apply.c` (apply-to-server + wiring with reconfigure).
- `src/wayland/fbwl_view_decor.c`
  - split into `fbwl_view_decor_layout.c` (geometry/layout), `fbwl_view_decor_input.c` (hit-testing + pointer actions),
    `fbwl_view_decor_scene.c` (scene-tree node creation + updates).
- `src/wmcore/fbwm_core.c`
  - split into `fbwm_core_workspaces.c`, `fbwm_core_focus.c`, `fbwm_core_stack.c` if it starts accreting policy glue.

---

# Monolith refactor plan (>1100 code lines, code-only)

Note: this file is currently ignored by git (see `.gitignore`). If you want it tracked, remove
`/refactor_plan.md` from `.gitignore`.

## Goal
Any **human-written** code file that exceeds **1100 “code lines”** should be split into **~3 cohesive
translation units** (usually `*.cc`), keeping behavior unchanged.

This is intentionally a **mechanical extraction plan** first; follow-up cleanups can happen after the
code is split and tests stay green.

Non-goals:
- Refactoring generated code (e.g. `src/wayland/protocol/**`).
- Large behavior changes while splitting.
- Reformatting everything “for style”.

## What counts as “code lines” here
We’re using a rough “SLOC-like” metric:
- ignore blank lines
- ignore comment-only lines (`//` and `/* … */`)
- count a line if it still contains any token after comment stripping

This is approximate, but stable enough to decide which files are clearly monoliths.

## Current monoliths (as of 2026-01-26)
Measured by `scripts/fbwl-check-code-sloc.py` using the “code lines” metric above, the files over
1100 are:
- none (guardrail passes)

Previously (before splitting):
- `src/Window.cc` (2924 code lines)
- `src/Screen.cc` (1326 code lines)
- `src/Remember.cc` (1232 code lines)
- `src/FbWinFrame.cc` (1178 code lines)

Near-misses (not required by this plan yet):
- `src/Slit.cc` (~1017)
- `src/FbTk/Menu.cc` (~1003)

To re-measure (prints offenders + counts; exits non-zero):
- `python3 scripts/fbwl-check-code-sloc.py --max 1100 --allowlist /dev/null`

## Status (as of 2026-01-26)
- Monolith splits completed for `src/FbWinFrame.cc`, `src/Remember.cc`, `src/Screen.cc`, and `src/Window.cc`.
- Verified green:
  - `make -j"$(nproc)"`
  - `scripts/fbwl-check-code-sloc.sh`
  - `scripts/fbwl-smoke-xvfb.sh`

## Guardrail (implemented)
We have a CI/automation gate that fails if any non-generated `*.c|*.cc|*.h|*.hh` exceeds 1100
code lines, excluding `src/wayland/protocol/**`:
- wrapper: `scripts/fbwl-check-code-sloc.sh`
- implementation: `scripts/fbwl-check-code-sloc.py`
- temporary allowlist (to keep CI green while we refactor): `scripts/fbwl-check-code-sloc-allowlist.txt`

This prevents the codebase from re-creating monoliths after we split these.

Workflow:
- Keep the allowlist minimal and temporary.
- After each monolith is split below the limit, remove it from the allowlist.
- When no files exceed the limit, the allowlist should be empty.

## Refactor principles
- **Extract without redesign**: move blocks verbatim first; clean APIs after.
- **Keep headers stable** where possible: move method bodies, not public interfaces.
- **Keep internal helpers internal**: prefer `namespace {}` in the new file; if shared, create a
  small `*_internal.hh` for inline helpers (avoid new `*.cc` just for helpers unless necessary).
- **One monolith at a time**: keep diffs reviewable and smoke tests green.

## Mechanical extraction checklist (per monolith)
1. Create the new `*.cc` file(s) and move method bodies (prefer moving whole method definitions).
2. If multiple `*.cc` need access to a private type, move that type into a private header
   (e.g. `*_internal.hh`) rather than duplicating it.
3. Update the build system source lists:
   - X11 fluxbox: `src/Makemodule.am` (`fluxbox_SOURCES`, `REMEMBER_SOURCE`, etc.)
   - other targets if applicable (Wayland-only builds may not compile these files).
4. `make -j"$(nproc)"` and run the smoke subset you normally use.
5. Remove the file from `scripts/fbwl-check-code-sloc-allowlist.txt` once it’s under the limit.

## Split proposals (target: ~3 files each)

### 1) `src/Window.cc` → 3 files
Goal: isolate X event plumbing and interactive move/resize away from window state & client mgmt.

Proposed layout:
- `src/Window.cc` (keep name; becomes “core”)
  - ctor/dtor, `init()`, client attach/detach, state toggles (iconify/maximize/fullscreen/shade/stick),
    focus, layering, decorations, property helpers, `setupWindow()`, `updateButtons()`, etc.
- `src/WindowEvents.cc`
  - `handleEvent()` and the `*Event(...)` handlers:
    `mapRequestEvent`, `mapNotifyEvent`, `unmapNotifyEvent`, `destroyNotifyEvent`,
    `propertyNotifyEvent`, `exposeEvent`, `configureRequestEvent`,
    `keyPressEvent`, `buttonPressEvent`, `buttonReleaseEvent`,
    `motionNotifyEvent`, `enterNotifyEvent`, `leaveNotifyEvent`.
  - move the local enter/leave “queue scanner” helper (`scanargs` + `queueScanner`) here.
- `src/WindowMoveResize.cc`
  - interactive move/resize and pointer-grab logic:
    `startMoving/stopMoving`, `doSnapping`, `getResizeDirection`,
    `startResizing/stopResizing`, `grabPointer/ungrabPointer`,
    `startTabbing/attachTo`, coordinate translation helpers.

Implementation note (current split):
- `src/Window.cc` now focuses on construction/init and client management.
- `src/WindowEvents.cc` also hosts `setState/getState`, decoration helpers, and `setupWindow()/updateButtons()`.
- `src/WindowMoveResize.cc` also hosts most window “action” methods (focus/iconify/fullscreen/maximize/shade/stick/layering).

Note:
- `getRootTransientFor()` is currently a file-local helper used by both state code and event code.
  Prefer a tiny `src/Window_internal.hh` with an `inline` helper for this rather than duplicating it.

### 2) `src/Screen.cc` → 3 files
Goal: separate workspace policy and “geometry/heads/struts” from core screen lifecycle.

Proposed layout:
- `src/Screen.cc` (keep name; becomes “core”)
  - ctor/dtor, `initWindows()`, `createWindow(...)`, remove window/client, event handlers
    (`propertyNotify`, `keyPressEvent`, `keyReleaseEvent`, `buttonPressEvent`), focus cycling,
    `reconfigure()` / `reconfigureTabs()`, menu setup (leave here unless it forces a 4th file).
- `src/ScreenWorkspaces.cc`
  - workspace list & naming:
    `currentWorkspaceID`, `addWorkspace`, `removeLastWorkspace`, `changeWorkspaceID`,
    `sendToWorkspace`, `updateWorkspaceName`, `removeWorkspaceNames`, `addWorkspaceName`,
    `getNameOfWorkspace`, workspace navigation (`next/prev/left/rightWorkspace`),
    plus window/workspace reassociation helpers.
- `src/ScreenGeometry.cc`
  - heads + struts + usable area:
    `initXinerama/clearXinerama/clearHeads`, `getHead*`, `clampToHead`,
    `requestStrut/clearStrut`, `reconfigureStruts`, `updateAvailableWorkspaceArea`,
    `availableWorkspaceArea`, `maxLeft/maxRight/maxTop/maxBottom`.

### 3) `src/Remember.cc` → 3 files (+ 1 private header)
Goal: separate (a) menu/UI, (b) file I/O + parsing, (c) runtime application of remembered state.

Proposed layout:
- `src/Remember.cc` (keep name; becomes “core”)
  - `Remember` singleton lifecycle and runtime integration:
    `find/add`, `isRemembered/rememberAttrib/forgetAttrib`,
    `setupFrame/setupClient`, `findGroup`, `updateDecoStateFromClient`,
    `updateClientClose`, `initForScreen`, `createMenu` (thin wrapper).
- `src/RememberIO.cc`
  - `Remember::reload()` and `Remember::save()` plus file parsing helpers (`parseApp`, group merging,
    startup entry persistence).
- `src/RememberMenu.cc`
  - remember-menu UI:
    `RememberMenuItem`, `createRememberMenu(BScreen&)`, and related menu-only helpers.

Private header:
- `src/RememberApp.hh` (or `src/Remember_internal.hh`)
  - move the `Application` class definition out of `Remember.cc` so all three `*.cc` can access it
    without duplicating code or exposing it publicly via `Remember.hh`.

### 4) `src/FbWinFrame.cc` → 3 files
Goal: separate “core geometry/state” from rendering/theming and X event dispatch.

Proposed layout:
- `src/FbWinFrame.cc` (keep name; becomes “core”)
  - ctor/dtor, move/resize functions, state application, layout offsets, size hints helpers,
    `applyDecorations`, border width/gravity translation.
- `src/FbWinFrameRender.cc`
  - reconfigure + render/apply pipeline:
    `reconfigure`, `redrawTitlebar`, `reconfigureTitlebar`, `renderAll/applyAll`,
    `renderTitlebar/applyTitlebar`, `renderHandles/applyHandles`,
    `renderButtons/applyButtons`, `renderTabContainer`, `applyTabContainer`, `applyButton`, etc.
  - move file-local rendering helpers (`render(...)`, `bg_pm_or_color(...)`) here.
- `src/FbWinFrameEvents.cc`
  - event handling and hit-testing:
    `setEventHandler/removeEventHandler`, `exposeEvent`, `handleEvent`,
    `configureNotifyEvent`, `insideTitlebar`, `getContext`.

## Execution order (low risk → high risk)
1. `FbWinFrame.cc` (mostly internal UI; relatively isolated)
2. `Remember.cc` (I/O and UI split; lots of logic but little X event coupling)
3. `Screen.cc` (touches global behavior; keep steps small)
4. `Window.cc` (largest and most risk; do last once guardrails are in)

After each extraction step:
- build: `make -j"$(nproc)"`
- run the relevant smoke subset (at minimum `scripts/fbwl-smoke-xvfb.sh` and any X11-focused tests you use)

## Appendix: historical Wayland monolith plan (obsolete)
The remainder of this file used to describe splitting `src/wayland/fluxbox_wayland.c` from ~10k LOC.
That work is effectively complete: `src/wayland/fluxbox_wayland.c` is now a thin entrypoint (~169 code lines).
