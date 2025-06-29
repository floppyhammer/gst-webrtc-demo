// Copyright 2020-2023, Collabora, Ltd.
// Copyright 2022-2023, PlutoVR
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Pipeline module ElectricMaple XR streaming solution
 * @author Rylie Pavlik <rpavlik@collabora.com>
 * @ingroup em_client
 */

#include "em_stream_client.h"

#include <gst/app/gstappsink.h>
#include <gst/gl/gl.h>
#include <gst/gl/gstglsyncmeta.h>
#include <gst/gst.h>
#include <gst/gstbus.h>
#include <gst/gstelement.h>
#include <gst/gstinfo.h>
#include <gst/gstmessage.h>
#include <gst/gstsample.h>
#include <gst/gstutils.h>
#include <gst/video/video-frame.h>
#include <gst/webrtc/webrtc.h>

#include "em_app_log.h"
#include "em_connection.h"
#include "gst_common.h" // for em_sample

// clang-format off
#include <EGL/egl.h>
#include <GLES2/gl2ext.h>
// clang-format on

#include <linux/time.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct em_sc_sample {
    struct em_sample base;
    GstSample *sample;
};

/*!
 * All in one helper that handles locking, waiting for change and starting a
 * thread.
 */
struct os_thread_helper {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    bool initialized;
    bool running;
};

/*!
 * Run function.
 *
 * @public @memberof os_thread
 */
typedef void *(*os_run_func_t)(void *);

/*!
 * Start the internal thread.
 *
 * @public @memberof os_thread_helper
 */
static inline int os_thread_helper_start(struct os_thread_helper *oth, os_run_func_t func, void *ptr) {
    pthread_mutex_lock(&oth->mutex);

    g_assert(oth->initialized);
    if (oth->running) {
        pthread_mutex_unlock(&oth->mutex);
        return -1;
    }

    int ret = pthread_create(&oth->thread, NULL, func, ptr);
    if (ret != 0) {
        pthread_mutex_unlock(&oth->mutex);
        return ret;
    }

    oth->running = true;

    pthread_mutex_unlock(&oth->mutex);

    return 0;
}

/*!
 * Zeroes the correct amount of memory based on the type pointed-to by the
 * argument.
 *
 * Use instead of memset(..., 0, ...) on a structure or pointer to structure.
 *
 * @ingroup aux_util
 */
#define U_ZERO(PTR) memset((PTR), 0, sizeof(*(PTR)))

/*!
 * Initialize the thread helper.
 *
 * @public @memberof os_thread_helper
 */
static inline int os_thread_helper_init(struct os_thread_helper *oth) {
    U_ZERO(oth);

    int ret = pthread_mutex_init(&oth->mutex, NULL);
    if (ret != 0) {
        return ret;
    }

    ret = pthread_cond_init(&oth->cond, NULL);
    if (ret) {
        pthread_mutex_destroy(&oth->mutex);
        return ret;
    }
    oth->initialized = true;

    return 0;
}

struct _EmStreamClient {
    GMainLoop *loop;
    EmConnection *connection;
    GstElement *pipeline;
    GstGLDisplay *gst_gl_display;
    GstGLContext *gst_gl_context;
    GstGLContext *gst_gl_other_context;

    GstGLDisplay *display;

    /// Wrapped version of the android_main/render context
    GstGLContext *android_main_context;

    /// GStreamer-created EGL context for its own use
    GstGLContext *context;

    GstElement *appsink;

    GLenum frame_texture_target;
    GLenum texture_target;
    GLuint texture_id;

    int width;
    int height;

    struct {
        EGLDisplay display;
        EGLContext android_main_context;
        // 16x16 pbuffer surface
        EGLSurface surface;
    } egl;

    struct os_thread_helper play_thread;

    bool received_first_frame;

    GMutex sample_mutex;
    GstSample *sample;
    struct timespec sample_decode_end_ts;

    guint timeout_src_id_dot_data;
    guint timeout_src_id_print_stats;
};

