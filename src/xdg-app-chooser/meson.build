xdg_app_chooser = executable('xdg-app-chooser',
  'main.c',
  protocols_code['xdg-shell'],
  protocols_client_header['xdg-shell'],
  dependencies: [
    wl_client,
    wl_cursor,
    cairo,
    gio_2_0,
  ],
  install: true,
)
