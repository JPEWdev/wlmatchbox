/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#include "output.h"

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>

#include "server.h"
#include "toplevel.h"

DEFINE_TYPE(output)

static void output_frame_notify(struct wl_listener *listener, void *data) {
  struct output *output = get_type_ptr(output, listener, output, frame);
  struct wlr_scene *scene = output->server->scene;

  struct wlr_scene_output *scene_output =
      wlr_scene_get_scene_output(scene, output->wlr_output);

  /* Render the scene if needed and commit the output */
  wlr_scene_output_commit(scene_output, NULL);

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state_notify(struct wl_listener *listener,
                                        void *data) {
  struct output *output = get_type_ptr(output, listener, output, request_state);
  const struct wlr_output_event_request_state *event = data;
  wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy_notify(struct wl_listener *listener, void *data) {
  struct output *output = get_type_ptr(output, listener, output, destroy);
  struct server *server = output->server;

  wl_list_remove(&output->frame.link);
  wl_list_remove(&output->request_state.link);
  wl_list_remove(&output->destroy.link);
  wl_list_remove(&output->link);
  free(output);

  // Unassign any toplevels on this output to another
  struct toplevel *toplevel;
  wl_list_for_each(toplevel, &server->toplevels, link) {
    if (toplevel->output == output) {
      toplevel_assign_any_output(toplevel);
    }
  }
}

void output_create(struct server *server, struct wlr_output *wlr_output) {
  struct output *o = alloc_output();
  o->server = server;
  o->wlr_output = wlr_output;
  wl_list_insert(&server->outputs, &o->link);

  bind_clbk(&o->frame, &wlr_output->events.frame, output_frame_notify);

  bind_clbk(&o->request_state, &wlr_output->events.request_state,
            output_request_state_notify);

  bind_clbk(&o->destroy, &wlr_output->events.destroy, output_destroy_notify);

  // Initialize output
  wlr_output_init_render(wlr_output, server->wlr_allocator,
                         server->wlr_renderer);

  // Configure mode
  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);

  struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
  if (mode) {
    wlr_output_state_set_mode(&state, mode);
  }

  wlr_output_commit_state(wlr_output, &state);
  wlr_output_state_finish(&state);

  // Add output to scene
  struct wlr_output_layout_output *l_output =
      wlr_output_layout_add_auto(server->output_layout, wlr_output);
  struct wlr_scene_output *scene_output =
      wlr_scene_output_create(server->scene, wlr_output);
  wlr_scene_output_layout_add_output(server->scene_layout, l_output,
                                     scene_output);

  // Assign any unassigned toplevels to this output
  struct toplevel *toplevel;
  wl_list_for_each(toplevel, &server->toplevels, link) {
    if (!toplevel->output) {
      toplevel_assign_output(toplevel, o);
    }
  }
}

