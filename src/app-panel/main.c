/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE

#include <cairo.h>
#include <ctype.h>
#include <glib.h>
#include <linux/input.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define DEFAULT_WIDTH (100)
#define DEFAULT_HEIGHT (100)

#define MENU_PADDING (10)

static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_cursor_theme *cursor_theme;
static struct wl_cursor *cursor_left_ptr;
static struct xdg_wm_base *xdg_wm_base;
static struct wl_surface *cursor_surface;
static struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager;

struct buffer {
  struct wl_list link;
  struct wl_buffer *buffer;
  bool busy;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t format;
  size_t size;
  void *data;
};

struct window {
  struct wl_list link;
  struct wl_surface *surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;

  double cursor_x;
  double cursor_y;

  struct wl_list buffers;
  uint32_t width;
  uint32_t height;
  struct wl_callback *frame;
  bool needs_draw;
  cairo_font_extents_t extents;
};
static struct wl_list window_list;

struct seat {
  struct wl_seat *seat;
  struct {
    struct wl_pointer *pointer;
    struct window *window;
  } pointer;
};
static struct wl_list seat_list;

struct toplevel {
  struct wl_list link;
  struct zwlr_foreign_toplevel_handle_v1 *toplevel;
  bool done;
  char *title;
  char *app_id;
  bool minimized;
  bool maximized;
  bool activated;
  bool fullscreen;
  cairo_rectangle_t rect;
};
static struct wl_list toplevel_list;

static void draw_window(struct window *window);

static void draw_all_windows(void) {
  struct window *window;
  wl_list_for_each(window, &window_list, link) { draw_window(window); }
}

static void set_cursor(struct wl_cursor *cursor, struct seat *seat,
                       uint32_t serial) {
  if (!cursor) {
    wl_pointer_set_cursor(seat->pointer.pointer, serial, NULL, 0, 0);
    return;
  }
  struct wl_buffer *buffer = wl_cursor_image_get_buffer(cursor->images[0]);
  wl_surface_attach(cursor_surface, buffer, 0, 0);
  wl_surface_commit(cursor_surface);
  wl_pointer_set_cursor(seat->pointer.pointer, serial, cursor_surface,
                        cursor->images[0]->hotspot_x,
                        cursor->images[0]->hotspot_y);
}

// Buffer
static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
  struct buffer *buffer = data;
  buffer->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    buffer_release,
};

static struct buffer *create_buffer(uint32_t width, uint32_t height,
                                    uint32_t stride, uint32_t format) {
  int fd = memfd_create("buffer", MFD_CLOEXEC);
  if (fd < 0) {
    perror("Unable to create memfd");
    return NULL;
  }

  size_t size = height * stride;

  if (ftruncate(fd, size)) {
    perror("Unable to truncate memfd");
    close(fd);
    return NULL;
  }

  void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    perror("Unable to map memfd");
    close(fd);
    return NULL;
  }

  struct buffer *buffer = calloc(1, sizeof(*buffer));

  struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
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

  return buffer;
}

static void free_buffer(struct buffer *buffer) {
  munmap(buffer->data, buffer->size);
  wl_buffer_destroy(buffer->buffer);
  free(buffer);
}

static void attach_buffer(struct wl_surface *surface, struct buffer *buffer,
                          int32_t x, int32_t y) {
  wl_surface_attach(surface, buffer->buffer, x, y);
  buffer->busy = true;
}

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                                  uint32_t serial) {
  struct window *window = data;
  xdg_surface_ack_configure(xdg_surface, serial);
  if (window->needs_draw) {
    draw_window(window);
  }
}

static const struct xdg_surface_listener window_xdg_surface_listener = {
    xdg_surface_configure,
};

