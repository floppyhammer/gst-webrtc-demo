#include <gst/gst.h>

#ifdef __linux__
    #include <glib-unix.h>
#endif
#define GST_USE_UNSTABLE_API
#include <gst/sdp/sdp.h>
#include <gst/webrtc/webrtc.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-session.h>
#include <stdint.h>

#include "stdio.h"

#define DEFAULT_WEBSOCKET_URI "ws://127.0.0.1:52356/ws"
// #define DEFAULT_WEBSOCKET_URI "ws://10.11.9.192:52356/ws"

struct RecvState {
    SoupWebsocketConnection *connection;
    GstElement *pipeline;
    GstElement *webrtcbin;
    GstWebRTCDataChannel *data_channel;
    // todo: release
    guint timeout_src_id_print_stats;
    guint timeout_src_id_dot_data;
};

struct RecvState recv_state = {};

/*
 *
 * Data channel functions.
 *
 */

static void data_channel_error_cb(GstWebRTCDataChannel *data_channel, void *data) {
    g_print("Data channel error\n");
    abort();
}

static void data_channel_close_cb(GstWebRTCDataChannel *data_channel, gpointer timeout_src_id_msg) {
    g_print("Data channel closed\n");

    g_source_remove(GPOINTER_TO_UINT(timeout_src_id_msg));
    g_clear_object(&data_channel);
}

static void data_channel_message_data_cb(GstWebRTCDataChannel *data_channel, GBytes *data, void *user_data) {
    uint32_t data_size = g_bytes_get_size(data);
    g_print("Received data channel message data, size: %u\n", data_size);
}

static void data_channel_message_string_cb(GstWebRTCDataChannel *data_channel, gchar *str, void *user_data) {
    // Save remote dot file
    if (strlen(str) > 1000) {
        g_print("Received remote dot file\n");

        FILE *fptr = fopen("remote_pipeline.dot", "w");
        fwrite(str, strlen(str), 1, fptr);
        fclose(fptr);
    } else {
        g_print("Received data channel message string: %s\n", str);
    }
}

static gboolean data_channel_send_message(gpointer unused) {
    g_signal_emit_by_name(recv_state.data_channel, "send-string", "Hi! from test client");

    return G_SOURCE_REMOVE;
}

static void webrtc_on_data_channel_cb(GstElement *webrtcbin, GstWebRTCDataChannel *new_data_channel, void *user_data) {
    g_print("Successfully created data channel\n");

    g_assert_null(recv_state.data_channel);
    recv_state.data_channel = GST_WEBRTC_DATA_CHANNEL(new_data_channel);

    // Send the message repeatedly
    guint timeout_src_id_msg = g_timeout_add_seconds(3, G_SOURCE_FUNC(data_channel_send_message), NULL);

    g_signal_connect(new_data_channel,
                     "on-close",
                     G_CALLBACK(data_channel_close_cb),
                     GUINT_TO_POINTER(timeout_src_id_msg));
    g_signal_connect(new_data_channel,
                     "on-error",
                     G_CALLBACK(data_channel_error_cb),
                     GUINT_TO_POINTER(timeout_src_id_msg));
    g_signal_connect(new_data_channel, "on-message-data", G_CALLBACK(data_channel_message_data_cb), NULL);
    g_signal_connect(new_data_channel, "on-message-string", G_CALLBACK(data_channel_message_string_cb), NULL);
}

/*
 *
 * Websocket connection.
 *
 */

// Main loop breaker
static gboolean sigint_handler(gpointer user_data) {
    g_main_loop_quit(user_data);
    return G_SOURCE_REMOVE;
}

