#!/bin/bash
set -e

# Build libwaypipe.so
if [ ! -f ../../libwaypipe.so ]; then
  gcc -fPIC -shared -o ../../libwaypipe.so ../../waypipe.c \
    $(pkg-config --cflags --libs libportal gstreamer-1.0 gstreamer-app-1.0 glib-2.0 gio-2.0)
fi

# Copy libwaypipe.so
cp --force --remove-destination ../../libwaypipe.so ./libwaypipe.so

gcc -o example_c main.c -L. -lwaypipe -Wl,-rpath,'$ORIGIN' \
  $(pkg-config --cflags --libs glib-2.0)