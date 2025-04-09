option(USE_LIBSOUP2 "Use libsoup2.4 instead of libsoup3.0" OFF)

find_package(PkgConfig REQUIRED)

if (UNIX)
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
    set(GST_ROOT "C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64")
    set(GST_LIB_ROOT "${GST_ROOT}\\lib")

    set(GST_INCLUDE_DIRS "${GST_ROOT}\\include\\gstreamer-1.0")
    set(GST_LIBRARIES "${GST_LIB_ROOT}\\libgstreamer-1.0.dll.a")

    set(GST_SDP_LIBRARIES "${GST_LIB_ROOT}\\libgstsdp-1.0.dll.a")

    set(GST_WEBRTC_LIBRARIES "${GST_LIB_ROOT}\\libgstwebrtc-1.0.dll.a")

    set(GLIB_INCLUDE_DIRS "${GST_ROOT}\\include\\glib-2.0" "${GST_LIB_ROOT}\\glib-2.0\\include")
    set(GLIB_LIBRARIES "${GST_LIB_ROOT}\\libgobject-2.0.dll.a" "${GST_LIB_ROOT}\\libglib-2.0.dll.a")

    set(JSONGLIB_INCLUDE_DIRS "${GST_ROOT}\\include\\json-glib-1.0")
    set(JSONGLIB_LIBRARIES "${GST_LIB_ROOT}\\libjson-glib-1.0.dll.a")

    set(LIBSOUP_INCLUDE_DIRS "${GST_ROOT}\\include\\libsoup-3.0")
    set(LIBSOUP_LIBRARIES "${GST_LIB_ROOT}\\libsoup-3.0.dll.a")

    set(GIO_INCLUDE_DIRS "${GST_ROOT}\\include\\gio-win32-2.0")
    set(GIO_LIBRARIES "${GST_LIB_ROOT}\\libgio-2.0.dll.a")
endif ()

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
