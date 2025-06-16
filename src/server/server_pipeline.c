#include "server_pipeline.h"

#include <gst/gst.h>
#include <gst/gststructure.h>

#include "../utils/logger.h"
#include "signaling_server.h"

#define GST_USE_UNSTABLE_API

#include <gst/webrtc/datachannel.h>
#include <gst/webrtc/rtcsessiondescription.h>

#undef GST_USE_UNSTABLE_API

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define AUDIO_TEE_NAME "audio_tee"
#define VIDEO_TEE_NAME "video_tee"

// Use encodebin instead of x264enc
#define USE_ENCODEBIN

static SignalingServer* signaling_server = NULL;

struct MyGstData {
    GstElement* pipeline;
    GstElement* webrtcbin;

    GObject* data_channel;
    guint timeout_src_id;
};

static gboolean sigint_handler(gpointer user_data) {
    g_main_loop_quit(user_data);
    return G_SOURCE_REMOVE;
}

static gboolean gst_bus_cb(GstBus* bus, GstMessage* message, gpointer user_data) {
    struct MyGstData* mgd = user_data;
    GstBin* pipeline = GST_BIN(mgd->pipeline);

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError* gerr;
            gchar* debug_msg;
            gst_message_parse_error(message, &gerr, &debug_msg);
            GST_DEBUG_BIN_TO_DOT_FILE(pipeline, GST_DEBUG_GRAPH_SHOW_ALL, "mss-pipeline-ERROR");
            g_error("Error: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
        } break;
        case GST_MESSAGE_WARNING: {
            GError* gerr;
            gchar* debug_msg;
            gst_message_parse_warning(message, &gerr, &debug_msg);
            GST_DEBUG_BIN_TO_DOT_FILE(pipeline, GST_DEBUG_GRAPH_SHOW_ALL, "mss-pipeline-WARNING");
            g_warning("Warning: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
        } break;
        case GST_MESSAGE_EOS: {
            g_error("Got EOS!!");
        } break;
        case GST_MESSAGE_LATENCY: {
            gst_bin_recalculate_latency(pipeline);
        } break;
        default:
            break;
    }
    return TRUE;
}

static GstElement* get_webrtcbin_for_client(GstBin* pipeline, ClientId client_id) {
    gchar* name = g_strdup_printf("webrtcbin_%p", client_id);
    GstElement* webrtcbin = gst_bin_get_by_name(pipeline, name);
    g_free(name);

    return webrtcbin;
}

static void link_webrtc_to_tee(GstElement* webrtcbin) {
    GstElement* pipeline = GST_ELEMENT(gst_element_get_parent(webrtcbin));
    if (pipeline == NULL) return;

    {
        GstElement* tee = gst_bin_get_by_name(GST_BIN(pipeline), VIDEO_TEE_NAME);
        GstPad* src_pad = gst_element_request_pad_simple(tee, "src_%u");

        GstPadTemplate* pad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(webrtcbin), "sink_%u");

        GstCaps* caps = gst_caps_from_string(
            "application/x-rtp,"
            "payload=96,encoding-name=H264,clock-rate=90000,media=video,packetization-mode=(string)1,"
            "profile-level-id=(string)42e01f");

        GstPad* sink_pad = gst_element_request_pad(webrtcbin, pad_template, "sink_0", caps);

        GstPadLinkReturn ret = gst_pad_link(src_pad, sink_pad);
        g_assert(ret == GST_PAD_LINK_OK);

        gst_caps_unref(caps);
        gst_object_unref(src_pad);
        gst_object_unref(sink_pad);
        gst_object_unref(tee);
    }

    {
        GstElement* tee = gst_bin_get_by_name(GST_BIN(pipeline), AUDIO_TEE_NAME);
        GstPad* src_pad = gst_element_request_pad_simple(tee, "src_%u");

        GstPadTemplate* pad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(webrtcbin), "sink_%u");

        GstCaps* caps =
            gst_caps_from_string("application/x-rtp,payload=127,encoding-name=OPUS,clock-rate=48000,media=audio");

        GstPad* sink_pad = gst_element_request_pad(webrtcbin, pad_template, "sink_1", caps);

        GstPadLinkReturn ret = gst_pad_link(src_pad, sink_pad);
        g_assert(ret == GST_PAD_LINK_OK);

        gst_caps_unref(caps);
        gst_object_unref(src_pad);
        gst_object_unref(sink_pad);
        gst_object_unref(tee);
    }

    {
        GArray* transceivers;
        g_signal_emit_by_name(webrtcbin, "get-transceivers", &transceivers);
        g_assert(transceivers != NULL);

        for (int idx = 0; idx < transceivers->len; idx++) {
            GstWebRTCRTPTransceiver* trans = g_array_index(transceivers, GstWebRTCRTPTransceiver*, idx);
            g_object_set(trans, "direction", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY, NULL);
            g_object_set(trans, "fec-type", GST_WEBRTC_FEC_TYPE_ULP_RED, "fec-percentage", 20, NULL);
        }

        g_array_unref(transceivers);
    }

    // gchar* dot_data = gst_debug_bin_to_dot_data(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
    // g_free(dot_data);

    gst_object_unref(pipeline);

    ALOGD("Linked webrtcbin to tee");
}

