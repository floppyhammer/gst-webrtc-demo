#include "connection.h"

#include <gst/gstelement.h>
#include <gst/gstobject.h>
#include <stdbool.h>
#include <string.h>

#include "../utils/logger.h"
#include "status.h"

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
#undef GST_USE_UNSTABLE_API

#include <json-glib/json-glib.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-session.h>

#define DEFAULT_WEBSOCKET_URI "ws://127.0.0.1:52356/ws"

/*!
 * Data required for the handshake to complete and to maintain the connection.
 */
struct _MyConnection {
    GObject parent;
    SoupSession *soup_session;
    gchar *websocket_uri;

    /// Cancellable for websocket connection process
    GCancellable *ws_cancel;
    SoupWebsocketConnection *ws;

    GstPipeline *pipeline;
    GstElement *webrtcbin;
    GstWebRTCDataChannel *data_channel;

    enum my_status status;
};

G_DEFINE_TYPE(MyConnection, my_connection, G_TYPE_OBJECT)

enum {
    // action signals
    SIGNAL_CONNECT,
    SIGNAL_DISCONNECT,
    SIGNAL_SET_PIPELINE,
    // signals
    SIGNAL_WEBSOCKET_CONNECTED,
    SIGNAL_WEBSOCKET_FAILED,
    SIGNAL_WEBRTC_CONNECTED,
    SIGNAL_STATUS_CHANGE,
    SIGNAL_ON_NEED_PIPELINE,
    SIGNAL_ON_DROP_PIPELINE,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef enum {
    PROP_WEBSOCKET_URI = 1,
    // PROP_STATUS,
    N_PROPERTIES
} MyConnectionProperty;

static GParamSpec *properties[N_PROPERTIES] = {
    NULL,
};

/* GObject method implementations */

static void my_connection_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    MyConnection *self = MY_CONNECTION(object);

