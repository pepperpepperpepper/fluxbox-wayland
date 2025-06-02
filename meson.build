project('fluxbox-wayland', 'c', 'cpp',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
    'werror=false',
    'c_std=c11'
  ]
)

add_project_arguments('-DWLR_USE_UNSTABLE', language: ['c', 'cpp'])
add_project_arguments('-Wno-vla', language: 'cpp')

# Dependencies
wlroots_dep = dependency('wlroots-0.18')
wayland_server_dep = dependency('wayland-server')
wayland_client_dep = dependency('wayland-client')
wayland_protocols_dep = dependency('wayland-protocols')
xkbcommon_dep = dependency('xkbcommon')
pixman_dep = dependency('pixman-1')
libinput_dep = dependency('libinput')

# Portal backend dependencies
glib_dep = dependency('glib-2.0')
gio_dep = dependency('gio-2.0')

# Protocol sources we generated
protocol_sources = [
  'protocols/xdg-shell-protocol.c',
  'protocols/wlr-screencopy-unstable-v1-client-protocol.c',
  'protocols/xdg-output-unstable-v1-client-protocol.c'
]

# C++ Source files for the main compositor
cpp_sources = [
  'src/main.cpp',
  'src/server.cpp',
  'src/surface.cpp',
  'src/workspace.cpp',
  'src/config.cpp',
  'src/theme.cpp',
  'src/keyboard.cpp',
  'src/cursor.cpp',
  'src/seat.cpp',
  'src/output.cpp',
  'src/xdg_shell.cpp',
]

# C wrapper sources to avoid C++ incompatibility issues
c_wrapper_sources = [
  'src/scene_wrapper.c',
]

# C sources for scene-based compositor (alternative)
c_sources = [
  'src/scene_main.c',
] + protocol_sources

# Include directories
inc_dirs = include_directories('include', '.', 'protocols')

# Main C++ executable
executable('fluxbox-wayland',
  cpp_sources + c_wrapper_sources + protocol_sources,
  dependencies: [
    wlroots_dep,
    wayland_server_dep,
    xkbcommon_dep,
    pixman_dep,
    libinput_dep,
  ],
  include_directories: inc_dirs,
  install: true
)

# C version (scene-based) - alternative implementation
executable('fluxbox-wayland-scene',
  c_sources,
  dependencies: [
    wlroots_dep,
    wayland_server_dep,
    xkbcommon_dep,
    pixman_dep,
    libinput_dep,
  ],
  include_directories: inc_dirs,
  install: false
)

# Portal backend executable
executable('fluxbox-portal-backend',
  'src/portal_backend.cpp',
  dependencies: [
    wlroots_dep,
    wayland_client_dep,
    glib_dep,
    gio_dep,
  ],
  include_directories: inc_dirs,
  install: true
)

# Wayland screenshot tool
executable('fluxbox-screenshot',
  ['src/wayland_screenshot.c'] + [
    'protocols/wlr-screencopy-unstable-v1-client-protocol.c',
    'protocols/xdg-output-unstable-v1-client-protocol.c'
  ],
  dependencies: [
    wayland_client_dep,
  ],
  include_directories: inc_dirs,
  install: true
)