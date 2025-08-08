#include "probe.h"

#include <stdint.h>

#include "../utils/logger.h"

GstPadProbeReturn buffer_probe_cb(GstPad* pad, const GstPadProbeInfo* info, gpointer user_data) {
    return GST_PAD_PROBE_OK;

    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
        const GstClockTime pts = GST_BUFFER_PTS(buf);

        static GstClockTime previous_pts = 0;
        static int64_t previous_time = 0;
        static uint16_t previous_seq_num = 0;

        if (previous_pts != 0) {
            const int64_t pts_diff = (pts - previous_pts) / 1e6;

            if (pts_diff > 50) {
                ALOGE("Buffer probe: PTS: %" GST_TIME_FORMAT ", PTS diff: %ld", GST_TIME_ARGS(pts), pts_diff);
            } else {
                ALOGD("Buffer probe: PTS: %" GST_TIME_FORMAT ", PTS diff: %ld", GST_TIME_ARGS(pts), pts_diff);
            }
        }
        previous_pts = pts;

        GstMapInfo map;
        if (gst_buffer_map(buf, &map, GST_MAP_READ)) {
            if (map.size >= 12) {
                const guint8* data = map.data;
                const uint16_t seq_num = (data[2] << 8) | data[3]; // For big-endian systems
                ALOGD("Buffer probe: sequence number: %u", seq_num);

                if (seq_num - previous_seq_num > 1) {
                    ALOGE("Discontinuous sequence number!");
                }

                previous_seq_num = seq_num;
            }
            gst_buffer_unmap(buf, &map);
        }
    }

    return GST_PAD_PROBE_OK;
}
