/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#include "toplevel.h"

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "output.h"
#include "server.h"

DEFINE_TYPE(toplevel)

static bool is_panel(struct toplevel *toplevel) {
  return wl_resource_get_client(toplevel->xdg_toplevel->resource) ==
         toplevel->server->panel_client;
}

static struct toplevel *
toplevel_try_from_wlr_surface(struct server *server,
                              struct wlr_surface *surface) {
  struct wlr_xdg_toplevel *xdg_toplevel =
      wlr_xdg_toplevel_try_from_wlr_surface(surface);
  if (!xdg_toplevel) {
    return NULL;
  }
  struct toplevel *toplevel;
  wl_list_for_each(toplevel, &server->toplevels, link) {
    if (toplevel->xdg_toplevel == xdg_toplevel) {
      return toplevel;
    }
  }

  return NULL;
}

static void toplevel_configure(struct toplevel *toplevel) {
  int32_t req_width = 0;
  int32_t req_height = 0;
  int32_t x = 0;
  int32_t y = 0;

  if (is_panel(toplevel)) {
    if (toplevel->output) {
      req_width = toplevel->output->wlr_output->width - 1;
    }
  } else {
    if (toplevel->output) {
      req_width = toplevel->output->wlr_output->width - 1;
      req_height = toplevel->output->wlr_output->height - 1;
      if (toplevel->output->panel) {
        struct wlr_box box;
        wlr_surface_get_extents(
            toplevel->output->panel->xdg_toplevel->base->surface, &box);
        y = box.height;
        req_height -= box.height;
      }
    }
    wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
  }

  wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, req_width, req_height);

  if (toplevel->foreign.handle) {
    wlr_foreign_toplevel_handle_v1_set_maximized(toplevel->foreign.handle,
                                                 true);
  }

  if (toplevel->output) {
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
        toplevel->server->scene, toplevel->output->wlr_output);
    if (scene_output) {
      wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                  scene_output->x + x, scene_output->y + y);
    }
  }
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
  struct toplevel *toplevel = get_type_ptr(toplevel, listener, toplevel, map);
  wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

  toplevel_focus(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
  struct toplevel *toplevel = get_type_ptr(toplevel, listener, toplevel, unmap);
  wl_list_remove(&toplevel->link);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
  struct toplevel *toplevel =
      get_type_ptr(toplevel, listener, toplevel, commit);

  if (toplevel->xdg_toplevel->base->initial_commit) {
    toplevel_configure(toplevel);
  }
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
  /* Called when the xdg_toplevel is destroyed. */
  struct toplevel *toplevel =
      get_type_ptr(toplevel, listener, toplevel, destroy);

  if (toplevel->foreign.handle) {
    wlr_foreign_toplevel_handle_v1_destroy(toplevel->foreign.handle);
  }

  wl_list_remove(&toplevel->map.link);
  wl_list_remove(&toplevel->unmap.link);
  wl_list_remove(&toplevel->commit.link);
  wl_list_remove(&toplevel->destroy.link);
  // wl_list_remove(&toplevel->request_move.link);
  // wl_list_remove(&toplevel->request_resize.link);
  wl_list_remove(&toplevel->request_maximize.link);
  wl_list_remove(&toplevel->request_fullscreen.link);
  wl_list_remove(&toplevel->set_app_id.link);
  wl_list_remove(&toplevel->set_title.link);

  free(toplevel);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener,
                                          void *data) {
  struct toplevel *toplevel =
      get_type_ptr(toplevel, listener, toplevel, request_maximize);
  if (toplevel->xdg_toplevel->base->initialized) {
    wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
  }
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener,
                                            void *data) {
  struct toplevel *toplevel =
      get_type_ptr(toplevel, listener, toplevel, request_fullscreen);
  if (toplevel->xdg_toplevel->base->initialized) {
    wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
  }
}

static void xdg_toplevel_set_app_id(struct wl_listener *listener, void *data) {
  struct toplevel *toplevel =
      get_type_ptr(toplevel, listener, toplevel, set_app_id);

  if (toplevel->foreign.handle) {
    wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign.handle,
                                              toplevel->xdg_toplevel->app_id);
  }
}

