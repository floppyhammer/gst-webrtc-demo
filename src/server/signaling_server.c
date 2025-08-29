#include "signaling_server.h"

#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup-server.h>
#include <libsoup/soup-version.h>

#if SOUP_CHECK_VERSION(3, 0, 0)
    #include <libsoup/soup-server-message.h>
#endif

#include "../utils/logger.h"

struct _SignalingServer {
    GObject parent;

    SoupServer *soup_server;

    GSList *websocket_connections;
};

G_DEFINE_TYPE(SignalingServer, signaling_server, G_TYPE_OBJECT)

enum {
    SIGNAL_WS_CLIENT_CONNECTED,
    SIGNAL_WS_CLIENT_DISCONNECTED,
    SIGNAL_SDP_ANSWER,
    SIGNAL_CANDIDATE,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

SignalingServer *signaling_server_new() {
    return GWD_SIGNALING_SERVER(g_object_new(TYPE_SIGNALING_SERVER, NULL));
}

#if !SOUP_CHECK_VERSION(3, 0, 0)

static void http_cb(SoupServer *server,
                    SoupMessage *msg,
                    const char *path,
                    GHashTable *query,
                    SoupClientContext *client,
                    gpointer user_data) {
    // We're not serving any HTTP traffic - if somebody (erroneously) submits an HTTP request,
    // tell them to get lost.
    ALOGE("Got an erroneous HTTP request from %s", soup_client_context_get_host(client));
    soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
}

#else

static void http_cb(SoupServer *server,     //
                    SoupServerMessage *msg, //
                    const char *path,       //
                    GHashTable *query,      //
                    gpointer user_data) {
    // We're not serving any HTTP traffic - if somebody (erroneously) submits an HTTP request,
    // tell them to get lost.
    ALOGE("Got an erroneous HTTP request from %s", soup_server_message_get_remote_host(msg));
    soup_server_message_set_status(msg, SOUP_STATUS_NOT_FOUND, NULL);
}

#endif

static void signaling_server_handle_message(SignalingServer *server,
                                            SoupWebsocketConnection *connection,
                                            GBytes *message) {
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
        if (g_str_equal(msg_type, "answer")) {
            const gchar *answer_sdp = json_object_get_string_member(msg, "sdp");
            ALOGD("Received SDP answer: \n%s", answer_sdp);

            g_signal_emit(server, signals[SIGNAL_SDP_ANSWER], 0, connection, answer_sdp);
        } else if (g_str_equal(msg_type, "candidate")) {
            JsonObject *candidate = json_object_get_object_member(msg, "candidate");

            g_signal_emit(server,
                          signals[SIGNAL_CANDIDATE],
                          0,
                          connection,
                          json_object_get_int_member(candidate, "sdpMLineIndex"),
                          json_object_get_string_member(candidate, "candidate"));
        }
    } else {
        ALOGD("Error parsing message: %s", error->message);
        g_clear_error(&error);
    }

out:
    g_object_unref(parser);
}

static void message_cb(SoupWebsocketConnection *connection, gint type, GBytes *message, gpointer user_data) {
    ALOGD("Signaling server received a message");

    switch (type) {
        case SOUP_WEBSOCKET_DATA_BINARY: {
            ALOGE("Received unknown binary message, ignoring\n");
            return;
        }
        case SOUP_WEBSOCKET_DATA_TEXT: {
            signaling_server_handle_message(GWD_SIGNALING_SERVER(user_data), connection, message);
        } break;
        default:
            g_assert_not_reached();
    }
}

static void signaling_server_remove_websocket_connection(SignalingServer *server, SoupWebsocketConnection *connection) {
    ALOGD("Removed websocket connection");

    const ClientId client_id = g_object_get_data(G_OBJECT(connection), "client_id");

    server->websocket_connections = g_slist_remove(server->websocket_connections, client_id);

    g_signal_emit(server, signals[SIGNAL_WS_CLIENT_DISCONNECTED], 0, client_id);
}

static void closed_cb(SoupWebsocketConnection *connection, gpointer user_data) {
    ALOGD("Connection closed");

    signaling_server_remove_websocket_connection(GWD_SIGNALING_SERVER(user_data), connection);
}

static void signaling_server_add_websocket_connection(SignalingServer *server, SoupWebsocketConnection *connection) {
    ALOGD("Added websocket connection");

    g_object_ref(connection);
    server->websocket_connections = g_slist_append(server->websocket_connections, connection);
    g_object_set_data(G_OBJECT(connection), "client_id", connection);

    g_signal_connect(connection, "message", (GCallback)message_cb, server);
    g_signal_connect(connection, "closed", (GCallback)closed_cb, server);

    g_signal_emit(server, signals[SIGNAL_WS_CLIENT_CONNECTED], 0, connection);
}

#if !SOUP_CHECK_VERSION(3, 0, 0)
static void websocket_cb(SoupServer *server,
                         SoupWebsocketConnection *connection,
                         const char *path,
                         SoupClientContext *client,
                         gpointer user_data) {
    ALOGD("New websocket connection from %s", soup_client_context_get_host(client));

    signaling_server_add_websocket_connection(GWD_SIGNALING_SERVER(user_data), connection);
}
#else