// XDG toplevel
static void xdg_toplevel_configure(void *data,
                                   struct xdg_toplevel *xdg_toplevel,
                                   int32_t width, int32_t height,
                                   struct wl_array *states) {
  struct window *window = data;
  if (width) {
    if (window->width != width) {
      window->width = width;
      window->needs_draw = true;
    }
  } else if (!window->width) {
    window->width = DEFAULT_WIDTH;
    window->needs_draw = true;
  }

  if (height) {
    if (window->height != height) {
      window->height = height;
      window->needs_draw = true;
    }
  } else if (!window->height) {
    window->height = DEFAULT_HEIGHT;
    window->needs_draw = true;
  }
  printf("Size is %dx%d\n", window->width, window->height);
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {}

static void xdg_toplevel_configure_bounds(void *data,
                                          struct xdg_toplevel *xdg_toplevel,
                                          int32_t width, int32_t height) {}

static void xdg_toplevel_wm_capabilities(void *data,
                                         struct xdg_toplevel *xdg_toplevel,
                                         struct wl_array *capabilities) {}

static const struct xdg_toplevel_listener window_xdg_toplevel_listener = {
    xdg_toplevel_configure,
    xdg_toplevel_close,
    xdg_toplevel_configure_bounds,
    xdg_toplevel_wm_capabilities,
};

// SHM
static void shm_format(void *data, struct wl_shm *wl_shm, uint32_t format) {}

static const struct wl_shm_listener shm_listener = {
    shm_format,
};

// Background surface
static void window_enter_output(void *data, struct wl_surface *wl_surface,
                                struct wl_output *output) {}

static void window_leave_output(void *data, struct wl_surface *wl_surface,
                                struct wl_output *output) {}

static const struct wl_surface_listener window_surface_listener = {
    window_enter_output,
    window_leave_output,
};

static void window_frame_done(void *data, struct wl_callback *wl_callback,
                              uint32_t callback_data) {
  struct window *window = data;
  wl_callback_destroy(window->frame);
  window->frame = NULL;
  if (window->needs_draw) {
    draw_window(window);
  }
}

static const struct wl_callback_listener window_frame_listener = {
    window_frame_done,
};

static void draw_window(struct window *window) {
  if (window->frame) {
    // Can't draw right now. Flag as needing to redraw when the frame callback
    // finishes
    window->needs_draw = true;
    return;
  }
  window->needs_draw = false;

  struct buffer *buffer = NULL;
  struct buffer *tmp;
  uint32_t stride =
      cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, window->width);

  // Remove any unused buffers that are the wrong size
  wl_list_for_each_safe(buffer, tmp, &window->buffers, link) {
    if (!buffer->busy &&
        (buffer->width != window->width || buffer->height != window->height ||
         buffer->stride != stride)) {
      wl_list_remove(&buffer->link);
      free_buffer(buffer);
    }
  }

  bool found = false;
  wl_list_for_each(buffer, &window->buffers, link) {
    if (!buffer->busy && buffer->width == window->width &&
        buffer->height == window->height && buffer->stride == stride) {
      found = true;
      break;
    }
  }

  if (!found) {
    buffer = create_buffer(window->width, window->height, stride,
                           WL_SHM_FORMAT_XRGB8888);
    wl_list_insert(&window->buffers, &buffer->link);
  }

  cairo_surface_t *surface = cairo_image_surface_create_for_data(
      buffer->data, CAIRO_FORMAT_RGB24, window->width, window->height, stride);
  cairo_t *cr = cairo_create(surface);

  cairo_rectangle(cr, 0, 0, window->width, window->height);
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_fill(cr);

  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 20);

  cairo_font_extents(cr, &window->extents);

  int count = 0;
  struct toplevel *toplevel;
  wl_list_for_each(toplevel, &toplevel_list, link) {
    if (!toplevel->done) {
      continue;
    }
    count++;
  }

  if (count) {
    int item_width = window->width / count;

    int i = 0;
    wl_list_for_each(toplevel, &toplevel_list, link) {
      if (!toplevel->done) {
        toplevel->rect.width = 0;
        toplevel->rect.height = 0;
        continue;
      }
      cairo_save(cr);

      if (toplevel->activated) {
        cairo_set_source_rgb(cr, 0, 1, 0);
      } else {
        cairo_set_source_rgb(cr, 0, 0, 0);
      }
      toplevel->rect.x = i * item_width;
      toplevel->rect.y = 0;
      toplevel->rect.width = item_width;
      toplevel->rect.height = window->height;

      cairo_rectangle(cr, toplevel->rect.x, toplevel->rect.y, toplevel->rect.width,
                      toplevel->rect.height);
      cairo_clip(cr);

      cairo_move_to(cr, toplevel->rect.x,
                    toplevel->rect.y + toplevel->rect.height / 2 +
                        window->extents.ascent / 2);
      cairo_show_text(cr, toplevel->title);
      cairo_restore(cr);
      i++;
    }
  }

  cairo_destroy(cr);
  cairo_surface_destroy(surface);

  attach_buffer(window->surface, buffer, 0, 0);
  wl_surface_damage(window->surface, 0, 0, window->width, window->height);
  window->frame = wl_surface_frame(window->surface);
  wl_callback_add_listener(window->frame, &window_frame_listener, window);
  wl_surface_commit(window->surface);
}

// XDG WM base
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                             uint32_t serial) {
  xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    xdg_wm_base_ping,
};

