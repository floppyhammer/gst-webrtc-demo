// Copyright 2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

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

#include "EglData.hpp"
#include "em/em_app_log.h"
#include "em/em_connection.h"
#include "em/em_stream_client.h"
#include "em/gst_common.h"
#include "em/render/render.hpp"
#include "em/render/render_api.h"

namespace {

struct em_state {
    bool connected = false;

    int32_t width = 0;
    int32_t height = 0;

    EmConnection *connection = nullptr;
};

std::unique_ptr<Renderer> renderer;

std::unique_ptr<EglData> egl_data;

EmStreamClient *stream_client{};

em_state _state = {};

void connected_cb(EmConnection *connection, struct em_state *state) {
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
            em_connection_disconnect(_state.connection);
            _state.connected = false;
            break;
        case APP_CMD_DESTROY:
            ALOGI("APP_CMD_DESTROY");
            break;
        case APP_CMD_INIT_WINDOW: {
            ALOGI("APP_CMD_INIT_WINDOW");

            egl_data = std::make_unique<EglData>(app->window);
            egl_data->makeCurrent();

            eglQuerySurface(egl_data->display, egl_data->surface, EGL_WIDTH, &_state.width);
            eglQuerySurface(egl_data->display, egl_data->surface, EGL_HEIGHT, &_state.height);

            stream_client = em_stream_client_new();
            em_stream_client_set_egl_context(stream_client, egl_data->context, egl_data->display, egl_data->surface);

            _state.connection = g_object_ref_sink(em_connection_new_localhost());

            g_signal_connect(_state.connection, "connected", G_CALLBACK(connected_cb), &_state);

            em_connection_connect(_state.connection);

            ALOGI("%s: starting stream client mainloop thread", __FUNCTION__);
            em_stream_client_spawn_thread(stream_client, _state.connection);

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
            em_connection_disconnect(_state.connection);
            _state.connected = false;
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

struct em_sample *prev_sample;

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

        if (!egl_data || !renderer || !stream_client) {
            continue;
        }

        egl_data->makeCurrent();

        struct timespec decodeEndTime;
        struct em_sample *sample = em_stream_client_try_pull_sample(stream_client, &decodeEndTime);

        if (sample == nullptr) {
            continue;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glViewport(0, 0, _state.width, _state.height);

        renderer->draw(sample->frame_texture_id, sample->frame_texture_target);

        eglSwapBuffers(egl_data->display, egl_data->surface);

        // Release old sample
        if (prev_sample != NULL) {
            em_stream_client_release_sample(stream_client, prev_sample);
            prev_sample = NULL;
        }
        prev_sample = sample;

        egl_data->makeNotCurrent();
    }

    ALOGI("DEBUG: Exited main loop, cleaning up");

    //
    // Clean up
    //

    g_clear_object(&_state.connection);

    em_stream_client_destroy(&stream_client);

    egl_data = nullptr;

    (*app->activity->vm).DetachCurrentThread();
}
