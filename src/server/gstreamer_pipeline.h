#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct MyGstData;

void gst_pipeline_play(struct MyGstData *mgd);

void gst_pipeline_stop(struct MyGstData *mgd);

void gst_pipeline_create(struct MyGstData **out_mgd);

void gst_pipeline_dump(struct MyGstData *mgd);

#ifdef __cplusplus
}
#endif
