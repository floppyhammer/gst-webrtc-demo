#include "server_pipeline.h"

#include <gst/app/app.h>
#include <gst/gst.h>
#include <gst/gststructure.h>

#include "../common/general.h"
#include "../utils/logger.h"
#include "signaling_server.h"

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/datachannel.h>
#include <gst/webrtc/rtcsessiondescription.h>
#undef GST_USE_UNSTABLE_API

#include <stdint.h>
#include <stdio.h>

#define VIDEO_TEE_NAME "video_tee"
#define AUDIO_TEE_NAME "audio_tee"

static SignalingServer* signaling_server = NULL;

struct MyGstData {
    GstElement* pipeline;
    GstElement* webrtcbin;

    GObject* data_channel;

    guint timeout_src_id_msg;
    guint timeout_src_id_dot_data;
};

static gboolean gst_bus_cb(GstBus* bus, GstMessage* message, gpointer user_data) {
    const struct MyGstData* mgd = user_data;
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
            g_error("Got EOS!");
        } break;
        case GST_MESSAGE_LATENCY: {
            gst_bin_recalculate_latency(pipeline);
        } break;
        case GST_MESSAGE_QOS: {
            const GstStructure* s = gst_message_get_structure(message);
            const GValue* val = gst_structure_get_value(s, "avg-intra-downstream-bitrate");
            if (val) {
                const gdouble avg_intra_downstream_bitrate = g_value_get_double(val);
                g_print("QoS message: Average Intra Downstream Bitrate = %f bps\n", avg_intra_downstream_bitrate);
            }

            val = gst_structure_get_value(s, "avg-downstream-bitrate");
            if (val) {
                const gdouble avg_downstream_bitrate = g_value_get_double(val);
                g_print("QoS message: Average Downstream Bitrate = %f bps\n", avg_downstream_bitrate);

                // This is where you implement your dynamic bitrate adjustment logic
                // For example, if the average bitrate is too low, you might decrease the encoder bitrate
                // The value "500000" is an example and should be adjusted to your needs
                // if (avg_downstream_bitrate < 500000) {
                // 	g_object_set(video_encoder, "bitrate", (gint)avg_downstream_bitrate * 0.8, NULL);
                // 	g_print("Adjusting encoder bitrate to %d\n", (gint)avg_downstream_bitrate * 0.8);
                // }
            }

            val = gst_structure_get_value(s, "rtt");
            if (val) {
            }

            val = gst_structure_get_value(s, "jitter");
            if (val) {
            }
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

static void on_prepare_data_channel(GstElement* webrtcbin,
                                    GstWebRTCDataChannel* channel,
                                    gboolean is_local,
                                    gpointer user_data) {
    ALOGE("%s", __FUNCTION__);

    // Adjust receive buffer size (IMPORTANT)
    GstWebRTCSCTPTransport* sctp_transport = NULL;
    g_object_get(webrtcbin, "sctp-transport", &sctp_transport, NULL);
    if (!sctp_transport) {
        g_error("Failed to get sctp_transport!");
    }

    GstWebRTCDTLSTransport* dtls_transport = NULL;
    g_object_get(sctp_transport, "transport", &dtls_transport, NULL);
    if (!dtls_transport) {
        g_error("Failed to get dtls_transport!");
    }

    GstWebRTCICETransport* ice_transport = NULL;
    g_object_get(dtls_transport, "transport", &ice_transport, NULL);

    if (ice_transport) {
        g_object_set(ice_transport, "send-buffer-size", 16 * 1024 * 1024, NULL);
    } else {
        g_error("Failed to get ice_transport!");
    }

    g_object_unref(ice_transport);
    g_object_unref(dtls_transport);
    g_object_unref(sctp_transport);
}

static void link_webrtc_to_tee(GstElement* webrtcbin) {
    GstElement* pipeline = GST_ELEMENT(gst_element_get_parent(webrtcbin));
    g_assert(pipeline != NULL);

    {
        GstElement* tee = gst_bin_get_by_name(GST_BIN(pipeline), VIDEO_TEE_NAME);
        GstPad* src_pad = gst_element_request_pad_simple(tee, "src_%u");

        GstPadTemplate* pad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(webrtcbin), "sink_%u");

        GstCaps* caps = gst_caps_from_string(
            "application/x-rtp,"
            "payload=96,encoding-name=H264,clock-rate=90000,media=video,packetization-mode=(string)1");

        GstPad* sink_pad = gst_element_request_pad(webrtcbin, pad_template, "sink_0", caps);

        const GstPadLinkReturn ret = gst_pad_link(src_pad, sink_pad);
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

        const GstPadLinkReturn ret = gst_pad_link(src_pad, sink_pad);
        g_assert(ret == GST_PAD_LINK_OK);

        gst_caps_unref(caps);
        gst_object_unref(src_pad);
        gst_object_unref(sink_pad);
        gst_object_unref(tee);
    }

    // Config existing transceivers
    {
        GArray* transceivers;
        g_signal_emit_by_name(webrtcbin, "get-transceivers", &transceivers);
        g_assert(transceivers != NULL);

        g_signal_connect(webrtcbin, "prepare-data-channel", G_CALLBACK(on_prepare_data_channel), NULL);

        for (int idx = 0; idx < transceivers->len; idx++) {
            GstWebRTCRTPTransceiver* trans = g_array_index(transceivers, GstWebRTCRTPTransceiver*, idx);
            g_object_set(trans, "direction", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY, NULL);
            g_object_set(trans, "fec-type", GST_WEBRTC_FEC_TYPE_ULP_RED, "fec-percentage", 5, NULL);
        }

        g_array_unref(transceivers);
    }

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

    return G_SOURCE_REMOVE;
}