static void on_offer_created(GstPromise* promise, GstElement* webrtcbin) {
    GstWebRTCSessionDescription* offer = NULL;

    // Create offer
    gst_structure_get(gst_promise_get_reply(promise), "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
    gst_promise_unref(promise);

    g_signal_emit_by_name(webrtcbin, "set-local-description", offer, NULL);

    gchar* sdp = gst_sdp_message_as_text(offer->sdp);
    signaling_server_send_sdp_offer(signaling_server, g_object_get_data(G_OBJECT(webrtcbin), "client_id"), sdp);
    g_free(sdp);

    gst_webrtc_session_description_free(offer);
}

static void webrtc_on_data_channel_cb(GstElement* webrtcbin, GObject* data_channel, struct MyGstData* mgd) {
    ALOGD(__func__);
}

static void webrtc_on_ice_candidate_cb(GstElement* webrtcbin, guint m_line_index, gchar* candidate) {
    signaling_server_send_candidate(signaling_server,
                                    g_object_get_data(G_OBJECT(webrtcbin), "client_id"),
                                    m_line_index,
                                    candidate);
}

static void data_channel_error_cb(GstWebRTCDataChannel* data_channel, struct MyGstData* mgd) {
    ALOGE(__func__);
}

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

gboolean data_channel_send_message(GstWebRTCDataChannel* data_channel) {
    g_signal_emit_by_name(data_channel, "send-string", "Hi! from test server");

    char test_data_buf[] = "This is some test data";
    GBytes* b = g_bytes_new_static(test_data_buf, ARRAY_SIZE(test_data_buf));
    gst_webrtc_data_channel_send_data(data_channel, b);

    return G_SOURCE_CONTINUE;
}

static void data_channel_open_cb(GstWebRTCDataChannel* data_channel, struct MyGstData* mgd) {
    ALOGD("Data channel opened");

    mgd->timeout_src_id = g_timeout_add_seconds(3, G_SOURCE_FUNC(data_channel_send_message), data_channel);
}

static void data_channel_close_cb(GstWebRTCDataChannel* data_channel, struct MyGstData* mgd) {
    ALOGD("Data channel closed");

    g_clear_handle_id(&mgd->timeout_src_id, g_source_remove);
    g_clear_object(&mgd->data_channel);
}

static void data_channel_message_data_cb(GstWebRTCDataChannel* data_channel, GBytes* data, struct MyGstData* mgd) {
    g_print("Received data channel message data, size: %u\n", (uint32_t)g_bytes_get_size(data));
}

static void data_channel_message_string_cb(GstWebRTCDataChannel* data_channel, gchar* str, struct MyGstData* mgd) {
    ALOGD("Received data channel message: %s", str);
}

static void webrtc_client_connected_cb(SignalingServer* server, ClientId client_id, struct MyGstData* mgd) {
    ALOGD("Client connected, ID: %p", client_id);

    GstStateChangeReturn ret;

    GstBin* pipeline_bin = GST_BIN(mgd->pipeline);

    gchar* name = g_strdup_printf("webrtcbin_%p", client_id);
    GstElement* webrtcbin = gst_element_factory_make("webrtcbin", name);

    g_object_set(webrtcbin, "bundle-policy", GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE, NULL);
    g_object_set_data(G_OBJECT(webrtcbin), "client_id", client_id);
    gst_bin_add(pipeline_bin, webrtcbin);
    mgd->webrtcbin = webrtcbin;

    ret = gst_element_set_state(webrtcbin, GST_STATE_READY);
    g_assert(ret != GST_STATE_CHANGE_FAILURE);

    g_signal_connect(webrtcbin, "on-data-channel", G_CALLBACK(webrtc_on_data_channel_cb), NULL);

    // I also think this would work if the pipeline state is READY but /shrug

    // Set up data channel
    {
        // TODO add priority
        GstStructure* data_channel_options = gst_structure_new_from_string("data-channel-options, ordered=true");
        g_signal_emit_by_name(webrtcbin, "create-data-channel", "channel", data_channel_options, &mgd->data_channel);
        gst_clear_structure(&data_channel_options);

        if (!mgd->data_channel) {
            ALOGE("Couldn't create data channel!");
            assert(false);
        } else {
            ALOGD("Successfully created data channel");

            g_signal_connect(mgd->data_channel, "on-open", G_CALLBACK(data_channel_open_cb), mgd);
            g_signal_connect(mgd->data_channel, "on-close", G_CALLBACK(data_channel_close_cb), mgd);
            g_signal_connect(mgd->data_channel, "on-error", G_CALLBACK(data_channel_error_cb), mgd);
            g_signal_connect(mgd->data_channel, "on-message-data", G_CALLBACK(data_channel_message_data_cb), mgd);
            g_signal_connect(mgd->data_channel, "on-message-string", G_CALLBACK(data_channel_message_string_cb), mgd);
        }
    }

    g_signal_connect(webrtcbin, "on-ice-candidate", G_CALLBACK(webrtc_on_ice_candidate_cb), NULL);

    link_webrtc_to_tee(webrtcbin);

    GstPromise* promise = gst_promise_new_with_change_func((GstPromiseChangeFunc)on_offer_created, webrtcbin, NULL);
    g_signal_emit_by_name(webrtcbin, "create-offer", NULL, promise);

    ret = gst_element_set_state(webrtcbin, GST_STATE_PLAYING);
    g_assert(ret != GST_STATE_CHANGE_FAILURE);

    g_free(name);
}

static void webrtc_sdp_answer_cb(SignalingServer* server, ClientId client_id, const gchar* sdp, struct MyGstData* mgd) {
    GstBin* pipeline = GST_BIN(mgd->pipeline);
    GstSDPMessage* sdp_msg = NULL;
    GstWebRTCSessionDescription* desc = NULL;

    if (gst_sdp_message_new_from_text(sdp, &sdp_msg) != GST_SDP_OK) {
        g_debug("Error parsing SDP description");
        goto out;
    }

    desc = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp_msg);
    if (desc) {
        GstElement* webrtcbin = get_webrtcbin_for_client(pipeline, client_id);
        if (!webrtcbin) {
            goto out;
        }

        GstPromise* promise = gst_promise_new();

        g_signal_emit_by_name(webrtcbin, "set-remote-description", desc, promise);

        gst_promise_wait(promise);
        gst_promise_unref(promise);

        gst_object_unref(webrtcbin);
    } else {
        gst_sdp_message_free(sdp_msg);
    }

out:
    g_clear_pointer(&desc, gst_webrtc_session_description_free);
}

