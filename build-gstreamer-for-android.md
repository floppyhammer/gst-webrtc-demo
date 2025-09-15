# Download the sources

```bash
git clone https://gitlab.freedesktop.org/gstreamer/cerbero
cd cerbero
```

# Bootstrap for Android Arm64 on Linux

```bash
./cerbero-uninstalled -c config/cross-android-arm64.cbc bootstrap
```

# Build everything and package for Android Arm64

```bash
./cerbero-uninstalled -c config/cross-android-arm64.cbc package gstreamer-1.0
```

# Set custom remote and branch for all gstreamer recipes

Create `localconf.cbc` containing

```
recipes_remotes = {'gstreamer-1.0': {'custom-remote': 'file:///home/<YOUR_REPO_PATH>/gstreamer'}}
recipes_commits = {'gstreamer-1.0': 'custom-remote/<YOUR_GIT_BRANCH>'}
```

```bash
./cerbero-uninstalled -c localconf.cbc -c config/cross-android-arm64.cbc package gstreamer-1.0
```