    switch ((MyConnectionProperty)property_id) {
        case PROP_WEBSOCKET_URI:
            g_free(self->websocket_uri);
            self->websocket_uri = g_value_dup_string(value);
            ALOGI("Websocket URI assigned: %s", self->websocket_uri);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void my_connection_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    MyConnection *self = MY_CONNECTION(object);

    switch ((MyConnectionProperty)property_id) {
        case PROP_WEBSOCKET_URI:
            g_value_set_string(value, self->websocket_uri);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void my_connection_init(MyConnection *conn) {
    conn->ws_cancel = g_cancellable_new();
    conn->soup_session = soup_session_new();
    conn->websocket_uri = g_strdup(DEFAULT_WEBSOCKET_URI);
}

static void my_connection_dispose(GObject *object) {
    MyConnection *self = MY_CONNECTION(object);

    my_connection_disconnect(self);

    g_clear_object(&self->soup_session);
    g_clear_object(&self->ws_cancel);
}

static void my_connection_finalize(GObject *object) {
    MyConnection *self = MY_CONNECTION(object);

    g_free(self->websocket_uri);
}

static void my_connection_class_init(MyConnectionClass *klass) {
    ALOGI("%s: Begin", __FUNCTION__);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = my_connection_dispose;
    gobject_class->finalize = my_connection_finalize;

    gobject_class->set_property = my_connection_set_property;
    gobject_class->get_property = my_connection_get_property;

    /**
     * MyConnection:websocket-uri:
     *
     * The websocket URI for the signaling server
     */
    g_object_class_install_property(
        gobject_class,
        PROP_WEBSOCKET_URI,
        g_param_spec_string("websocket-uri",
                            "WebSocket URI",
                            "WebSocket URI for signaling server.",
                            DEFAULT_WEBSOCKET_URI /* default value */,
                            G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /**
     * MyConnection::connect
     * @object: the #MyConnection
     *
     * Start the connection process
     */
    signals[SIGNAL_CONNECT] = g_signal_new_class_handler("connect",
                                                         G_OBJECT_CLASS_TYPE(klass),
                                                         G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                         G_CALLBACK(my_connection_connect),
                                                         NULL,
                                                         NULL,
                                                         NULL,
                                                         G_TYPE_NONE,
                                                         0);

    /**
     * MyConnection::disconnect
     * @object: the #MyConnection
     *
     * Stop the connection process or shutdown the connection
     */
    signals[SIGNAL_DISCONNECT] = g_signal_new_class_handler("disconnect",
                                                            G_OBJECT_CLASS_TYPE(klass),
                                                            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                            G_CALLBACK(my_connection_disconnect),
                                                            NULL,
                                                            NULL,
                                                            NULL,
                                                            G_TYPE_NONE,
                                                            0);

    /**
     * MyConnection::set-pipeline
     * @object: the #MyConnection
     * @pipeline: A #GstPipeline
     *
     * Sets the #GstPipeline containing a #GstWebRTCBin element and begins the WebRTC connection negotiation.
     * Should be signalled in response to @on-need-pipeline
     */
    signals[SIGNAL_SET_PIPELINE] = g_signal_new_class_handler("set-pipeline",
                                                              G_OBJECT_CLASS_TYPE(klass),
                                                              G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                              G_CALLBACK(my_connection_set_pipeline),
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              G_TYPE_NONE,
                                                              1,
                                                              G_TYPE_POINTER);

    /**
     * MyConnection::websocket-connected
     * @object: the #MyConnection
     */
    signals[SIGNAL_WEBSOCKET_CONNECTED] = g_signal_new("websocket-connected",
                                                       G_OBJECT_CLASS_TYPE(klass),
                                                       G_SIGNAL_RUN_LAST,
                                                       0,
                                                       NULL,
                                                       NULL,
                                                       NULL,
                                                       G_TYPE_NONE,
                                                       0);

    /**
     * MyConnection::websocket-failed
     * @object: the #MyConnection
     */
    signals[SIGNAL_WEBSOCKET_FAILED] = g_signal_new("websocket-failed",
                                                    G_OBJECT_CLASS_TYPE(klass),
                                                    G_SIGNAL_RUN_LAST,
                                                    0,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    G_TYPE_NONE,
                                                    0);
    /**
     * MyConnection::connected
     * @object: the #MyConnection
     */
    signals[SIGNAL_WEBRTC_CONNECTED] = g_signal_new("webrtc_connected",
                                                    G_OBJECT_CLASS_TYPE(klass),
                                                    G_SIGNAL_RUN_LAST,
                                                    0,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    G_TYPE_NONE,
                                                    0);

    /**
     * MyConnection::on-need-pipeline
     * @object: the #MyConnection
     *
     * Your handler for this must emit @set-pipeline
     */
    signals[SIGNAL_ON_NEED_PIPELINE] = g_signal_new("on-need-pipeline",
                                                    G_OBJECT_CLASS_TYPE(klass),
                                                    G_SIGNAL_RUN_LAST,
                                                    0,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    G_TYPE_NONE,
                                                    0);

    /**
     * MyConnection::on-drop-pipeline
     * @object: the #MyConnection
     *
     * If you store any references in your handler for @on-need-pipeline you must make a handler for this signal to
     * drop them.
     */
    signals[SIGNAL_ON_DROP_PIPELINE] = g_signal_new("on-drop-pipeline",
                                                    G_OBJECT_CLASS_TYPE(klass),
                                                    G_SIGNAL_RUN_LAST,
                                                    0,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    G_TYPE_NONE,
                                                    0);
    ALOGI("%s: End", __FUNCTION__);
}

#define MAKE_CASE(E) \
    case E:          \
        return #E

static const char *peer_connection_state_to_string(GstWebRTCPeerConnectionState state) {
    switch (state) {
        MAKE_CASE(GST_WEBRTC_PEER_CONNECTION_STATE_NEW);
        MAKE_CASE(GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTING);
        MAKE_CASE(GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED);
        MAKE_CASE(GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED);
        MAKE_CASE(GST_WEBRTC_PEER_CONNECTION_STATE_FAILED);
        MAKE_CASE(GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED);
        default:
            return "!Unknown!";
    }
}

#undef MAKE_CASE

static void conn_update_status(MyConnection *conn, enum my_status status) {
    if (status == conn->status) {
        ALOGI("conn: state update: already in %s", my_status_to_string(conn->status));
        return;
    }
    ALOGI("conn: state update: %s -> %s", my_status_to_string(conn->status), my_status_to_string(status));
    conn->status = status;
}

static void conn_update_status_from_peer_connection_state(MyConnection *conn, GstWebRTCPeerConnectionState state) {
    switch (state) {
        case GST_WEBRTC_PEER_CONNECTION_STATE_NEW:
            break;
        case GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTING:
            conn_update_status(conn, MY_STATUS_NEGOTIATING);
            break;
        case GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED:
            conn_update_status(conn, MY_STATUS_CONNECTED_NO_DATA);
            break;

        case GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED:
        case GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED:
            conn_update_status(conn, MY_STATUS_IDLE_NOT_CONNECTED);
            break;

        case GST_WEBRTC_PEER_CONNECTION_STATE_FAILED:
            conn_update_status(conn, MY_STATUS_DISCONNECTED_ERROR);
            break;
    }
}

static void conn_disconnect_internal(MyConnection *conn, enum my_status status) {
    if (conn->ws_cancel != NULL) {
        g_cancellable_cancel(conn->ws_cancel);
        gst_clear_object(&conn->ws_cancel);
    }
    // Stop the pipeline, if it exists
    if (conn->pipeline != NULL) {
        gst_element_set_state(GST_ELEMENT(conn->pipeline), GST_STATE_NULL);
        g_signal_emit(conn, signals[SIGNAL_ON_DROP_PIPELINE], 0);
    }
    if (conn->ws) {
        soup_websocket_connection_close(conn->ws, 0, "");
    }
    g_clear_object(&conn->ws);

    gst_clear_object(&conn->webrtcbin);
    gst_clear_object(&conn->data_channel);
    gst_clear_object(&conn->pipeline);
    conn_update_status(conn, status);
}

static void conn_data_channel_error_cb(GstWebRTCDataChannel *datachannel, MyConnection *conn) {
    ALOGE("%s: error", __FUNCTION__);
    conn_disconnect_internal(conn, MY_STATUS_DISCONNECTED_ERROR);
    // abort();
}

static void conn_data_channel_close_cb(GstWebRTCDataChannel *datachannel, MyConnection *conn) {
    ALOGI("%s: Data channel closed", __FUNCTION__);
    conn_disconnect_internal(conn, MY_STATUS_DISCONNECTED_REMOTE_CLOSE);
}

static void conn_data_channel_message_string_cb(GstWebRTCDataChannel *datachannel, gchar *str, MyConnection *conn) {
    ALOGI("%s: Received data channel message: %s", __FUNCTION__, str);
}

static void conn_connect_internal(MyConnection *conn, enum my_status status);

static void conn_webrtc_deep_notify_callback(GstObject *self,
                                             GstObject *prop_object,
                                             GParamSpec *prop,
                                             MyConnection *conn) {
    GstWebRTCPeerConnectionState state;
    g_object_get(prop_object, "connection-state", &state, NULL);
    ALOGI("deep-notify callback says peer connection state is %s - but it lies sometimes",
          peer_connection_state_to_string(state));
    //	conn_update_status_from_peer_connection_state(conn, state);
}

static void conn_webrtc_prepare_data_channel_cb(GstElement *webrtc,
                                                GObject *data_channel,
                                                gboolean is_local,
                                                MyConnection *conn) {
    ALOGI("Preparing data channel");

    g_signal_connect(data_channel, "on-close", G_CALLBACK(conn_data_channel_close_cb), conn);
    g_signal_connect(data_channel, "on-error", G_CALLBACK(conn_data_channel_error_cb), conn);
    g_signal_connect(data_channel, "on-message-string", G_CALLBACK(conn_data_channel_message_string_cb), conn);
}

static void conn_webrtc_on_data_channel_cb(GstElement *webrtcbin,
                                           GstWebRTCDataChannel *data_channel,
                                           MyConnection *conn) {
    ALOGI("Successfully created data channel");

    g_assert_null(conn->data_channel);

    conn->data_channel = GST_WEBRTC_DATA_CHANNEL(data_channel);

    conn_update_status(conn, MY_STATUS_CONNECTED);
    g_signal_emit(conn, signals[SIGNAL_WEBRTC_CONNECTED], 0);
}

void conn_send_sdp_answer(MyConnection *conn, const gchar *sdp) {
    JsonBuilder *builder;
    JsonNode *root;
    gchar *msg_str;

    // ALOGI("Send SDP answer: %s", sdp);

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "msg");
    json_builder_add_string_value(builder, "answer");

    json_builder_set_member_name(builder, "sdp");
    json_builder_add_string_value(builder, sdp);
    json_builder_end_object(builder);

    root = json_builder_get_root(builder);

    msg_str = json_to_string(root, TRUE);
    soup_websocket_connection_send_text(conn->ws, msg_str);
    g_clear_pointer(&msg_str, g_free);

    json_node_unref(root);
    g_object_unref(builder);
}

static void conn_webrtc_on_ice_candidate_cb(GstElement *webrtcbin,
                                            guint mlineindex,
                                            gchar *candidate,
                                            MyConnection *conn) {
    JsonBuilder *builder;
    JsonNode *root;
    gchar *msg_str;

    // ALOGI("Send candidate: line %u: %s", mlineindex, candidate);

    builder = json_builder_new();
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

    root = json_builder_get_root(builder);

    msg_str = json_to_string(root, TRUE);
    // ALOGD("%s: candidate message: %s", __FUNCTION__, msg_str);
    soup_websocket_connection_send_text(conn->ws, msg_str);
    g_clear_pointer(&msg_str, g_free);

    json_node_unref(root);
    g_object_unref(builder);
}

static void conn_webrtc_on_answer_created(GstPromise *promise, MyConnection *conn) {
    GstWebRTCSessionDescription *answer = NULL;
    gchar *sdp;

    ALOGD("%s", __FUNCTION__);
    gst_structure_get(gst_promise_get_reply(promise), "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
    gst_promise_unref(promise);

    if (NULL == answer) {
        ALOGE("%s : ERROR !  get_promise answer = null !", __FUNCTION__);
    }

    g_signal_emit_by_name(conn->webrtcbin, "set-local-description", answer, NULL);

    sdp = gst_sdp_message_as_text(answer->sdp);
    if (NULL == sdp) {
        ALOGE("%s : ERROR !  sdp = null !", __FUNCTION__);
    }
    conn_send_sdp_answer(conn, sdp);
    g_free(sdp);

    gst_webrtc_session_description_free(answer);
}

static void conn_webrtc_process_sdp_offer(MyConnection *conn, const gchar *sdp) {
    GstSDPMessage *sdp_msg = NULL;
    GstWebRTCSessionDescription *desc = NULL;

    // ALOGI("Received SDP offer: %s\n", sdp);

    if (gst_sdp_message_new_from_text(sdp, &sdp_msg) != GST_SDP_OK) {
        g_debug("Error parsing SDP description");
        goto out;
    }

    desc = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp_msg);
    if (desc) {
        GstPromise *promise;

        promise = gst_promise_new();

        g_signal_emit_by_name(conn->webrtcbin, "set-remote-description", desc, promise);

        gst_promise_wait(promise);
        gst_promise_unref(promise);

        g_signal_emit_by_name(
            conn->webrtcbin,
            "create-answer",
            NULL,
            gst_promise_new_with_change_func((GstPromiseChangeFunc)conn_webrtc_on_answer_created, conn, NULL));
    } else {
        gst_sdp_message_free(sdp_msg);
    }

out:
    g_clear_pointer(&desc, gst_webrtc_session_description_free);
}

static void conn_webrtc_process_candidate(MyConnection *conn, guint mlineindex, const gchar *candidate) {
    // ALOGI("process_candidate: %d %s", mlineindex, candidate);

    g_signal_emit_by_name(conn->webrtcbin, "add-ice-candidate", mlineindex, candidate);
}

static void conn_on_ws_message_cb(SoupWebsocketConnection *connection, gint type, GBytes *message, MyConnection *conn) {
    // ALOGD("%s", __FUNCTION__);
    gsize length = 0;
    const gchar *msg_data = g_bytes_get_data(message, &length);
    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    // TODO convert gsize to gssize after range check

    if (json_parser_load_from_data(parser, msg_data, length, &error)) {
        JsonObject *msg = json_node_get_object(json_parser_get_root(parser));
        const gchar *msg_type;

        if (!json_object_has_member(msg, "msg")) {
            // Invalid message
            goto out;
        }

        msg_type = json_object_get_string_member(msg, "msg");
        // ALOGI("Websocket message received: %s", msg_type);

        if (g_str_equal(msg_type, "offer")) {
            const gchar *offer_sdp = json_object_get_string_member(msg, "sdp");
            conn_webrtc_process_sdp_offer(conn, offer_sdp);
        } else if (g_str_equal(msg_type, "candidate")) {
            JsonObject *candidate;

            candidate = json_object_get_object_member(msg, "candidate");

            conn_webrtc_process_candidate(conn,
                                          json_object_get_int_member(candidate, "sdpMLineIndex"),
                                          json_object_get_string_member(candidate, "candidate"));
        }
    } else {
        g_debug("Error parsing message: %s", error->message);
        g_clear_error(&error);
    }

out:
    g_object_unref(parser);
}

static void conn_websocket_connected_cb(GObject *session, GAsyncResult *res, MyConnection *conn) {
    GError *error = NULL;

    g_assert(!conn->ws);

    conn->ws = g_object_ref_sink(soup_session_websocket_connect_finish(SOUP_SESSION(session), res, &error));

    if (error) {
        ALOGE("Websocket connection failed, error: '%s'", error->message);
        g_signal_emit(conn, signals[SIGNAL_WEBSOCKET_FAILED], 0);
        conn_update_status(conn, MY_STATUS_WEBSOCKET_FAILED);
        return;
    }
    g_assert_no_error(error);
    GstBus *bus;

    ALOGI("WebSocket connected");
    g_signal_connect(conn->ws, "message", G_CALLBACK(conn_on_ws_message_cb), conn);
    g_signal_emit(conn, signals[SIGNAL_WEBSOCKET_CONNECTED], 0);

    ALOGI("Creating pipeline");
    g_assert_null(conn->pipeline);
    g_signal_emit(conn, signals[SIGNAL_ON_NEED_PIPELINE], 0);
    if (conn->pipeline == NULL) {
        ALOGE("on-need-pipeline signal did not return a pipeline!");
        my_connection_disconnect(conn);
        return;
    }

    // OK, if we get here, we have a websocket connection, and a pipeline fully configured
    // so we can start the pipeline playing

    ALOGI("Setting pipeline state to PLAYING");
    gst_element_set_state(GST_ELEMENT(conn->pipeline), GST_STATE_PLAYING);
    ALOGI("%s: Done with function", __FUNCTION__);
}

static void on_ice_connection_state_change(GstElement *webrtcbin, GParamSpec *pspec, gpointer user_data) {
    GstWebRTCICEConnectionState state;
    g_object_get(webrtcbin, "ice-connection-state", &state, NULL);

    if (state == GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED) {
        GObject *sctp_transport = NULL;
        g_object_get(webrtcbin, "sctptransport", &sctp_transport, NULL);

        if (sctp_transport) {
            // Adjust SCTP buffer size
            g_object_set(sctp_transport,
                         "sctp-association-send-buffer-size",
                         2 * 1024 * 1024, // Sender 2MB
                         "sctp-association-recv-buffer-size",
                         4 * 1024 * 1024, // Receiveer 4MB
                         NULL);
            g_object_unref(sctp_transport);
        }
    }
}

void my_connection_set_pipeline(MyConnection *conn, GstPipeline *pipeline) {
    g_assert_nonnull(pipeline);
    if (conn->pipeline) {
        // Stop old pipeline if applicable
        gst_element_set_state(GST_ELEMENT(conn->pipeline), GST_STATE_NULL);
    }
    gst_clear_object(&conn->pipeline);
    conn->pipeline = gst_object_ref_sink(pipeline);

    conn_update_status(conn, MY_STATUS_NEGOTIATING);

    conn->webrtcbin = gst_bin_get_by_name(GST_BIN(conn->pipeline), "webrtc");
    g_assert_nonnull(conn->webrtcbin);
    g_assert(G_IS_OBJECT(conn->webrtcbin));
    g_signal_connect(conn->webrtcbin, "on-ice-candidate", G_CALLBACK(conn_webrtc_on_ice_candidate_cb), conn);
    g_signal_connect(conn->webrtcbin, "prepare-data-channel", G_CALLBACK(conn_webrtc_prepare_data_channel_cb), conn);
    g_signal_connect(conn->webrtcbin, "on-data-channel", G_CALLBACK(conn_webrtc_on_data_channel_cb), conn);
    g_signal_connect(conn->webrtcbin,
                     "deep-notify::connection-state",
                     G_CALLBACK(conn_webrtc_deep_notify_callback),
                     conn);
    //    g_signal_connect(conn->webrtcbin,
    //                     "on-ice-connection-state-change",
    //                     G_CALLBACK(on_ice_connection_state_change),
    //                     NULL);
}

static void conn_connect_internal(MyConnection *conn, enum my_status status) {
    my_connection_disconnect(conn);
    if (!conn->ws_cancel) {
        conn->ws_cancel = g_cancellable_new();
    }
    g_cancellable_reset(conn->ws_cancel);

    ALOGI("calling soup_session_websocket_connect_async. websocket_uri = %s", conn->websocket_uri);

#if SOUP_MAJOR_VERSION == 2
    soup_session_websocket_connect_async(conn->soup_session,                                     // session
                                         soup_message_new(SOUP_METHOD_GET, conn->websocket_uri), // message
                                         NULL,                                                   // origin
                                         NULL,                                                   // protocols
                                         conn->ws_cancel,                                        // cancellable
                                         (GAsyncReadyCallback)conn_websocket_connected_cb,       // callback
                                         conn);                                                  // user_data

#else
    soup_session_websocket_connect_async(conn->soup_session,                                     // session
                                         soup_message_new(SOUP_METHOD_GET, conn->websocket_uri), // message
                                         NULL,                                                   // origin
                                         NULL,                                                   // protocols
                                         0,                                                      // io_prority
                                         conn->ws_cancel,                                        // cancellable
                                         (GAsyncReadyCallback)conn_websocket_connected_cb,       // callback
                                         conn);                                                  // user_data

#endif
    conn_update_status(conn, status);
}

/* public (non-GObject) methods */

MyConnection *my_connection_new(const gchar *websocket_uri) {
    return MY_CONNECTION(g_object_new(MY_TYPE_CONNECTION, "websocket-uri", websocket_uri, NULL));
}

MyConnection *my_connection_new_localhost() {
    return MY_CONNECTION(g_object_new(MY_TYPE_CONNECTION, NULL));
}

void my_connection_connect(MyConnection *conn) {
    conn_connect_internal(conn, MY_STATUS_CONNECTING);
}

void my_connection_disconnect(MyConnection *conn) {
    if (conn) {
        conn_disconnect_internal(conn, MY_STATUS_IDLE_NOT_CONNECTED);
    }
}

bool my_connection_send_bytes(MyConnection *conn, GBytes *bytes) {
    if (conn->status != MY_STATUS_CONNECTED) {
        ALOGW("Cannot send bytes when status is %s", my_status_to_string(conn->status));
        return false;
    }

    gboolean success = gst_webrtc_data_channel_send_data_full(conn->data_channel, bytes, NULL);

    return success == TRUE;
}