static void xdg_toplevel_set_title(struct wl_listener *listener, void *data) {
  struct toplevel *toplevel =
      get_type_ptr(toplevel, listener, toplevel, set_title);
  if (toplevel->foreign.handle) {
    wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign.handle,
                                             toplevel->xdg_toplevel->title);
  }
}

static void toplevel_foreign_request_maximize(struct wl_listener *listener,
                                              void *data) {
  struct toplevel *toplevel =
      get_type_ptr(toplevel, listener, toplevel, foreign.request_maximize);
}

static void toplevel_foreign_request_minimize(struct wl_listener *listener,
                                              void *data) {
  struct toplevel *toplevel =
      get_type_ptr(toplevel, listener, toplevel, foreign.request_minimize);
}

static void toplevel_foreign_request_activate(struct wl_listener *listener,
                                              void *data) {
  struct toplevel *toplevel =
      get_type_ptr(toplevel, listener, toplevel, foreign.request_activate);
  toplevel_focus(toplevel);
}

static void toplevel_foreign_request_fullscreen(struct wl_listener *listener,
                                                void *data) {
  struct toplevel *toplevel =
      get_type_ptr(toplevel, listener, toplevel, foreign.request_fullscreen);
}

static void toplevel_foreign_request_close(struct wl_listener *listener,
                                           void *data) {
  struct toplevel *toplevel =
      get_type_ptr(toplevel, listener, toplevel, foreign.request_close);
}

static void toplevel_foreign_destroy(struct wl_listener *listener, void *data) {
  struct toplevel *toplevel =
      get_type_ptr(toplevel, listener, toplevel, foreign.destroy);
  wl_list_remove(&toplevel->foreign.request_maximize.link);
  wl_list_remove(&toplevel->foreign.request_minimize.link);
  wl_list_remove(&toplevel->foreign.request_activate.link);
  wl_list_remove(&toplevel->foreign.request_fullscreen.link);
  wl_list_remove(&toplevel->foreign.request_close.link);
  wl_list_remove(&toplevel->foreign.destroy.link);
  toplevel->foreign.handle = NULL;
}

void toplevel_create(struct server *server,
                     struct wlr_xdg_toplevel *xdg_toplevel) {
  struct toplevel *toplevel = alloc_toplevel();
  toplevel->server = server;
  toplevel->xdg_toplevel = xdg_toplevel;
  toplevel->scene_tree = wlr_scene_xdg_surface_create(
      &toplevel->server->scene->tree, xdg_toplevel->base);
  toplevel->scene_tree->node.data = toplevel;

  // The data pointer must be set to the scene tree for popups to
  // work
  xdg_toplevel->base->data = toplevel->scene_tree;

  bind_clbk(&toplevel->map, &xdg_toplevel->base->surface->events.map,
            xdg_toplevel_map);
  bind_clbk(&toplevel->unmap, &xdg_toplevel->base->surface->events.unmap,
            xdg_toplevel_unmap);
  bind_clbk(&toplevel->commit, &xdg_toplevel->base->surface->events.commit,
            xdg_toplevel_commit);
  bind_clbk(&toplevel->destroy, &xdg_toplevel->events.destroy,
            xdg_toplevel_destroy);
  bind_clbk(&toplevel->set_app_id, &xdg_toplevel->events.set_app_id,
            xdg_toplevel_set_app_id);
  bind_clbk(&toplevel->set_title, &xdg_toplevel->events.set_title,
            xdg_toplevel_set_title);

  bind_clbk(&toplevel->request_maximize, &xdg_toplevel->events.request_maximize,
            xdg_toplevel_request_maximize);
  bind_clbk(&toplevel->request_fullscreen,
            &xdg_toplevel->events.request_fullscreen,
            xdg_toplevel_request_fullscreen);
  // toplevel->request_move.notify = xdg_toplevel_request_move;
  // wl_signal_add(&xdg_toplevel->events.request_move,
  // &toplevel->request_move); toplevel->request_resize.notify =
  // xdg_toplevel_request_resize;
  // wl_signal_add(&xdg_toplevel->events.request_resize,
  //               &toplevel->request_resize);
  toplevel_assign_any_output(toplevel);

  if (!is_panel(toplevel)) {
    toplevel->foreign.handle =
        wlr_foreign_toplevel_handle_v1_create(server->foreign_toplevel_manager);
    bind_clbk(&toplevel->foreign.request_maximize,
              &toplevel->foreign.handle->events.request_maximize,
              toplevel_foreign_request_maximize);
    bind_clbk(&toplevel->foreign.request_minimize,
              &toplevel->foreign.handle->events.request_minimize,
              toplevel_foreign_request_minimize);
    bind_clbk(&toplevel->foreign.request_activate,
              &toplevel->foreign.handle->events.request_activate,
              toplevel_foreign_request_activate);
    bind_clbk(&toplevel->foreign.request_fullscreen,
              &toplevel->foreign.handle->events.request_fullscreen,
              toplevel_foreign_request_fullscreen);
    bind_clbk(&toplevel->foreign.request_close,
              &toplevel->foreign.handle->events.request_close,
              toplevel_foreign_request_close);
    bind_clbk(&toplevel->foreign.destroy,
              &toplevel->foreign.handle->events.destroy,
              toplevel_foreign_destroy);
  }
}

