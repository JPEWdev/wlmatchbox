/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _POPUP_H
#define _POPUP_H

#include <wayland-server.h>

#include "util.h"

struct popup {
  struct server* server;
  struct wlr_xdg_popup *xdg_popup;
  struct wl_listener commit;
  struct wl_listener destroy;

  struct popup_sig const *sig;
};
DECLARE_TYPE(popup)

void popup_create(struct server *server, struct wlr_xdg_popup *xdg_popup);

#endif
