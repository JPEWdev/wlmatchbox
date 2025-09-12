/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wlmatchapp/display.h>
#include <wlmatchapp/seat.h>
#include <wlmatchapp/toplevel.h>
#include <wlmatchapp/window.h>

#include "seat-internal.h"

#include "xdg-shell-client-protocol.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

struct shm_format {
  struct wl_list link;
  uint32_t format;
};

struct cursor_theme {
  struct wl_list link;
  char *name;
  uint32_t bpp;
  struct wl_cursor_theme *theme;
  struct wl_list cursor_list;
};

struct cursor {
  struct wl_list link;
  char *name;
  struct wl_cursor *cursor;
};

static void cursor_destroy(struct cursor *cursor) {
  wl_list_remove(&cursor->link);
  free(cursor->name);
  free(cursor);
}

static void cursor_theme_destroy(struct cursor_theme *theme) {
  struct cursor *cursor, *next;
  wl_list_for_each_safe(cursor, next, &theme->cursor_list, link) {
    cursor_destroy(cursor);
  }

  wl_list_remove(&theme->link);
  wl_cursor_theme_destroy(theme->theme);
  free(theme->name);
  free(theme);
}

// SHM
static void shm_format(void *data, struct wl_shm *wl_shm, uint32_t format) {
  struct wlm_display *display = data;
  struct shm_format *f = calloc(1, sizeof(*f));
  f->format = format;
  wl_list_insert(&display->private.shm_format_list, &f->link);
}

static const struct wl_shm_listener shm_listener = {
    shm_format,
};

// XDG WM Base
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                             uint32_t serial) {
  xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    xdg_wm_base_ping,
};

static void registry_global(void *data, struct wl_registry *wl_registry,
                            uint32_t name, char const *interface,
                            uint32_t version) {
  struct wlm_display *display = data;

  if (strcmp(interface, "wl_compositor") == 0) {
    display->compositor = wl_registry_bind(
        wl_registry, name, &wl_compositor_interface, MIN(version, 5));

  } else if (strcmp(interface, "wl_seat") == 0) {
    struct wl_seat *wl_seat = wl_registry_bind(
        wl_registry, name, &wl_seat_interface, MIN(version, 5));
    wlm_seat_create(display, wl_seat);

  } else if (strcmp(interface, "wl_shm") == 0) {
    if (!display->shm) {
      display->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface,
                                      MIN(version, 1));
      wl_shm_add_listener(display->shm, &shm_listener, display);
      display->needs_roundtrip = true;
    }

  } else if (strcmp(interface, "xdg_wm_base") == 0) {
    display->xdg_wm_base = wl_registry_bind(
        wl_registry, name, &xdg_wm_base_interface, MIN(version, 4));
    xdg_wm_base_add_listener(display->xdg_wm_base, &xdg_wm_base_listener,
                             display);
  }

  if (display->on_global) {
    display->on_global(display, name, interface, version);
  }
}

static void registry_global_remove(void *data, struct wl_registry *wl_registry,
                                   uint32_t name) {
  struct wlm_display *display = data;
  struct wlm_seat *seat, *next_seat;
  wl_list_for_each_safe(seat, next_seat, &display->private.seat_list,
                        private.link) {
    if (wl_proxy_get_id((struct wl_proxy *)seat->seat) == name) {
      wlm_seat_destroy(seat);
    }
  }

  if (display->on_global_remove) {
    display->on_global_remove(display, name);
  }
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove,
};

struct wlm_display *wlm_display_create(void) {
  struct wlm_display *display = calloc(1, sizeof(*display));
  wl_list_init(&display->private.seat_list);
  wl_list_init(&display->private.shm_format_list);
  wl_list_init(&display->private.window_list);
  wl_list_init(&display->private.toplevel_list);
  wl_list_init(&display->private.cursor_theme_list);

  return display;
}