// clang-format off
#define VIDEO_SINK_CAPS \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY "), "              \
    "format = (string) RGBA, "                                          \
    "width = " GST_VIDEO_SIZE_RANGE ", "                                \
    "height = " GST_VIDEO_SIZE_RANGE ", "                               \
    "framerate = " GST_VIDEO_FPS_RANGE ", "                             \
    "texture-target = (string) { 2D, external-oes } "
// clang-format on

/*
 * Callbacks
 */

static void on_need_pipeline_cb(EmConnection *emconn, EmStreamClient *sc);

static void on_drop_pipeline_cb(EmConnection *emconn, EmStreamClient *sc);

static void *em_stream_client_thread_func(void *ptr);

/*
 * Helper functions
 */

static void em_stream_client_set_connection(EmStreamClient *sc, EmConnection *connection);

/* GObject method implementations */

static void em_stream_client_init(EmStreamClient *sc) {
    ALOGI("%s: creating stuff", __FUNCTION__);

    memset(sc, 0, sizeof(EmStreamClient));
    sc->loop = g_main_loop_new(NULL, FALSE);
    g_assert(os_thread_helper_init(&sc->play_thread) >= 0);
    g_mutex_init(&sc->sample_mutex);
    ALOGI("%s: done creating stuff", __FUNCTION__);
}

void em_stream_client_set_egl_context(EmStreamClient *sc, EGLContext context, EGLDisplay display, EGLSurface surface) {
    ALOGI("Wrapping egl context");

    sc->egl.display = display;
    sc->egl.android_main_context = context;
    sc->egl.surface = surface;

    const GstGLPlatform egl_platform = GST_GL_PLATFORM_EGL;
    guintptr android_main_egl_context_handle = gst_gl_context_get_current_gl_context(egl_platform);
    GstGLAPI gl_api = gst_gl_context_get_current_gl_api(egl_platform, NULL, NULL);
    sc->gst_gl_display = g_object_ref_sink(gst_gl_display_new());
    sc->android_main_context = g_object_ref_sink(
        gst_gl_context_new_wrapped(sc->gst_gl_display, android_main_egl_context_handle, egl_platform, gl_api));
}

static void em_stream_client_dispose(EmStreamClient *self) {
    // May be called multiple times during destruction.
    // Stop things and clear ref counted things here.
    // EmStreamClient *self = EM_STREAM_CLIENT(object);
    em_stream_client_stop(self);
    g_clear_object(&self->loop);
    g_clear_object(&self->connection);
    gst_clear_object(&self->sample);
    gst_clear_object(&self->pipeline);
    gst_clear_object(&self->gst_gl_display);
    gst_clear_object(&self->gst_gl_context);
    gst_clear_object(&self->gst_gl_other_context);
    gst_clear_object(&self->display);
    gst_clear_object(&self->context);
    gst_clear_object(&self->appsink);
}

static void em_stream_client_finalize(EmStreamClient *self) {
    // Only called once, after dispose
}

/*
 * Callbacks
 */

static GstBusSyncReply bus_sync_handler_cb(GstBus *bus, GstMessage *msg, EmStreamClient *sc) {
    // LOG_MSG(msg);

    /* Do not let GstGL retrieve the display handle on its own
     * because then it believes it owns it and calls eglTerminate()
     * when disposed */
    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_NEED_CONTEXT) {
        const gchar *type;
        gst_message_parse_context_type(msg, &type);
        if (g_str_equal(type, GST_GL_DISPLAY_CONTEXT_TYPE)) {
            ALOGI("Got message: Need display context");
            g_autoptr(GstContext) context = gst_context_new(GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);
            gst_context_set_gl_display(context, sc->display);
            gst_element_set_context(GST_ELEMENT(msg->src), context);
        } else if (g_str_equal(type, "gst.gl.app_context")) {
            ALOGI("Got message: Need app context");
            g_autoptr(GstContext) app_context = gst_context_new("gst.gl.app_context", TRUE);
            GstStructure *s = gst_context_writable_structure(app_context);
            gst_structure_set(s, "context", GST_TYPE_GL_CONTEXT, sc->android_main_context, NULL);
            gst_element_set_context(GST_ELEMENT(msg->src), app_context);
        }
    }

    return GST_BUS_PASS;
}

