#pragma once

#include <glib-object.h>

#define TYPE_SIGNALING_SERVER signaling_server_get_type()

G_DECLARE_FINAL_TYPE(SignalingServer, signaling_server, GWD, SIGNALING_SERVER, GObject)

typedef gpointer ClientId;

SignalingServer *signaling_server_new();

void signaling_server_send_sdp_offer(SignalingServer *server, ClientId client_id, const gchar *msg);

void signaling_server_send_candidate(SignalingServer *server,
                                     ClientId client_id,
                                     guint line_index,
                                     const gchar *candidate);
