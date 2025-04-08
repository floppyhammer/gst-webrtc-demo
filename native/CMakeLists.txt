
option(USE_LIBSOUP2 "Use libsoup2.4 instead of libsoup3.0" OFF)
set(USE_LIBSOUP2 ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GLIB REQUIRED glib-2.0)
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

# Default to PIC code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_executable(webrtc_server_native main.c)

add_subdirectory(../src common)

target_link_libraries(
        webrtc_server_native
        PRIVATE
        webrtc_server_common
)

target_include_directories(
        webrtc_server_native
        PRIVATE
        webrtc_server_common
)
