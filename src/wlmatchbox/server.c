/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#include "server.h"

#include <sys/socket.h>
#include <unistd.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "keyboard.h"
#include "output.h"
#include "popup.h"
#include "toplevel.h"

DEFINE_TYPE(server)

static void new_output_notify(struct wl_listener *listener, void *data) {
  struct server *server = get_type_ptr(server, listener, server, new_output);
  struct wlr_output *wlr_output = data;

  output_create(server, wlr_output);
}

// Seat handling
static void seat_request_cursor(struct wl_listener *listener, void *data) {
  struct server *server =
      get_type_ptr(server, listener, server, request_cursor);
  struct wlr_seat_pointer_request_set_cursor_event *event = data;

  struct wlr_seat_client *focused_client =
      server->seat->pointer_state.focused_client;
  if (focused_client == event->seat_client) {
    wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x,
                           event->hotspot_y);
  }
}

static void seat_pointer_focus_change(struct wl_listener *listener,
                                      void *data) {
  struct server *server =
      get_type_ptr(server, listener, server, pointer_focus_change);
  struct wlr_seat_pointer_focus_change_event *event = data;
  if (event->new_surface == NULL) {
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
  }
}

static void seat_request_set_selection(struct wl_listener *listener,
                                       void *data) {
  struct server *server =
      get_type_ptr(server, listener, server, request_set_selection);
  struct wlr_seat_request_set_selection_event *event = data;
  wlr_seat_set_selection(server->seat, event->source, event->serial);
}

// Input handling
static void server_new_input(struct wl_listener *listener, void *data) {
  struct server *server = get_type_ptr(server, listener, server, new_input);
  struct wlr_input_device *device = data;
  switch (device->type) {
  case WLR_INPUT_DEVICE_KEYBOARD:
    keyboard_create(server, device);
    break;
  case WLR_INPUT_DEVICE_POINTER:
    wlr_cursor_attach_input_device(server->cursor, device);
    break;
  default:
    break;
  }

  // Set the seat capabilities. A pointer is always available
  uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
  if (!wl_list_empty(&server->keyboards)) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  wlr_seat_set_capabilities(server->seat, caps);
}

// Cursor Handling
static void process_cursor_motion(struct server *server, uint32_t time) {
  double sx, sy;
  struct wlr_seat *seat = server->seat;
  struct wlr_surface *surface = NULL;
  struct toplevel *toplevel = toplevel_at(
      server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
  if (!toplevel) {
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
  }
  if (surface) {
    /*
     * Send pointer enter and motion events.
     *
     * The enter event gives the surface "pointer focus", which is distinct
     * from keyboard focus. You get pointer focus by moving the pointer over
     * a window.
     *
     * Note that wlroots will avoid sending duplicate enter/motion events if
     * the surface has already has pointer focus or if the client is already
     * aware of the coordinates passed.
     */
    wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(seat, time, sx, sy);
  } else {
    /* Clear pointer focus so future button events and such are not sent to
     * the last client to have the cursor over it. */
    wlr_seat_pointer_clear_focus(seat);
  }
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
  struct server *server = get_type_ptr(server, listener, server, cursor_motion);
  struct wlr_pointer_motion_event *event = data;
  wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x,
                  event->delta_y);
  process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener *listener,
                                          void *data) {
  struct server *server =
      get_type_ptr(server, listener, server, cursor_motion_absolute);
  struct wlr_pointer_motion_absolute_event *event = data;
  wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
                           event->y);
  process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
  struct server *server = get_type_ptr(server, listener, server, cursor_button);
  struct wlr_pointer_button_event *event = data;
  /* Notify the client with pointer focus that a button press has occurred */
  wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button,
                                 event->state);

  if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct toplevel *toplevel = toplevel_at(
        server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    toplevel_focus(toplevel);
  }
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
  struct server *server = get_type_ptr(server, listener, server, cursor_axis);
  struct wlr_pointer_axis_event *event = data;
  wlr_seat_pointer_notify_axis(
      server->seat, event->time_msec, event->orientation, event->delta,
      event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
  struct server *server = get_type_ptr(server, listener, server, cursor_frame);
  wlr_seat_pointer_notify_frame(server->seat);
}

// XWayland
static void new_xwayland_surface_notify(struct wl_listener *listener,
                                        void *data) {}

// XDG Handling
static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
  struct server *server =
      get_type_ptr(server, listener, server, new_xdg_toplevel);
  struct wlr_xdg_toplevel *xdg_toplevel = data;

  toplevel_create(server, xdg_toplevel);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
  struct server *server =
      get_type_ptr(server, listener, server, new_xdg_popup);
  struct wlr_xdg_popup *xdg_popup = data;

  popup_create(server, xdg_popup);
}

static struct wl_client *exec_client(struct server *server,
                                     char const *program) {
  int socks[2];
  if (socketpair(AF_UNIX, SOCK_DGRAM, 0, socks)) {
    fprintf(stderr, "Unable to create socket pair: %s\n", strerror(errno));
    return NULL;
  }
  int ret = fork();
  if (ret == 0) {
    char sock_buf[512];
    close(socks[1]);
    snprintf(sock_buf, sizeof(sock_buf), "%d", socks[0]);
    setenv("WAYLAND_SOCKET", sock_buf, 1);
    execlp(program, program, NULL);
    _exit(EXIT_FAILURE);
  } else if (ret > 0) {
    close(socks[0]);

    struct wl_client *client = wl_client_create(server->wl_display, socks[1]);
    if (!client) {
      close(socks[1]);
    }
    return client;

  } else {
    fprintf(stderr, "Unable to fork: %s\n", strerror(errno));
    close(socks[0]);
    close(socks[1]);
    return NULL;
  }
}

