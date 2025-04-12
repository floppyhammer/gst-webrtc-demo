add_subdirectory(../../src webrtc_demo_common)

# Build native_app_glue as a static lib
add_library(
        native_app_glue STATIC
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

# Now build app's shared lib
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror")

add_library(gst_webrtc_server_android SHARED main.cpp)

target_include_directories(gst_webrtc_server_android PRIVATE ${GLIB_INCLUDE_DIRS} ${GST_INCLUDE_DIRS})

# Add lib dependencies
target_link_libraries(
        gst_webrtc_server_android
        PRIVATE
        native_app_glue
        Vulkan::Vulkan
        EGL::EGL
        OpenGLES::OpenGLESv3
        ${ANDROID_LIBRARY}
        ${ANDROID_LOG_LIBRARY}
        webrtc_demo_common
        gstreamer_android
)

# Export ANativeActivity_onCreate()
set_property(
        TARGET gst_webrtc_server_android
        APPEND_STRING
        PROPERTY LINK_FLAGS " -u ANativeActivity_onCreate"
)