// Pointer
static void pointer_enter(void *data, struct wl_pointer *wl_pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t surface_x, wl_fixed_t surface_y) {
  struct seat *seat = data;
  struct window *window = wl_surface_get_user_data(surface);

  seat->pointer.window = window;

  set_cursor(cursor_left_ptr, seat, serial);

  window->cursor_x = wl_fixed_to_double(surface_x);
  window->cursor_y = wl_fixed_to_double(surface_y);

  draw_window(window);
}

static void pointer_leave(void *data, struct wl_pointer *wl_pointer,
                          uint32_t serial, struct wl_surface *surface) {
  struct seat *seat = data;
  struct window *window = wl_surface_get_user_data(surface);
  window->cursor_x = -1;
  window->cursor_y = -1;

  draw_window(window);

  seat->pointer.window = NULL;
}

static void pointer_motion(void *data, struct wl_pointer *wl_pointer,
                           uint32_t time, wl_fixed_t surface_x,
                           wl_fixed_t surface_y) {
  struct seat *seat = data;
  struct window *window = seat->pointer.window;
  if (!window) {
    return;
  }
  window->cursor_x = wl_fixed_to_double(surface_x);
  window->cursor_y = wl_fixed_to_double(surface_y);

  draw_window(window);
}

static void pointer_button(void *data, struct wl_pointer *wl_pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state) {
  struct seat *seat = data;
  struct window *window = seat->pointer.window;
  if (!window) {
    return;
  }

  if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_RELEASED) {
    struct toplevel *toplevel;
    wl_list_for_each(toplevel, &toplevel_list, link) {
      if (!toplevel->rect.width && !toplevel->rect.height) {
        continue;
      }
      if (window->cursor_x >= toplevel->rect.x &&
          window->cursor_y >= toplevel->rect.y &&
          window->cursor_x < toplevel->rect.x + toplevel->rect.width &&
          window->cursor_y < toplevel->rect.y + toplevel->rect.height) {
        printf("Activate %s\n", toplevel->title);
        zwlr_foreign_toplevel_handle_v1_activate(toplevel->toplevel,
                                                 seat->seat);
        break;
      }
    }
  }
}

static void pointer_axis(void *data, struct wl_pointer *wl_pointer,
                         uint32_t time, uint32_t axis, wl_fixed_t value) {
  struct seat *seat = data;
  struct window *window = seat->pointer.window;
  if (!window) {
    return;
  }
  // if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
  //   window->y_scroll += wl_fixed_to_int(value);
  //   draw_window(window);
  // }
}

static void pointer_frame(void *data, struct wl_pointer *wl_pointer) {}

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

// seat
static void seat_capabilities(void *data, struct wl_seat *wl_seat,
                              uint32_t capabilities) {
  struct seat *seat = data;
  if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
    seat->pointer.pointer = wl_seat_get_pointer(seat->seat);
    seat->pointer.window = NULL;
    wl_pointer_add_listener(seat->pointer.pointer, &pointer_listener, seat);
  }
}

static void seat_name(void *data, struct wl_seat *wl_seat, const char *name) {}

static const struct wl_seat_listener seat_listener = {
    seat_capabilities,
    seat_name,
};

// Toplevel handle
static void toplevel_handle_title(
    void *data,
    struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
    const char *title) {
  struct toplevel *toplevel = data;
  free(toplevel->title);
  toplevel->title = strdup(title);
}

static void toplevel_handle_app_id(
    void *data,
    struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
    const char *app_id) {
  struct toplevel *toplevel = data;
  free(toplevel->app_id);
  toplevel->app_id = strdup(app_id);
}

static void toplevel_handle_output_enter(
    void *data,
    struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
    struct wl_output *output) {}

static void toplevel_handle_output_leave(
    void *data,
    struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
    struct wl_output *output) {}

static void toplevel_handle_state(
    void *data,
    struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
    struct wl_array *state) {
  struct toplevel *toplevel = data;
  toplevel->minimized = false;
  toplevel->maximized = false;
  toplevel->activated = false;
  toplevel->fullscreen = false;
  enum zwlr_foreign_toplevel_handle_v1_state *s;
  wl_array_for_each(s, state) {
    switch (*s) {
    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:
      toplevel->maximized = true;
      break;
    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:
      toplevel->minimized = true;
      break;
    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:
      toplevel->activated = true;
      break;
    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN:
      toplevel->fullscreen = true;
      break;
    }
  }
}

static void toplevel_handle_done(
    void *data,
    struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1) {
  struct toplevel *toplevel = data;
  toplevel->done = true;
  draw_all_windows();
}

static void toplevel_handle_closed(
    void *data,
    struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1) {
  struct toplevel *toplevel = data;

  wl_list_remove(&toplevel->link);
  free(toplevel->app_id);
  free(toplevel->title);

  free(toplevel);

  draw_all_windows();
}

