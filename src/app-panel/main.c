/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE

#include <cairo.h>
#include <glib.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlmatchapp/display.h>
#include <wlmatchapp/seat.h>
#include <wlmatchapp/toplevel.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#define DEFAULT_WIDTH (100)
#define DEFAULT_HEIGHT (100)

static struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager;

struct app {
  struct wl_list link;
  struct zwlr_foreign_toplevel_handle_v1 *handle;
  bool done;
  char *title;
  char *app_id;
  bool minimized;
  bool maximized;
  bool activated;
  bool fullscreen;
  cairo_rectangle_t rect;
};
static struct wl_list app_list;

struct window {
  struct wl_list link;
  struct wlm_toplevel *toplevel;
  cairo_font_extents_t extents;
};
static struct wl_list window_list;

static void draw_all_windows(void) {
  struct window *window;
  wl_list_for_each(window, &window_list, link) {
    wlm_window_schedule_redraw(&window->toplevel->base);
  }
}

static void window_configure(struct wlm_window *window, uint32_t serial) {
  struct window *w = wlm_window_get_user_data(window);
  struct wlm_toplevel *toplevel = w->toplevel;

  uint32_t width = DEFAULT_WIDTH;
  uint32_t height = DEFAULT_HEIGHT;

  if (toplevel->configure.width) {
    width = toplevel->configure.width;
  }
  if (toplevel->configure.height) {
    height = toplevel->configure.height;
  }

  wlm_window_set_size(window, width, height);
}

static void window_draw(struct wlm_window *window, cairo_t *cr) {
  struct window *w = wlm_window_get_user_data(window);

  cairo_rectangle(cr, 0, 0, window->width, window->height);
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_fill(cr);

  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 20);

  cairo_font_extents(cr, &w->extents);

  int count = 0;
  struct app *app;
  wl_list_for_each(app, &app_list, link) {
    if (!app->done) {
      continue;
    }
    count++;
  }

  if (count) {
    int item_width = window->width / count;

    int i = 0;
    wl_list_for_each(app, &app_list, link) {
      if (!app->done) {
        app->rect.width = 0;
        app->rect.height = 0;
        continue;
      }
      cairo_save(cr);

      if (app->activated) {
        cairo_set_source_rgb(cr, 0, 1, 0);
      } else {
        cairo_set_source_rgb(cr, 0, 0, 0);
      }
      app->rect.x = i * item_width;
      app->rect.y = 0;
      app->rect.width = item_width;
      app->rect.height = window->height;

      cairo_rectangle(cr, app->rect.x, app->rect.y, app->rect.width,
                      app->rect.height);
      cairo_clip(cr);

      cairo_move_to(cr, app->rect.x,
                    app->rect.y + app->rect.height / 2 + w->extents.ascent / 2);
      cairo_show_text(cr, app->title);
      cairo_restore(cr);
      i++;
    }
  }
}

static void window_pointer_enter(struct wlm_window *window,
                                 struct wlm_seat *seat, uint32_t serial) {
  wlm_seat_set_theme_cursor(seat, serial, NULL, 0, "left_ptr");
}

static void window_pointer_button(struct wlm_window *window,
                                  struct wlm_seat *seat, uint32_t serial,
                                  uint32_t time, uint32_t button,
                                  uint32_t state) {
  if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_RELEASED) {
    struct app *app;
    wl_list_for_each(app, &app_list, link) {
      if (!app->rect.width && !app->rect.height) {
        continue;
      }
      if (window->pointer.x >= app->rect.x &&
          window->pointer.y >= app->rect.y &&
          window->pointer.x < app->rect.x + app->rect.width &&
          window->pointer.y < app->rect.y + app->rect.height) {
        zwlr_foreign_toplevel_handle_v1_activate(app->handle, seat->seat);
        break;
      }
    }
  }
}

// Toplevel handle
static void toplevel_handle_title(
    void *data,
    struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
    const char *title) {
  struct app *app = data;
  free(app->title);
  app->title = strdup(title);
}

static void toplevel_handle_app_id(
    void *data,
    struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1,
    const char *app_id) {
  struct app *app = data;
  free(app->app_id);
  app->app_id = strdup(app_id);
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
  struct app *app = data;
  app->minimized = false;
  app->maximized = false;
  app->activated = false;
  app->fullscreen = false;
  enum zwlr_foreign_toplevel_handle_v1_state *s;
  wl_array_for_each(s, state) {
    switch (*s) {
    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:
      app->maximized = true;
      break;
    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:
      app->minimized = true;
      break;
    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:
      app->activated = true;
      break;
    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN:
      app->fullscreen = true;
      break;
    }
  }
}

static void toplevel_handle_done(
    void *data,
    struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1) {
  struct app *app = data;
  app->done = true;
  draw_all_windows();
}

static void toplevel_handle_closed(
    void *data,
    struct zwlr_foreign_toplevel_handle_v1 *zwlr_foreign_toplevel_handle_v1) {
  struct app *app = data;

  wl_list_remove(&app->link);
  free(app->app_id);
  free(app->title);

  free(app);

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
    struct zwlr_foreign_toplevel_handle_v1 *handle) {
  struct app *app = calloc(1, sizeof(*app));

  app->handle = handle;

  wl_list_insert(&app_list, &app->link);

  zwlr_foreign_toplevel_handle_v1_add_listener(handle,
                                               &toplevel_handle_listener, app);
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

static void on_global(struct wlm_display *display, uint32_t name,
                      char const *interface, uint32_t version) {
  if (strcmp(interface, "zwlr_foreign_toplevel_manager_v1") == 0) {
    toplevel_manager = wl_registry_bind(
        display->registry, name, &zwlr_foreign_toplevel_manager_v1_interface,
        MIN(version, 3));

    zwlr_foreign_toplevel_manager_v1_add_listener(
        toplevel_manager, &toplevel_manager_listener, NULL);
  }
}

int main(int argc, char **argv) {
  int ret = 0;
  struct wlm_display *display = wlm_display_create();
  wl_list_init(&app_list);
  wl_list_init(&window_list);
  display->on_global = on_global;

  if (!wlm_display_connect(display, NULL)) {
    fprintf(stderr, "Unable to connect to display\n");
    return 1;
  }

  struct window *w = calloc(1, sizeof(*w));
  struct wlm_toplevel *window = wlm_toplevel_create(display);
  wlm_toplevel_set_user_data(window, w);
  w->toplevel = window;
  wl_list_insert(&window_list, &w->link);

  wlm_toplevel_set_app_id(window, "org.openembedded.matchbox-app-panel");
  wlm_toplevel_set_title(window, "Application Panel");

  window->base.on_draw = window_draw;
  window->base.on_configure = window_configure;
  window->base.pointer.on_button = window_pointer_button;
  window->base.pointer.on_enter = window_pointer_enter;

  while (true) {
    if (wlm_display_dispatch(display) == -1) {
      perror("Error dispatching display");
      break;
    }
  }

  wlm_toplevel_destroy(window);
  free(w);
  wlm_display_destroy(display);
  return ret;
}
