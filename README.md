# GStreamer WebRTC Demo

This is a demo showing how to use GStreamer WebRTC on Android/Linux/Windows.

## Get Dependencies (Linux)

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
    pkg-config \
    libjson-glib-dev \
    gstreamer1.0-libav \
    libglib2.0-dev
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

## Get Dependencies (Android)

You can get an upstream build of GStreamer by running `./download_gst.sh` which
will extract it to `deps/gstreamer_android`. This is the default search
location. If you are intending to use a different build (such as a local build
from Cerbero), you will need to set one of these in `local.properties`:

- `gstreamerArchDir=/home/user/src/cerbero/build/dist/android_arm64` - if you've
  done a single-arch cerbero build in `~/src/cerbero`
- `gstreamerBaseDir=/home/user/gstreamer_android_universal` - if you have a
  universal (all architectures) build like the one downloaded by the script.
