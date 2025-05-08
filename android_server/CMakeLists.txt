cmake_minimum_required(VERSION 3.10.2)

project(gst_webrtc_server_demo VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Vulkan REQUIRED)
find_package(EGL REQUIRED)
find_package(OpenGLES REQUIRED COMPONENTS V3)

# Default to PIC code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (ANDROID)
    find_library(ANDROID_LOG_LIBRARY log)
    find_library(ANDROID_LIBRARY android)
endif ()

if (ANDROID)
    if (ANDROID_ABI STREQUAL "arm64-v8a")
        set(GST_ARCH arm64)
    elseif (ANDROID_ABI STREQUAL "armeabi-v7a")
        set(GST_ARCH armv7)
    elseif (ANDROID_ABI STREQUAL "x86")
        set(GST_ARCH x86)
    elseif (ANDROID_ABI STREQUAL "x86_64")
        set(GST_ARCH x86_64)
    else ()
        message(FATAL_ERROR "What is this ABI? ${ANDROID_ABI}")
    endif ()

    if (GST_ARCH_DIR)
        message(STATUS "Using gstreamer from ${GST_ARCH_DIR}")
    else ()
        if (GST_BASE_DIR)
            message(STATUS "GST_ARCH_DIR not set, computing from arch and GST_BASE_DIR")
        else ()
            message(
                    STATUS
                    "GST_ARCH_DIR and GST_BASE_DIR not set, assuming default location"
            )
            set(GST_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../deps/gstreamer_android")
        endif ()
        set(GST_ARCH_DIR ${GST_BASE_DIR}/${GST_ARCH})
    endif ()

    set(USE_GST_SHARED OFF)
    if (USE_GST_SHARED)
        include(libraries_shared.cmake)
    else ()
        include(libraries_static.cmake)
    endif ()

    set(GST_INCLUDE_DIRS ${GST_ARCH_DIR}/include/gstreamer-1.0/
            ${GST_ARCH_DIR}/lib/gstreamer-1.0/include/
    )
    set(GLIB_INCLUDE_DIRS ${GST_ARCH_DIR}/include/glib-2.0
            ${GST_ARCH_DIR}/lib/glib-2.0/include/
    )
    set(LIBSOUP_INCLUDE_DIRS ${GST_ARCH_DIR}/include/libsoup-3.0/)
    set(JSONGLIB_INCLUDE_DIRS ${GST_ARCH_DIR}/include/json-glib-1.0)
    set(GIO_INCLUDE_DIRS ${GST_ARCH_DIR}/lib/gio-unix-2.0/)

    add_subdirectory(gstreamer_android)
    add_subdirectory(src)
endif ()