static void toplevel_handle_parent(
    void *data,
    struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
    struct zwlr_foreign_toplevel_handle_v1 *parent) {}

struct zwlr_foreign_toplevel_handle_v1_listener toplevel_handle_listener = {
    toplevel_handle_title,        toplevel_handle_app_id,
    toplevel_handle_output_enter, toplevel_handle_output_leave,
    toplevel_handle_state,        toplevel_handle_done,
    toplevel_handle_closed,       toplevel_handle_parent,
};

// Toplevel manager
static void toplevel_manager_toplevel(
    void *data,
    struct zwlr_foreign_toplevel_manager_v1 *zwlr_foreign_toplevel_manager_v1,
    struct zwlr_foreign_toplevel_handle_v1 *toplevel) {
  struct toplevel *t = calloc(1, sizeof(*t));

  t->toplevel = toplevel;

  wl_list_insert(&toplevel_list, &t->link);

  zwlr_foreign_toplevel_handle_v1_add_listener(toplevel,
                                               &toplevel_handle_listener, t);
}

static void toplevel_manager_finished(
    void *data,
    struct zwlr_foreign_toplevel_manager_v1 *zwlr_foreign_toplevel_manager_v1) {
}

static struct zwlr_foreign_toplevel_manager_v1_listener
    toplevel_manager_listener = {
        toplevel_manager_toplevel,
        toplevel_manager_finished,
};

// Registry
static void registry_global(void *data, struct wl_registry *wl_registry,
                            uint32_t name, char const *interface,
                            uint32_t version) {
  if (strcmp(interface, "wl_compositor") == 0) {
    compositor = wl_registry_bind(registry, name, &wl_compositor_interface,
                                  MIN(version, 5));

  } else if (strcmp(interface, "wl_seat") == 0) {
    struct seat *seat = calloc(1, sizeof(*seat));
    seat->seat =
        wl_registry_bind(registry, name, &wl_seat_interface, MIN(version, 5));
    wl_seat_add_listener(seat->seat, &seat_listener, seat);

  } else if (strcmp(interface, "wl_shm") == 0) {
    shm = wl_registry_bind(registry, name, &wl_shm_interface, MIN(version, 1));
    wl_shm_add_listener(shm, &shm_listener, NULL);

  } else if (strcmp(interface, "xdg_wm_base") == 0) {
    xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface,
                                   MIN(version, 4));
    xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, NULL);
  }

  else if (strcmp(interface, "zwlr_foreign_toplevel_manager_v1") == 0) {
    toplevel_manager = wl_registry_bind(
        registry, name, &zwlr_foreign_toplevel_manager_v1_interface,
        MIN(version, 3));

    zwlr_foreign_toplevel_manager_v1_add_listener(
        toplevel_manager, &toplevel_manager_listener, NULL);
  }
}

static void registry_global_remove(void *data, struct wl_registry *wl_registry,
                                   uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove,
};

int main(int argc, char **argv) {
  int ret = 0;
  wl_list_init(&seat_list);
  wl_list_init(&toplevel_list);
  wl_list_init(&window_list);

  struct window window = {0};
  wl_list_init(&window.buffers);
  wl_list_insert(&window_list, &window.link);

  display = wl_display_connect(NULL);
  registry = wl_display_get_registry(display);

  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(display);
  wl_display_roundtrip(display);

  cursor_surface = wl_compositor_create_surface(compositor);
  cursor_theme = wl_cursor_theme_load(NULL, 32, shm);
  cursor_left_ptr = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr");

  window.surface = wl_compositor_create_surface(compositor);
  wl_surface_add_listener(window.surface, &window_surface_listener, &window);

  window.xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, window.surface);
  xdg_surface_add_listener(window.xdg_surface, &window_xdg_surface_listener,
                           &window);

  window.xdg_toplevel = xdg_surface_get_toplevel(window.xdg_surface);
  xdg_toplevel_add_listener(window.xdg_toplevel, &window_xdg_toplevel_listener,
                            &window);
  xdg_toplevel_set_title(window.xdg_toplevel, "Launch Application");
  xdg_toplevel_set_app_id(window.xdg_toplevel,
                          "org.openembedded.xdg-app-chooser");
  xdg_toplevel_set_maximized(window.xdg_toplevel);
  wl_surface_commit(window.surface);

  while (true) {
    if (wl_display_dispatch(display) == -1) {
      perror("Error dispatching display");
      break;
    }
  }

  if (compositor) {
    wl_compositor_destroy(compositor);
  }
  if (shm) {
    wl_shm_destroy(shm);
  }
  wl_registry_destroy(registry);
  wl_display_disconnect(display);
  return ret;
}
