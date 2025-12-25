

project('ade-compositor', 'c',
  version: '0.1.0',
  default_options: ['c_std=c11']
)

wlroots = dependency('wlroots-0.19')
wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
wayland_protocols = dependency('wayland-protocols')
libinput = dependency('libinput')
xkbcommon = dependency('xkbcommon')

wayland_scanner = find_program('wayland-scanner')

protocol_dir = wayland_protocols.get_variable('pkgdatadir')

xdg_shell_xml = join_paths(protocol_dir, 'stable', 'xdg-shell', 'xdg-shell.xml')

xdg_shell_header = custom_target(
  'xdg-shell-protocol.h',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@']
)

xdg_shell_code = custom_target(
  'xdg-shell-protocol.c',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@']
)

wlr_protocols = dependency('wlr-protocols')
wlr_protocol_dir = wlr_protocols.get_variable('pkgdatadir')

layer_shell_xml = join_paths(wlr_protocol_dir, 'unstable', 'wlr-layer-shell-unstable-v1.xml')

layer_shell_header = custom_target(
  'wlr-layer-shell-unstable-v1-protocol.h',
  input: layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@']
)

layer_shell_code = custom_target(
  'wlr-layer-shell-unstable-v1-protocol.c',
  input: layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@']
)

executable(
  'ade-compositor',
  [
    'ade-compositor.c',
    xdg_shell_header,
    xdg_shell_code,
    layer_shell_header,
    layer_shell_code,
  ],
  dependencies: [
    wlroots,
    wayland_server,
    wayland_client,
    libinput,
    xkbcommon
  ],
  c_args: ['-DWLR_USE_UNSTABLE'],
  install: false
)



