# Wayland “monolith” list (by LOC)

Updated: 2026-02-17

## Scope
- `src/wayland/**/*.c` + `src/wayland/**/*.h`
- Excluding `src/wayland/protocol/**` (generated)

## Measurement
Physical LOC via `wc -l` (intentionally simple).

Reproduce (sorted descending):
```sh
rg --files src/wayland -g'*.c' -g'*.h' \
  | rg -v '^src/wayland/protocol/' \
  | xargs -r wc -l \
  | awk '$2 != "total" {print}' \
  | sort -nr
```

## Summary (excluding protocol)
- Files: 208
- Total LOC: 56,569
- Max LOC: 999 (`src/wayland/fbwl_server_ui.c`)
- Count >=900 LOC: 17
- Count >=800 LOC: 23
- Count >=700 LOC: 28
- Count >=600 LOC: 36

Note: We intentionally enforce `src/wayland/*.c|*.h < 1000 LOC` (see `scripts/fbwl-check-wayland-loc.sh`), so the “monoliths” in this repo are effectively the files clustered around ~900–999 LOC.

## Largest Wayland files (>=600 LOC)

| LOC | File |
| ---: | --- |
| 999 | `src/wayland/fbwl_server_ui.c` |
| 998 | `src/wayland/fbwl_server_key_mode.c` |
| 998 | `src/wayland/fbwl_fluxbox_cmd.c` |
| 996 | `src/wayland/fbwl_server_keybinding_actions.c` |
| 995 | `src/wayland/fbwl_view.c` |
| 993 | `src/wayland/fbwl_server_policy.c` |
| 982 | `src/wayland/fbwl_style_parse.c` |
| 982 | `src/wayland/fbwl_server_policy_input.c` |
| 978 | `src/wayland/fbwl_ui_toolbar.c` |
| 977 | `src/wayland/fbwl_ui_toolbar_iconbar_pattern.c` |
| 969 | `src/wayland/fbwl_server_menu.c` |
| 964 | `src/wayland/fbwl_server_menu_actions.c` |
| 962 | `src/wayland/fbwl_server_config.c` |
| 950 | `src/wayland/fbwl_keybindings_execute.c` |
| 940 | `src/wayland/fbwl_server_keybinding_actions_windows.c` |
| 924 | `src/wayland/fbwl_server_bootstrap.c` |
| 913 | `src/wayland/fbwl_ui_slit.c` |
| 899 | `src/wayland/fbwl_style_parse_textures.c` |
| 895 | `src/wayland/fbwl_ui_menu.c` |
| 845 | `src/wayland/fbwl_util.c` |
| 838 | `src/wayland/fbwl_sni_item_requests.c` |
| 814 | `src/wayland/fbwl_texture.c` |
| 809 | `src/wayland/fbwl_server_keybinding_actions_resources_style.c` |
| 773 | `src/wayland/fbwl_server_outputs.c` |
| 755 | `src/wayland/fbwl_view_decor.c` |
| 742 | `src/wayland/fbwl_server_xdg_xwayland.c` |
| 733 | `src/wayland/fbwl_menu_parse.c` |
| 723 | `src/wayland/fbwl_cmdlang.c` |
| 677 | `src/wayland/fbwl_xwayland.c` |
| 664 | `src/wayland/fbwl_texture_render.c` |
| 659 | `src/wayland/fbwl_server_reconfigure.c` |
| 646 | `src/wayland/fbwl_keys_parse.c` |
| 641 | `src/wayland/fbwl_style_parse_toolbar_slit.c` |
| 637 | `src/wayland/fbwl_screen_config.c` |
| 636 | `src/wayland/fbwl_server_internal.h` |
| 608 | `src/wayland/fbwl_apps_rules_load.c` |

## Wayland utilities (`util/fbwl-*.c`)
These are Wayland protocol test/tools shipped with the repo (not the compositor itself), but they’re
still part of the Wayland implementation surface area.

Reproduce:
```sh
rg --files util -g'fbwl-*.c' \
  | xargs -r wc -l \
  | awk '$2 != "total" {print}' \
  | sort -nr
```

Summary:
- Files: 29
- Total LOC: 16,676
- Max LOC: 1242 (`util/fbwl-input-injector.c`)

Largest utilities (>=600 LOC):

| LOC | File |
| ---: | --- |
| 1242 | `util/fbwl-input-injector.c` |
| 1080 | `util/fbwl-dnd-client.c` |
| 938 | `util/fbwl-clipboard-client.c` |
| 871 | `util/fbwl-primary-selection-client.c` |
| 852 | `util/fbwl-data-control-client.c` |
| 849 | `util/fbwl-xdp-portal-client.c` |
| 806 | `util/fbwl-sni-item-client.c` |
| 697 | `util/fbwl-xdg-activation-client.c` |
| 676 | `util/fbwl-relptr-client.c` |

## Shared WM core (`src/wmcore/**`)
This is the shared focus/stacking/workspace model used by the Wayland server.

Summary:
- Files: 4
- Total LOC: 1,116
- Max LOC: 925 (`src/wmcore/fbwm_core.c`)