static void webrtc_candidate_cb(SignalingServer* server,
                                ClientId client_id,
                                guint m_line_index,
                                const gchar* candidate,
                                struct MyGstData* mgd) {
    GstBin* pipeline = GST_BIN(mgd->pipeline);

    if (strlen(candidate)) {
        GstElement* webrtcbin = get_webrtcbin_for_client(pipeline, client_id);
        if (webrtcbin) {
            g_signal_emit_by_name(webrtcbin, "add-ice-candidate", m_line_index, candidate);
            gst_object_unref(webrtcbin);
        }
    }

    g_debug("Remote candidate: %s", candidate);
}

static GstPadProbeReturn remove_webrtcbin_probe_cb(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    GstElement* webrtcbin = GST_ELEMENT(user_data);

    gst_bin_remove(GST_BIN(GST_ELEMENT_PARENT(webrtcbin)), webrtcbin);
    gst_element_set_state(webrtcbin, GST_STATE_NULL);

    return GST_PAD_PROBE_REMOVE;
}

static void webrtc_client_disconnected_cb(SignalingServer* server, ClientId client_id, struct MyGstData* mgd) {
    GstBin* pipeline = GST_BIN(mgd->pipeline);

    GstElement* webrtcbin = get_webrtcbin_for_client(pipeline, client_id);

    if (webrtcbin) {
        GstPad* sinkpad;

        sinkpad = gst_element_get_static_pad(webrtcbin, "sink_0");
        if (sinkpad) {
            gst_pad_add_probe(GST_PAD_PEER(sinkpad),
                              GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
                              remove_webrtcbin_probe_cb,
                              webrtcbin,
                              gst_object_unref);

            gst_clear_object(&sinkpad);
        }
    }
}

