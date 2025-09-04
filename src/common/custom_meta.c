#include "custom_meta.h"

#include "../utils/logger.h"
#include "pb_encode.h"

void buffer_add_custom_meta(GstBuffer *buffer, GBytes *bytes) {
    g_assert(bytes != NULL);

    size_t payload_size;
    const gconstpointer payload_ptr = g_bytes_get_data(bytes, &payload_size);

    // Repack the protobuf into a GstBuffer
    GstBuffer *struct_buf = gst_buffer_new_memdup(payload_ptr, payload_size);
    if (!struct_buf) {
        ALOGE("Failed to allocate GstBuffer with payload.");
        return;
    }

    // Check if the buffer is writable. If not, make a copy.
    if (!gst_buffer_is_writable(buffer)) {
        buffer = gst_buffer_make_writable(buffer);
        // GST_PAD_PROBE_INFO_DATA(info) = buffer;
    }

    // Add it to a custom meta
    GstCustomMeta *custom_meta = gst_buffer_add_custom_meta(buffer, "down-message");
    if (custom_meta == NULL) {
        ALOGE("Failed to add custom meta");
        gst_buffer_unref(struct_buf);
        return;
    }
    GstStructure *custom_structure = gst_custom_meta_get_structure(custom_meta);
    gst_structure_set(custom_structure, "protobuf", GST_TYPE_BUFFER, struct_buf, NULL);

    gst_buffer_unref(struct_buf);
}

GBytes *encode_down_message(const struct _webrtc_proto_DownFrameData *msg) {
    uint8_t buf[webrtc_proto_DownFrameData_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));

    if (!pb_encode(&os, webrtc_proto_DownFrameData_fields, msg)) {
        ALOGE("Failed to encode protobuf.");
        return NULL;
    }

    return g_bytes_new(buf, os.bytes_written);
}
