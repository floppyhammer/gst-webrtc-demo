add_executable(webrtc_client_native main.c)

target_link_libraries(
        webrtc_client_native
        PRIVATE
        webrtc_demo_common
)

target_include_directories(
        webrtc_client_native
        PRIVATE
        webrtc_demo_common
)