static void websocket_cb(SoupServer *server,
                         SoupServerMessage *msg,
                         const char *path,
                         SoupWebsocketConnection *connection,
                         gpointer user_data) {
    ALOGD("New connection from somewhere");

    signaling_server_add_websocket_connection(GWD_SIGNALING_SERVER(user_data), connection);
}

#endif

static void signaling_server_init(SignalingServer *server) {
    GError *error = NULL;

    server->soup_server = soup_server_new(NULL, NULL);
    g_assert_no_error(error);

    soup_server_add_handler(server->soup_server, NULL, http_cb, server, NULL);
    soup_server_add_websocket_handler(server->soup_server, "/ws", NULL, NULL, websocket_cb, server, NULL);

    soup_server_listen_all(server->soup_server, 52356, 0, &error);
    g_assert_no_error(error);
}

static void signaling_server_send_to_websocket_client(SignalingServer *server, ClientId client_id, JsonNode *msg) {
    SoupWebsocketConnection *connection = client_id;
    g_info("%s", __func__);

    if (!g_slist_find(server->websocket_connections, connection)) {
        g_warning("Unknown websocket connection.");
        return;
    }

    SoupWebsocketState socket_state = soup_websocket_connection_get_state(connection);

    if (socket_state == SOUP_WEBSOCKET_STATE_OPEN) {
        gchar *msg_str = json_to_string(msg, TRUE);

        soup_websocket_connection_send_text(connection, msg_str);

        g_free(msg_str);
    } else {
        g_warning("Trying to send message using websocket that isn't open.");
    }
}

void signaling_server_send_sdp_offer(SignalingServer *server, const ClientId client_id, const gchar *sdp) {
    ALOGD("Send SDP offer: \n%s", sdp);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "msg");
    json_builder_add_string_value(builder, "offer");

    json_builder_set_member_name(builder, "sdp");
    json_builder_add_string_value(builder, sdp);
    json_builder_end_object(builder);

    JsonNode *root = json_builder_get_root(builder);

    signaling_server_send_to_websocket_client(server, client_id, root);

    json_node_unref(root);
    g_object_unref(builder);
}

void signaling_server_send_candidate(SignalingServer *server,
                                     const ClientId client_id,
                                     const guint m_line_index,
                                     const gchar *candidate) {
    ALOGD("Send ICE candidate: %u %s", m_line_index, candidate);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "msg");
    json_builder_add_string_value(builder, "candidate");

    json_builder_set_member_name(builder, "candidate");
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "candidate");
    json_builder_add_string_value(builder, candidate);
    json_builder_set_member_name(builder, "sdpMLineIndex");
    json_builder_add_int_value(builder, m_line_index);
    json_builder_end_object(builder);
    json_builder_end_object(builder);

    JsonNode *root = json_builder_get_root(builder);

    signaling_server_send_to_websocket_client(server, client_id, root);

    json_node_unref(root);
    g_object_unref(builder);
}

static void signaling_server_dispose(GObject *object) {
    SignalingServer *self = GWD_SIGNALING_SERVER(object);

    soup_server_disconnect(self->soup_server);
    g_clear_object(&self->soup_server);
}

static void signaling_server_class_init(SignalingServerClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = signaling_server_dispose;

    signals[SIGNAL_WS_CLIENT_CONNECTED] = g_signal_new("ws-client-connected",
                                                       G_OBJECT_CLASS_TYPE(klass),
                                                       G_SIGNAL_RUN_LAST,
                                                       0,
                                                       NULL,
                                                       NULL,
                                                       NULL,
                                                       G_TYPE_NONE,
                                                       1,
                                                       G_TYPE_POINTER);

    signals[SIGNAL_WS_CLIENT_DISCONNECTED] = g_signal_new("ws-client-disconnected",
                                                          G_OBJECT_CLASS_TYPE(klass),
                                                          G_SIGNAL_RUN_LAST,
                                                          0,
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          G_TYPE_NONE,
                                                          1,
                                                          G_TYPE_POINTER);

    signals[SIGNAL_SDP_ANSWER] = g_signal_new("sdp-answer",
                                              G_OBJECT_CLASS_TYPE(klass),
                                              G_SIGNAL_RUN_LAST,
                                              0,
                                              NULL,
                                              NULL,
                                              NULL,
                                              G_TYPE_NONE,
                                              2,
                                              G_TYPE_POINTER,
                                              G_TYPE_STRING);

    signals[SIGNAL_CANDIDATE] = g_signal_new("candidate",
                                             G_OBJECT_CLASS_TYPE(klass),
                                             G_SIGNAL_RUN_LAST,
                                             0,
                                             NULL,
                                             NULL,
                                             NULL,
                                             G_TYPE_NONE,
                                             3,
                                             G_TYPE_POINTER,
                                             G_TYPE_UINT,
                                             G_TYPE_STRING);
}