static void on_panel_client_destroy(struct wl_listener *listener, void *data) {
  struct server *server =
      get_type_ptr(server, listener, server, panel_client_destroy);

  wlr_log(WLR_DEBUG, "Panel client exited");

  wl_list_remove(&server->panel_client_destroy.link);
  server->panel_client = NULL;
}

static bool global_filter(struct wl_client const *client,
                          struct wl_global const *global, void *data) {
  struct server *server = data;
  if (global == server->foreign_toplevel_manager->global) {
    return client == server->panel_client;
  }
  return true;
}

bool server_handle_keybinding(struct server *server, xkb_keysym_t sym) {
  /*
   * This function assumes Alt is held down.
   */
  switch (sym) {
  case XKB_KEY_Tab:
    /* Cycle to the next toplevel */
    if (wl_list_length(&server->toplevels) < 2) {
      break;
    }
    struct toplevel *next_toplevel =
        wl_container_of(server->toplevels.prev, next_toplevel, link);
    toplevel_focus(next_toplevel);
    break;

  default:
    return false;
  }
  return true;
}

void server_create_panel(struct server *server, char const *program) {
  if (server->panel_client) {
    return;
  }

  server->panel_client = exec_client(server, program);
  if (server->panel_client) {
    server->panel_client_destroy.notify = on_panel_client_destroy;
    wl_client_add_destroy_listener(server->panel_client,
                                   &server->panel_client_destroy);
  }
}

struct server *server_create(void) {
  struct server *server = alloc_server();
  wl_list_init(&server->outputs);
  wl_list_init(&server->keyboards);
  wl_list_init(&server->toplevels);

  server->wl_display = wl_display_create();
  wl_display_set_global_filter(server->wl_display, global_filter, server);

  server->wlr_backend = wlr_backend_autocreate(
      wl_display_get_event_loop(server->wl_display), NULL);
  if (server->wlr_backend == NULL) {
    wlr_log(WLR_ERROR, "failed to create wlr_backend");
    return NULL;
  }

  server->wlr_renderer = wlr_renderer_autocreate(server->wlr_backend);
  if (server->wlr_renderer == NULL) {
    wlr_log(WLR_ERROR, "failed to create wlr_renderer");
    return NULL;
  }

  wlr_renderer_init_wl_display(server->wlr_renderer, server->wl_display);

  server->wlr_allocator =
      wlr_allocator_autocreate(server->wlr_backend, server->wlr_renderer);
  if (server->wlr_allocator == NULL) {
    wlr_log(WLR_ERROR, "failed to create wlr_allocator");
    return NULL;
  }

  wlr_compositor_create(server->wl_display, 5, server->wlr_renderer);
  wlr_subcompositor_create(server->wl_display);
  wlr_data_device_manager_create(server->wl_display);

  // Layout
  server->output_layout = wlr_output_layout_create(server->wl_display);
  server->scene = wlr_scene_create();
  server->scene_layout =
      wlr_scene_attach_output_layout(server->scene, server->output_layout);

  // Outputs
  bind_clbk(&server->new_output, &server->wlr_backend->events.new_output,
            new_output_notify);

  // Seat
  server->seat = wlr_seat_create(server->wl_display, "seat0");
  bind_clbk(&server->request_cursor, &server->seat->events.request_set_cursor,
            seat_request_cursor);

  bind_clbk(&server->pointer_focus_change,
            &server->seat->pointer_state.events.focus_change,
            seat_pointer_focus_change);

  bind_clbk(&server->request_set_selection,
            &server->seat->events.request_set_selection,
            seat_request_set_selection);

  // Cursor
  server->cursor = wlr_cursor_create();
  wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
  server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

  bind_clbk(&server->cursor_motion, &server->cursor->events.motion,
            server_cursor_motion);

  bind_clbk(&server->cursor_motion_absolute,
            &server->cursor->events.motion_absolute,
            server_cursor_motion_absolute);

  bind_clbk(&server->cursor_button, &server->cursor->events.button,
            server_cursor_button);

  bind_clbk(&server->cursor_axis, &server->cursor->events.axis,
            server_cursor_axis);

  bind_clbk(&server->cursor_frame, &server->cursor->events.frame,
            server_cursor_frame);

  // Keyboard
  server->new_input.notify = server_new_input;
  wl_signal_add(&server->wlr_backend->events.new_input, &server->new_input);

  // XWayland
  server->xwayland =
      wlr_xwayland_create(server->wl_display, server->wlr_compositor, true);
  bind_clbk(&server->new_xwayland_surface,
            &server->xwayland->events.new_surface, new_xwayland_surface_notify);

  // XDG shell
  server->xdg_shell = wlr_xdg_shell_create(server->wl_display, 3);
  bind_clbk(&server->new_xdg_toplevel, &server->xdg_shell->events.new_toplevel,
            server_new_xdg_toplevel);
  bind_clbk(&server->new_xdg_popup, &server->xdg_shell->events.new_popup,
            server_new_xdg_popup);

  // Foreign toplevel
  server->foreign_toplevel_manager =
      wlr_foreign_toplevel_manager_v1_create(server->wl_display);

  return server;
}
