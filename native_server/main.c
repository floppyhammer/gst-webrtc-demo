#include <glib.h>

#include "../src/server/server_pipeline.h"

int main(int argc, char *argv[]) {
    struct MyGstData *mgd = NULL;
    server_pipeline_create(&mgd);

    server_pipeline_play(mgd);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    g_main_loop_run(loop);

    g_main_loop_unref(loop);

    // Cleanup
    server_pipeline_stop(mgd);
}
