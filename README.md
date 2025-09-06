# wlmatchbox

A Wayland Implementation of the Matchbox minimal compositor using wlroots

## Building

The compositor is built using `meson` and `ninja`. To build run:

```shell
meson setup builddir
ninja -C builddir
```

As a convinence, a `run` target is provided that will run the compositor with
the app chooser and panel. To launch it, run:

```shell
ninja -C builddir run
```

## Components

### wlmatchbox

This is the main compositor, built using wlroots.

### xdg-app-chooser

This is a simple application launcher that searches for XDG .desktop files and
shows the applications so that the user can launch one.

### app-panel

A simple panel application that show all running applications and allows the
user to bring on to the foreground. Uses the wlroots foreign toplevel
management protocol.

