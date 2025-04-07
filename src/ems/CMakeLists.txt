add_subdirectory(include)

add_library(gws_gst STATIC ems_gstreamer_pipeline.c ems_signaling_server.c ems_callbacks.cpp)

target_link_libraries(
        gws_gst
        PRIVATE
        ems_build_defines
        gstreamer_android
        ${LIBSOUP_LIBRARIES}
)

target_include_directories(
        gws_gst
        PRIVATE
        ${GLIB_INCLUDE_DIRS}
        ${GST_INCLUDE_DIRS}
        ${LIBSOUP_INCLUDE_DIRS}
        ${JSONGLIB_INCLUDE_DIRS}
        ${GIO_INCLUDE_DIRS}
)
target_compile_definitions(gws_gst PUBLIC G_LOG_DOMAIN="GstWebrtcServer")
