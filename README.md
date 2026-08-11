# Waypipe
### Wayland screensharing made easy.

Waypipe is a minimal C library (libwaypipe) for capturing screen content on Wayland using PipeWire and xdg-desktop-portal (libportal). It uses GStreamer to produce RGBA frames that are accessible in-process, allowing applications to easily capture and process output from the desktop environment.

## Requirements

##### Core dependencies:

- Wayland compositor with a compatible xdg-desktop-portal backend (i.e., KDE with xdg-desktop-portal-kde, GNOME with xdg-desktop-portal-gnome, Sway with xdg-desktop-portal-wlr)

##### Build dependencies:

- libportal (>= 0.6, for xdg-desktop-portal API)
- glib-2.0 (development headers)
- gio-2.0 (development headers)
- gstreamer-1.0 (development headers)
- gstreamer-app-1.0 (development headers)
- pkg-config (for build configuration)

## Usage
```c
Waypipe *wp = waypipe_init();
if (!wp) { 
    fprintf(stderr, "Waypipe init failed\n"); 
    return 1; 
}

int rc = waypipe_start(wp, 15000, 1);
if (rc != 0) {
    fprintf(stderr, "Waypipe start timed out or failed (rc=%d). Is xdg-desktop-portal running and did you approve the dialogue?\n", rc);
    waypipe_exit(wp);

    return 2;
}	

// Poll until a frame is available
while (true) {
    uint8_t *rgba = NULL; int w=0,h=0,stride=0;
    if (waypipe_get_frame(wp, &rgba, &w, &h, &stride) == 0) {
        printf("Got frame %dx%d stride=%d (first 4 bytes: %u %u %u %u)\n", w, h, stride, rgba[0], rgba[1], rgba[2], rgba[3]);
        waypipe_free(rgba);
        break;
    }
    sleep(1);
}	

waypipe_exit(wp);

return 0;
```

## Build (shared library)

Install requiremed build dependencies:

- Fedora:
```bash
sudo dnf install libportal-devel glib2-devel gstreamer1-devel gstreamer1-plugins-base-devel gstreamer1-plugins-good-devel pkgconf-pkg-config pipewire-devel
```

Compile the shared library:

```bash
gcc -fPIC -shared -o libwaypipe.so waypipe.c \
  $(pkg-config --cflags --libs libportal gstreamer-1.0 gstreamer-app-1.0 glib-2.0 gio-2.0 libpipewire-0.3)
```

To use libwaypipe system-wide, install `libwaypipe.so` to `/usr/local/lib` and run `sudo ldconfig`. Otherwise, set `LD_LIBRARY_PATH` to the folder containing `libwaypipe.so`.

## Examples

Examples are provided in the `examples/` directory, demonstrating how to use the libwaypipe in C and C#.

## License
Waypipe is licensed under the [MIT License](LICENSE).
