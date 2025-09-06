wlmatchbox_app_panel = executable('wlmatchbox-app-panel',
  'main.c',
  protocols_code['xdg-shell'],
  protocols_code['wlr-foreign-toplevel-management-unstable-v1'],
  protocols_client_header['xdg-shell'],
  protocols_client_header['wlr-foreign-toplevel-management-unstable-v1'],
  dependencies: [
    cairo,
    glib_2_0,
    wl_client,
    wl_cursor,
  ],
  install: true,
)

