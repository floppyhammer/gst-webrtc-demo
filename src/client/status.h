#pragma once

#include <stdbool.h>

/// Status of the connection
enum my_status {
    /// Not connected, not connecting, and not waiting before retrying connection.
    MY_STATUS_IDLE_NOT_CONNECTED = 0,
    /// Connecting to the signaling server websocket
    MY_STATUS_CONNECTING,
    /// Failed connecting to the signaling server websocket
    MY_STATUS_WEBSOCKET_FAILED,
    /// Signaling server connection established, negotiating for full WebRTC connection
    // TODO do we need more steps here?
    MY_STATUS_NEGOTIATING,
    /// WebRTC connection established, awaiting data channel
    MY_STATUS_CONNECTED_NO_DATA,
    /// Full WebRTC connection (with data channel) established.
    MY_STATUS_CONNECTED,
    /// Disconnected following a connection error, will not retry.
    MY_STATUS_DISCONNECTED_ERROR,
    /// Disconnected following remote closing of the channel, will not retry.
    MY_STATUS_DISCONNECTED_REMOTE_CLOSE,
};

#define MY_MAKE_CASE(E) \
    case E:             \
        return #E

static inline const char* my_status_to_string(enum my_status status) {
    switch (status) {
        MY_MAKE_CASE(MY_STATUS_IDLE_NOT_CONNECTED);
        MY_MAKE_CASE(MY_STATUS_CONNECTING);
        MY_MAKE_CASE(MY_STATUS_WEBSOCKET_FAILED);
        MY_MAKE_CASE(MY_STATUS_NEGOTIATING);
        MY_MAKE_CASE(MY_STATUS_CONNECTED_NO_DATA);
        MY_MAKE_CASE(MY_STATUS_CONNECTED);
        MY_MAKE_CASE(MY_STATUS_DISCONNECTED_ERROR);
        MY_MAKE_CASE(MY_STATUS_DISCONNECTED_REMOTE_CLOSE);
        default:
            return "Unknown!";
    }
}

#undef MY_MAKE_CASE