static gboolean gst_bus_cb(GstBus *bus, GstMessage *message, gpointer user_data) {
    // LOG_MSG(message);

    GstBin *pipeline = GST_BIN(user_data);

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *gerr = NULL;
            gchar *debug_msg = NULL;
            gst_message_parse_error(message, &gerr, &debug_msg);

            GST_DEBUG_BIN_TO_DOT_FILE(pipeline, GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-error");

            gchar *dot_data = gst_debug_bin_to_dot_data(pipeline, GST_DEBUG_GRAPH_SHOW_ALL);
            ALOGE("gst_bus_cb: DOT data: %s", dot_data);
            g_free(dot_data);

            ALOGE("gst_bus_cb: Error: %s (%s)", gerr->message, debug_msg);
            g_error("gst_bus_cb: Error: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
        } break;
        case GST_MESSAGE_WARNING: {
            GError *gerr = NULL;
            gchar *debug_msg = NULL;
            gst_message_parse_warning(message, &gerr, &debug_msg);
            GST_DEBUG_BIN_TO_DOT_FILE(pipeline, GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-warning");
            ALOGW("gst_bus_cb: Warning: %s (%s)", gerr->message, debug_msg);
            g_warning("gst_bus_cb: Warning: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
        } break;
        case GST_MESSAGE_EOS: {
            g_error("gst_bus_cb: Got EOS!");
        } break;
        default:
            break;
    }
    return TRUE;
}

static GstFlowReturn on_new_sample_cb(GstAppSink *appsink, gpointer user_data) {
    EmStreamClient *sc = (EmStreamClient *)user_data;

    // TODO record the frame ID, get frame pose
    struct timespec ts;
    int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (ret != 0) {
        ALOGE("%s: clock_gettime failed, which is very bizarre.", __FUNCTION__);
        return GST_FLOW_ERROR;
    }

    GstSample *sample = gst_app_sink_pull_sample(appsink);
    g_assert_nonnull(sample);

    GstSample *prevSample = NULL;

    // Update client sample
    {
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&sc->sample_mutex);
        prevSample = sc->sample;
        sc->sample = sample;
        sc->sample_decode_end_ts = ts;
        sc->received_first_frame = true;
    }

    // Previous client sample is not used.
    if (prevSample) {
        ALOGI("Discarding unused, replaced sample");
        gst_sample_unref(prevSample);
    }

    return GST_FLOW_OK;
}

static GstPadProbeReturn buffer_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
        GstClockTime pts = GST_BUFFER_PTS(buf);

        static GstClockTime previous_pts = 0;
        static int64_t previous_time = 0;
        if (previous_pts != 0) {
            int64_t pts_diff = (pts - previous_pts) / 1e6;
            ALOGD("Received frame PTS: %" GST_TIME_FORMAT ", PTS diff: %ld", GST_TIME_ARGS(pts), pts_diff);
        }
        previous_pts = pts;
    }
    return GST_PAD_PROBE_OK;
}

static void on_new_transceiver(GstElement *webrtc, GstWebRTCRTPTransceiver *trans) {
    g_print("Hit on_new_transceiver\n");
    g_object_set(trans, "fec-type", GST_WEBRTC_FEC_TYPE_ULP_RED, NULL);
}

