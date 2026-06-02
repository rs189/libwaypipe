#!/bin/bash
set -e

# Build libwaypipe.so
if [ ! -f ../../libwaypipe.so ]; then
  gcc -fPIC -shared -o ../../libwaypipe.so ../../waypipe.c \
    $(pkg-config --cflags --libs libportal gstreamer-1.0 gstreamer-app-1.0 glib-2.0 gio-2.0)
fi

# Copy libwaypipe.so
cp --force --remove-destination ../../libwaypipe.so ./bin/Release/net9.0/

dotnet build --configuration Release