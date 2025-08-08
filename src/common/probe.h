#pragma once

#include <gst/gst.h>

GstPadProbeReturn buffer_probe_cb(GstPad* pad, const GstPadProbeInfo* info, gpointer user_data);
