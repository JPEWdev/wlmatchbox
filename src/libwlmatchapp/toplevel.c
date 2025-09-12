/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <wayland-client.h>
#include <wlmatchapp/display.h>
#include <wlmatchapp/toplevel.h>

#include "window-internal.h"
#include "xdg-shell-client-protocol.h"

static void xdg_toplevel_configure(void *data,
                                   struct xdg_toplevel *xdg_toplevel,
                                   int32_t width, int32_t height,
                                   struct wl_array *states) {
  struct wlm_toplevel *toplevel = data;
  toplevel->configure.width = width;
  toplevel->configure.height = height;
  toplevel->configure.maximized = false;
  toplevel->configure.fullscreen = false;
  toplevel->configure.resizing = false;
  toplevel->configure.activated = false;

  uint32_t *s;
  wl_array_for_each(s, states) {
    switch (*s) {
    case XDG_TOPLEVEL_STATE_MAXIMIZED:
      toplevel->configure.maximized = true;
      break;
    case XDG_TOPLEVEL_STATE_FULLSCREEN:
      toplevel->configure.fullscreen = true;
      break;
    case XDG_TOPLEVEL_STATE_RESIZING:
      toplevel->configure.resizing = true;
      break;
    case XDG_TOPLEVEL_STATE_ACTIVATED:
      toplevel->configure.activated = true;
      break;
    }
  }
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
  struct wlm_toplevel *toplevel = data;
  if (toplevel->on_close) {
    toplevel->on_close(toplevel);
  }
}

static void xdg_toplevel_configure_bounds(void *data,
                                          struct xdg_toplevel *xdg_toplevel,
                                          int32_t width, int32_t height) {
  struct wlm_toplevel *toplevel = data;
  toplevel->configure.bounds_width = width;
  toplevel->configure.bounds_height = height;
}

static void xdg_toplevel_wm_capabilities(void *data,
                                         struct xdg_toplevel *xdg_toplevel,
                                         struct wl_array *capabilities) {
  struct wlm_toplevel *toplevel = data;
  toplevel->wm_cap.window_menu = false;
  toplevel->wm_cap.maximize = false;
  toplevel->wm_cap.fullscreen = false;
  toplevel->wm_cap.minimize = false;

  uint32_t *cap;
  wl_array_for_each(cap, capabilities) {
    switch (*cap) {
    case XDG_TOPLEVEL_WM_CAPABILITIES_WINDOW_MENU:
      toplevel->wm_cap.window_menu = true;
      break;
    case XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE:
      toplevel->wm_cap.maximize = true;
      break;
    case XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN:
      toplevel->wm_cap.fullscreen = true;
      break;
    case XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE:
      toplevel->wm_cap.minimize = true;
      break;
    }
  }
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    xdg_toplevel_configure,
    xdg_toplevel_close,
    xdg_toplevel_configure_bounds,
    xdg_toplevel_wm_capabilities,
};

struct wlm_toplevel *wlm_toplevel_create(struct wlm_display *display) {
  struct wlm_toplevel *toplevel = calloc(1, sizeof(*toplevel));

  wlm_window_init(display, &toplevel->base);

  toplevel->xdg_toplevel = xdg_surface_get_toplevel(toplevel->base.xdg_surface);
  xdg_toplevel_add_listener(toplevel->xdg_toplevel, &xdg_toplevel_listener,
                            toplevel);

  // Assume all capabilities by default
  toplevel->wm_cap.window_menu = true;
  toplevel->wm_cap.maximize = true;
  toplevel->wm_cap.fullscreen = true;
  toplevel->wm_cap.minimize = true;

  toplevel->base.private.needs_commit = true;

  wl_list_insert(&display->private.toplevel_list, &toplevel->private.link);

  return toplevel;
}

void wlm_toplevel_destroy(struct wlm_toplevel *toplevel) {
  wl_list_remove(&toplevel->private.link);
  wlm_window_deinit(&toplevel->base);
  xdg_toplevel_destroy(toplevel->xdg_toplevel);
  free(toplevel);
}

void wlm_toplevel_set_maximized(struct wlm_toplevel *toplevel, bool maximized) {
  if (maximized) {
    xdg_toplevel_set_maximized(toplevel->xdg_toplevel);
  } else {
    xdg_toplevel_unset_maximized(toplevel->xdg_toplevel);
  }
  toplevel->base.private.needs_commit = true;
}

void wlm_toplevel_set_fullscreen(struct wlm_toplevel *toplevel,
                                 bool fullscreen) {
  if (fullscreen) {
    if (toplevel->base.output) {
      xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel,
                                  toplevel->base.output);
      toplevel->base.private.needs_commit = true;
    }
  } else {
    xdg_toplevel_unset_fullscreen(toplevel->xdg_toplevel);
    toplevel->base.private.needs_commit = true;
  }
}

void wlm_toplevel_set_minimized(struct wlm_toplevel *toplevel) {
  xdg_toplevel_set_minimized(toplevel->xdg_toplevel);
  toplevel->base.private.needs_commit = true;
}

void wlm_toplevel_set_app_id(struct wlm_toplevel *toplevel,
                             char const *app_id) {
  xdg_toplevel_set_app_id(toplevel->xdg_toplevel, app_id);
  toplevel->base.private.needs_commit = true;
}

void wlm_toplevel_set_title(struct wlm_toplevel *toplevel, char const *title) {
  xdg_toplevel_set_title(toplevel->xdg_toplevel, title);
  toplevel->base.private.needs_commit = true;
}
