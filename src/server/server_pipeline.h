#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct MyGstData;

void server_pipeline_play(struct MyGstData* mgd);

void server_pipeline_stop(struct MyGstData* mgd);

void server_pipeline_create(struct MyGstData** out_mgd);

void server_pipeline_dump(struct MyGstData* mgd);

#ifdef __cplusplus
}
#endif
