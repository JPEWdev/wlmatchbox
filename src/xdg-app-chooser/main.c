/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE

#include <cairo.h>
#include <ctype.h>
#include <gio/gio.h>
#include <glob.h>
#include <linux/input.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

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
static struct wl_list seat_list;
static GList *application_list;

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
  struct wl_surface *surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;

  double cursor_x;
  double cursor_y;

  struct wl_list buffers;
  uint32_t width;
  uint32_t height;
  double y_scroll;
  struct wl_callback *frame;
  bool needs_draw;
  cairo_font_extents_t extents;
};

struct seat {
  struct wl_seat *seat;
  struct {
    struct wl_pointer *pointer;
    struct window *window;
  } pointer;
};

static void draw_window(struct window *window);

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

static void launch_app(GAppInfo *app) {
  GError *error = NULL;
  if (!g_app_info_launch(app, NULL, NULL, &error)) {
    fprintf(stderr, "Unable to launch '%s': %s\n", g_app_info_get_name(app),
            error->message);
  }

  g_clear_error(&error);
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
  cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
  cairo_fill(cr);

  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 20);

  cairo_font_extents(cr, &window->extents);

  double total_height =
      g_list_length(application_list) * window->extents.height;
  double max_y_scroll = total_height - (window->height - MENU_PADDING * 2);
  if (max_y_scroll > 0) {
    window->y_scroll = fmax(window->y_scroll, 0);
    window->y_scroll = fmin(window->y_scroll, max_y_scroll);
  } else {
    window->y_scroll = 0;
  }

  double y = MENU_PADDING - window->y_scroll;
  for (GList *cur = application_list; cur != NULL; cur = cur->next) {
    GAppInfo *app = cur->data;
    if (y + window->extents.height >= 0 && y <= window->height) {
      if (window->cursor_y > y &&
          window->cursor_y <= y + window->extents.height) {
        cairo_set_source_rgb(cr, 0, 1, 1);
      } else {
        cairo_set_source_rgb(cr, 0, 0, 0);
      }
      cairo_move_to(cr, MENU_PADDING, y + window->extents.ascent);
      cairo_show_text(cr, g_app_info_get_name(app));
    }
    y += window->extents.height;
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

  if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_RELEASED &&
      window->extents.height) {
    double menu_y = window->cursor_y + window->y_scroll - MENU_PADDING;
    if (menu_y >= 0) {
      guint menu_idx = menu_y / window->extents.height;
      GAppInfo *app = g_list_nth_data(application_list, menu_idx);
      if (app) {
        launch_app(app);
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
  if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
    window->y_scroll += wl_fixed_to_int(value);
    draw_window(window);
  }
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
}

static void registry_global_remove(void *data, struct wl_registry *wl_registry,
                                   uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove,
};

static void sigchild_handler(int s) {
  int status;
  pid_t pid;

  while (pid = waitpid(-1, &status, WNOHANG), pid > 0)
    fprintf(stderr, "child %d exited\n", pid);
}

static int sort_apps(gconstpointer a, gconstpointer b) {
  GAppInfo *app_a = (GAppInfo *)a;
  GAppInfo *app_b = (GAppInfo *)b;

  return g_strcmp0(g_app_info_get_name(app_a), g_app_info_get_name(app_b));
}

int main(int argc, char **argv) {
  int ret = 0;
  wl_list_init(&seat_list);

  struct window window = {0};
  wl_list_init(&window.buffers);

  GList *app_list = g_app_info_get_all();
  application_list = NULL;
  for (GList *cur = app_list; cur != NULL; cur = cur->next) {
    GAppInfo *app = cur->data;
    if (g_app_info_should_show(app)) {
      application_list = g_list_insert_sorted(application_list, app, sort_apps);
      g_object_ref(app);
    }
  }
  g_list_free_full(app_list, g_object_unref);

  display = wl_display_connect(NULL);
  registry = wl_display_get_registry(display);

  signal(SIGCHLD, sigchild_handler);

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

  g_list_free_full(application_list, g_object_unref);
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
