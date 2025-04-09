#include <stdbool.h>

#include "stdio.h"

#include "../src/server/gstreamer_pipeline.h"
#include "../src/server/signaling_server.h"
#include "../src/utils/logger.h"

int main(int argc, char *argv[]) {
    setenv("GST_DEBUG", "GST_TRACER:7", 1);
    setenv("GST_TRACERS", "latency(flags=element+pipeline)", 1);
    setenv("GST_DEBUG_FILE", "./latency.log", 1);

    // Specify dot file dir
    setenv("GST_DEBUG_DUMP_DOT_DIR", "./", 1);

    // Do not do ansi color codes
    setenv("GST_DEBUG_NO_COLOR", "1", 1);

    struct MyGstData *mgd = NULL;
    gst_pipeline_create(&mgd);

    gst_pipeline_play(mgd);

    time_t start_seconds = time(NULL);
    bool wrote_dot = false;

    ALOGD("Starting main loop");
    while (1) {
        time_t now_seconds = time(NULL);
        if (!wrote_dot && now_seconds - start_seconds > 5) {
            wrote_dot = true;
            gst_pipeline_debug(mgd);
        }
    }

    ALOGD("Exited main loop, cleaning up\n");

    gst_pipeline_stop(mgd);
}
