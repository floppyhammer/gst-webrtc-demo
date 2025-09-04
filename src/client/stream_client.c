#include "stream_client.h"

#include <gst/app/gstappsink.h>
#ifdef ANDROID
    #include <gst/gl/gl.h>
    #include <gst/gl/gstglsyncmeta.h>
#endif
#include <gst/gst.h>
#include <gst/gstbus.h>
#include <gst/gstelement.h>
#include <gst/gstinfo.h>
#include <gst/gstmessage.h>
#include <gst/gstsample.h>
#include <gst/gstutils.h>
#include <gst/video/video-frame.h>
#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
#undef GST_USE_UNSTABLE_API
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../common/general.h"
#include "../utils/logger.h"
#include "connection.h"
#include "gst_common.h"

#ifdef ANDROID
    #include <EGL/egl.h>
    #include <GLES2/gl2ext.h>
#endif

#ifdef ANDROID
struct my_sc_sample {
    struct my_sample base;
    GstSample *sample;
};
#endif

/*!
 * Run function.
 *
 * @public @memberof os_thread
 */
typedef void *(*os_run_func_t)(void *);

struct os_thread_helper {
    GThread *thread;
    GMutex mutex;
    GCond cond;

    bool initialized;
    bool running;
};

/*!
 * Initialize the thread helper.
 *
 * @public @memberof os_thread_helper
 */
static int os_thread_helper_init(struct os_thread_helper *oth) {
    g_assert(oth != NULL);
    memset(oth, 0, sizeof(struct os_thread_helper));

    g_mutex_init(&oth->mutex);
    g_cond_init(&oth->cond);

    oth->initialized = true;

    return 0;
}

static int os_thread_helper_start(struct os_thread_helper *oth, os_run_func_t func, void *ptr) {
    g_mutex_lock(&oth->mutex);

    g_assert(oth->initialized);

    if (oth->running) {
        g_mutex_unlock(&oth->mutex);
        return -1;
    }

    oth->thread = g_thread_new(NULL, func, ptr);
    if (!oth->thread) {
        g_mutex_unlock(&oth->mutex);
        return -1; // -1 for "creation failed"
    }

    oth->running = true;

    g_mutex_unlock(&oth->mutex);

    return 0;
}

struct my_stream_client {
    GMainLoop *loop;
    MyConnection *connection;
    GstElement *pipeline;

#ifdef ANDROID
    GstGLDisplay *gst_gl_display;
    GstGLContext *gst_gl_context;
    GstGLContext *gst_gl_other_context;

    GstGLDisplay *display;

    /// Wrapped version of the android_main/render context
    GstGLContext *android_main_context;

    /// GStreamer-created EGL context for its own use
    GstGLContext *context;

    struct {
        EGLDisplay display;
        EGLContext android_main_context;
        // 16x16 pbuffer surface
        EGLSurface surface;
    } egl;

    GLenum frame_texture_target;
    GLenum texture_target;
    GLuint texture_id;

    int width;
    int height;
#endif

