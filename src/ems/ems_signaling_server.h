#pragma once

#include <glib-object.h>

#define EMS_TYPE_SIGNALING_SERVER ems_signaling_server_get_type()

G_DECLARE_FINAL_TYPE(EmsSignalingServer, ems_signaling_server, EMS, SIGNALING_SERVER, GObject)

typedef gpointer EmsClientId;

EmsSignalingServer *ems_signaling_server_new();

void ems_signaling_server_send_sdp_offer(EmsSignalingServer *server, EmsClientId client_id, const gchar *msg);

void ems_signaling_server_send_candidate(EmsSignalingServer *server,
                                         EmsClientId client_id,
                                         guint line_index,
                                         const gchar *candidate);
