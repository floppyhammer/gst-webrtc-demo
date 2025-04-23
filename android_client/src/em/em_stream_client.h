// Copyright 2022-2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

/*!
 * @file
 * @brief  Header for the stream client module of the ElectricMaple XR streaming solution
 * @author Rylie Pavlik <rpavlik@collabora.com>
 * @ingroup em_client
 */
#pragma once

#include <EGL/egl.h>
#include <glib-object.h>
#include <stdbool.h>

#include "em_connection.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

struct em_sample;

typedef struct _EmStreamClient EmStreamClient;

/*!
 * Create a stream client object, providing the connection object
 *
 * @memberof EmStreamClient
 */
EmStreamClient *em_stream_client_new();

/*!
 * Clear a pointer and free the associate stream client, if any.
 *
 * Handles null checking for you.
 */
void em_stream_client_destroy(EmStreamClient **ptr_sc);

/*!
 * Start the GMainLoop embedded in this object in a new thread
 *
 * @param connection The connection to use
 */
void em_stream_client_spawn_thread(EmStreamClient *sc, EmConnection *connection);

/*!
 * Stop the pipeline and the mainloop thread.
 */
void em_stream_client_stop(EmStreamClient *sc);

/*!
 * Attempt to retrieve a sample, if one has been decoded.
 *
 * Non-null return values need to be released with @ref em_stream_client_release_sample.

* @param sc self
* @param[out] out_decode_end struct to populate with decode-end time.
 */
struct em_sample *em_stream_client_try_pull_sample(EmStreamClient *sc, struct timespec *out_decode_end);

/*!
 * Release a sample returned from @ref em_stream_client_try_pull_sample
 */
void em_stream_client_release_sample(EmStreamClient *sc, struct em_sample *ems);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