bool wlm_display_connect(struct wlm_display *display, char const *name) {
  display->display = wl_display_connect(name);
  if (!display->display) {
    return false;
  }

  display->registry = wl_display_get_registry(display->display);

  wl_registry_add_listener(display->registry, &registry_listener, display);

  display->needs_roundtrip = true;
  while (display->needs_roundtrip) {
    display->needs_roundtrip = false;
    wl_display_roundtrip(display->display);
  }

  return true;
}

void wlm_display_destroy(struct wlm_display *display) {
  struct cursor_theme *cursor_theme, *cursor_theme_next;
  wl_list_for_each_safe(cursor_theme, cursor_theme_next,
                        &display->private.cursor_theme_list, link) {
    cursor_theme_destroy(cursor_theme);
  }

  struct wlm_seat *seat, *seat_next;
  wl_list_for_each_safe(seat, seat_next, &display->private.seat_list,
                        private.link) {
    wlm_seat_destroy(seat);
  }

  struct shm_format *shm_format, *shm_format_next;
  wl_list_for_each_safe(shm_format, shm_format_next,
                        &display->private.shm_format_list, link) {
    wl_list_remove(&shm_format->link);
    free(shm_format);
  }

  struct wlm_toplevel *toplevel, *toplevel_next;
  wl_list_for_each_safe(toplevel, toplevel_next,
                        &display->private.toplevel_list, private.link) {
    wlm_toplevel_destroy(toplevel);
  }

  if (display->xdg_wm_base) {
    xdg_wm_base_destroy(display->xdg_wm_base);
  }
  if (display->shm) {
    wl_shm_destroy(display->shm);
  }
  if (display->compositor) {
    wl_compositor_destroy(display->compositor);
  }
  if (display->registry) {
    wl_registry_destroy(display->registry);
  }
  if (display->display) {
    wl_display_disconnect(display->display);
  }

  free(display);
}

int wlm_display_dispatch(struct wlm_display *display) {
  struct wlm_window *window;
  wl_list_for_each(window, &display->private.window_list, private.link) {
    if (window->private.needs_draw) {
      wlm_window_draw(window);
    }
    if (window->private.needs_commit) {
      wlm_window_commit(window);
    }
  }

  return wl_display_dispatch(display->display);
}

static struct cursor_theme *get_cursor_theme(struct wlm_display *display,
                                             char const *name, uint32_t bpp) {
  if (!bpp) {
    bpp = 24;
  }

  struct cursor_theme *theme;
  wl_list_for_each(theme, &display->private.cursor_theme_list, link) {
    if (!name && !theme->name) {
      return theme;
    }
    if (!name || !theme->name) {
      continue;
    }
    if (strcmp(name, theme->name) == 0) {
      return theme;
    }
  }

  struct wl_cursor_theme *wl_cursor_theme =
      wl_cursor_theme_load(name, bpp, display->shm);

  if (!wl_cursor_theme) {
    return NULL;
  }
  theme = calloc(1, sizeof(*theme));
  wl_list_init(&theme->cursor_list);
  if (name) {
    theme->name = strdup(name);
  }
  theme->bpp = bpp;
  theme->theme = wl_cursor_theme;
  wl_list_insert(&display->private.cursor_theme_list, &theme->link);
  return theme;
}

struct wl_cursor *wlm_display_get_cursor(struct wlm_display *display,
                                         char const *theme_name, uint32_t bpp,
                                         char const *name) {
  if (!name) {
    return NULL;
  }

  struct cursor_theme *theme = get_cursor_theme(display, theme_name, bpp);
  if (!theme) {
    return NULL;
  }

  struct cursor *cursor;
  wl_list_for_each(cursor, &theme->cursor_list, link) {
    if (strcmp(name, cursor->name) == 0) {
      return cursor->cursor;
    }
  }

  struct wl_cursor *wl_cursor = wl_cursor_theme_get_cursor(theme->theme, name);
  if (!wl_cursor) {
    return NULL;
  }

  cursor = calloc(1, sizeof(*cursor));
  cursor->cursor = wl_cursor;
  cursor->name = strdup(name);
  wl_list_insert(&theme->cursor_list, &cursor->link);

  return wl_cursor;
}