    GstElement *app_sink;

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

static void on_need_pipeline_cb(MyConnection *my_conn, MyStreamClient *sc);

static void on_drop_pipeline_cb(MyConnection *my_conn, MyStreamClient *sc);

static void *my_stream_client_thread_func(void *ptr);

/*
 * Helper functions
 */

static void my_stream_client_set_connection(MyStreamClient *sc, MyConnection *connection);

/* GObject method implementations */

static void my_stream_client_init(MyStreamClient *sc) {
    g_assert_nonnull(sc);
    memset(sc, 0, sizeof(MyStreamClient));

    sc->loop = g_main_loop_new(NULL, FALSE);
    g_assert(os_thread_helper_init(&sc->play_thread) >= 0);
    g_mutex_init(&sc->sample_mutex);
}

#ifdef ANDROID
void my_stream_client_set_egl_context(MyStreamClient *sc, EGLContext context, EGLDisplay display, EGLSurface surface) {
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
#endif

static void my_stream_client_dispose(MyStreamClient *self) {
    // May be called multiple times during destruction.
    // Stop things and clear ref counted things here.
    // MyStreamClient *self = EM_STREAM_CLIENT(object);
    my_stream_client_stop(self);
    g_clear_object(&self->loop);
    g_clear_object(&self->connection);
    gst_clear_object(&self->sample);
    gst_clear_object(&self->pipeline);
#ifdef ANDROID
    gst_clear_object(&self->gst_gl_display);
    gst_clear_object(&self->gst_gl_context);
    gst_clear_object(&self->gst_gl_other_context);
    gst_clear_object(&self->display);
    gst_clear_object(&self->context);
#endif
    gst_clear_object(&self->app_sink);
}

static void my_stream_client_finalize(MyStreamClient *self) {
    // Only called once, after dispose
}

/*
 * Callbacks
 */

#ifdef ANDROID
static GstBusSyncReply bus_sync_handler_cb(GstBus *bus, GstMessage *msg, MyStreamClient *sc) {
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
#endif

static gboolean gst_bus_cb(GstBus *bus, GstMessage *message, gpointer user_data) {
    GstBin *pipeline = GST_BIN(user_data);

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *gerr = NULL;
            gchar *debug_msg = NULL;
            gst_message_parse_error(message, &gerr, &debug_msg);

            gchar *dot_data = gst_debug_bin_to_dot_data(pipeline, GST_DEBUG_GRAPH_SHOW_ALL);

            ALOGE("gst_bus_cb: Error: %s (%s)", gerr->message, debug_msg);
            g_error("gst_bus_cb: Error: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
            g_free(dot_data);
        } break;
        case GST_MESSAGE_WARNING: {
            GError *gerr = NULL;
            gchar *debug_msg = NULL;
            gst_message_parse_warning(message, &gerr, &debug_msg);
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

#ifdef ANDROID
static GstFlowReturn on_new_sample_cb(GstAppSink *appsink, gpointer user_data) {
    MyStreamClient *sc = (MyStreamClient *)user_data;

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
#endif

static GstPadProbeReturn buffer_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
        GstClockTime pts = GST_BUFFER_PTS(buf);

        static GstClockTime newest_pts = 0;
        static uint32_t seq_num = 0;

        if (newest_pts != 0) {
            int64_t pts_diff = ((int64_t)pts - (int64_t)newest_pts) / 1e6;

            if (pts_diff < 0) {
                ALOGE("Webrtcbin video src pad: buffer PTS: %" GST_TIME_FORMAT
                      ", PTS diff: %ld. Bad packet: decreasing timestamp",
                      GST_TIME_ARGS(pts),
                      pts_diff);
            } else if (pts_diff > 50) {
                ALOGE("Webrtcbin video src pad: buffer PTS: %" GST_TIME_FORMAT
                      ", PTS diff: %ld. Bad packet: arrives too late",
                      GST_TIME_ARGS(pts),
                      pts_diff);
            } else {
                ALOGD("Webrtcbin video src pad: buffer PTS: %" GST_TIME_FORMAT ", PTS diff: %ld",
                      GST_TIME_ARGS(pts),
                      pts_diff);
            }
        }
        newest_pts = newest_pts > pts ? newest_pts : pts;

        GstMapInfo map;
        if (gst_buffer_map(buf, &map, GST_MAP_READ)) {
            if (map.size >= 12) {
                guint8 *data = map.data;
                uint32_t new_seq_num = (data[2] << 8) | data[3];
                ALOGD("Webrtcbin video src pad: buffer sequence number: %u\n", new_seq_num);

                if (new_seq_num - seq_num > 1) {
                    ALOGE("Packet lost!");
                }

                seq_num = new_seq_num;
            }
            gst_buffer_unmap(buf, &map);
        }
    }
    return GST_PAD_PROBE_OK;
}

static void on_new_transceiver(GstElement *webrtc, GstWebRTCRTPTransceiver *trans) {
    g_object_set(trans, "fec-type", GST_WEBRTC_FEC_TYPE_ULP_RED, NULL);
}

// This is the gstwebrtc entry point where we create the offer and so on.
// It will be called when the pipeline goes to PLAYING.
static void on_negotiation_needed(GstElement *element, gpointer user_data) {
    // Pass
}

static void on_video_handoff(GstElement *identity, GstBuffer *buffer, gpointer user_data) {
    GstClockTime pts = GST_BUFFER_PTS(buffer);
    GstClockTime dts = GST_BUFFER_DTS(buffer);
    // g_print("Buffer PTS: %" GST_TIME_FORMAT ", DTS: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(pts), GST_TIME_ARGS(dts));
}

static void on_audio_handoff(GstElement *identity, GstBuffer *buffer, gpointer user_data) {
    const GstClockTime pts = GST_BUFFER_PTS(buffer);
    const GstClockTime duration = GST_BUFFER_DURATION(buffer);

    g_print("Audio buffer PTS: %" GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT "\n",
            GST_TIME_ARGS(pts),
            GST_TIME_ARGS(duration));
}

static void handle_media_stream(GstPad *src_pad, MyStreamClient *sc, const char *convert_name, const char *sink_name) {
    gst_println("Trying to handle stream with %s ! %s", convert_name, sink_name);

    // Audio
    if (g_strcmp0(convert_name, "audioconvert") == 0) {
        GstElement *q = gst_element_factory_make("queue", NULL);
        g_assert_nonnull(q);

        GstElement *conv = gst_element_factory_make(convert_name, NULL);
        g_assert_nonnull(conv);

        GstElement *sink = gst_element_factory_make(sink_name, NULL);
        g_assert_nonnull(sink);

        GstElement *identity = gst_element_factory_make("identity", NULL);
        g_assert_nonnull(identity);
        g_object_set(identity, "signal-handoffs", TRUE, NULL);
        g_signal_connect(identity, "handoff", G_CALLBACK(on_audio_handoff), NULL);

        /* Might also need to resample, so add it just in case.
         * Will be a no-op if it's not required. */
        GstElement *resample = gst_element_factory_make("audioresample", NULL);
        g_assert_nonnull(resample);

        gst_bin_add_many(GST_BIN(sc->pipeline), q, conv, resample, sink, identity, NULL);

        // State sync is necessary
        gst_element_sync_state_with_parent(q);
        gst_element_sync_state_with_parent(conv);
        gst_element_sync_state_with_parent(resample);
        gst_element_sync_state_with_parent(sink);
        gst_element_sync_state_with_parent(identity);

        gst_element_link_many(q, conv, identity, resample, sink, NULL);

        GstPad *q_pad = gst_element_get_static_pad(q, "sink");

        const GstPadLinkReturn ret = gst_pad_link(src_pad, q_pad);
        g_assert_cmphex(ret, ==, GST_PAD_LINK_OK);

        gst_object_unref(q_pad);
    }
    // Video
    else {
#ifdef ANDROID
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
            sc->app_sink = gst_element_factory_make("appsink", NULL);
            g_object_set(sc->app_sink,
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
            gst_app_sink_set_callbacks(GST_APP_SINK(sc->app_sink), &callbacks, sc, NULL);
            sc->received_first_frame = false;

            g_object_set(glsinkbin, "sink", sc->app_sink, NULL);
        }

        gst_element_sync_state_with_parent(glsinkbin);
#else
        GstElement *q = gst_element_factory_make("queue", NULL);
        g_assert_nonnull(q);

        GstElement *conv = gst_element_factory_make(convert_name, NULL);
        g_assert_nonnull(conv);

        GstElement *sink = gst_element_factory_make(sink_name, NULL);
        g_assert_nonnull(sink);

        GstElement *identity = gst_element_factory_make("identity", NULL);
        g_assert_nonnull(identity);
        g_object_set(identity, "signal-handoffs", TRUE, NULL);
        g_signal_connect(identity, "handoff", G_CALLBACK(on_video_handoff), NULL);

        g_object_set(sink, "sync", FALSE, NULL);

        gst_bin_add_many(GST_BIN(sc->pipeline), q, conv, sink, identity, NULL);
        gst_element_sync_state_with_parent(q);
        gst_element_sync_state_with_parent(conv);
        gst_element_sync_state_with_parent(sink);
        gst_element_sync_state_with_parent(identity);
        gst_element_link_many(q, conv, identity, sink, NULL);

        GstPad *q_pad = gst_element_get_static_pad(q, "sink");

        const GstPadLinkReturn ret = gst_pad_link(src_pad, q_pad);
        g_assert_cmphex(ret, ==, GST_PAD_LINK_OK);

        gst_object_unref(q_pad);
#endif
    }
}

static void on_decodebin_pad_added(GstElement *decodebin, GstPad *pad, MyStreamClient *sc) {
    // We don't care about sink pads
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) {
        return;
    }

    const GstCaps *caps = NULL;

    // When using decodebin
    if (gst_pad_has_current_caps(pad)) {
        caps = gst_pad_get_current_caps(pad);
    }
    // When using decodebin3
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
        gst_printerr("We should not use decodebin3 to handle audio");
        abort();
    } else {
        gst_printerr("Unknown pad %s, ignoring", GST_PAD_NAME(pad));
    }
}

static void on_prepare_data_channel(GstElement *webrtcbin,
                                    GstWebRTCDataChannel *channel,
                                    gboolean is_local,
                                    gpointer user_data) {
    ALOGE("%s", __FUNCTION__);

    // Adjust receive buffer size (IMPORTANT)
    GstWebRTCSCTPTransport *sctp_transport = NULL;
    g_object_get(webrtcbin, "sctp-transport", &sctp_transport, NULL);
    if (!sctp_transport) {
        g_error("Failed to get sctp_transport!");
    }

    GstWebRTCDTLSTransport *dtls_transport = NULL;
    g_object_get(sctp_transport, "transport", &dtls_transport, NULL);
    if (!dtls_transport) {
        g_error("Failed to get dtls_transport!");
    }

    GstWebRTCICETransport *ice_transport = NULL;
    g_object_get(dtls_transport, "transport", &ice_transport, NULL);

    if (ice_transport) {
        g_object_set(ice_transport, "receive-buffer-size", 16 * 1024 * 1024, NULL);
    } else {
        g_error("Failed to get ice_transport!");
    }

    g_object_unref(ice_transport);
    g_object_unref(dtls_transport);
    g_object_unref(sctp_transport);
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

static gboolean print_fec_stats(MyStreamClient *sc) {
    if (!sc) {
        return G_SOURCE_CONTINUE;
    }

    GstElement *webrtcbin = gst_bin_get_by_name(GST_BIN(sc->pipeline), "webrtc");

    //    GstElement *rtpbin = NULL;
    //    gst_bin_get_by_name(GST_BIN(webrtcbin), "rtpbin");
    //
    //    // For RTP session
    //    GstElement *rtpsession = gst_bin_get_by_name(GST_BIN(rtpbin), "rtpsession0");
    //    g_object_set(rtpsession, "rtcp-rr-bandwidth", 16777216, NULL);
    //    g_object_set(rtpsession, "bandwidth", 16777216, NULL);

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

        GstElement *rtpstorage = find_element_by_name(GST_BIN(webrtcbin), name_storage);
        if (rtpstorage) {
            g_object_set(rtpstorage, "size-time", 220000000, NULL);
        }
    }

    return G_SOURCE_CONTINUE;
}

static void on_webrtcbin_pad_added(GstElement *webrtcbin, GstPad *pad, MyStreamClient *sc) {
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
        gst_bin_add(GST_BIN(sc->pipeline), opusdec);

        gst_element_link(depay, opusdec);

        GstPad *src_pad = gst_element_get_static_pad(opusdec, "src");

#ifdef ANDROID
        const char *sink_name = "openslessink";
#else
        const char *sink_name = "autoaudiosink";
#endif

        handle_media_stream(src_pad, sc, "audioconvert", sink_name);

        gst_element_sync_state_with_parent(depay);
        gst_element_sync_state_with_parent(opusdec);
    } else {
        // Check webrtcbin output
        // gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)buffer_probe_cb, NULL, NULL);

        GstElement *decodebin = gst_element_factory_make("decodebin3", NULL);

        g_signal_connect(decodebin, "pad-added", G_CALLBACK(on_decodebin_pad_added), sc);
        gst_bin_add(GST_BIN(sc->pipeline), decodebin);

        // Print stats repeatedly
        //        sc->timeout_src_id_print_stats = g_timeout_add_seconds(3, G_SOURCE_FUNC(print_stats), sc);

        GstPad *sink_pad = gst_element_get_static_pad(decodebin, "sink");
        gst_pad_link(pad, sink_pad);
        gst_object_unref(sink_pad);

        gst_element_sync_state_with_parent(decodebin);
    }
}

static void on_need_pipeline_cb(MyConnection *my_conn, MyStreamClient *sc) {
    g_info("%s", __FUNCTION__);
    g_assert_nonnull(sc);
    g_assert_nonnull(my_conn);

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
    g_object_set(webrtcbin, "latency", 50, NULL);

    // Connect callbacks on webrtcbin
    // g_signal_connect(webrtcbin, "on-negotiation-needed", G_CALLBACK(on_negotiation_needed), NULL);
    // g_signal_connect(webrtcbin, "on-data-channel", G_CALLBACK(webrtc_on_data_channel_cb), NULL);
    // g_signal_connect(webrtcbin, "on-ice-candidate", G_CALLBACK(webrtc_on_ice_candidate_cb), NULL);
    g_signal_connect(webrtcbin, "on-new-transceiver", G_CALLBACK(on_new_transceiver), NULL);
    g_signal_connect(webrtcbin, "pad-added", G_CALLBACK(on_webrtcbin_pad_added), sc);
    g_signal_connect(webrtcbin, "prepare-data-channel", G_CALLBACK(on_prepare_data_channel), NULL);

    gst_bin_add_many(GST_BIN(sc->pipeline), webrtcbin, NULL);

    {
        GstBus *bus = gst_element_get_bus(sc->pipeline);

#ifdef ANDROID
        // We set this up to inject the EGL context
        gst_bus_set_sync_handler(bus, (GstBusSyncHandler)bus_sync_handler_cb, sc, NULL);
#endif

        // This just watches for errors and such
        gst_bus_add_watch(bus, gst_bus_cb, sc->pipeline);

        g_object_unref(bus);
    }

    // This actually hands over the pipeline. Once our own handler returns,
    // the pipeline will be started by the connection.
    g_signal_emit_by_name(my_conn, "set-pipeline", GST_PIPELINE(sc->pipeline), NULL);

    sc->timeout_src_id_dot_data = g_timeout_add_seconds(3, G_SOURCE_FUNC(check_pipeline_dot_data), sc->pipeline);
}

static void on_drop_pipeline_cb(MyConnection *my_conn, MyStreamClient *sc) {
    if (sc->pipeline) {
        gst_element_set_state(sc->pipeline, GST_STATE_NULL);
    }
    gst_clear_object(&sc->pipeline);
    gst_clear_object(&sc->app_sink);
}

static void *my_stream_client_thread_func(void *ptr) {
    const MyStreamClient *sc = ptr;

    ALOGI("%s: running GMainLoop", __FUNCTION__);
    g_main_loop_run(sc->loop);
    ALOGI("%s: g_main_loop_run returned", __FUNCTION__);

    return NULL;
}

/*
 * Public functions
 */
MyStreamClient *my_stream_client_new() {
    MyStreamClient *self = calloc(1, sizeof(MyStreamClient));
    my_stream_client_init(self);
    return self;
}

void my_stream_client_destroy(MyStreamClient **ptr_sc) {
    if (ptr_sc == NULL) {
        return;
    }
    MyStreamClient *sc = *ptr_sc;
    if (sc == NULL) {
        return;
    }
    my_stream_client_dispose(sc);
    my_stream_client_finalize(sc);
    free(sc);
    *ptr_sc = NULL;
}

void my_stream_client_spawn_thread(MyStreamClient *sc, MyConnection *connection) {
    ALOGI("%s: Starting stream client mainloop thread", __FUNCTION__);
    my_stream_client_set_connection(sc, connection);
    const int ret = os_thread_helper_start(&sc->play_thread, &my_stream_client_thread_func, sc);
    g_assert(ret == 0);
}

void my_stream_client_stop(MyStreamClient *sc) {
    ALOGI("%s: Stopping pipeline and ending thread", __FUNCTION__);

    if (sc->pipeline != NULL) {
        gst_element_set_state(sc->pipeline, GST_STATE_NULL);
    }
    if (sc->connection != NULL) {
        my_connection_disconnect(sc->connection);
    }
    gst_clear_object(&sc->pipeline);
    gst_clear_object(&sc->app_sink);
#ifdef ANDROID
    gst_clear_object(&sc->context);
#endif
}

#ifdef ANDROID
struct my_sample *my_stream_client_try_pull_sample(MyStreamClient *sc, struct timespec *out_decode_end) {
    if (!sc->app_sink) {
        // Not setup yet.
        return NULL;
    }

