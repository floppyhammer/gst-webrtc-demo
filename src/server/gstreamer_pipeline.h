#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct ems_callbacks;

struct MyGstData;

void gst_pipeline_play(struct MyGstData *mgd);

void gst_pipeline_stop(struct MyGstData *mgd);

void gst_pipeline_create(struct ems_callbacks *callbacks_collection, struct MyGstData **out_gst_data);

#ifdef __cplusplus
}
#endif