static void handle_media_stream(GstPad *src_pad, EmStreamClient *sc, const char *convert_name, const char *sink_name) {
    gst_println("Trying to handle stream with %s ! %s", convert_name, sink_name);

    if (g_strcmp0(convert_name, "audioconvert") == 0) {
        GstElement *q = gst_element_factory_make("queue", NULL);
        g_assert_nonnull(q);
        GstElement *conv = gst_element_factory_make(convert_name, NULL);
        g_assert_nonnull(conv);
        GstElement *sink = gst_element_factory_make(sink_name, NULL);
        g_assert_nonnull(sink);

        /* Might also need to resample, so add it just in case.
         * Will be a no-op if it's not required. */
        GstElement *resample = gst_element_factory_make("audioresample", NULL);
        g_assert_nonnull(resample);
        gst_bin_add_many(GST_BIN(sc->pipeline), q, conv, resample, sink, NULL);
        gst_element_sync_state_with_parent(q);
        gst_element_sync_state_with_parent(conv);
        gst_element_sync_state_with_parent(resample);
        gst_element_sync_state_with_parent(sink);
        gst_element_link_many(q, conv, resample, sink, NULL);

        GstPad *qpad = gst_element_get_static_pad(q, "sink");

        GstPadLinkReturn ret = gst_pad_link(src_pad, qpad);
        g_assert_cmphex(ret, ==, GST_PAD_LINK_OK);

        gst_object_unref(qpad);
    } else {
        GstElement *glsinkbin = gst_element_factory_make("glsinkbin", NULL);

        // Disable clock sync to reduce latency
        g_object_set(glsinkbin, "sync", FALSE, NULL);

        gst_bin_add_many(GST_BIN(sc->pipeline), glsinkbin, NULL);

        GstPad *sink_pad = gst_element_get_static_pad(glsinkbin, "sink");
        GstPadLinkReturn ret = gst_pad_link(src_pad, sink_pad);
        g_assert_cmphex(ret, ==, GST_PAD_LINK_OK);
        gst_object_unref(sink_pad);

        // Set a custom appsink for glsinkbin
        {
            // We convert the string SINK_CAPS above into a GstCaps that elements below can understand.
            // the "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY ")," part of the caps is read :
            // video/x-raw(memory:GLMemory) and is really important for getting zero-copy gl textures.
            // It tells the pipeline (especially the decoder) that an internal android:Surface should
            // get created internally (using the provided gstgl contexts above) so that the appsink
            // can basically pull the samples out using an GLConsumer (this is just for context, as
            // all of those constructs will be hidden from you, but are turned on by that CAPS).
            g_autoptr(GstCaps) caps = gst_caps_from_string(VIDEO_SINK_CAPS);

            // FRED: We create the appsink 'manually' here because glsink's ALREADY a sink and so if we stick
            //       glsinkbin ! appsink in our pipeline_string for automatic linking, gst_parse will NOT like this,
            //       as glsinkbin (a sink) cannot link to anything upstream (appsink being 'another' sink). So we
            //       manually link them below using glsinkbin's 'sink' pad -> appsink.
            sc->appsink = gst_element_factory_make("appsink", NULL);
            g_object_set(sc->appsink,
                         // Set caps
                         "caps",
                         caps,
                         // Fixed size buffer
                         "max-buffers",
                         1,
                         // Drop old buffers when queue is filled
                         "drop",
                         true,
                         // Terminator
                         NULL);

            // Lower overhead than new-sample signal.
            GstAppSinkCallbacks callbacks = {};
            callbacks.new_sample = on_new_sample_cb;
            gst_app_sink_set_callbacks(GST_APP_SINK(sc->appsink), &callbacks, sc, NULL);
            sc->received_first_frame = false;

            g_object_set(glsinkbin, "sink", sc->appsink, NULL);
        }

        gst_element_sync_state_with_parent(glsinkbin);
    }
}

static void on_decodebin_pad_added(GstElement *decodebin, GstPad *pad, EmStreamClient *sc) {
    // We don't care about sink pads
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) {
        return;
    }

    GstCaps *caps = NULL;

    // For using decodebin
    if (gst_pad_has_current_caps(pad)) {
        caps = gst_pad_get_current_caps(pad);
    }
    // For using decodebin3
    else {
        gst_print("Pad '%s' has no caps, use gst_pad_get_stream to get caps\n", GST_PAD_NAME(pad));

        GstStream *stream = gst_pad_get_stream(pad);
        caps = gst_stream_get_caps(stream);
        gst_clear_object(&stream);
    }

    const gchar *name = gst_structure_get_name(gst_caps_get_structure(caps, 0));

    gchar *str = gst_caps_serialize(caps, 0);
    g_print("decodebin src pad caps: %s\n", str);
    g_free(str);

    if (g_str_has_prefix(name, "video")) {
        handle_media_stream(pad, sc, "videoconvert", "autovideosink");
    } else if (g_str_has_prefix(name, "audio")) {
        handle_media_stream(pad, sc, "audioconvert", "openslessink");
    } else {
        gst_printerr("Unknown pad %s, ignoring", GST_PAD_NAME(pad));
    }
}

