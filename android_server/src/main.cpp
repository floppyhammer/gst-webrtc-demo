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

#include "../../src/server/server_pipeline.h"
#include "../../src/utils/logger.h"

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

// For GStreamer (ensure you have includes and linking set up in CMakeLists.txt)
// #include <gst/gst.h>
// #include <gst/app/gstappsrc.h>

static struct MyGstData *mgd = NULL;

#define TAG "GstWebrtcServerNative"

// Example: Pointer to a GStreamer AppSrc element if you're pushing data to it
// GstAppSrc *audio_app_src = nullptr;

extern "C" {

// JNI function corresponding to nativeProcessAudio(data: ByteArray, size: Int, timestamp: Long)
JNIEXPORT void JNICALL Java_com_gst_webrtc_1server_ScreenCaptureService_nativeProcessAudio(JNIEnv *env,
                                                                                           jobject /* this */,
                                                                                           jbyteArray data,
                                                                                           jint size,
                                                                                           jlong timestamp) {
    if (!data || size <= 0) {
        __android_log_print(ANDROID_LOG_WARN, TAG, "Received empty or invalid audio data.");
        return;
    }

    // Get the byte array elements from Java. This might involve a copy.
    jbyte *audio_bytes = env->GetByteArrayElements(data, nullptr);
    if (!audio_bytes) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Failed to get byte array elements.");
        return;
    }

    // --- Process the audio_bytes (PCM data) here ---
    // For example, log the first few bytes or the size
    // __android_log_print(ANDROID_LOG_INFO, TAG, "Native received %d audio bytes. Timestamp: %lld", size, timestamp);
    // if (size > 4) {
    //     __android_log_print(ANDROID_LOG_DEBUG, TAG, "First 4 bytes: %d %d %d %d",
    //                         audio_bytes[0], audio_bytes[1], audio_bytes[2], audio_bytes[3]);
    // }

    server_pipeline_push_pcm(mgd, audio_bytes, size);

    // Release the byte array elements. '0' means copy back changes (if any) and free the buffer.
    // JNI_ABORT means free the buffer without copying back if you didn't modify it.
    env->ReleaseByteArrayElements(data, audio_bytes, JNI_ABORT);
}

// If using a direct ByteBuffer:
// JNIEXPORT void JNICALL
// Java_com_gst_webrtc_1server_StreamingActivity_nativeProcessDirectAudio(
//         JNIEnv *env,
//         jobject /* this */,
//         jobject buffer, // This is the ByteBuffer jobject
//         jint size,
//         jlong timestamp) {
//
//     if (!buffer || size <= 0) {
//         __android_log_print(ANDROID_LOG_WARN, TAG, "Received null or empty direct buffer.");
//         return;
//     }
//
//     // Get direct access to the buffer's memory
//     uint8_t *direct_audio_bytes = static_cast<uint8_t *>(env->GetDirectBufferAddress(buffer));
//     if (!direct_audio_bytes) {
//         __android_log_print(ANDROID_LOG_ERROR, TAG, "Failed to get direct buffer address.");
//         return;
//     }
//
//     // --- Process the direct_audio_bytes (PCM data) here ---
//     // __android_log_print(ANDROID_LOG_INFO, TAG, "Native received %d direct audio bytes. Timestamp: %lld", size,
//     timestamp);
//
//     // No need to release direct buffer elements like GetByteArrayElements,
//     // as you are working directly with the memory managed by the Java ByteBuffer.
// }

// Optional: JNI functions for initializing/releasing native components
/*
JNIEXPORT jlong JNICALL
Java_com_gst_webrtc_1server_StreamingActivity_nativeInit(JNIEnv *env, jobject thiz) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "NativeInit called.");
    // Initialize GStreamer, create pipeline, setup appsrc, etc.
    // gst_init(nullptr, nullptr);
    // GstElement *pipeline = gst_pipeline_new("audio-pipeline");
    // audio_app_src = GST_APP_SRC(gst_element_factory_make("appsrc", "audio_source"));
    // ... configure appsrc (caps, format, stream-type, etc.)
    // gst_bin_add(GST_BIN(pipeline), GST_ELEMENT(audio_app_src));
    // ... add other GStreamer elements and link them ...
    // gst_element_set_state(pipeline, GST_STATE_PLAYING);
    // return (jlong)pipeline; // Or some other native object handle
    return 0; // Placeholder
}

JNIEXPORT void JNICALL
Java_com_gst_webrtc_1server_StreamingActivity_nativeRelease(JNIEnv *env, jobject thiz, jlong ptr) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "NativeRelease called.");
    // GstElement *pipeline = (GstElement *)ptr;
    // if (pipeline) {
    //     gst_element_set_state(pipeline, GST_STATE_NULL);
    //     gst_object_unref(pipeline);
    //     audio_app_src = nullptr; // Clear the global pointer
    // }
    // gst_deinit(); // If GStreamer is no longer needed
}
*/

} // extern "C"

void android_main(struct android_app *app) {
    // Debugging gstreamer.
    // GST_DEBUG = *:3 will give you ONLY ERROR-level messages.
    // GST_DEBUG = *:6 will give you ALL messages (make sure you BOOST your android-studio's
    // Logcat buffer to be able to capture everything gstreamer's going to spit at you !
    // in Tools -> logcat -> Cycle Buffer Size (I set it to 102400 KB).

    // setenv("GST_DEBUG", "*:3", 1);
    // setenv("GST_DEBUG", "*ssl*:9,*tls*:9,*webrtc*:9", 1);
    // setenv("GST_DEBUG", "GST_CAPS:5", 1);
    //    setenv("GST_DEBUG", "*:2,webrtc*:9,sctp*:2,dtls*:2,amcvideodec:9", 1);

    // Specify dot file dir
    setenv("GST_DEBUG_DUMP_DOT_DIR", "/sdcard", 1);

    // Do not do ansi color codes
    setenv("GST_DEBUG_NO_COLOR", "1", 1);

    JNIEnv *env = nullptr;
    (*app->activity->vm).AttachCurrentThread(&env, NULL);
    app->onAppCmd = onAppCmd;

    //////////////////////////////////////////
    server_pipeline_create(&mgd);

    server_pipeline_play(mgd);

    ALOGD("Starting main loop");
    while (!app->destroyRequested) {
        poll_events(app, _state);
    }

    ALOGD("DEBUG: Exited main loop, cleaning up");

    server_pipeline_stop(mgd);
    //////////////////////////////////////////

    (*app->activity->vm).DetachCurrentThread();
}
