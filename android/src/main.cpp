#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES3/gl32.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <assert.h>
#include <errno.h>
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <thread>

#include "../../src/utils/app_log.h"
#include "../../src/server/gstreamer_pipeline.h"

namespace {

void onAppCmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_START:
            ALOGI("APP_CMD_START");
            break;
        case APP_CMD_RESUME:
            ALOGI("APP_CMD_RESUME");
            break;
        case APP_CMD_PAUSE:
            ALOGI("APP_CMD_PAUSE");
            break;
        case APP_CMD_STOP:
            ALOGE("APP_CMD_STOP - shutting down connection");
            break;
        case APP_CMD_DESTROY:
            ALOGI("APP_CMD_DESTROY");
            break;
        case APP_CMD_INIT_WINDOW:
            ALOGI("APP_CMD_INIT_WINDOW");
            break;
        case APP_CMD_TERM_WINDOW:
            ALOGI("APP_CMD_TERM_WINDOW - shutting down connection");
            break;
    }
}

struct MyState {
    bool connected;

    uint32_t width;
    uint32_t height;
};

MyState _state = {};

/**
 * Poll for Android events, and handle them
 *
 * @param state app state
 *
 * @return true if we should go to the render code
 */
bool poll_events(struct android_app *app, struct MyState &state) {
    // Poll Android events
    for (;;) {
        int events;
        struct android_poll_source *source;
        bool wait = !app->window || app->activityState != APP_CMD_RESUME;
        int timeout = wait ? -1 : 0;
        if (ALooper_pollAll(timeout, NULL, &events, (void **)&source) >= 0) {
            if (source) {
                source->process(app, source);
            }

            if (timeout == 0 && (!app->window || app->activityState != APP_CMD_RESUME)) {
                break;
            }
        } else {
            break;
        }
    }

    return true;
}

} // namespace

void android_main(struct android_app *app) {
    // Debugging gstreamer.
    // GST_DEBUG = *:3 will give you ONLY ERROR-level messages.
    // GST_DEBUG = *:6 will give you ALL messages (make sure you BOOST your android-studio's
    // Logcat buffer to be able to capture everything gstreamer's going to spit at you !
    // in Tools -> logcat -> Cycle Buffer Size (I set it to 102400 KB).

    // setenv("GST_DEBUG", "*:3", 1);
    // setenv("GST_DEBUG", "*ssl*:9,*tls*:9,*webrtc*:9", 1);
    // setenv("GST_DEBUG", "GST_CAPS:5", 1);
    setenv("GST_DEBUG", "*:2,webrtc*:9,sctp*:9,dtls*:9,amcvideodec:9", 1);

    // Specify dot file dir
    setenv("GST_DEBUG_DUMP_DOT_DIR", "/sdcard", 1);

    // Do not do ansi color codes
    setenv("GST_DEBUG_NO_COLOR", "1", 1);

    JNIEnv *env = nullptr;
    (*app->activity->vm).AttachCurrentThread(&env, NULL);
    app->onAppCmd = onAppCmd;

    //////////////////////////////////////////
    struct MyGstData *mgd = NULL;
    gst_pipeline_create(&mgd);

    gst_pipeline_play(mgd);

    time_t start_seconds = time(NULL);
    bool wrote_dot = false;

    ALOGD("Starting main loop");
    while (!app->destroyRequested) {
        poll_events(app, _state);

        time_t now_seconds = time(NULL);
        if (!wrote_dot && now_seconds - start_seconds > 5) {
            wrote_dot = true;
            gst_pipeline_debug(mgd);
        }
    }

    ALOGD("DEBUG: Exited main loop, cleaning up");

    gst_pipeline_stop(mgd);
    //////////////////////////////////////////

    (*app->activity->vm).DetachCurrentThread();
}