static void data_channel_open_cb(GstWebRTCDataChannel* data_channel, struct MyGstData* mgd) {
    ALOGD("Data channel opened");

    // mgd->timeout_src_id_msg = g_timeout_add_seconds(3, G_SOURCE_FUNC(data_channel_send_message), data_channel);
}

static void data_channel_close_cb(GstWebRTCDataChannel* data_channel, struct MyGstData* mgd) {
    ALOGD("Data channel closed");

    // g_clear_handle_id(&mgd->timeout_src_id_msg, g_source_remove);
    g_clear_object(&mgd->data_channel);
}

static void data_channel_message_data_cb(GstWebRTCDataChannel* data_channel, GBytes* data, struct MyGstData* mgd) {
    ALOGD("Received data channel message (data), size: %u\n", (uint32_t)g_bytes_get_size(data));
}

static void data_channel_message_string_cb(GstWebRTCDataChannel* data_channel, gchar* str, struct MyGstData* mgd) {
    ALOGD("Received data channel message (string): %s", str);
}

static void webrtc_client_connected_cb(SignalingServer* server, const ClientId client_id, struct MyGstData* mgd) {
    ALOGI("WebSocket client connected, ID: %p", client_id);

    GstBin* pipeline_bin = GST_BIN(mgd->pipeline);

    // Create webrtcbin
    gchar* name = g_strdup_printf("webrtcbin_%p", client_id);
    GstElement* webrtcbin = gst_element_factory_make("webrtcbin", name);
    g_free(name);

    g_object_set(webrtcbin, "bundle-policy", GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE, NULL);
    g_object_set_data(G_OBJECT(webrtcbin), "client_id", client_id); // Custom data

    g_signal_connect(webrtcbin, "on-ice-candidate", G_CALLBACK(webrtc_on_ice_candidate_cb), NULL);
    g_signal_connect(webrtcbin, "on-data-channel", G_CALLBACK(webrtc_on_data_channel_cb), NULL);

    gst_bin_add(pipeline_bin, webrtcbin);

    GstStateChangeReturn ret = gst_element_set_state(webrtcbin, GST_STATE_READY);
    g_assert(ret != GST_STATE_CHANGE_FAILURE);

    mgd->webrtcbin = webrtcbin;

    // I also think this would work if the pipeline state is READY but /shrug

    // Set up a data channel
    {
        // TODO add priority
        GstStructure* data_channel_options = gst_structure_new_from_string("data-channel-options, ordered=true");
        g_signal_emit_by_name(webrtcbin, "create-data-channel", "channel", data_channel_options, &mgd->data_channel);
        gst_clear_structure(&data_channel_options);

        // Make sure a data channel is successfully created
        g_assert(mgd->data_channel != NULL);

        g_signal_connect(mgd->data_channel, "on-open", G_CALLBACK(data_channel_open_cb), mgd);
        g_signal_connect(mgd->data_channel, "on-close", G_CALLBACK(data_channel_close_cb), mgd);
        g_signal_connect(mgd->data_channel, "on-error", G_CALLBACK(data_channel_error_cb), mgd);
        g_signal_connect(mgd->data_channel, "on-message-data", G_CALLBACK(data_channel_message_data_cb), mgd);
        g_signal_connect(mgd->data_channel, "on-message-string", G_CALLBACK(data_channel_message_string_cb), mgd);
    }

    link_webrtc_to_tee(webrtcbin);

    GstPromise* promise = gst_promise_new_with_change_func((GstPromiseChangeFunc)on_offer_created, webrtcbin, NULL);
    g_signal_emit_by_name(webrtcbin, "create-offer", NULL, promise);

    ret = gst_element_set_state(webrtcbin, GST_STATE_PLAYING);
    g_assert(ret != GST_STATE_CHANGE_FAILURE);

    // Debug
    mgd->timeout_src_id_dot_data = g_timeout_add_seconds(3, G_SOURCE_FUNC(check_pipeline_dot_data), mgd->pipeline);
}

