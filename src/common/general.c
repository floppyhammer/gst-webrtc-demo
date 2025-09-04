#include "general.h"

#include <stdint.h>

#include "../utils/logger.h"

GstPadProbeReturn on_buffer_probe_cb(GstPad* pad, const GstPadProbeInfo* info, gpointer user_data) {
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        return GST_PAD_PROBE_OK;
    }

    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);

    const GstClockTime pts = GST_BUFFER_PTS(buf);
    const GstClockTime dts = GST_BUFFER_DTS(buf);
    const GstClockTime duration = GST_BUFFER_DURATION(buf);

    static GstClockTime prev_pts = 0;

    if (prev_pts != 0) {
        const int64_t pts_diff = (pts - prev_pts) / 1e6;
        ALOGD("Buffer probe: PTS: %" GST_TIME_FORMAT ", PTS diff: %ld", GST_TIME_ARGS(pts), pts_diff);
    }
    prev_pts = pts;

    static uint16_t prev_seq_num = 0;

    GstMapInfo map;
    if (gst_buffer_map(buf, &map, GST_MAP_READ)) {
        if (map.size >= 12) {
            const guint8* data = map.data;
            const uint16_t seq_num = (data[2] << 8) | data[3]; // For big-endian systems
            ALOGD("Buffer probe: sequence number: %u", seq_num);

            if (seq_num - prev_seq_num > 1) {
                ALOGE("Discontinuous sequence number!");
            }

            prev_seq_num = seq_num;
        }
        gst_buffer_unmap(buf, &map);
    }

    return GST_PAD_PROBE_OK;
}

void list_element_properties(GstElement* element) {
    GObjectClass* gobject_class = G_OBJECT_GET_CLASS(element);

    guint num_properties;
    GParamSpec** properties = g_object_class_list_properties(gobject_class, &num_properties);

    ALOGI("Properties of element '%s':", gst_element_get_name(element));

    for (guint i = 0; i < num_properties; i++) {
        GParamSpec* pspec = properties[i];
        ALOGI("  - Name: %s (Type: %s, Flags: %s)",
              g_param_spec_get_name(pspec),
              g_type_name(G_PARAM_SPEC_TYPE(pspec)),
              g_strdup_printf("%s%s%s",
                              pspec->flags & G_PARAM_READABLE ? "R" : "",
                              pspec->flags & G_PARAM_WRITABLE ? "W" : "",
                              pspec->flags & G_PARAM_CONSTRUCT ? "C" : "" // Add more flags as needed
                              ));
    }

    g_free(properties); // Free the array of GParamSpec pointers
}