static gboolean gst_bus_cb(GstBus *bus, GstMessage *message, gpointer data) {
    GstBin *pipeline = GST_BIN(data);

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *gerr;
            gchar *debug_msg;
            gst_message_parse_error(message, &gerr, &debug_msg);
            gchar *dot_data = gst_debug_bin_to_dot_data(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
            g_error("Error: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
            g_free(dot_data);
        } break;
        case GST_MESSAGE_WARNING: {
            GError *gerr;
            gchar *debug_msg;
            gst_message_parse_warning(message, &gerr, &debug_msg);
            g_warning("Warning: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
        } break;
        case GST_MESSAGE_EOS: {
            g_error("Got EOS!");
        } break;
        default:
            break;
    }
    return TRUE;
}

void send_sdp_answer(const gchar *sdp) {
    g_print("Send SDP answer: %s\n", sdp);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "msg");
    json_builder_add_string_value(builder, "answer");

    json_builder_set_member_name(builder, "sdp");
    json_builder_add_string_value(builder, sdp);
    json_builder_end_object(builder);

    JsonNode *root = json_builder_get_root(builder);

    gchar *msg_str = json_to_string(root, TRUE);
    soup_websocket_connection_send_text(recv_state.connection, msg_str);
    g_clear_pointer(&msg_str, g_free);

    json_node_unref(root);
    g_object_unref(builder);
}

static void webrtc_on_ice_candidate_cb(GstElement *webrtcbin, guint mlineindex, gchar *candidate) {
    g_print("Send ICE candidate: %u %s\n", mlineindex, candidate);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "msg");
    json_builder_add_string_value(builder, "candidate");

    json_builder_set_member_name(builder, "candidate");
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "candidate");
    json_builder_add_string_value(builder, candidate);
    json_builder_set_member_name(builder, "sdpMLineIndex");
    json_builder_add_int_value(builder, mlineindex);
    json_builder_end_object(builder);
    json_builder_end_object(builder);

    JsonNode *root = json_builder_get_root(builder);

    gchar *msg_str = json_to_string(root, TRUE);
    soup_websocket_connection_send_text(recv_state.connection, msg_str);
    g_clear_pointer(&msg_str, g_free);

    json_node_unref(root);
    g_object_unref(builder);
}

static void on_handoff(GstElement *identity, GstBuffer *buffer, gpointer user_data) {
    GstClockTime pts = GST_BUFFER_PTS(buffer);
    GstClockTime dts = GST_BUFFER_DTS(buffer);
    // g_print("Buffer PTS: %" GST_TIME_FORMAT ", DTS: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(pts), GST_TIME_ARGS(dts));
}

/// Handle incoming media stream
static void handle_media_stream(GstPad *pad, GstElement *pipeline, const char *convert_name, const char *sink_name) {
    gst_println("Trying to handle stream with %s ! %s", convert_name, sink_name);

    GstElement *q = gst_element_factory_make("queue", NULL);
    g_assert_nonnull(q);

    GstElement *conv = gst_element_factory_make(convert_name, NULL);
    g_assert_nonnull(conv);

    GstElement *sink = gst_element_factory_make(sink_name, NULL);
    g_assert_nonnull(sink);

    if (g_strcmp0(convert_name, "audioconvert") == 0) {
        /* Might also need to resample, so add it just in case.
         * Will be a no-op if it's not required. */
        GstElement *resample = gst_element_factory_make("audioresample", NULL);
        g_assert_nonnull(resample);
        gst_bin_add_many(GST_BIN(pipeline), q, conv, resample, sink, NULL);
        gst_element_sync_state_with_parent(q);
        gst_element_sync_state_with_parent(conv);
        gst_element_sync_state_with_parent(resample);
        gst_element_sync_state_with_parent(sink);
        gst_element_link_many(q, conv, resample, sink, NULL);
    } else {
        GstElement *identity = gst_element_factory_make("identity", NULL);
        g_assert_nonnull(identity);
        g_object_set(identity, "signal-handoffs", TRUE, NULL);

        g_object_set(sink, "sync", FALSE, NULL);

        gst_bin_add_many(GST_BIN(pipeline), q, conv, sink, identity, NULL);
        gst_element_sync_state_with_parent(q);
        gst_element_sync_state_with_parent(conv);
        gst_element_sync_state_with_parent(sink);
        gst_element_sync_state_with_parent(identity);
        gst_element_link_many(q, conv, identity, sink, NULL);

        g_signal_connect(identity, "handoff", G_CALLBACK(on_handoff), NULL);
    }

    GstPad *qpad = gst_element_get_static_pad(q, "sink");

    GstPadLinkReturn ret = gst_pad_link(pad, qpad);
    g_assert_cmphex(ret, ==, GST_PAD_LINK_OK);

    gst_object_unref(qpad);
}

