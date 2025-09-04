#pragma once

#include <gst/gst.h>

GstPadProbeReturn on_buffer_probe_cb(GstPad* pad, const GstPadProbeInfo* info, gpointer user_data);

void list_element_properties(GstElement* element);

gboolean check_pipeline_dot_data(GstElement* pipeline);

void hook_android_log(GstDebugCategory* category,
                      GstDebugLevel level,
                      const gchar* file,
                      const gchar* function,
                      gint line,
                      GObject* object,
                      GstDebugMessage* message,
                      gpointer data);
