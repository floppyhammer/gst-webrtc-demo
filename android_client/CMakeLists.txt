cmake_minimum_required(VERSION 3.10.2)

project(gst_webrtc_client VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Vulkan REQUIRED)
find_package(OpenXR REQUIRED)
find_package(EGL REQUIRED)
find_package(OpenGLES REQUIRED COMPONENTS V3)

# Default to PIC code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

### Set configuration options for Monado

# Helper to obey CMP0077 and work around warnings.
function(setForce NAME VALUE)
    set(${NAME}
            ${VALUE}
            CACHE INTERNAL "" FORCE
    )
endfunction()

setForce(BUILD_TESTING OFF)
setForce(BUILD_DOC OFF)

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

    option(EMC_OLDER_GST "Some older versions of gstreamer use libsoup2.4 instead of libsoup3.0" OFF)
    set(EMC_OLDER_GST OFF)

    include(libraries_static.cmake)

    set(GST_INCLUDE_DIRS ${GST_ARCH_DIR}/include/gstreamer-1.0/
            ${GST_ARCH_DIR}/lib/gstreamer-1.0/include/
    )
    set(GLIB_INCLUDE_DIRS ${GST_ARCH_DIR}/include/glib-2.0
            ${GST_ARCH_DIR}/lib/glib-2.0/include/
    )
    if (EMC_OLDER_GST)
        set(LIBSOUP_INCLUDE_DIRS ${GST_ARCH_DIR}/include/libsoup-2.4/)
    else ()
        set(LIBSOUP_INCLUDE_DIRS ${GST_ARCH_DIR}/include/libsoup-3.0/)
    endif ()
    set(JSONGLIB_INCLUDE_DIRS ${GST_ARCH_DIR}/include/json-glib-1.0)
    set(GIO_INCLUDE_DIRS ${GST_ARCH_DIR}/include/glib-2.0/ ${GST_ARCH_DIR}/lib/gio-unix-2.0/)

    add_subdirectory(gstreamer_android)
    add_subdirectory(src)
endif ()
