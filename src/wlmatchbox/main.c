/*
 * Copyright 2025 Joshua Watt
 *
 * SPDX-License-Identifier: MIT
 */
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>
#include <wlr/backend.h>
#include <wlr/util/log.h>

#include "server.h"

static struct option options[] = {
    {"init", required_argument, NULL, 'i'},
    {"panel", required_argument, NULL, 'p'},
    {NULL},
};

struct init_prog {
  struct wl_list link;
  char *prog;
};

int main(int argc, char **argv) {
  char *panel_program = NULL;
  struct wl_list init_progs;
  wl_list_init(&init_progs);

  int opt;
  while ((opt = getopt_long(argc, argv, "i:p:", options, NULL)) != -1) {
    switch (opt) {
    case 'i': {
      struct init_prog *p = calloc(1, sizeof(*p));
      p->prog = strdup(optarg);
      wl_list_insert(&init_progs, &p->link);
      break;
    }
    case 'p':
      panel_program = strdup(optarg);
      break;

    default:
      printf("Usage: %s [-i|--init PROG] [-p|--panel PROG]\n", argv[0]);
      printf("\n");
      printf("  -i|--init PROG      Launch PROG on startup\n");
      printf("  -p|--panel PROG     Launch PROG as application panel\n");
      exit(EXIT_FAILURE);
      break;
    }
  }

  wlr_log_init(WLR_DEBUG, NULL);

  struct server *server = server_create();

  if (!server) {
    return 1;
  }

  // Run server
  const char *socket = wl_display_add_socket_auto(server->wl_display);
  if (!socket) {
    wlr_log(WLR_ERROR, "Failed to create display socket\n");
    return 1;
  }
  setenv("WAYLAND_DISPLAY", socket, true);
  printf("Display is %s\n", socket);

  if (!wlr_backend_start(server->wlr_backend)) {
    wlr_log(WLR_ERROR, "Failed to start backend\n");
    return 1;
  }

  struct init_prog *p, *next_p;
  wl_list_for_each_safe(p, next_p, &init_progs, link) {
    if (fork() == 0) {
      execlp(p->prog, p->prog, NULL);
      _exit(EXIT_FAILURE);
    }
    wl_list_remove(&p->link);
    free(p->prog);
    free(p);
  }

  if (panel_program) {
    server_create_panel(server, panel_program);
    free(panel_program);
  }

  wl_display_run(server->wl_display);
  wl_display_destroy(server->wl_display);
  free(server);
  return 0;
}
