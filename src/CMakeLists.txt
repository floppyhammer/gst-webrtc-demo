add_library(webrtc_server_common server/gstreamer_pipeline.c server/signaling_server.c)

target_link_libraries(
        webrtc_server_common
        PRIVATE
        ${GST_LIBRARIES}
        ${GST_SDP_LIBRARIES}
        ${GST_WEBRTC_LIBRARIES}
        ${GLIB_LIBRARIES}
        ${LIBSOUP_LIBRARIES}
        ${JSONGLIB_LIBRARIES}
        ${GIO_LIBRARIES}
)

target_include_directories(
        webrtc_server_common
        PRIVATE
        ${GST_INCLUDE_DIRS}
        ${LIBSOUP_INCLUDE_DIRS}
        ${JSONGLIB_INCLUDE_DIRS}
        ${GIO_INCLUDE_DIRS}
        PUBLIC
        ${GLIB_INCLUDE_DIRS}
)
