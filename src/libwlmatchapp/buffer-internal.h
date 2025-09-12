/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _BUFFER_INTERNAL_H
#define _BUFFER_INTERNAL_H

#include <stdint.h>
#include <wayland-client-core.h>

struct wlm_display;

struct buffer {
  struct wl_buffer *buffer;
  bool busy;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t format;
  size_t size;
  void *data;

  struct {
    struct wl_list link;
  } private;
};

struct buffer *buffer_create(struct wlm_display *display, uint32_t width,
                             uint32_t height, uint32_t stride, uint32_t format);
void buffer_free(struct buffer *buffer);

#endif
