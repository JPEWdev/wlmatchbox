/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _SEAT_INTERNAL_H
#define _SEAT_INTERNAL_H

struct wlm_display;
struct wlm_seat;
struct wl_seat;

struct wlm_seat *wlm_seat_create(struct wlm_display *display,
                                 struct wl_seat *wl_seat);
void wlm_seat_destroy(struct wlm_seat *seat);

#endif
