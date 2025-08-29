#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct MyGstData;

void server_pipeline_play(struct MyGstData* mgd);

void server_stop(struct MyGstData* mgd);

void server_pipeline_create(struct MyGstData** out_mgd);

void server_pipeline_push_pcm(struct MyGstData* mgd, const void* audio_bytes, int size);

#ifdef __cplusplus
}
#endif
