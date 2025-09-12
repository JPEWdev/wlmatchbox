/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#include "seat-internal.h"

#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wlmatchapp/display.h>
#include <wlmatchapp/seat.h>
#include <wlmatchapp/window.h>

// Pointer
static void pointer_enter(void *data, struct wl_pointer *wl_pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t surface_x, wl_fixed_t surface_y) {
  struct wlm_seat *seat = data;
  struct wlm_window *window = wl_surface_get_user_data(surface);

  seat->private.pointer_window = window;

  // set_cursor(cursor_left_ptr, seat, serial);
  window->pointer.x = wl_fixed_to_double(surface_x);
  window->pointer.y = wl_fixed_to_double(surface_y);

  if (window->pointer.on_enter) {
    window->pointer.on_enter(window, seat, serial);
  }
}

static void pointer_leave(void *data, struct wl_pointer *wl_pointer,
                          uint32_t serial, struct wl_surface *surface) {
  struct wlm_seat *seat = data;
  struct wlm_window *window = wl_surface_get_user_data(surface);

  if (window->pointer.on_leave) {
    window->pointer.on_leave(window, seat, serial);
  }

  seat->private.pointer_window = NULL;
}

static void pointer_motion(void *data, struct wl_pointer *wl_pointer,
                           uint32_t time, wl_fixed_t surface_x,
                           wl_fixed_t surface_y) {
  struct wlm_seat *seat = data;
  struct wlm_window *window = seat->private.pointer_window;
  if (!window) {
    return;
  }
  window->pointer.x = wl_fixed_to_double(surface_x);
  window->pointer.y = wl_fixed_to_double(surface_y);

  if (window->pointer.on_move) {
    window->pointer.on_move(window, seat, time);
  }
}

static void pointer_button(void *data, struct wl_pointer *wl_pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state) {
  struct wlm_seat *seat = data;
  struct wlm_window *window = seat->private.pointer_window;
  if (!window) {
    return;
  }

  if (window->pointer.on_button) {
    window->pointer.on_button(window, seat, serial, time, button, state);
  }
}

static void pointer_axis(void *data, struct wl_pointer *wl_pointer,
                         uint32_t time, uint32_t axis, wl_fixed_t value) {
  struct wlm_seat *seat = data;
  struct wlm_window *window = seat->private.pointer_window;
  if (!window) {
    return;
  }

  if (window->pointer.on_axis) {
    window->pointer.on_axis(window, seat, time, axis,
                            wl_fixed_to_double(value));
  }
}

static void pointer_frame(void *data, struct wl_pointer *wl_pointer) {
  struct wlm_seat *seat = data;
  struct wlm_window *window = seat->private.pointer_window;
  if (!window) {
    return;
  }

  if (window->pointer.on_frame) {
    window->pointer.on_frame(window, seat);
  }
}

static void pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
                                uint32_t axis_source) {}

static void pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
                              uint32_t time, uint32_t axis) {}

static void pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t axis, int32_t discrete) {}

static const struct wl_pointer_listener pointer_listener = {
    pointer_enter,       pointer_leave,     pointer_motion,
    pointer_button,      pointer_axis,      pointer_frame,
    pointer_axis_source, pointer_axis_stop, pointer_axis_discrete,
};

// Seat
static void seat_capabilities(void *data, struct wl_seat *wl_seat,
                              uint32_t capabilities) {
  struct wlm_seat *seat = data;
  if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
    seat->pointer = wl_seat_get_pointer(seat->seat);
    seat->private.pointer_window = NULL;
    wl_pointer_add_listener(seat->pointer, &pointer_listener, seat);
  }
}

static void seat_name(void *data, struct wl_seat *wl_seat, const char *name) {
  struct wlm_seat *seat = data;
  if (seat->name) {
    free(seat->name);
  }

  seat->name = strdup(name);
}

static const struct wl_seat_listener seat_listener = {
    seat_capabilities,
    seat_name,
};

struct wlm_seat *wlm_seat_create(struct wlm_display *display,
                                 struct wl_seat *wl_seat) {
  struct wlm_seat *seat = calloc(1, sizeof(*seat));
  seat->seat = wl_seat;
  seat->display = display;
  wl_seat_add_listener(seat->seat, &seat_listener, seat);

  seat->private.cursor_surface =
      wl_compositor_create_surface(display->compositor);

  wl_list_insert(&display->private.seat_list, &seat->private.link);
  return seat;
}

void wlm_seat_destroy(struct wlm_seat *seat) {
  wl_list_remove(&seat->private.link);
  free(seat->name);
  free(seat);
}

void wlm_seat_set_cursor(struct wlm_seat *seat, uint32_t serial,
                         struct wl_cursor *cursor) {
  if (!cursor) {
    wl_pointer_set_cursor(seat->pointer, serial, NULL, 0, 0);
    return;
  }

  struct wl_buffer *buffer = wl_cursor_image_get_buffer(cursor->images[0]);
  wl_surface_attach(seat->private.cursor_surface, buffer, 0, 0);
  wl_surface_commit(seat->private.cursor_surface);
  wl_pointer_set_cursor(seat->pointer, serial, seat->private.cursor_surface,
                        cursor->images[0]->hotspot_x,
                        cursor->images[0]->hotspot_y);
}

bool wlm_seat_set_theme_cursor(struct wlm_seat *seat, uint32_t serial,
                               char const *theme_name, uint32_t bpp,
                               char const *name) {
  struct wl_cursor *cursor =
      wlm_display_get_cursor(seat->display, theme_name, bpp, name);
  if (!cursor) {
    return false;
  }
  wlm_seat_set_cursor(seat, serial, cursor);
  return true;
}
