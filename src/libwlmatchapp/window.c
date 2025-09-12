/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#include <cairo.h>
#include <stdlib.h>
#include <wayland-client.h>
#include <wayland-util.h>
#include <wlmatchapp/display.h>
#include <wlmatchapp/window.h>

#include "buffer-internal.h"
#include "window-internal.h"
#include "xdg-shell-client-protocol.h"

static void frame_done(void *data, struct wl_callback *wl_callback,
                       uint32_t callback_data) {
  struct wlm_window *window = data;
  wl_callback_destroy(window->private.frame);
  window->private.frame = NULL;
  if (window->private.needs_draw) {
    wlm_window_draw(window);
  }
}

static const struct wl_callback_listener frame_listener = {
    frame_done,
};

static void surface_enter_output(void *data, struct wl_surface *wl_surface,
                                 struct wl_output *output) {
  struct wlm_window *window = data;
  window->output = output;
}

static void surface_leave_output(void *data, struct wl_surface *wl_surface,
                                 struct wl_output *output) {
  struct wlm_window *window = data;
  window->output = NULL;
}

static const struct wl_surface_listener surface_listener = {
    surface_enter_output,
    surface_leave_output,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                                  uint32_t serial) {
  struct wlm_window *window = data;
  if (window->on_configure) {
    window->on_configure(window, serial);
  }
  xdg_surface_ack_configure(window->xdg_surface, serial);
  window->private.configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure,
};

void wlm_window_init(struct wlm_display *display, struct wlm_window *window) {
  window->display = display;
  wl_list_init(&window->private.buffers);
  wl_list_insert(&display->private.window_list, &window->private.link);

  window->surface = wl_compositor_create_surface(display->compositor);
  wl_surface_add_listener(window->surface, &surface_listener, window);

  window->xdg_surface =
      xdg_wm_base_get_xdg_surface(display->xdg_wm_base, window->surface);
  xdg_surface_add_listener(window->xdg_surface, &xdg_surface_listener, window);

  // Set the needs draw flag. This will cause the window to be drawn after the
  // first configure event from the compositor
  wlm_window_schedule_redraw(window);
}

void wlm_window_deinit(struct wlm_window *window) {
  xdg_surface_destroy(window->xdg_surface);
  wl_surface_destroy(window->surface);
  if (window->private.frame) {
    wl_callback_destroy(window->private.frame);
  }

  struct buffer *cur, *next;
  wl_list_for_each_safe(cur, next, &window->private.buffers, private.link) {
    buffer_free(cur);
  }
  wl_list_remove(&window->private.link);
}

static bool buffer_match(struct buffer const *buffer, uint32_t format,
                         uint32_t width, uint32_t height, uint32_t stride) {
  return buffer->format == format && buffer->width == width &&
         buffer->height == height && buffer->stride == stride;
}

void wlm_window_set_size(struct wlm_window *window, uint32_t width,
                         uint32_t height) {
  if (window->width != width || window->height != height) {
    window->width = width;
    window->height = height;
    wlm_window_schedule_redraw(window);
  }
}

void wlm_window_schedule_redraw(struct wlm_window *window) {
  window->private.needs_draw = true;
}

void wlm_window_draw(struct wlm_window *window) {
  if (window->private.frame || !window->private.configured ||
      !window->on_draw || !window->width || !window->height) {
    // Can't draw right now. Flag as needing to redraw when the frame callback
    // finishes
    window->private.needs_draw = true;
    return;
  }
  window->private.needs_draw = false;

  struct buffer *buffer = NULL;
  struct buffer *tmp;
  uint32_t format = WL_SHM_FORMAT_XRGB8888;
  uint32_t stride =
      cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, window->width);

  // Remove any unused buffers that are the wrong size
  wl_list_for_each_safe(buffer, tmp, &window->private.buffers, private.link) {
    if (!buffer->busy &&
        !buffer_match(buffer, format, window->width, window->height, stride)) {
      buffer_free(buffer);
    }
  }

  bool found = false;
  wl_list_for_each(buffer, &window->private.buffers, private.link) {
    if (!buffer->busy &&
        buffer_match(buffer, format, window->width, window->height, stride)) {
      found = true;
      break;
    }
  }

  if (!found) {
    buffer = buffer_create(window->display, window->width, window->height,
                           stride, WL_SHM_FORMAT_XRGB8888);
    wl_list_insert(&window->private.buffers, &buffer->private.link);
  }

  cairo_surface_t *surface = cairo_image_surface_create_for_data(
      buffer->data, CAIRO_FORMAT_RGB24, window->width, window->height, stride);
  cairo_t *cr = cairo_create(surface);

  window->on_draw(window, cr);

  cairo_destroy(cr);
  cairo_surface_destroy(surface);

  wl_surface_attach(window->surface, buffer->buffer, 0, 0);
  buffer->busy = true;
  wl_surface_damage(window->surface, 0, 0, window->width, window->height);
  window->private.frame = wl_surface_frame(window->surface);
  wl_callback_add_listener(window->private.frame, &frame_listener, window);
  window->private.needs_commit = true;
}

void wlm_window_commit(struct wlm_window *window) {
  window->private.needs_commit = false;
  wl_surface_commit(window->surface);
}