GstElement *find_element_by_name(GstBin *bin, const gchar *element_name) {
    GstIterator *iter = gst_bin_iterate_elements(bin);
    GValue item = G_VALUE_INIT;
    GstElement *result = NULL;

    while (gst_iterator_next(iter, &item) == GST_ITERATOR_OK) {
        GstElement *element = GST_ELEMENT(g_value_get_object(&item));
        if (g_strcmp0(GST_ELEMENT_NAME(element), element_name) == 0) {
            result = element;
            // Take a ref
            // gst_object_ref(result);
            g_value_unset(&item);
            break;
        }
        if (GST_IS_BIN(element)) {
            result = find_element_by_name(GST_BIN(element), element_name);
            if (result) {
                g_value_unset(&item);
                break;
            }
        }
        g_value_unset(&item);
    }

    gst_iterator_free(iter);
    return result;
}

static void on_webrtcbin_stats(GstPromise *promise, GstElement *user_data) {
    const GstStructure *reply = gst_promise_get_reply(promise);
    gchar *str = gst_structure_to_string(reply);
    g_print("webrtcbin stats: %s\n", str);
    g_free(str);
}

static gboolean print_stats(EmStreamClient *sc) {
    if (!sc) {
        return G_SOURCE_CONTINUE;
    }

    GstElement *webrtcbin = gst_bin_get_by_name(GST_BIN(sc->pipeline), "webrtc");

    GstPromise *promise = gst_promise_new_with_change_func((GstPromiseChangeFunc)on_webrtcbin_stats, NULL, NULL);
    g_signal_emit_by_name(webrtcbin, "get-stats", NULL, promise);

    // FEC stats
    for (int i = 0; i < 2; i++) {
        gchar *name;
        gchar *name_jitter;
        gchar *name_storage;
        if (i == 0) {
            name = "rtpulpfecdec0";
            name_jitter = "rtpjitterbuffer0";
            name_storage = "rtpstorage0";
        } else {
            name = "rtpulpfecdec1";
            name_jitter = "rtpjitterbuffer1";
            name_storage = "rtpstorage1";
        }

        GstElement *rtpulpfecdec = find_element_by_name(GST_BIN(webrtcbin), name);

        if (rtpulpfecdec) {
            GValue pt = G_VALUE_INIT;
            GValue recovered = G_VALUE_INIT;
            GValue unrecovered = G_VALUE_INIT;

            g_object_get_property(G_OBJECT(rtpulpfecdec), "pt", &pt);
            g_object_get_property(G_OBJECT(rtpulpfecdec), "recovered", &recovered);
            g_object_get_property(G_OBJECT(rtpulpfecdec), "unrecovered", &unrecovered);

            g_object_set(G_OBJECT(rtpulpfecdec), "passthrough", FALSE, NULL);

            g_print("FEC stats: pt %u, recovered %u, unrecovered %u\n",
                   g_value_get_uint(&pt),
                   g_value_get_uint(&recovered),
                   g_value_get_uint(&unrecovered));

            g_value_unset(&pt);
            g_value_unset(&recovered);
            g_value_unset(&unrecovered);
        }

//        GstElement *jitter = find_element_by_name(GST_BIN(webrtcbin), name_jitter);
//        if (jitter) {
//            g_object_set(jitter, "rfc7273-sync", TRUE, NULL);
//        }

        GstElement *rtpstorage = find_element_by_name(GST_BIN(webrtcbin), name_storage);
        if (rtpstorage) {
            g_object_set(rtpstorage, "size-time", 220000000, NULL);
        }
    }

    return G_SOURCE_CONTINUE;
}