static void webrtc_sdp_answer_cb(SignalingServer* server,
                                 const ClientId client_id,
                                 const gchar* sdp,
                                 const struct MyGstData* mgd) {
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
                                const ClientId client_id,
                                const guint m_line_index,
                                const gchar* candidate,
                                const struct MyGstData* mgd) {
    GstBin* pipeline = GST_BIN(mgd->pipeline);

    if (strlen(candidate)) {
        GstElement* webrtcbin = get_webrtcbin_for_client(pipeline, client_id);
        if (webrtcbin) {
            g_signal_emit_by_name(webrtcbin, "add-ice-candidate", m_line_index, candidate);
            gst_object_unref(webrtcbin);
        }
    }

    ALOGD("Remote candidate: %s", candidate);
}

static GstPadProbeReturn remove_webrtcbin_probe_audio(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    GstElement* webrtcbin = GST_ELEMENT(user_data);

    gst_element_set_state(webrtcbin, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(GST_ELEMENT_PARENT(webrtcbin)), webrtcbin);

    return GST_PAD_PROBE_REMOVE;
}

static GstPadProbeReturn remove_webrtcbin_probe_cb_video(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
    GstElement* webrtcbin = GST_ELEMENT(user_data);

    GstPad* audio_sinkpad = gst_element_get_static_pad(webrtcbin, "sink_1");

    // Handle audio sinkpad if there's any
    if (audio_sinkpad) {
        gst_pad_add_probe(GST_PAD_PEER(audio_sinkpad),
                          GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
                          remove_webrtcbin_probe_audio,
                          webrtcbin,
                          gst_object_unref);

        gst_clear_object(&audio_sinkpad);
    }
    // Otherwise release webrtcbin directly
    else {
        gst_element_set_state(webrtcbin, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(GST_ELEMENT_PARENT(webrtcbin)), webrtcbin);
        gst_object_unref(webrtcbin);
    }

    return GST_PAD_PROBE_REMOVE;
}

static void webrtc_client_disconnected_cb(SignalingServer* server, ClientId client_id, struct MyGstData* mgd) {
    ALOGI("WebSocket client disconnected, ID: %p", client_id);

    GstBin* pipeline = GST_BIN(mgd->pipeline);

    GstElement* webrtcbin = get_webrtcbin_for_client(pipeline, client_id);

    if (webrtcbin) {
        GstPad* video_sinkpad = gst_element_get_static_pad(webrtcbin, "sink_0");

        if (video_sinkpad) {
            gst_pad_add_probe(GST_PAD_PEER(video_sinkpad),
                              GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
                              remove_webrtcbin_probe_cb_video,
                              webrtcbin,
                              NULL);

            gst_clear_object(&video_sinkpad);
        }
    }
}