    // We actually pull the sample in the new-sample signal handler,
    // so here we're just receiving the sample already pulled.
    GstSample *sample = NULL;
    struct timespec decode_end;
    {
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&sc->sample_mutex);
        sample = sc->sample;
        sc->sample = NULL;
        decode_end = sc->sample_decode_end_ts;
    }

    if (sample == NULL) {
        if (gst_app_sink_is_eos(GST_APP_SINK(sc->app_sink))) {
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

    struct my_sc_sample *ret = calloc(1, sizeof(struct my_sc_sample));

    GstVideoFrame frame;
    GstMapFlags flags = (GstMapFlags)(GST_MAP_READ | GST_MAP_GL);
    gst_video_frame_map(&frame, &info, buffer, flags);
    ret->base.frame_texture_id = *(GLuint *)frame.data[0];

    if (sc->context == NULL) {
        ALOGI("%s: Retrieving the GStreamer EGL context", __FUNCTION__);
        /* Get GStreamer's gl context. */
        gst_gl_query_local_gl_context(sc->app_sink, GST_PAD_SINK, &sc->context);

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

    return (struct my_sample *)ret;
}

void my_stream_client_release_sample(MyStreamClient *sc, struct my_sample *sample) {
    struct my_sc_sample *impl = (struct my_sc_sample *)sample;
    //    ALOGI("Releasing sample with texture ID %d", sample->frame_texture_id);
    gst_sample_unref(impl->sample);
    free(impl);
}
#endif

/*
 * Helper functions
 */

static void my_stream_client_set_connection(MyStreamClient *sc, MyConnection *connection) {
    g_clear_object(&sc->connection);
    if (connection != NULL) {
        sc->connection = g_object_ref(connection);
        g_signal_connect(sc->connection, "on-need-pipeline", G_CALLBACK(on_need_pipeline_cb), sc);
        g_signal_connect(sc->connection, "on-drop-pipeline", G_CALLBACK(on_drop_pipeline_cb), sc);
        ALOGI("%s: a connection assigned to the stream client", __FUNCTION__);
    }
}
