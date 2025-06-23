## Build

```bash
sudo apt install libcairo2-dev
meson setup build
cd build
meson configure --prefix=/home/zzz/gstreamer
meson configure -Dtests=disabled
ninja -j20
ninja install
```

## Set environment

```bash
meson devenv
```

## Check

```bash
gst-inspect-1.0 --version
```
