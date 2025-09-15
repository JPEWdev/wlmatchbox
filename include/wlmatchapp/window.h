/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _WLMATCHAPP_WINDOW_H
#define _WLMATCHAPP_WINDOW_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client-core.h>
#include <wlmatchapp/symbols.h>

struct wlm_seat;

struct wlm_window {
  struct wlm_display *display;
  struct wl_surface *surface;
  struct xdg_surface *xdg_surface;

  uint32_t width;
  uint32_t height;

  struct wl_output *output;

  void (*on_draw)(struct wlm_window *window, cairo_t *cr);
  void (*on_configure)(struct wlm_window *window, uint32_t serial);

  struct {
    double x;
    double y;
    void (*on_move)(struct wlm_window *window, struct wlm_seat *seat,
                    uint32_t time);
    void (*on_button)(struct wlm_window *window, struct wlm_seat *seat,
                      uint32_t serial, uint32_t time, uint32_t button,
                      uint32_t state);
    void (*on_enter)(struct wlm_window *window, struct wlm_seat *seat,
                     uint32_t serial);
    void (*on_leave)(struct wlm_window *window, struct wlm_seat *seat,
                     uint32_t serial);
    void (*on_axis)(struct wlm_window *window, struct wlm_seat *seat,
                    uint32_t time, uint32_t axis, double value);
    void (*on_frame)(struct wlm_window *window, struct wlm_seat *seat);

  } pointer;

  void *userdata;

  struct {
    struct wl_list link;

    struct wl_list buffers;
    bool needs_draw;
    struct wl_callback *frame;
    bool configured;
    bool needs_commit;
  } private;
};

WLM_API void wlm_window_set_size(struct wlm_window *window, uint32_t width,
                                 uint32_t height);
WLM_API void wlm_window_schedule_redraw(struct wlm_window *window);
WLM_API void wlm_window_draw(struct wlm_window *window);
WLM_API void wlm_window_commit(struct wlm_window *window);

static inline void wlm_window_set_user_data(struct wlm_window *window,
                                            void *userdata) {
  window->userdata = userdata;
}

static inline void *wlm_window_get_user_data(struct wlm_window *window) {
  return window->userdata;
}

#endif