static void on_webrtcbin_pad_added(GstElement *webrtcbin, GstPad *pad, EmStreamClient *sc) {
    // We don't care about sink pads
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) {
        return;
    }

    GstCaps *caps = gst_pad_get_current_caps(pad);
    gchar *str = gst_caps_serialize(caps, 0);
    g_print("webrtcbin src pad caps: %s\n", str);
    bool is_audio = g_strstr_len(str, -1, "audio") != NULL;
    g_free(str);
    gst_caps_unref(caps);

    if (is_audio) {
        GstElement *depay = gst_element_factory_make("rtpopusdepay", NULL);
        gst_bin_add(GST_BIN(sc->pipeline), depay);

        GstPad *sink_pad = gst_element_get_static_pad(depay, "sink");
        gst_pad_link(pad, sink_pad);
        gst_object_unref(sink_pad);

        GstElement *opusdec = gst_element_factory_make("opusdec", NULL);

        // Not triggered
        //        g_signal_connect(opusdec, "pad-added", G_CALLBACK(on_decodebin_pad_added), sc);
        gst_bin_add(GST_BIN(sc->pipeline), opusdec);

        gst_element_link(depay, opusdec);

        GstPad *src_pad = gst_element_get_static_pad(opusdec, "src");
        handle_media_stream(src_pad, sc, "audioconvert", "openslessink");

        gst_element_sync_state_with_parent(depay);
        gst_element_sync_state_with_parent(opusdec);
    } else {
        GstElement *decodebin = gst_element_factory_make("decodebin3", NULL);

        g_signal_connect(decodebin, "pad-added", G_CALLBACK(on_decodebin_pad_added), sc);
        gst_bin_add(GST_BIN(sc->pipeline), decodebin);

        // Print stats repeatedly
        sc->timeout_src_id_print_stats = g_timeout_add_seconds(3, G_SOURCE_FUNC(print_stats), sc);

        GstPad *sink_pad = gst_element_get_static_pad(decodebin, "sink");
        gst_pad_link(pad, sink_pad);
        gst_object_unref(sink_pad);

        gst_element_sync_state_with_parent(decodebin);
    }
}