GMainLoop* main_loop = NULL;

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
    ALOGI("Starting pipeline");

    main_loop = g_main_loop_new(NULL, FALSE);

    // Play the pipeline (but webrtcbin is not linked yet)
    const GstStateChangeReturn ret = gst_element_set_state(mgd->pipeline, GST_STATE_PLAYING);
    g_assert(ret != GST_STATE_CHANGE_FAILURE);

    g_signal_connect(signaling_server, "ws-client-connected", G_CALLBACK(webrtc_client_connected_cb), mgd);

    GThread* thread = g_thread_new("loop_thread", (GThreadFunc)loop_thread, NULL);
}

void server_pipeline_stop(struct MyGstData* mgd) {
    ALOGI("Stopping pipeline");

    // Settle the pipeline.
    ALOGD("Sending EOS event");
    gst_element_send_event(mgd->pipeline, gst_event_new_eos());

    // Wait for an EOS message on the pipeline bus.
    ALOGD("Waiting for EOS message");
    GstMessage* msg = gst_bus_timed_pop_filtered(GST_ELEMENT_BUS(mgd->pipeline),
                                                 GST_CLOCK_TIME_NONE,
                                                 GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

    // TODO: should check if we got an error message here or an eos.
    (void)msg;

    // Completely stop the pipeline.
    ALOGI("Setting pipeline state to NULL");
    gst_element_set_state(mgd->pipeline, GST_STATE_NULL);

    g_clear_handle_id(&mgd->timeout_src_id_dot_data, g_source_remove);
}

#define U_TYPED_CALLOC(TYPE) ((TYPE*)calloc(1, sizeof(TYPE)))

static void on_handoff(GstElement* identity, GstBuffer* buffer, gpointer user_data) {
    GstClockTime pts = GST_BUFFER_PTS(buffer);
    GstClockTime dts = GST_BUFFER_DTS(buffer);
    // ALOGD("Buffer PTS: %" GST_TIME_FORMAT ", DTS: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(pts), GST_TIME_ARGS(dts));
}

void server_pipeline_create(struct MyGstData** out_mgd) {
    GError* error = NULL;

    signaling_server = signaling_server_new();

    struct MyGstData* mgd = U_TYPED_CALLOC(struct MyGstData);

#ifdef __linux__
    // Trace logs
    // setenv("GST_DEBUG", "GST_TRACER:7", 1);
    // setenv("GST_TRACERS", "latency(flags=pipeline)", 1); // Latency
    // setenv("GST_DEBUG_FILE", "./latency.log", 1);        // Redirect log to a file
    //
    // // Specify dot file dir
    // setenv("GST_DEBUG_DUMP_DOT_DIR", "./", 1);
    //
    // // Do not do ansi color codes
    // setenv("GST_DEBUG_NO_COLOR", "1", 1);
#endif

    // Set up gst logger
    {
#ifdef __ANDROID__
        gst_debug_add_log_function(&hook_android_log, NULL, NULL);
#endif

        gst_debug_set_default_threshold(GST_LEVEL_WARNING);
        // gst_debug_set_threshold_for_name("encodebin2", GST_LEVEL_LOG);
        // gst_debug_set_threshold_for_name("webrtcbin", GST_LEVEL_LOG);
    }

    gst_init(NULL, NULL);

    // Setup pipeline
    // is-live=true is to fix first frame delay
    gchar* pipeline_str = g_strdup_printf(
#ifndef ANDROID
        "filesrc location=test.mp4 ! "
        "decodebin3 name=dec "
        "dec. ! "
        "queue ! "
#else
        // "openslessrc ! " // Mic
        // "audiotestsrc is-live=true wave=red-noise ! " // Test audio
        "appsrc name=audiosrc format=GST_FORMAT_TIME is-live=true ! "
        "audio/x-raw,format=S16LE,layout=interleaved,rate=44100,channels=2 ! "
#endif
        "audioconvert ! "
        "audioresample ! "
        "queue ! "
        "opusenc perfect-timestamp=true ! "
        "rtpopuspay ! "
        "application/x-rtp,encoding-name=OPUS,media=audio,payload=127,ssrc=(uint)3484078953 ! "
        "queue ! "
        "tee name=%s allow-not-linked=true "
#ifndef ANDROID
        "dec. ! "
#else
        "videotestsrc pattern=colors is-live=true horizontal-speed=2 ! "
        "video/x-raw,format=NV12,width=1280,height=720,framerate=60/1 ! "
#endif
        "queue name=q1 ! "
        "videoconvert ! "
        "timeoverlay ! "
#ifndef ANDROID
        // FIXME: this autovideosink is added to fix stream arriving super late on native platforms
        // Local display sink for latency comparison
        "tee name=testlocalsink ! videoconvert ! autovideosink testlocalsink. ! "
#endif
        "encodebin2 "
        "profile=\"video/x-h264|element-properties,tune=4,speed-preset=1,bframes=0,key-int-max=120,bitrate=16000\" ! "
        "rtph264pay name=rtppay config-interval=-1 aggregate-mode=zero-latency ! "
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

    GstPad* pad = gst_element_get_static_pad(gst_bin_get_by_name(GST_BIN(pipeline), "rtppay"), "src");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)on_buffer_probe_cb, NULL, NULL);
    gst_object_unref(pad);

    GstElement* iden = gst_bin_get_by_name(GST_BIN(pipeline), "identity");
    if (iden) {
        g_signal_connect(iden, "handoff", G_CALLBACK(on_handoff), NULL);
        gst_object_unref(iden);
    }

    GstBus* bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, gst_bus_cb, mgd);
    gst_object_unref(bus);

    // "ws-client-connected" will be connected later when the pipeline starts playing
    g_signal_connect(signaling_server, "ws-client-disconnected", G_CALLBACK(webrtc_client_disconnected_cb), mgd);
    g_signal_connect(signaling_server, "sdp-answer", G_CALLBACK(webrtc_sdp_answer_cb), mgd);
    g_signal_connect(signaling_server, "candidate", G_CALLBACK(webrtc_candidate_cb), mgd);

    mgd->pipeline = pipeline;
    *out_mgd = mgd;
}