struct RestartData {
    GstElement* src;
    GstElement* pipeline;
};

static void free_restart_data(gpointer user_data) {
    struct RestartData* rd = user_data;

    gst_object_unref(rd->src);
    g_free(rd);
}

static gboolean restart_source(gpointer user_data) {
    struct RestartData* rd = user_data;

    gst_element_set_state(rd->src, GST_STATE_NULL);
    gst_element_set_locked_state(rd->src, TRUE);
    GstElement* e = gst_bin_get_by_name(GST_BIN(rd->pipeline), "srtqueue");
    gst_bin_add(GST_BIN(rd->pipeline), rd->src);
    if (!gst_element_link(rd->src, e)) g_assert_not_reached();
    gst_element_set_locked_state(rd->src, FALSE);
    GstStateChangeReturn ret = gst_element_set_state(rd->src, GST_STATE_PLAYING);
    g_assert(ret != GST_STATE_CHANGE_FAILURE);
    gst_object_unref(e);

    g_debug("Restarted source after EOS");

    return G_SOURCE_REMOVE;
}

static GstPadProbeReturn src_event_cb(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    GstElement* pipeline = user_data;

    if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) != GST_EVENT_EOS) return GST_PAD_PROBE_PASS;

    GstElement* src = gst_pad_get_parent_element(pad);

    gst_bin_remove(GST_BIN(pipeline), src);

    struct RestartData* rd = g_new(struct RestartData, 1);
    rd->src = src;
    rd->pipeline = pipeline;
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, restart_source, rd, free_restart_data);

    return GST_PAD_PROBE_DROP;
}

GMainLoop* main_loop;

void* loop_thread(void* data) {
    g_main_loop_run(main_loop);
    return NULL;
}

/*
 *
 * Exported functions.
 *
 */

void server_pipeline_play(struct MyGstData* mgd) {
    ALOGD("Starting pipeline");
    main_loop = g_main_loop_new(NULL, FALSE);

    // Play the pipeline
    // Note that webrtcbin is not linked yet
    GstStateChangeReturn ret = gst_element_set_state(mgd->pipeline, GST_STATE_PLAYING);
    g_assert(ret != GST_STATE_CHANGE_FAILURE);

    g_signal_connect(signaling_server, "ws-client-connected", G_CALLBACK(webrtc_client_connected_cb), mgd);

    GThread* thread = g_thread_new("loop_thread", (GThreadFunc)loop_thread, NULL);
}

