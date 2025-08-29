#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct my_client_state;

void client_create(struct my_client_state** out_client_state);

void client_stop(struct my_client_state* client_state);

#ifdef __cplusplus
}
#endif
