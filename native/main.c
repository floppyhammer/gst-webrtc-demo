#include "stdio.h"

#include "../src/server/gstreamer_pipeline.h"
#include "../src/server/signaling_server.h"
#include "../src/utils/app_log.h"

int main(int argc, char *argv[]) {
    struct MyGstData *mgd = NULL;
    gst_pipeline_create(&mgd);

    gst_pipeline_play(mgd);

    ALOGD("Starting main loop.\n");
    while (1) {
    }

    ALOGI("DEBUG: Exited main loop, cleaning up.\n");

    gst_pipeline_stop(mgd);
}
