/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _UTIL_H
#define _UTIL_H

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#define DECLARE_TYPE(_type)                                                    \
  struct _type##_sig {                                                         \
    uint8_t dummy;                                                             \
  };                                                                           \
  extern struct _type##_sig const _type##_sig;                                 \
  static inline struct _type *check_sig_##_type(struct _type *p) {             \
    assert(p->sig == &_type##_sig);                                            \
    return p;                                                                  \
  }                                                                            \
  static inline struct _type *alloc_##_type(void) {                            \
    struct _type *p = calloc(1, sizeof(*p));                                   \
    p->sig = &_type##_sig;                                                     \
    return p;                                                                  \
  }

#define DEFINE_TYPE(_type) struct _type##_sig const _type##_sig;

#define get_type_ptr(_type, _ptr, _sample, _member)                            \
  check_sig_##_type(wl_container_of(_ptr, _sample, _member))

static inline void bind_clbk(struct wl_listener *listener,
                             struct wl_signal *signal, wl_notify_func_t clbk) {
  listener->notify = clbk;
  wl_signal_add(signal, listener);
}

#endif
