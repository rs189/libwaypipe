/*
 * waypipe.c
 *
 * MIT License
 *
 * Copyright (c) 2025 rs189
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <glib.h>
#include <gio/gio.h>
#include <libportal/portal.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <pipewire/pipewire.h>

#ifndef XDP_OUTPUT_MONITOR
#define XDP_OUTPUT_MONITOR ((XdpOutputType)1)
#endif
#ifndef XDP_OUTPUT_WINDOW
#define XDP_OUTPUT_WINDOW ((XdpOutputType)2)
#endif

#include "waypipe.h"

typedef struct Waypipe {
    XdpPortal *portal;
    XdpSession *session;
    GstElement *pipeline;
    GstSample *last_sample;
    int pw_fd;
    guint node_id;
    gchar *restore_token;

    // Sync/state
    GMutex lock;
    GCond cond;
    gboolean started;
    gboolean failed;
    gchar *error_msg;
} Waypipe;

/* Helper for synchronous waiting of async libportal calls on the calling thread */
typedef struct StartWait {
    Waypipe *s;
    GMainLoop *loop;
    GError *error;
} StartWait;

static void cleanup_appstate(Waypipe *s) {
    if (!s) return;

    if (s->pipeline) {
        gst_element_set_state(s->pipeline, GST_STATE_NULL);
        gst_object_unref(s->pipeline);
        s->pipeline = NULL;
    }
    if (s->last_sample) {
        gst_sample_unref(s->last_sample);
        s->last_sample = NULL;
    }
}

