xdg_app_chooser = executable('xdg-app-chooser',
  'main.c',
  protocols_code['xdg-shell'],
  protocols_client_header['xdg-shell'],
  dependencies: [
    cairo,
    gio_2_0,
    libm_dep,
    wl_client,
    wl_cursor,
  ],
  install: true,
)