void toplevel_assign_output(struct toplevel *toplevel, struct output *output) {
  if (is_panel(toplevel)) {
    if (toplevel->output) {
      toplevel->output->panel = NULL;
    }
    output->panel = toplevel;
  }
  toplevel->output = output;
  if (toplevel->xdg_toplevel->base->initialized) {
    toplevel_configure(toplevel);
    wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
  }
}

void toplevel_assign_any_output(struct toplevel *toplevel) {
  if (is_panel(toplevel) && toplevel->output) {
    toplevel->output->panel = NULL;
  }
  toplevel->output = NULL;
  // Assign to oldest output
  struct output *output;
  wl_list_for_each_reverse(output, &toplevel->server->outputs, link) {
    toplevel_assign_output(toplevel, output);
    break;
  }
}

void toplevel_focus(struct toplevel *toplevel) {
  /* Note: this function only deals with keyboard focus. */
  if (toplevel == NULL) {
    return;
  }
  struct server *server = toplevel->server;
  struct wlr_seat *seat = server->seat;
  struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
  struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
  if (prev_surface == surface) {
    /* Don't re-focus an already focused surface. */
    return;
  }
  if (prev_surface) {
    /*
     * Deactivate the previously focused surface. This lets the
     * client know it no longer has focus and the client will
     * repaint accordingly, e.g. stop displaying a caret.
     */
    struct toplevel *prev_toplevel =
        toplevel_try_from_wlr_surface(server, prev_surface);
    if (prev_toplevel != NULL) {
      wlr_xdg_toplevel_set_activated(prev_toplevel->xdg_toplevel, false);
      if (prev_toplevel->foreign.handle) {
        wlr_foreign_toplevel_handle_v1_set_activated(
            prev_toplevel->foreign.handle, false);
      }
    }
  }
  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
  /* Move the toplevel to the front */
  wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
  wl_list_remove(&toplevel->link);
  wl_list_insert(&server->toplevels, &toplevel->link);
  /* Activate the new surface */
  wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
  if (toplevel->foreign.handle) {
    wlr_foreign_toplevel_handle_v1_set_activated(toplevel->foreign.handle,
                                                 true);
  }
  /*
   * Tell the seat to have the keyboard enter this surface. wlroots
   * will keep track of this and automatically send key events to
   * the appropriate clients without additional work on your part.
   */
  if (keyboard != NULL) {
    wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
                                   keyboard->num_keycodes,
                                   &keyboard->modifiers);
  }
}

struct toplevel *toplevel_at(struct server *server, double lx, double ly,
                             struct wlr_surface **surface, double *sx,
                             double *sy) {
  struct wlr_scene_node *node =
      wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
  if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
    return NULL;
  }
  struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
  struct wlr_scene_surface *scene_surface =
      wlr_scene_surface_try_from_buffer(scene_buffer);
  if (!scene_surface) {
    return NULL;
  }

  *surface = scene_surface->surface;

  struct wlr_scene_tree *tree = node->parent;
  while (tree != NULL && tree->node.data == NULL) {
    tree = tree->node.parent;
  }
  return tree->node.data;
}

