/*!
 * @file
 * @brief Main file for WebRTC client.
 * @author Moshi Turner <moses@collabora.com>
 * @author Rylie Pavlik <rpavlik@collabora.com>
 */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES3/gl32.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <assert.h>
#include <errno.h>
#include <gst/gst.h>
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

#include "../../src/client/connection.h"
#include "../../src/client/gst_common.h"
#include "../../src/client/stream_client.h"
#include "../../src/utils/logger.h"
#include "EglData.hpp"
#include "render/render.hpp"
#include "render/render_api.h"

namespace {

struct my_client_state {
    bool connected;

    int32_t width;
    int32_t height;

    MyConnection *connection;
    MyStreamClient *stream_client;
};

std::unique_ptr<Renderer> renderer;

std::unique_ptr<EglData> egl_data;

my_client_state my_state = {};

void connected_cb(MyConnection *connection, struct my_client_state *state) {
    ALOGI("%s: Got signal that we are connected!", __FUNCTION__);

    state->connected = true;
}

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
            my_connection_disconnect(my_state.connection);
            my_state.connected = false;
            break;
        case APP_CMD_DESTROY:
            ALOGI("APP_CMD_DESTROY");
            break;
        case APP_CMD_INIT_WINDOW: {
            ALOGI("APP_CMD_INIT_WINDOW");

            egl_data = std::make_unique<EglData>(app->window);
            egl_data->makeCurrent();

            eglQuerySurface(egl_data->display, egl_data->surface, EGL_WIDTH, &my_state.width);
            eglQuerySurface(egl_data->display, egl_data->surface, EGL_HEIGHT, &my_state.height);

            my_state.stream_client = my_stream_client_new();
            my_stream_client_set_egl_context(my_state.stream_client,
                                             egl_data->context,
                                             egl_data->display,
                                             egl_data->surface);

            my_state.connection = g_object_ref_sink(my_connection_new_localhost());

            g_signal_connect(my_state.connection, "webrtc_connected", G_CALLBACK(connected_cb), &my_state);

            my_connection_connect(my_state.connection);

            ALOGI("%s: starting stream client mainloop thread", __FUNCTION__);
            my_stream_client_spawn_thread(my_state.stream_client, my_state.connection);

            try {
                ALOGI("%s: Setup renderer...", __FUNCTION__);
                renderer = std::make_unique<Renderer>();
                renderer->setupRender();
            } catch (std::exception const &e) {
                ALOGE("%s: Caught exception setting up renderer: %s", __FUNCTION__, e.what());
                renderer->reset();
                abort();
            }
        } break;
        case APP_CMD_TERM_WINDOW:
            ALOGI("APP_CMD_TERM_WINDOW - shutting down connection");
            my_connection_disconnect(my_state.connection);
            my_state.connected = false;
            break;
    }
}

/**
 * Poll for Android and OpenXR events, and handle them
 *
 * @param state app state
 *
 * @return true if we should go to the render code
 */
bool poll_events(struct android_app *app) {
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

struct my_sample *prev_sample;

void android_main(struct android_app *app) {
    setenv("GST_DEBUG", "*:2,webrtc*:9,sctp*:9,dtls*:9,amcvideodec:9", 1);

    // Do not do ansi color codes
    setenv("GST_DEBUG_NO_COLOR", "1", 1);

    JNIEnv *env = nullptr;
    (*app->activity->vm).AttachCurrentThread(&env, NULL);
    app->onAppCmd = onAppCmd;

    //
    // Start of remote-rendering-specific code
    //

    // Set up gstreamer
    gst_init(0, NULL);

    // Set up gst logger
    {
        gst_debug_set_default_threshold(GST_LEVEL_WARNING);
        //		gst_debug_set_threshold_for_name("webrtcbin", GST_LEVEL_MEMDUMP);
        //      gst_debug_set_threshold_for_name("webrtcbindatachannel", GST_LEVEL_TRACE);
    }

    // Set rank for decoder c2qtiavcdecoder
    GstRegistry *plugins_register = gst_registry_get();
    GstPluginFeature *dec = gst_registry_lookup_feature(plugins_register, "amcviddec-c2qtiavcdecoder");
    if (dec == NULL) {
        ALOGW("c2qtiavcdecoder not available!");
    } else {
        gst_plugin_feature_set_rank(dec, GST_RANK_PRIMARY + 1);
        gst_object_unref(dec);
    }

    //
    // End of remote-rendering-specific setup, into main loop
    //

    // Main rendering loop.
    ALOGI("DEBUG: Starting main loop");
    while (!app->destroyRequested) {
        if (!poll_events(app)) {
            continue;
        }

        if (!egl_data || !renderer || !my_state.stream_client) {
            continue;
        }

        egl_data->makeCurrent();

        struct timespec decodeEndTime;
        struct my_sample *sample = my_stream_client_try_pull_sample(my_state.stream_client, &decodeEndTime);

        if (sample == nullptr) {
            continue;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glViewport(0, 0, my_state.width, my_state.height);

        renderer->draw(sample->frame_texture_id, sample->frame_texture_target);

        eglSwapBuffers(egl_data->display, egl_data->surface);

        // Release old sample
        if (prev_sample != NULL) {
            my_stream_client_release_sample(my_state.stream_client, prev_sample);
            prev_sample = NULL;
        }
        prev_sample = sample;

        egl_data->makeNotCurrent();
    }

    ALOGI("DEBUG: Exited main loop, cleaning up");

    //
    // Clean up
    //

    g_clear_object(&my_state.connection);

    my_stream_client_destroy(&my_state.stream_client);

    egl_data = nullptr;

    (*app->activity->vm).DetachCurrentThread();
}
