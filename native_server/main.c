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

    ALOGD("Starting main loop");
    while (1) {
    }

    ALOGD("Exited main loop, cleaning up\n");

    server_pipeline_stop(mgd);
}
