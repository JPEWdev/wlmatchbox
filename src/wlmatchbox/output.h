/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _OUTPUT_H
#define _OUTPUT_H

#include <wayland-server.h>

#include "util.h"

struct output {
  struct wl_list link;
  struct wlr_output *wlr_output;
  struct server *server;

  struct wl_listener frame;
  struct wl_listener request_state;
  struct wl_listener destroy;

  struct toplevel *panel;

  struct output_sig const *sig;
};
DECLARE_TYPE(output)

void output_create(struct server *server, struct wlr_output *wlr_output);
#endif
