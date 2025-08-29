#pragma once

#include <glib-object.h>

#include "connection.h"

#ifdef __cplusplus
extern "C" {
#endif

struct my_sample;

typedef struct my_stream_client MyStreamClient;

/*!
 * Create a stream client object, providing the connection object
 */
MyStreamClient *my_stream_client_new();

#ifdef ANDROID
/// Initialize the EGL context and surface.
void my_stream_client_set_egl_context(MyStreamClient *sc, EGLContext context, EGLDisplay display, EGLSurface surface);
#endif

/*!
 * Clear a pointer and free the associate stream client, if any.
 *
 * Handles null checking for you.
 */
void my_stream_client_destroy(MyStreamClient **ptr_sc);

/*!
 * Start the GMainLoop embedded in this object in a new thread
 *
 * @param connection The connection to use
 */
void my_stream_client_spawn_thread(MyStreamClient *sc, MyConnection *connection);

/*!
 * Stop the pipeline and the mainloop thread.
 */
void my_stream_client_stop(MyStreamClient *sc);

/*!
 * Attempt to retrieve a sample, if one has been decoded.
 *
 * Non-null return values need to be released with @ref my_stream_client_release_sample.

* @param sc self
* @param[out] out_decode_end struct to populate with decode-end time.
 */
struct my_sample *my_stream_client_try_pull_sample(MyStreamClient *sc, struct timespec *out_decode_end);

/*!
 * Release a sample returned from @ref my_stream_client_try_pull_sample
 */
void my_stream_client_release_sample(MyStreamClient *sc, struct my_sample *sample);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
