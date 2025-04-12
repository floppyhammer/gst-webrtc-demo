if (UNIX)
    option(USE_LIBSOUP2 "Use libsoup2.4 instead of libsoup3.0" OFF)

    find_package(PkgConfig REQUIRED)

    pkg_check_modules(GLIB REQUIRED glib-2.0)
    pkg_check_modules(GST REQUIRED gstreamer-1.0)
    pkg_check_modules(GST_SDP REQUIRED gstreamer-sdp-1.0)
    pkg_check_modules(GST_WEBRTC REQUIRED gstreamer-webrtc-1.0)
    pkg_check_modules(GST REQUIRED gstreamer-plugins-base-1.0)
    pkg_check_modules(GST REQUIRED gstreamer-plugins-bad-1.0)

    if (USE_LIBSOUP2)
        pkg_check_modules(LIBSOUP REQUIRED libsoup-2.4)
    else ()
        pkg_check_modules(LIBSOUP REQUIRED libsoup-3.0)
    endif ()

    pkg_check_modules(JSONGLIB REQUIRED json-glib-1.0)
    pkg_check_modules(GIO REQUIRED gio-2.0)
else ()
    # To ensure dll linking, add "C:\Program Files\gstreamer\1.0\msvc_x86_64\bin" to your PATH

    set(GST_ROOT "C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64")
    set(GST_LIB_ROOT "${GST_ROOT}\\lib")

    set(GST_INCLUDE_DIRS "${GST_ROOT}\\include\\gstreamer-1.0")
    set(GST_LIBRARIES "${GST_LIB_ROOT}\\gstreamer-1.0.lib")
    set(GST_SDP_LIBRARIES "${GST_LIB_ROOT}\\gstsdp-1.0.lib")
    set(GST_WEBRTC_LIBRARIES "${GST_LIB_ROOT}\\gstwebrtc-1.0.lib")

    set(GLIB_INCLUDE_DIRS "${GST_ROOT}\\include\\glib-2.0" "${GST_LIB_ROOT}\\glib-2.0\\include")
    set(GLIB_LIBRARIES "${GST_LIB_ROOT}\\gobject-2.0.lib" "${GST_LIB_ROOT}\\glib-2.0.lib")

    set(JSONGLIB_INCLUDE_DIRS "${GST_ROOT}\\include\\json-glib-1.0")
    set(JSONGLIB_LIBRARIES "${GST_LIB_ROOT}\\json-glib-1.0.lib")

    set(LIBSOUP_INCLUDE_DIRS "${GST_ROOT}\\include\\libsoup-3.0")
    set(LIBSOUP_LIBRARIES "${GST_LIB_ROOT}\\soup-3.0.lib")

    set(GIO_INCLUDE_DIRS "${GST_ROOT}\\include\\gio-win32-2.0")
    set(GIO_LIBRARIES "${GST_LIB_ROOT}\\gio-2.0.lib")
endif ()

add_library(webrtc_demo_common server/server_pipeline.c server/signaling_server.c client/client_pipeline.c)

target_link_libraries(
        webrtc_demo_common
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
        webrtc_demo_common
        PRIVATE
        ${GST_INCLUDE_DIRS}
        ${LIBSOUP_INCLUDE_DIRS}
        ${JSONGLIB_INCLUDE_DIRS}
        ${GIO_INCLUDE_DIRS}
        PUBLIC
        ${GLIB_INCLUDE_DIRS}
)
