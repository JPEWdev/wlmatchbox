/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _TOPLEVEL_H
#define _TOPLEVEL_H

#include <wayland-server.h>

#include "util.h"

struct wlr_surface;

struct toplevel {
  struct wl_list link;
  struct server *server;
  struct wlr_xdg_toplevel *xdg_toplevel;
  struct wlr_scene_tree *scene_tree;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener commit;
  struct wl_listener destroy;
  struct wl_listener request_maximize;
  struct wl_listener request_fullscreen;
  struct wl_listener set_title;
  struct wl_listener set_app_id;

  struct {
    struct wlr_foreign_toplevel_handle_v1 *handle;
    struct wl_listener request_maximize;
    struct wl_listener request_minimize;
    struct wl_listener request_activate;
    struct wl_listener request_fullscreen;
    struct wl_listener request_close;
    struct wl_listener destroy;
  } foreign;

  struct output *output;

  struct toplevel_sig const *sig;
};
DECLARE_TYPE(toplevel)

void toplevel_create(struct server *server,
                     struct wlr_xdg_toplevel *xdg_toplevel);
void toplevel_assign_output(struct toplevel *toplevel, struct output *output);
void toplevel_assign_any_output(struct toplevel *toplevel);
void toplevel_focus(struct toplevel *toplevel);

struct toplevel *toplevel_at(struct server *server, double lx, double ly,
                             struct wlr_surface **surface, double *sx,
                             double *sy);

#endif
