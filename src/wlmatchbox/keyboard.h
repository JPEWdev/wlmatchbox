/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <wayland-server.h>

#include "util.h"

struct wlr_input_device;

struct keyboard {
  struct wl_list link;
  struct server *server;
  struct wlr_keyboard *wlr_keyboard;

  struct wl_listener modifiers;
  struct wl_listener key;
  struct wl_listener destroy;

  struct keyboard_sig const *sig;
};
DECLARE_TYPE(keyboard)

void keyboard_create(struct server *server, struct wlr_input_device* device);

#endif
