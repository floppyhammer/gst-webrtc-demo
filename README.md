# GStreamer WebRTC Client

This is a test client on Linux. See the compatible [server](https://github.com/floppyhammer/gst-webrtc-server).

## Build Dependencies

```sh
sudo apt install libeigen3-dev \
    gstreamer1.0-plugins-good \
    gstreamer1.0-nice \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-plugins-bad \
    libgstreamer-plugins-bad1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer1.0-dev \
    glslang-tools \
    libbsd-dev \
    libgl1-mesa-dev \
    libsystemd-dev \
    libvulkan-dev \
    libx11-dev \
    libx11-xcb-dev \
    libxxf86vm-dev \
    pkg-config
```

Plus some version of libsoup. Run the following, and see if it shows
`libsoup-3.0-0` at the end:

```sh
apt info gstreamer1.0-plugins-good | grep libsoup-3.0
```

- If so, you can use libsoup3:
    - `libsoup-3.0-dev`
- Otherwise, you must use libsoup 2:
    - `libsoup2.4-dev`
    - In this case, you must also pass `-DUSE_LIBSOUP2=ON` to CMake.

Best to only have one of the two libsoup dev packages installed at a time.