static gboolean check_pipeline_dot_data(EmStreamClient *sc) {
    if (!sc || !sc->pipeline) {
        return G_SOURCE_CONTINUE;
    }

    gchar *dot_data = gst_debug_bin_to_dot_data(GST_BIN(sc->pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
    g_free(dot_data);

    return G_SOURCE_CONTINUE;
}

static void on_need_pipeline_cb(EmConnection *emconn, EmStreamClient *sc) {
    g_info("%s", __FUNCTION__);
    g_assert_nonnull(sc);
    g_assert_nonnull(emconn);

    //    GError *error = NULL;

    // decodebin3 seems to .. hang?
    // omxh264dec doesn't seem to exist

    //    GList *decoders = gst_element_factory_list_get_elements(GST_ELEMENT_FACTORY_TYPE_DECODABLE,
    //                                                            GST_RANK_MARGINAL);
    //
    //    // Iterate through the list
    //    for (GList *iter = decoders; iter != NULL; iter = iter->next) {
    //        GstElementFactory *factory = (GstElementFactory *) iter->data;
    //
    //        // Get the factory name suitable for use in a string pipeline
    //        const gchar *name = gst_element_get_name(factory);
    //
    //        // Print the factory name
    //        g_print("Decoder: %s\n", name);
    //    }

    // We'll need an active egl context below before setting up gstgl (as explained previously)

    //    // clang-format off
    //    gchar *pipeline_string = g_strdup_printf(
    //        "webrtcbin name=webrtc bundle-policy=max-bundle latency=0 ! "
    //        "decodebin3 ! "
    ////        "amcviddec-c2qtiavcdecoder ! "        // Hardware
    ////        "amcviddec-omxqcomvideodecoderavc ! " // Hardware
    ////        "amcviddec-c2androidavcdecoder ! "    // Software
    ////        "amcviddec-omxgoogleh264decoder ! "   // Software
    ////
    ///"video/x-raw(memory:GLMemory),format=(string)RGBA,width=(int)1280,height=(int)720,texture-target=(string)external-oes
    ///! "
    //        "glsinkbin name=glsink");
    //    // clang-format on
    //
    //    sc->pipeline = gst_object_ref_sink(gst_parse_launch(pipeline_string, &error));
    //    if (sc->pipeline == NULL) {
    //        ALOGE("Failed creating pipeline : Bad source: %s", error->message);
    //        abort();
    //    }
    //    if (error) {
    //        ALOGE("Error creating a pipeline from string: %s", error ? error->message : "Unknown");
    //        abort();
    //    }

    sc->pipeline = gst_pipeline_new("webrtc-recv-pipeline");

    GstElement *webrtcbin = gst_element_factory_make("webrtcbin", "webrtc");
    // Matching this to the offerer's bundle policy is necessary for negotiation
    g_object_set(webrtcbin, "bundle-policy", GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE, NULL);
    g_object_set(webrtcbin, "latency", 0, NULL);
    g_signal_connect(webrtcbin, "on-new-transceiver", G_CALLBACK(on_new_transceiver), NULL);
    g_signal_connect(webrtcbin, "pad-added", G_CALLBACK(on_webrtcbin_pad_added), sc);

    gst_bin_add_many(GST_BIN(sc->pipeline), webrtcbin, NULL);

    //    GstPad *pad = gst_element_get_static_pad(gst_bin_get_by_name(GST_BIN(sc->pipeline), "depay"), "src");
    //    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)buffer_probe_cb, NULL, NULL);
    //    gst_object_unref(pad);

    g_autoptr(GstBus) bus = gst_element_get_bus(sc->pipeline);
    // We set this up to inject the EGL context
    gst_bus_set_sync_handler(bus, (GstBusSyncHandler)bus_sync_handler_cb, sc, NULL);

    // This just watches for errors and such
    gst_bus_add_watch(bus, gst_bus_cb, sc->pipeline);
    g_object_unref(bus);

    // This actually hands over the pipeline. Once our own handler returns,
    // the pipeline will be started by the connection.
    g_signal_emit_by_name(emconn, "set-pipeline", GST_PIPELINE(sc->pipeline), NULL);

    sc->timeout_src_id_dot_data = g_timeout_add_seconds(3, G_SOURCE_FUNC(check_pipeline_dot_data), sc);
}

static void on_drop_pipeline_cb(EmConnection *emconn, EmStreamClient *sc) {
    if (sc->pipeline) {
        gst_element_set_state(sc->pipeline, GST_STATE_NULL);
    }
    gst_clear_object(&sc->pipeline);
    gst_clear_object(&sc->appsink);
}

static void *em_stream_client_thread_func(void *ptr) {
    EmStreamClient *sc = (EmStreamClient *)ptr;

    ALOGI("%s: running GMainLoop", __FUNCTION__);
    g_main_loop_run(sc->loop);
    ALOGI("%s: g_main_loop_run returned", __FUNCTION__);

    return NULL;
}

/*
 * Public functions
 */
EmStreamClient *em_stream_client_new() {
    EmStreamClient *self = calloc(1, sizeof(EmStreamClient));
    em_stream_client_init(self);
    return self;
}

void em_stream_client_destroy(EmStreamClient **ptr_sc) {
    if (ptr_sc == NULL) {
        return;
    }
    EmStreamClient *sc = *ptr_sc;
    if (sc == NULL) {
        return;
    }
    em_stream_client_dispose(sc);
    em_stream_client_finalize(sc);
    free(sc);
    *ptr_sc = NULL;
}

void em_stream_client_spawn_thread(EmStreamClient *sc, EmConnection *connection) {
    ALOGI("%s: Starting stream client mainloop thread", __FUNCTION__);
    em_stream_client_set_connection(sc, connection);
    int ret = os_thread_helper_start(&sc->play_thread, &em_stream_client_thread_func, sc);
    (void)ret;
    g_assert(ret == 0);
}

void em_stream_client_stop(EmStreamClient *sc) {
    ALOGI("%s: Stopping pipeline and ending thread", __FUNCTION__);

    if (sc->pipeline != NULL) {
        gst_element_set_state(sc->pipeline, GST_STATE_NULL);
    }
    if (sc->connection != NULL) {
        em_connection_disconnect(sc->connection);
    }
    gst_clear_object(&sc->pipeline);
    gst_clear_object(&sc->appsink);
    gst_clear_object(&sc->context);
}

struct em_sample *em_stream_client_try_pull_sample(EmStreamClient *sc, struct timespec *out_decode_end) {
    if (!sc->appsink) {
        // Not setup yet.
        return NULL;
    }

    // We actually pull the sample in the new-sample signal handler, so here we're just receiving the sample already
    // pulled.
    GstSample *sample = NULL;
    struct timespec decode_end;
    {
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&sc->sample_mutex);
        sample = sc->sample;
        sc->sample = NULL;
        decode_end = sc->sample_decode_end_ts;
    }

    // Check pipeline
    //    gchar *dot_data = gst_debug_bin_to_dot_data(GST_BIN(sc->pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
    //    g_free(dot_data);

    if (sample == NULL) {
        if (gst_app_sink_is_eos(GST_APP_SINK(sc->appsink))) {
            //            ALOGW("%s: EOS", __FUNCTION__);
            // TODO trigger teardown?
        }
        return NULL;
    }
    *out_decode_end = decode_end;

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);

    GstVideoInfo info;
    gst_video_info_from_caps(&info, caps);
    gint width = GST_VIDEO_INFO_WIDTH(&info);
    gint height = GST_VIDEO_INFO_HEIGHT(&info);
    //    ALOGI("%s: frame %d (w) x %d (h)", __FUNCTION__, width, height);

    // TODO: Handle resize?
#if 0
    if (width != sc->width || height != sc->height) {
        sc->width = width;
        sc->height = height;
    }
#endif

    struct em_sc_sample *ret = calloc(1, sizeof(struct em_sc_sample));

    GstVideoFrame frame;
    GstMapFlags flags = (GstMapFlags)(GST_MAP_READ | GST_MAP_GL);
    gst_video_frame_map(&frame, &info, buffer, flags);
    ret->base.frame_texture_id = *(GLuint *)frame.data[0];

    if (sc->context == NULL) {
        ALOGI("%s: Retrieving the GStreamer EGL context", __FUNCTION__);
        /* Get GStreamer's gl context. */
        gst_gl_query_local_gl_context(sc->appsink, GST_PAD_SINK, &sc->context);

        /* Check if we have 2D or OES textures */
        GstStructure *s = gst_caps_get_structure(caps, 0);
        const gchar *texture_target_str = gst_structure_get_string(s, "texture-target");
        if (g_str_equal(texture_target_str, GST_GL_TEXTURE_TARGET_EXTERNAL_OES_STR)) {
            sc->frame_texture_target = GL_TEXTURE_EXTERNAL_OES;
        } else if (g_str_equal(texture_target_str, GST_GL_TEXTURE_TARGET_2D_STR)) {
            sc->frame_texture_target = GL_TEXTURE_2D;
            ALOGE("Got GL_TEXTURE_2D instead of expected GL_TEXTURE_EXTERNAL_OES");
        } else {
            g_assert_not_reached();
        }
    }
    ret->base.frame_texture_target = sc->frame_texture_target;

    GstGLSyncMeta *sync_meta = gst_buffer_get_gl_sync_meta(buffer);
    if (sync_meta) {
        /* MOSHI: the set_sync() seems to be needed for resizing */
        gst_gl_sync_meta_set_sync_point(sync_meta, sc->context);
        gst_gl_sync_meta_wait(sync_meta, sc->context);
    }

    gst_video_frame_unmap(&frame);
    // Move sample ownership into the return value
    ret->sample = sample;

    // Check pipeline
    //    gchar* dot_data = gst_debug_bin_to_dot_data(GST_BIN(sc->pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
    //    g_free(dot_data);

    return ret;
}

void em_stream_client_release_sample(EmStreamClient *sc, struct em_sample *ems) {
    struct em_sc_sample *impl = (struct em_sc_sample *)ems;
    //    ALOGI("Releasing sample with texture ID %d", ems->frame_texture_id);
    gst_sample_unref(impl->sample);
    free(impl);
}

/*
 * Helper functions
 */

static void em_stream_client_set_connection(EmStreamClient *sc, EmConnection *connection) {
    g_clear_object(&sc->connection);
    if (connection != NULL) {
        sc->connection = g_object_ref(connection);
        g_signal_connect(sc->connection, "on-need-pipeline", G_CALLBACK(on_need_pipeline_cb), sc);
        g_signal_connect(sc->connection, "on-drop-pipeline", G_CALLBACK(on_drop_pipeline_cb), sc);
        ALOGI("%s: EmConnection assigned", __FUNCTION__);
    }
}
