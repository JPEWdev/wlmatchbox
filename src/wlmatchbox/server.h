/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _SERVER_H
#define _SERVER_H

#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

#include "util.h"

struct server {
  struct wl_display *wl_display;
  struct wlr_backend *wlr_backend;
  struct wlr_renderer *wlr_renderer;
  struct wlr_allocator *wlr_allocator;

  struct wlr_compositor *wlr_compositor;
  struct wlr_subcompositor *wlr_subcompositor;
  struct wlr_data_device_manager *wlr_data_device_manager;

  struct wlr_output_layout *output_layout;
  struct wlr_scene *scene;
  struct wlr_scene_output_layout *scene_layout;

  struct wlr_seat *seat;
  struct wl_listener request_cursor;
  struct wl_listener pointer_focus_change;
  struct wl_listener request_set_selection;

  struct wlr_cursor *cursor;
  struct wlr_xcursor_manager *cursor_mgr;
  struct wl_listener cursor_motion;
  struct wl_listener cursor_motion_absolute;
  struct wl_listener cursor_button;
  struct wl_listener cursor_axis;
  struct wl_listener cursor_frame;

  struct wl_listener new_input;
  struct wl_list keyboards;

  struct wl_listener new_output;
  struct wl_list outputs;

  struct wlr_xwayland *xwayland;
  struct wl_listener new_xwayland_surface;

  struct wlr_xdg_shell *xdg_shell;
  struct wl_listener new_xdg_toplevel;
  struct wl_listener new_xdg_popup;
  struct wl_list toplevels;

  struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;

  struct wl_client *panel_client;
  struct wl_listener panel_client_destroy;

  struct server_sig const *sig;
};
DECLARE_TYPE(server)

bool server_handle_keybinding(struct server *server, xkb_keysym_t sym);

void server_create_panel(struct server *server, char const *program);

struct server *server_create(void);

#endif
