/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _WLMATCHAPP_TOPLEVEL
#define _WLMATCHAPP_TOPLEVEL

#include <wlmatchapp/symbols.h>
#include <wlmatchapp/window.h>

struct wlm_display;

struct wlm_toplevel {
  struct wlm_window base;
  struct xdg_toplevel *xdg_toplevel;

  void (*on_close)(struct wlm_toplevel *toplevel);

  struct {
    uint32_t width;
    uint32_t height;
    uint32_t bounds_width;
    uint32_t bounds_height;
    bool maximized;
    bool fullscreen;
    bool resizing;
    bool activated;
  } configure;

  struct {
    bool window_menu;
    bool maximize;
    bool minimize;
    bool fullscreen;
  } wm_cap;

  struct {
    struct wl_list link;
  } private;
};

WLM_API struct wlm_toplevel *wlm_toplevel_create(struct wlm_display *display);
WLM_API void wlm_toplevel_destroy(struct wlm_toplevel *toplevel);

WLM_API void wlm_toplevel_set_maximized(struct wlm_toplevel *toplevel,
                                        bool maximized);
WLM_API void wlm_toplevel_set_fullscreen(struct wlm_toplevel *toplevel,
                                         bool fullscreen);
WLM_API void wlm_toplevel_set_minimized(struct wlm_toplevel *toplevel);
WLM_API void wlm_toplevel_set_app_id(struct wlm_toplevel *toplevel,
                                     char const *app_id);
WLM_API void wlm_toplevel_set_title(struct wlm_toplevel *toplevel,
                                    char const *title);

static inline void wlm_toplevel_set_user_data(struct wlm_toplevel *toplevel,
                                              void *userdata) {
  wlm_window_set_user_data(&toplevel->base, userdata);
}

static inline void *wlm_toplevel_get_user_data(struct wlm_toplevel *toplevel) {
  return wlm_window_get_user_data(&toplevel->base);
}

#endif
