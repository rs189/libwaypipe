#!/bin/bash
set -e

gcc -fPIC -shared -o libwaypipe.so waypipe.c \
  $(pkg-config --cflags --libs libportal gstreamer-1.0 gstreamer-app-1.0 glib-2.0 gio-2.0 libpipewire-0.3)