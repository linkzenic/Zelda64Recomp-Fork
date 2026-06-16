# Zelda64 Recompiled Android

This directory contains the first Android packaging slice for Zelda64 Recompiled.

The default Gradle build is an SDL lifecycle probe:

```sh
gradle --no-daemon -p android :app:assembleDebug
```

The probe builds Android `libmain.so`, loads it through SDLActivity, initializes SDL, creates a window for a few seconds, and exits. Build the full native runtime later with:

```sh
tools/ci/prepare_android_generated_sources.sh
gradle --no-daemon -p android :app:assembleDebug -PzeldaProbe=false
```

Required local inputs:

- `ANDROID_HOME`, defaulting to `$HOME/Library/Android/sdk`
- `ANDROID_NDK_HOME`, defaulting to `$ANDROID_HOME/ndk/26.0.10792818`
- `ZELDA_ANDROID_SDL2_PREFIX`, defaulting to `$HOME/Android/prefixes/SDL2-2.32.10-android-arm64`
- `ZELDA_ANDROID_FREETYPE_PREFIX`, required only for full runtime builds

To build the native dependency prefixes:

```sh
ANDROID_HOME="$HOME/Library/Android/sdk" tools/ci/setup_android_deps.sh
```

Runtime builds copy public program assets into APK assets and use app-private storage. `ZeldaSDLActivity` sets:

- `APP_PROGRAM_PATH` to `<app files>/program`
- `APP_FOLDER_PATH` to `<app files>/data`

Do not commit ROMs, decompressed ROMs, generated recompilation sources from private inputs, APKs, AABs, Gradle output, CMake output, app-private saves, or imported user mods.