static void on_decodebin_pad_added(GstElement *decodebin, GstPad *pad, GstElement *pipeline) {
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
    g_print("decodebin pad caps: %s\n", str);
    g_free(str);

    gst_caps_unref(caps);

    if (g_str_has_prefix(name, "video")) {
        handle_media_stream(pad, pipeline, "videoconvert", "autovideosink");
    } else if (g_str_has_prefix(name, "audio")) {
        handle_media_stream(pad, pipeline, "audioconvert", "autoaudiosink");
    } else {
        gst_printerr("Unknown pad %s, ignoring", GST_PAD_NAME(pad));
    }
}

static void on_webrtcbin_pad_added(GstElement *webrtcbin, GstPad *pad, GstElement *pipeline) {
    // We don't care about sink pads
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) {
        return;
    }

    g_print("Hit on_webrtcbin_pad_added\n");

    {
        GstCaps *caps = gst_pad_get_current_caps(pad);
        gchar *str = gst_caps_serialize(caps, 0);
        g_print("webrtcbin pad caps: %s\n", str);
        g_free(str);
        gst_caps_unref(caps);
    }

    GstElement *decodebin = gst_element_factory_make("decodebin3", NULL);
    g_signal_connect(decodebin, "pad-added", G_CALLBACK(on_decodebin_pad_added), pipeline);
    gst_bin_add(GST_BIN(pipeline), decodebin);

    gst_element_sync_state_with_parent(decodebin);

    GstPad *sink_pad = gst_element_get_static_pad(decodebin, "sink");

    GstPadLinkReturn ret = gst_pad_link(pad, sink_pad);
    g_assert_cmphex(ret, ==, GST_PAD_LINK_OK);
    gst_object_unref(sink_pad);
}

static void on_prepare_data_channel(GstElement *webrtcbin,
                                    GstWebRTCDataChannel *channel,
                                    gboolean is_local,
                                    gpointer udata) {
    // Adjust receive buffer size (IMPORTANT)
    {
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
            g_object_set(ice_transport, "receive-buffer-size", 8 * 1024 * 1024, NULL);
        } else {
            g_error("Failed to get ice_transport!");
        }

        g_object_unref(ice_transport);
        g_object_unref(dtls_transport);
        g_object_unref(sctp_transport);
    }
}

static void on_answer_created(GstPromise *promise, gpointer user_data) {
    GstWebRTCSessionDescription *answer = NULL;

    gst_structure_get(gst_promise_get_reply(promise), "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
    gst_promise_unref(promise);
    g_assert(answer);

    g_signal_emit_by_name(recv_state.webrtcbin, "set-local-description", answer, NULL);

    gchar *sdp = gst_sdp_message_as_text(answer->sdp);
    send_sdp_answer(sdp);
    g_free(sdp);

    gst_webrtc_session_description_free(answer);
}

static void process_sdp_offer(const gchar *sdp) {
    GstSDPMessage *sdp_msg = NULL;
    GstWebRTCSessionDescription *desc = NULL;

    g_print("Received SDP offer: %s\n", sdp);

    if (gst_sdp_message_new_from_text(sdp, &sdp_msg) != GST_SDP_OK) {
        g_debug("Error parsing SDP description");
        goto out;
    }

    desc = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp_msg);
    if (desc) {
        GstPromise *promise = gst_promise_new();

        g_signal_emit_by_name(recv_state.webrtcbin, "set-remote-description", desc, promise);

        gst_promise_wait(promise);
        gst_promise_unref(promise);

        g_signal_emit_by_name(recv_state.webrtcbin,
                              "create-answer",
                              NULL,
                              gst_promise_new_with_change_func((GstPromiseChangeFunc)on_answer_created, NULL, NULL));
    } else {
        gst_sdp_message_free(sdp_msg);
    }

out:
    g_clear_pointer(&desc, gst_webrtc_session_description_free);
}

static void process_candidate(guint mlineindex, const gchar *candidate) {
    g_print("Received ICE candidate: %d %s\n", mlineindex, candidate);

    g_signal_emit_by_name(recv_state.webrtcbin, "add-ice-candidate", mlineindex, candidate);
}