void server_pipeline_push_pcm(struct MyGstData* mgd, const void* audio_bytes, const int size) {
    if (!mgd) {
        ALOGW("MyGstData is null");
        return;
    }

    if (size < 1 || !audio_bytes) {
        ALOGW("Invalid audio data");
        return;
    }

    GstElement* audio_app_src = gst_bin_get_by_name(GST_BIN(mgd->pipeline), "audiosrc");
    if (!audio_app_src) {
        ALOGE("GStreamer audio_app_src is null");
        return;
    }

    GstClock* clock = gst_element_get_clock(mgd->pipeline);
    const GstClockTime current_time = gst_clock_get_time(clock);
    const GstClockTime base_time = gst_element_get_base_time(mgd->pipeline);
    const GstClockTime running_time = current_time - base_time;

    if (base_time == 0) {
        ALOGE("Pipeline clock has not been started yet, skipping audio push");
        return;
    }

    GstBuffer* buffer = gst_buffer_new_allocate(NULL, size, NULL);
    if (buffer) {
        gst_buffer_fill(buffer, 0, audio_bytes, size);

        // For 16-bit stereo at 44.1kHz
        const int channels = 2;
        const GstClockTime duration = gst_util_uint64_scale_int(size / (2 * channels), GST_SECOND, 44100);

        // Set presentation timestamp (PTS) and duration if known/needed.
        // Ensure timestamp units match GStreamer expectations.
        GST_BUFFER_PTS(buffer) = running_time;
        GST_BUFFER_DURATION(buffer) = duration;

        //        ALOGI("Audio buffer PTS: %" GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT "\n",
        //              GST_TIME_ARGS(running_time),
        //              GST_TIME_ARGS(duration));

        GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(audio_app_src), buffer);
        if (ret != GST_FLOW_OK) {
            ALOGW("Error pushing GStreamer buffer: %d", ret);
        }
    } else {
        ALOGE("Failed to allocate GStreamer buffer.");
    }

    gst_object_unref(audio_app_src);
}
