#pragma once

#include <gst/gst.h>

#include "../../proto/generated/webrtc.pb.h"

#define RTP_TWOBYTES_HDR_EXT_ID 1 // Must be in the [1,15] range
#define RTP_TWOBYTES_HDR_EXT_MAX_SIZE 255

void buffer_add_custom_meta(GstBuffer *buffer, GBytes *bytes);

GBytes *encode_down_message(const struct _webrtc_proto_DownFrameData *msg);
