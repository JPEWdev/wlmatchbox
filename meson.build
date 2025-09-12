project('wlmatchbox', 'c')

wlroots = dependency('wlroots-0.19', fallback: ['wlroots', 'wlroots'])
wl_server = dependency('wayland-server')
wl_client = dependency('wayland-client')
wl_scanner = dependency('wayland-scanner')
wl_cursor = dependency('wayland-cursor')
cairo = dependency('cairo')
glib_2_0 = dependency('glib-2.0')
gio_2_0 = dependency('gio-2.0')
xkbcommon = dependency('xkbcommon')
wayland_scanner = wl_scanner.get_variable('wayland_scanner')

add_project_arguments('-DWLR_USE_UNSTABLE=1', language: 'c')

subdir('protocols')
subdir('src/wlmatchbox')
subdir('src/xdg-app-chooser')
subdir('src/app-panel')

run_target('run',
  command: [
    wlmatchbox, '-p', wlmatchbox_app_panel, '-i', xdg_app_chooser,
  ]
)
