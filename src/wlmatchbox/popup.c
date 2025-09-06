/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#include "popup.h"

#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

DEFINE_TYPE(popup)

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
  /* Called when a new surface state is committed. */
  struct popup *popup = get_type_ptr(popup, listener, popup, commit);

  if (popup->xdg_popup->base->initial_commit) {
    wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
  }
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
  /* Called when the xdg_popup is destroyed. */
  struct popup *popup = get_type_ptr(popup, listener, popup, destroy);

  wl_list_remove(&popup->commit.link);
  wl_list_remove(&popup->destroy.link);

  free(popup);
}

void popup_create(struct server *server, struct wlr_xdg_popup *xdg_popup) {
  struct popup *popup = alloc_popup();
  popup->server = server;
  popup->xdg_popup = xdg_popup;

  // XDG surfaces must set their data point to their scene tree so that they
  // the popup can find it
  struct wlr_xdg_surface *parent =
      wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
  if (!parent) {
    return;
  }
  struct wlr_scene_tree *parent_tree = parent->data;
  xdg_popup->base->data =
      wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

  bind_clbk(&popup->commit, &xdg_popup->base->surface->events.commit,
            xdg_popup_commit);

  bind_clbk(&popup->destroy, &xdg_popup->events.destroy, xdg_popup_destroy);
}
