/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _WLMATCHAPP_DISPLAY
#define _WLMATCHAPP_DISPLAY

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client-core.h>
#include <wlmatchapp/symbols.h>

struct wlm_display {
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_shm *shm;
  struct xdg_wm_base *xdg_wm_base;

  void (*on_global)(struct wlm_display *display, uint32_t name,
                    char const *interface, uint32_t version);
  void (*on_global_remove)(struct wlm_display *display, uint32_t name);

  void *userdata;

  bool needs_roundtrip;

  struct {
    struct wl_list seat_list;
    struct wl_list shm_format_list;
    struct wl_list window_list;
    struct wl_list toplevel_list;
    struct wl_list cursor_theme_list;
  } private;
};

WLM_API struct wlm_display *wlm_display_create(void);
WLM_API bool wlm_display_connect(struct wlm_display *display, char const *name);
WLM_API void wlm_display_destroy(struct wlm_display *display);
WLM_API int wlm_display_dispatch(struct wlm_display *display);

WLM_API struct wl_cursor *wlm_display_get_cursor(struct wlm_display *display,
                                                 char const *theme_name,
                                                 uint32_t bpp,
                                                 char const *name);

static inline void wlm_display_set_user_data(struct wlm_display *display,
                                             void *userdata) {
  display->userdata = userdata;
}

static inline void *wlm_display_get_user_data(struct wlm_display *display) {
  return display->userdata;
}

#endif