static void websocket_message_cb(SoupWebsocketConnection *connection, gint type, GBytes *message, gpointer user_data) {
    gsize length = 0;
    const gchar *msg_data = g_bytes_get_data(message, &length);

    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    if (json_parser_load_from_data(parser, msg_data, length, &error)) {
        JsonObject *msg = json_node_get_object(json_parser_get_root(parser));

        if (!json_object_has_member(msg, "msg")) {
            // Invalid message
            goto out;
        }

        const gchar *msg_type = json_object_get_string_member(msg, "msg");
        g_print("Websocket message received: %s\n", msg_type);

        if (g_str_equal(msg_type, "offer")) {
            const gchar *offer_sdp = json_object_get_string_member(msg, "sdp");
            process_sdp_offer(offer_sdp);
        } else if (g_str_equal(msg_type, "candidate")) {
            JsonObject *candidate = json_object_get_object_member(msg, "candidate");

            process_candidate(json_object_get_int_member(candidate, "sdpMLineIndex"),
                              json_object_get_string_member(candidate, "candidate"));
        }
    } else {
        g_debug("Error parsing message: %s", error->message);
        g_clear_error(&error);
    }

out:
    g_object_unref(parser);
}

static void on_new_transceiver(GstElement *webrtc, GstWebRTCRTPTransceiver *trans) {
    g_object_set(trans, "fec-type", GST_WEBRTC_FEC_TYPE_ULP_RED, NULL);
}