void server_pipeline_stop(struct MyGstData* mgd) {
    ALOGD("Stopping pipeline");

    // Settle the pipeline.
    ALOGD("Sending EOS");
    gst_element_send_event(mgd->pipeline, gst_event_new_eos());

    // Wait for an EOS message on the pipeline bus.
    ALOGD("Waiting for EOS");
    GstMessage* msg = NULL;
    msg = gst_bus_timed_pop_filtered(GST_ELEMENT_BUS(mgd->pipeline),
                                     GST_CLOCK_TIME_NONE,
                                     GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
    //! @todo Should check if we got an error message here or an eos.
    (void)msg;

    // Completely stop the pipeline.
    ALOGD("Setting to NULL");
    gst_element_set_state(mgd->pipeline, GST_STATE_NULL);
}

void gstAndroidLog(GstDebugCategory* category,
                   GstDebugLevel level,
                   const gchar* file,
                   const gchar* function,
                   gint line,
                   GObject* object,
                   GstDebugMessage* message,
                   gpointer data) {
    if (level <= gst_debug_category_get_threshold(category)) {
        if (level == GST_LEVEL_ERROR) {
            ALOGE("%s, %s: %s", file, function, gst_debug_message_get(message));
        } else {
            ALOGD("%s, %s: %s", file, function, gst_debug_message_get(message));
        }
    }
}

#define U_TYPED_CALLOC(TYPE) ((TYPE*)calloc(1, sizeof(TYPE)))

static GstPadProbeReturn buffer_probe_cb(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
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

void server_pipeline_create(struct MyGstData** out_gst_data) {
    GError* error = NULL;

    signaling_server = signaling_server_new();

    struct MyGstData* mgd = U_TYPED_CALLOC(struct MyGstData);

    // Trace logs
    setenv("GST_DEBUG", "GST_TRACER:7", 1);
    setenv("GST_TRACERS", "latency(flags=element+pipeline)", 1); // Latency
    setenv("GST_DEBUG_FILE", "./latency.log", 1);                // Redirect log to a file

    // Specify dot file dir
    setenv("GST_DEBUG_DUMP_DOT_DIR", "./", 1);

    // Do not do ansi color codes
    setenv("GST_DEBUG_NO_COLOR", "1", 1);

    // Set up gst logger
    {
#ifdef __ANDROID__
        gst_debug_add_log_function(&gstAndroidLog, NULL, NULL);
#endif

        gst_debug_set_default_threshold(GST_LEVEL_WARNING);
        gst_debug_set_threshold_for_name("encodebin2", GST_LEVEL_INFO);
        gst_debug_set_threshold_for_name("webrtcbin", GST_LEVEL_INFO);
        gst_debug_set_threshold_for_name("fec", GST_LEVEL_INFO);
        //        gst_debug_set_threshold_for_name("webrtcbin", GST_LEVEL_MEMDUMP);
        //        gst_debug_set_threshold_for_name("webrtcbindatachannel", GST_LEVEL_TRACE);
    }

    gst_init(NULL, NULL);

    // Setup pipeline
    // is-live=true is to fix first frame delay
    gchar* pipeline_str = g_strdup_printf(
        // Audio
        "audiotestsrc is-live=true wave=red-noise ! "
        "audioconvert ! "
        "audioresample ! "
        "queue ! "
        "opusenc perfect-timestamp=true ! "
        "rtpopuspay ! "
        "application/x-rtp,encoding-name=OPUS,payload=127,ssrc=(uint)3484078953 ! "
        "queue ! "
        "tee name=%s allow-not-linked=true "
        // Video
        // "filesrc location=test.mp4 ! decodebin3 ! "
        "videotestsrc pattern=colors is-live=true horizontal-speed=2 ! "
        "timeoverlay ! "
        "video/x-raw,format=NV12,width=1280,height=720,framerate=60/1 ! "
#ifdef USE_ENCODEBIN
        // zerolatency is not available for some hw encoders
        "encodebin2 profile=\"video/x-h264|element-properties,bitrate=8192\" ! "
#else
        "x264enc tune=zerolatency bitrate=8192 ! "
        "video/x-h264,profile=baseline ! "
#endif
        "h264parse name=parser ! "
        "rtph264pay config-interval=-1 aggregate-mode=zero-latency ! "
        "application/x-rtp,payload=96,ssrc=(uint)3484078952 ! "
        "tee name=%s allow-not-linked=true",
        AUDIO_TEE_NAME,
        VIDEO_TEE_NAME);
    // No webrtcbin yet until later!

    GstElement* pipeline = gst_parse_launch(pipeline_str, &error);
    if (error) {
        ALOGE("Pipeline parsing error: %s", error->message);
    }
    g_assert_no_error(error);
    g_free(pipeline_str);

    // GstPad* pad = gst_element_get_static_pad(gst_bin_get_by_name(GST_BIN(pipeline), "parser"), "src");
    // gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)buffer_probe_cb, NULL, NULL);
    // gst_object_unref(pad);

    GstBus* bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, gst_bus_cb, mgd);
    gst_object_unref(bus);

    // "ws-client-connected" will be connected later when the pipeline starts playing
    g_signal_connect(signaling_server, "ws-client-disconnected", G_CALLBACK(webrtc_client_disconnected_cb), mgd);
    g_signal_connect(signaling_server, "sdp-answer", G_CALLBACK(webrtc_sdp_answer_cb), mgd);
    g_signal_connect(signaling_server, "candidate", G_CALLBACK(webrtc_candidate_cb), mgd);

    mgd->pipeline = pipeline;
    *out_gst_data = mgd;
}

void server_pipeline_dump(struct MyGstData* mgd) {
    // GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(mgd->pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");
    gchar* dot_data = gst_debug_bin_to_dot_data(GST_BIN(mgd->pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
    g_free(dot_data);
}
