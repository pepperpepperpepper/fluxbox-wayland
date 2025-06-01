# Fluxbox for Wayland

A port of the Fluxbox window manager to the Wayland display protocol using wlroots.

## Dependencies

- wlroots
- wayland-server
- wayland-protocols
- xkbcommon
- pixman
- libinput
- meson (build system)

## Building

```bash
meson setup build
meson compile -C build
```

## Installation

```bash
meson install -C build
```

## Running

```bash
./build/fluxbox-wayland
```

## Configuration

Configuration files will be located in `~/.config/fluxbox-wayland/` (to be implemented).

## Architecture

The compositor is built using:
- **wlroots**: Low-level Wayland compositor library
- **Scene API**: For efficient rendering and window management
- **XDG Shell**: Standard Wayland window management protocol

## License

MIT License (following original Fluxbox licensing)