project('fluxbox-wayland', 'c', 'cpp',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
    'werror=false'
  ]
)

add_project_arguments('-DWLR_USE_UNSTABLE', language: ['c', 'cpp'])

# Dependencies
wlroots_dep = dependency('wlroots-0.18')
wayland_server_dep = dependency('wayland-server')
wayland_protocols_dep = dependency('wayland-protocols')
xkbcommon_dep = dependency('xkbcommon')
pixman_dep = dependency('pixman-1')
libinput_dep = dependency('libinput')

# Protocol sources we generated
protocol_sources = ['protocols/xdg-shell-protocol.c']
protocol_headers = []

# Source files - using full compositor with input and configuration
sources = [
  'src/full_compositor.cpp',
  'src/config.cpp',
] + protocol_sources

# Include directories
inc_dirs = include_directories('include', '.', 'protocols')

# Main executable
executable('fluxbox-wayland',
  sources,
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