static GstFlowReturn on_new_sample(GstAppSink *appsink, gpointer user_data) {
    Waypipe *s = (Waypipe*)user_data;
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    if (!sample) return GST_FLOW_OK;

    g_mutex_lock(&s->lock);
    if (s->last_sample) gst_sample_unref(s->last_sample);
    s->last_sample = gst_sample_ref(sample);
    g_mutex_unlock(&s->lock);

    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

static gboolean start_gst_pipeline(Waypipe *s) {
    if (!s) return FALSE;

    if (s->pw_fd < 0) {
        g_printerr("Invalid pipewire fd\n");

        return FALSE;
    }

    char pipeline_descr[1024];
    // Force RGBA output for a simple, usable pixel format
    snprintf(pipeline_descr, sizeof(pipeline_descr),
             "pipewiresrc fd=%d path=%u do-timestamp=true ! videoconvert ! video/x-raw,format=RGBA ! appsink name=mysink max-buffers=5 drop=false",
             s->pw_fd, s->node_id);

    GError *err = NULL;
    s->pipeline = gst_parse_launch(pipeline_descr, &err);
    if (!s->pipeline) {
        g_printerr("Failed to create pipeline: %s\n", err ? err->message : "(unknown)");
        g_clear_error(&err);

        return FALSE;
    }

    GstElement *appsink = gst_bin_get_by_name(GST_BIN(s->pipeline), "mysink");
    if (!appsink) {
        g_printerr("Failed to get appsink element from pipeline\n");
        gst_object_unref(s->pipeline);
        s->pipeline = NULL;

        return FALSE;
    }

    g_object_set(appsink, "emit-signals", TRUE, NULL);
    g_signal_connect(appsink, "new-sample", G_CALLBACK(on_new_sample), s);
    gst_object_unref(appsink);

    gst_element_set_state(s->pipeline, GST_STATE_PLAYING);

    return TRUE;
}

static gboolean extract_first_node_id(GVariant *streams, guint *out_node) {
    if (!streams || !out_node) return FALSE;
    gsize n_children = g_variant_n_children(streams);
    if (n_children == 0) return FALSE;
    GVariant *child = g_variant_get_child_value(streams, 0);
    if (!child) return FALSE;
    GVariant *node_variant = g_variant_get_child_value(child, 0);
    if (!node_variant) {
        g_variant_unref(child);

        return FALSE;
    }
    guint node = g_variant_get_uint32(node_variant);
    *out_node = node;

    g_variant_unref(node_variant);
    g_variant_unref(child);

    return TRUE;
}

/* Async finish callbacks used with temporary main loops to run synchronously */
static void create_session_cb_sync(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    StartWait *w = (StartWait*)user_data;
    GError *err = NULL;
    XdpSession *session = xdp_portal_create_screencast_session_finish(XDP_PORTAL(source_object), res, &err);
    if (err) {
        if (w) w->error = g_error_copy(err);
        g_clear_error(&err);
    } else if (w && w->s) {
        w->s->session = session; // take ownership
    }
    if (w && w->loop) g_main_loop_quit(w->loop);
}

static void session_start_cb_sync(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    StartWait *w = (StartWait*)user_data;
    GError *err = NULL;
    xdp_session_start_finish((XdpSession*)source_object, res, &err);
    if (err) {
        if (w) w->error = g_error_copy(err);
        g_clear_error(&err);
    }
    if (w && w->loop) g_main_loop_quit(w->loop);
}

Waypipe *waypipe_init(void) {
    // Initialise GStreamer
    static gsize gst_inited = 0;
    if (g_once_init_enter(&gst_inited)) {
        int argc = 0; char **argv = NULL;
        gst_init(&argc, &argv);
        g_once_init_leave(&gst_inited, 1);
    }

    Waypipe *s = g_new0(Waypipe, 1);
    g_mutex_init(&s->lock);
    g_cond_init(&s->cond);

    s->portal = NULL;
    s->session = NULL;
    s->pipeline = NULL;
    s->last_sample = NULL;
    s->pw_fd = -1;
    s->node_id = 0;
    s->restore_token = NULL;
    s->started = FALSE;
    s->failed = FALSE;
    s->error_msg = NULL;

    return s;
}

int waypipe_start(Waypipe *s, guint timeout_ms, gboolean persistent) {
    (void)timeout_ms;
    if (!s) return -1;

    char *restore_token = NULL;
    
    // If persistent mode, try to load saved token first
    if (persistent) {
        GError *error = NULL;
        gchar *content = NULL;
        gsize length = 0;
        
        if (g_file_get_contents("waypipe_token.txt", &content, &length, &error)) {
            // Remove trailing newline if present
            if (length > 0 && content[length-1] == '\n') {
                content[length-1] = '\0';
            }
            restore_token = content;
            g_print("Using saved token: %s\n", restore_token);
        } else {
            // No saved token, generate new one
            restore_token = g_uuid_string_random();
            g_print("Generated new token: %s\n", restore_token);
        }
    }

    if (!s->portal) {
        s->portal = xdp_portal_new();
        if (!s->portal) {
            if (restore_token) g_free(restore_token);
            g_mutex_lock(&s->lock);
            s->failed = TRUE;
            g_free(s->error_msg);
            s->error_msg = g_strdup("Failed to create XdpPortal");
            g_mutex_unlock(&s->lock);

            return -2;
        }
    }

    XdpOutputType outputs = (XdpOutputType)(XDP_OUTPUT_MONITOR | XDP_OUTPUT_WINDOW);
    XdpScreencastFlags flags = 0;
#ifdef XDP_SCREENCAST_FLAG_NONE
    flags = XDP_SCREENCAST_FLAG_NONE;
#endif
    XdpCursorMode cursor = XDP_CURSOR_MODE_EMBEDDED;
    XdpPersistMode persist_mode = persistent ? XDP_PERSIST_MODE_PERSISTENT : XDP_PERSIST_MODE_TRANSIENT;
    
    // Store token in context
    g_free(s->restore_token);
    s->restore_token = restore_token ? g_strdup(restore_token) : NULL;

    StartWait w1 = { .s = s, .loop = g_main_loop_new(NULL, FALSE), .error = NULL };
    xdp_portal_create_screencast_session(s->portal,
                                         outputs,
                                         flags,
                                         cursor,
                                         persist_mode,
                                         restore_token,
                                         NULL,
                                         create_session_cb_sync,
                                         &w1);
    if (restore_token) g_free(restore_token);
    g_main_loop_run(w1.loop);
    g_main_loop_unref(w1.loop);

    if (w1.error) {
        g_mutex_lock(&s->lock);
        s->failed = TRUE;
        g_free(s->error_msg);
        s->error_msg = g_strdup(w1.error->message);
        g_mutex_unlock(&s->lock);
        g_error_free(w1.error);

        return -2;
    }
    if (!s->session) {
        g_mutex_lock(&s->lock);
        s->failed = TRUE;
        g_free(s->error_msg);
        s->error_msg = g_strdup("CreateSession returned no session");
        g_mutex_unlock(&s->lock);

        return -2;
    }

    // Start the session
    StartWait w2 = { .s = s, .loop = g_main_loop_new(NULL, FALSE), .error = NULL };
    xdp_session_start(s->session, NULL, NULL, session_start_cb_sync, &w2);
    g_main_loop_run(w2.loop);
    g_main_loop_unref(w2.loop);

    if (w2.error) {
        g_mutex_lock(&s->lock);
        s->failed = TRUE;
        g_free(s->error_msg);
        s->error_msg = g_strdup(w2.error->message);
        g_mutex_unlock(&s->lock);
        g_error_free(w2.error);

        return -2;
    }

    GVariant *streams = xdp_session_get_streams(s->session);
    if (!streams) {
        g_mutex_lock(&s->lock);
        s->failed = TRUE;
        g_free(s->error_msg);
        s->error_msg = g_strdup("xdp_session_get_streams returned NULL");
        g_mutex_unlock(&s->lock);

        return -2;
    }

    guint node_id = 0;
    if (!extract_first_node_id(streams, &node_id)) {
        g_variant_unref(streams);
        g_mutex_lock(&s->lock);
        s->failed = TRUE;
        g_free(s->error_msg);
        s->error_msg = g_strdup("Failed to parse streams variant");
        g_mutex_unlock(&s->lock);

        return -2;
    }
    g_variant_unref(streams);
    s->node_id = node_id;

    int fd = xdp_session_open_pipewire_remote(s->session);
    if (fd < 0) {
        g_mutex_lock(&s->lock);
        s->failed = TRUE;
        g_free(s->error_msg);
        s->error_msg = g_strdup("xdp_session_open_pipewire_remote failed");
        g_mutex_unlock(&s->lock);

        return -2;
    }
    s->pw_fd = fd;

    if (!start_gst_pipeline(s)) {
        g_mutex_lock(&s->lock);
        s->failed = TRUE;
        g_free(s->error_msg);
        s->error_msg = g_strdup("Failed to start GStreamer pipeline");
        g_mutex_unlock(&s->lock);

        return -2;
    }

    // If persistent, save the current restore token for next time
    if (persistent && s->session) {
        char *new_restore_token = xdp_session_get_restore_token(s->session);
        if (new_restore_token) {
            GError *error = NULL;
            if (!g_file_set_contents("waypipe_token.txt", new_restore_token, -1, &error)) {
                g_print("Failed to save token: %s\n", error->message);
                g_error_free(error);
            }
            g_free(new_restore_token);
        }
    }

    g_mutex_lock(&s->lock);
    s->started = TRUE;
    g_mutex_unlock(&s->lock);

    return 0;
}

int waypipe_get_frame(Waypipe *s, guint8 **out_rgba, int *out_width, int *out_height, int *out_stride) {
    if (!s || !out_rgba || !out_width || !out_height || !out_stride) return -1;

    g_mutex_lock(&s->lock);
    if (!s->last_sample) {
        g_mutex_unlock(&s->lock);

        return -2; // no frame yet
    }
    GstSample *sample = gst_sample_ref(s->last_sample);
    g_mutex_unlock(&s->lock);

    GstBuffer *buf = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);
    if (!buf || !caps) { gst_sample_unref(sample); return -3; }

    GstStructure *st = gst_caps_get_structure(caps, 0);
    gint width = 0, height = 0;
    gst_structure_get_int(st, "width", &width);
    gst_structure_get_int(st, "height", &height);
    if (width <= 0 || height <= 0) { gst_sample_unref(sample); return -4; }

    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) { gst_sample_unref(sample); return -5; }

    int stride = width * 4; // RGBA
    gsize need = (gsize)(stride * height);
    guint8 *dst = g_malloc(need);
    memcpy(dst, map.data, need);

    gst_buffer_unmap(buf, &map);
    gst_sample_unref(sample);

    *out_rgba = dst;
    *out_width = width;
    *out_height = height;
    *out_stride = stride;

    return 0;
}

const char *waypipe_get_restore_token(Waypipe *s) {
    return s ? s->restore_token : NULL;
}

void waypipe_free(void *ptr) {
    if (ptr) g_free(ptr);
}

void waypipe_exit(Waypipe *s) {
    if (!s) return;

    cleanup_appstate(s);

    g_mutex_clear(&s->lock);
    g_cond_clear(&s->cond);

    g_clear_pointer(&s->error_msg, g_free);
    g_clear_pointer(&s->restore_token, g_free);

    g_free(s);
}