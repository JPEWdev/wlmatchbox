/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _WLMATCHAPP_SEAT
#define _WLMATCHAPP_SEAT

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client-core.h>
#include <wlmatchapp/symbols.h>

struct wl_cursor;

struct wlm_seat {
  struct wl_seat *seat;
  struct wlm_display *display;
  struct wl_pointer *pointer;
  char *name;

  struct {
    struct wl_list link;
    struct wlm_window *pointer_window;
    struct wl_surface *cursor_surface;
  } private;
};

WLM_API void wlm_seat_set_cursor(struct wlm_seat *seat, uint32_t serial,
                                 struct wl_cursor *cursor);
WLM_API bool wlm_seat_set_theme_cursor(struct wlm_seat *seat, uint32_t serial,
                                       char const *theme_name, uint32_t bpp,
                                       char const *name);

#endif
