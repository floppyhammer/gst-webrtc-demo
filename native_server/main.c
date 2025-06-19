#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../src/server/server_pipeline.h"
#include "../src/utils/logger.h"
#include "../src/utils/platform.h"

int main(int argc, char *argv[]) {
    struct MyGstData *mgd = NULL;
    server_pipeline_create(&mgd);

    server_pipeline_play(mgd);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    ALOGD("Starting main loop");
    g_main_loop_run(loop);

    ALOGD("Exited main loop, cleaning up\n");
    g_main_loop_unref(loop);

    // Cleanup
    server_pipeline_stop(mgd);
}
