# GStreamer WebRTC Demo

## Build dep: GStreamer

You can get an upstream build of GStreamer by running `./download_gst.sh` which
will extract it to `deps/gstreamer_android`. This is the default search
location. If you are intending to use a different build (such as a local build
from Cerbero), you will need to set one of these in `local.properties`:

- `gstreamerArchDir=/home/user/src/cerbero/build/dist/android_arm64` - if you've
  done a single-arch cerbero build in `~/src/cerbero`
- `gstreamerBaseDir=/home/user/gstreamer_android_universal` - if you have a
  universal (all architectures) build like the one downloaded by the script.
