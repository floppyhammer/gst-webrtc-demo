add_executable(webrtc_server_native main.c)

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
