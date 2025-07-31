#pragma once

#include <gst/gst.h>

GstPadProbeReturn buffer_probe_cb(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
