cmake_minimum_required(VERSION 3.10.2)

project(gst_webrtc_demo LANGUAGES C VERSION 0.0.1)

# Default to PIC code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(proto)
add_subdirectory(src)
add_subdirectory(native_server)
add_subdirectory(native_client)
