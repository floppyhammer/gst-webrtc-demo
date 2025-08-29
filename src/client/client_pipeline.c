#if !defined(ANDROID)

    #include <gst/gst.h>
    #include <stdint.h>

    #include "../utils/logger.h"
    #include "stdio.h"
    #include "stream_client.h"

struct my_client_state {
    bool connected;

    int32_t width;
    int32_t height;

    MyConnection *connection;
    MyStreamClient *stream_client;
};

void webrtc_connected_cb(MyConnection *connection, struct my_client_state *state) {
    ALOGI("%s: Got signal that we are connected!", __FUNCTION__);

    state->connected = true;
}

void client_create(struct my_client_state **out_client_state) {
    struct my_client_state *my_state = malloc(sizeof(struct my_client_state));

    // setenv("GST_DEBUG", "*:2,webrtc*:9,sctp*:9,dtls*:9,amcvideodec:9", 1);

    // Do not do ansi color codes
    // setenv("GST_DEBUG_NO_COLOR", "1", 1);

    // Set up gstreamer
    gst_init(NULL, NULL);

    // Set up gst logger
    {
        // gst_debug_set_default_threshold(GST_LEVEL_WARNING);
        //		gst_debug_set_threshold_for_name("webrtcbin", GST_LEVEL_MEMDUMP);
        //      gst_debug_set_threshold_for_name("webrtcbindatachannel", GST_LEVEL_TRACE);
    }

    my_state->stream_client = my_stream_client_new();

    my_state->connection = g_object_ref_sink(my_connection_new_localhost());

    g_signal_connect(my_state->connection, "webrtc_connected", G_CALLBACK(webrtc_connected_cb), &my_state);

    my_connection_connect(my_state->connection);

    ALOGI("%s: starting stream client mainloop thread", __FUNCTION__);
    my_stream_client_spawn_thread(my_state->stream_client, my_state->connection);

    *out_client_state = my_state;
}

void client_stop(struct my_client_state *client_state) {
    my_connection_disconnect(client_state->connection);
    client_state->connected = false;

    g_clear_object(&client_state->connection);

    my_stream_client_destroy(&client_state->stream_client);

    free(client_state);
}

#endif
