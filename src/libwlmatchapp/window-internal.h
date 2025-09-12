/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _WINDOW_INTERNAL
#define _WINDOW_INTERNAL

#include <wlmatchapp/window.h>

void wlm_window_init(struct wlm_display *display, struct wlm_window *window);
void wlm_window_deinit(struct wlm_window *window);

#endif
