/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE

#include <cairo.h>
#include <gio/gio.h>
#include <linux/input.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <wayland-client.h>
#include <wlmatchapp/display.h>
#include <wlmatchapp/seat.h>
#include <wlmatchapp/toplevel.h>

#define DEFAULT_WIDTH (100)
#define DEFAULT_HEIGHT (100)

#define MENU_PADDING (10)

static GList *application_list;

struct window_data {
  struct wlm_toplevel *toplevel;
  double y_scroll;
  cairo_font_extents_t extents;
};

static void launch_app(GAppInfo *app) {
  GError *error = NULL;
  if (!g_app_info_launch(app, NULL, NULL, &error)) {
    fprintf(stderr, "Unable to launch '%s': %s\n", g_app_info_get_name(app),
            error->message);
  }

  g_clear_error(&error);
}

static void window_configure(struct wlm_window *window, uint32_t serial) {
  struct window_data *d = wlm_window_get_user_data(window);
  struct wlm_toplevel *toplevel = d->toplevel;

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
  struct window_data *d = wlm_window_get_user_data(window);

  cairo_rectangle(cr, 0, 0, window->width, window->height);
  cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
  cairo_fill(cr);

  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 20);

  cairo_font_extents(cr, &d->extents);

  double total_height = g_list_length(application_list) * d->extents.height;
  double max_y_scroll = total_height - (window->height - MENU_PADDING * 2);
  if (max_y_scroll > 0) {
    d->y_scroll = fmax(d->y_scroll, 0);
    d->y_scroll = fmin(d->y_scroll, max_y_scroll);
  } else {
    d->y_scroll = 0;
  }

  double y = MENU_PADDING - d->y_scroll;
  for (GList *cur = application_list; cur != NULL; cur = cur->next) {
    GAppInfo *app = cur->data;
    if (y + d->extents.height >= 0 && y <= window->height) {
      if (window->pointer.y > y && window->pointer.y <= y + d->extents.height) {
        cairo_set_source_rgb(cr, 0, 1, 1);
      } else {
        cairo_set_source_rgb(cr, 0, 0, 0);
      }
      cairo_move_to(cr, MENU_PADDING, y + d->extents.ascent);
      cairo_show_text(cr, g_app_info_get_name(app));
    }
    y += d->extents.height;
  }
}

// Pointer
static void window_pointer_enter(struct wlm_window *window,
                                 struct wlm_seat *seat, uint32_t serial) {
  wlm_seat_set_theme_cursor(seat, serial, NULL, 0, "left_ptr");
  wlm_window_schedule_redraw(window);
}

static void window_pointer_leave(struct wlm_window *window,
                                 struct wlm_seat *seat, uint32_t serial) {
  wlm_window_schedule_redraw(window);
}

static void window_pointer_move(struct wlm_window *window,
                                struct wlm_seat *seat, uint32_t time) {
  wlm_window_schedule_redraw(window);
}

static void window_pointer_button(struct wlm_window *window,
                                  struct wlm_seat *seat, uint32_t serial,
                                  uint32_t time, uint32_t button,
                                  uint32_t state) {
  struct window_data *d = wlm_window_get_user_data(window);

  if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_RELEASED &&
      d->extents.height) {
    double menu_y = window->pointer.y + d->y_scroll - MENU_PADDING;
    if (menu_y >= 0) {
      guint menu_idx = menu_y / d->extents.height;
      GAppInfo *app = g_list_nth_data(application_list, menu_idx);
      if (app) {
        launch_app(app);
      }
    }
  }
}

static void window_pointer_axis(struct wlm_window *window,
                                struct wlm_seat *seat, uint32_t time,
                                uint32_t axis, double value) {
  struct window_data *d = wlm_window_get_user_data(window);
  if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
    d->y_scroll += value;
    wlm_window_schedule_redraw(window);
  }
}

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

static GList *get_application_list(void) {
  GList *app_list = g_app_info_get_all();
  GList *result = NULL;
  for (GList *cur = app_list; cur != NULL; cur = cur->next) {
    GAppInfo *app = cur->data;
    if (g_app_info_should_show(app)) {
      result = g_list_insert_sorted(result, app, sort_apps);
      g_object_ref(app);
    }
  }
  g_list_free_full(app_list, g_object_unref);
  return result;
}

int main(int argc, char **argv) {
  int ret = 0;
  struct wlm_display *display = wlm_display_create();
  if (!wlm_display_connect(display, NULL)) {
    fprintf(stderr, "Unable to connect to display\n");
    return 1;
  }

  application_list = get_application_list();

  signal(SIGCHLD, sigchild_handler);

  struct window_data window_data = {};
  struct wlm_toplevel *window = wlm_toplevel_create(display);
  window_data.toplevel = window;
  wlm_toplevel_set_user_data(window, &window_data);

  wlm_toplevel_set_app_id(window, "org.openembedded.xdg-app-chooser");
  wlm_toplevel_set_title(window, "Launch Application");
  wlm_toplevel_set_maximized(window, true);

  window->base.on_draw = window_draw;
  window->base.on_configure = window_configure;
  window->base.pointer.on_move = window_pointer_move;
  window->base.pointer.on_button = window_pointer_button;
  window->base.pointer.on_enter = window_pointer_enter;
  window->base.pointer.on_leave = window_pointer_leave;
  window->base.pointer.on_axis = window_pointer_axis;

  while (true) {
    if (wlm_display_dispatch(display) == -1) {
      perror("Error dispatching display");
      break;
    }
  }

  g_list_free_full(application_list, g_object_unref);
  wlm_toplevel_destroy(window);
  wlm_display_destroy(display);
  return ret;
}
