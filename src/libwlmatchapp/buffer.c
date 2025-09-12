/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE

#include "buffer-internal.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wlmatchapp/display.h>

// Buffer
static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
  struct buffer *buffer = data;
  buffer->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    buffer_release,
};

struct buffer *buffer_create(struct wlm_display *display, uint32_t width,
                             uint32_t height, uint32_t stride,
                             uint32_t format) {
  int fd = memfd_create("buffer", MFD_CLOEXEC);
  if (fd < 0) {
    return NULL;
  }

  size_t size = height * stride;

  if (ftruncate(fd, size)) {
    int e = errno;
    close(fd);
    errno = e;
    return NULL;
  }

  void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    int e = errno;
    close(fd);
    errno = e;
    return NULL;
  }

  struct buffer *buffer = calloc(1, sizeof(*buffer));

  struct wl_shm_pool *pool = wl_shm_create_pool(display->shm, fd, size);
  buffer->buffer =
      wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
  wl_buffer_add_listener(buffer->buffer, &buffer_listener, buffer);
  wl_shm_pool_destroy(pool);
  close(fd);

  buffer->width = width;
  buffer->height = height;
  buffer->stride = stride;
  buffer->format = format;
  buffer->size = size;
  buffer->data = data;

  wl_list_init(&buffer->private.link);

  return buffer;
}

void buffer_free(struct buffer *buffer) {
  wl_list_remove(&buffer->private.link);
  wl_buffer_destroy(buffer->buffer);
  if (buffer->data) {
    munmap(buffer->data, buffer->size);
  }

  free(buffer);
}

