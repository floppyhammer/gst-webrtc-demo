# Copyright 2023, Pluto VR, Inc.
#
# SPDX-License-Identifier: BSL-1.0

add_library(
        gst_webrtc_client SHARED
        em_connection.c
        em_stream_client.c
        render/GLDebug.cpp
        render/GLError.cpp
        render/GLSwapchain.cpp
        render/render.cpp
)
target_link_libraries(
        gst_webrtc_client
        PRIVATE ${ANDROID_LOG_LIBRARY}
        PUBLIC
        EGL::EGL
        OpenGLES::OpenGLESv3
        gstreamer_android
)
target_compile_definitions(
        gst_webrtc_client PUBLIC XR_USE_PLATFORM_ANDROID XR_USE_GRAPHICS_API_OPENGL_ES
        XR_USE_TIMESPEC
)
target_include_directories(
        gst_webrtc_client
        PRIVATE ${LIBSOUP_INCLUDE_DIRS} ${JSONGLIB_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS}
        PUBLIC .. ${GST_INCLUDE_DIRS} ${GLIB_INCLUDE_DIRS}
)