// This is the gstwebrtc entry point where we create the offer and so on.
// It will be called when the pipeline goes to PLAYING.
static void on_negotiation_needed(GstElement *element, gpointer user_data) {
    // Pass
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

static gboolean print_stats() {
    if (!recv_state.webrtcbin) {
        return G_SOURCE_CONTINUE;
    }

    GstPromise *promise = gst_promise_new_with_change_func((GstPromiseChangeFunc)on_webrtcbin_stats, NULL, NULL);
    g_signal_emit_by_name(recv_state.webrtcbin, "get-stats", NULL, promise);

    // FEC stats
    for (int i = 0; i < 2; i++) {
        gchar *name;
        if (i == 0) {
            name = "rtpulpfecdec0";
        } else {
            name = "rtpulpfecdec1";
        }

        GstElement *rtpulpfecdec = find_element_by_name(GST_BIN(recv_state.webrtcbin), name);

        if (rtpulpfecdec) {
            GValue pt = G_VALUE_INIT;
            GValue recovered = G_VALUE_INIT;
            GValue unrecovered = G_VALUE_INIT;

            g_object_get_property(G_OBJECT(rtpulpfecdec), "pt", &pt);
            g_object_get_property(G_OBJECT(rtpulpfecdec), "recovered", &recovered);
            g_object_get_property(G_OBJECT(rtpulpfecdec), "unrecovered", &unrecovered);

            g_print("FEC stats: pt %u, recovered %u, unrecovered %u\n",
                    g_value_get_uint(&pt),
                    g_value_get_uint(&recovered),
                    g_value_get_uint(&unrecovered));

            g_value_unset(&pt);
            g_value_unset(&recovered);
            g_value_unset(&unrecovered);
        }
    }

    return G_SOURCE_CONTINUE;
}

static gboolean check_pipeline_dot_data() {
    if (!recv_state.pipeline) {
        return G_SOURCE_CONTINUE;
    }

    gchar *dot_data = gst_debug_bin_to_dot_data(GST_BIN(recv_state.pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
    g_free(dot_data);

    return G_SOURCE_CONTINUE;
}

static void websocket_connected_cb(GObject *session, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;

    g_assert(!recv_state.connection);

    recv_state.connection = soup_session_websocket_connect_finish(SOUP_SESSION(session), res, &error);

    if (error) {
        g_print("Error creating websocket: %s\n", error->message);
        g_clear_error(&error);
    } else {
        g_print("Websocket connected\n");

        g_signal_connect(recv_state.connection, "message", G_CALLBACK(websocket_message_cb), NULL);

        recv_state.pipeline = gst_pipeline_new("webrtc-recv-pipeline");

        recv_state.webrtcbin = gst_element_factory_make("webrtcbin", NULL);
        // Matching this to the offerer's bundle policy is necessary for negotiation
        g_object_set(recv_state.webrtcbin, "bundle-policy", GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE, NULL);
        g_object_set(recv_state.webrtcbin, "latency", 5, NULL);

        gst_bin_add_many(GST_BIN(recv_state.pipeline), recv_state.webrtcbin, NULL);

        // Connect callbacks on sinks
        g_signal_connect(recv_state.webrtcbin, "on-negotiation-needed", G_CALLBACK(on_negotiation_needed), NULL);
        g_signal_connect(recv_state.webrtcbin, "on-data-channel", G_CALLBACK(webrtc_on_data_channel_cb), NULL);
        g_signal_connect(recv_state.webrtcbin, "on-ice-candidate", G_CALLBACK(webrtc_on_ice_candidate_cb), NULL);
        g_signal_connect(recv_state.webrtcbin, "on-new-transceiver", G_CALLBACK(on_new_transceiver), NULL);
        g_signal_connect(recv_state.webrtcbin, "pad-added", G_CALLBACK(on_webrtcbin_pad_added), recv_state.pipeline);
        g_signal_connect(recv_state.webrtcbin, "prepare-data-channel", G_CALLBACK(on_prepare_data_channel), NULL);

        GstBus *bus = gst_element_get_bus(recv_state.pipeline);
        gst_bus_add_watch(bus, gst_bus_cb, recv_state.pipeline);
        gst_clear_object(&bus);

        // Start pipeline
        g_assert(gst_element_set_state(recv_state.pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

        // Print stats repeatedly
        // recv_state.timeout_src_id_print_stats = g_timeout_add_seconds(3, G_SOURCE_FUNC(print_stats), NULL);
        recv_state.timeout_src_id_dot_data = g_timeout_add_seconds(3, G_SOURCE_FUNC(check_pipeline_dot_data), NULL);
    }
}

int create_client(int argc, char *argv[]) {
    GError *error = NULL;

    // setenv("GST_DEBUG", "rtpulpfecdec:7", 1);
    // setenv("GST_DEBUG", "GST_TRACER:7", 1);
    // setenv("GST_TRACERS", "latency(flags=pipeline)", 1); // Latency
    // setenv("GST_DEBUG_FILE", "./latency.log", 1);

    gst_init(&argc, &argv);

    gchar *websocket_uri = g_strdup(DEFAULT_WEBSOCKET_URI);

    const GOptionEntry options[] = {{
                                        "websocket-uri",
                                        'u',
                                        0,
                                        G_OPTION_ARG_STRING,
                                        &websocket_uri,
                                        "Websocket URI of webrtc signaling connection",
                                        "URI",
                                    },
                                    {NULL}};

    GOptionContext *option_context = g_option_context_new(NULL);
    g_option_context_add_main_entries(option_context, options, NULL);

    if (!g_option_context_parse(option_context, &argc, &argv, &error)) {
        g_print("Option context parsing failed: %s\n", error->message);
        exit(1);
    }

    SoupSession *soup_session = soup_session_new();

#if !SOUP_CHECK_VERSION(3, 0, 0)
    soup_session_websocket_connect_async(soup_session,                                     // session
                                         soup_message_new(SOUP_METHOD_GET, websocket_uri), // message
                                         NULL,                                             // origin
                                         NULL,                                             // protocols
                                         NULL,                                             // cancellable
                                         websocket_connected_cb,                           // callback
                                         NULL);                                            // user_data
#else
    soup_session_websocket_connect_async(soup_session,                                     // session
                                         soup_message_new(SOUP_METHOD_GET, websocket_uri), // message
                                         NULL,                                             // origin
                                         NULL,                                             // protocols
                                         0,                                                // io_priority
                                         NULL,                                             // cancellable
                                         websocket_connected_cb,                           // callback
                                         NULL);                                            // user_data
#endif

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
#ifdef __linux__
    g_unix_signal_add(SIGINT, sigint_handler, loop);
#endif

    g_main_loop_run(loop);

    // Cleanup
    g_main_loop_unref(loop);
    g_clear_pointer(&websocket_uri, g_free);

    return 0;
}
