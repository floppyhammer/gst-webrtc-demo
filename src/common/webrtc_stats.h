#pragma once

#include <gio/gio.h>
#include <gst/gst.h>

GString *webrtc_stats_get_json(const GstStructure *stats);
