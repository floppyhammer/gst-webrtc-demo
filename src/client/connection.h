#pragma once

#include <glib-object.h>
#include <gst/gstpipeline.h>
#include <stdbool.h>

G_BEGIN_DECLS

#define MY_TYPE_CONNECTION my_connection_get_type()

G_DECLARE_FINAL_TYPE(MyConnection, my_connection, MY, CONNECTION, GObject)

/*!
 * Create a connection object
 *
 * @param websocket_uri The websocket URI to connect to. Ownership does not transfer (we copy it)
 */
MyConnection *my_connection_new(const gchar *websocket_uri);

MyConnection *my_connection_new_localhost();

/*!
 * Actually start connecting to the server
 */
void my_connection_connect(MyConnection *conn);

/*!
 * Drop the server connection, if any.
 */
void my_connection_disconnect(MyConnection *conn);

/*!
 * Send a message to the server over data channel
 */
bool my_connection_send_bytes(MyConnection *conn, GBytes *bytes);

/*!
 * Assign a pipeline for use.
 *
 * Will be started when the websocket connection comes up in order to negotiate using the webrtcbin.
 */
void my_connection_set_pipeline(MyConnection *conn, GstPipeline *pipeline);

G_END_DECLS
