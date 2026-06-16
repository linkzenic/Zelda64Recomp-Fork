# Android Build Notes

This directory contains the Android Gradle project for the Zelda64 Recompiled Android port.

## Build

From the repository root:

```sh
ANDROID_HOME="$HOME/Library/Android/sdk" tools/ci/setup_android_deps.sh
tools/ci/prepare_android_generated_sources.sh
gradle --no-daemon -p android :app:assembleDebug -PzeldaProbe=false
```

The default probe target can still be built for quick SDL lifecycle checks:

```sh
gradle --no-daemon -p android :app:assembleDebug
```

## Required Inputs

- `ANDROID_HOME`, defaulting to `$HOME/Library/Android/sdk`
- `ANDROID_NDK_HOME`, defaulting to `$ANDROID_HOME/ndk/26.0.10792818`
- `ZELDA_ANDROID_SDL2_PREFIX`, defaulting to `$HOME/Android/prefixes/SDL2-2.32.10-android-arm64`
- `ZELDA_ANDROID_FREETYPE_PREFIX`, required for full runtime builds

## Runtime Paths

`ZeldaSDLActivity` extracts program assets from the APK and sets:

- `APP_PROGRAM_PATH` to `<app files>/program`
- `APP_FOLDER_PATH` to `/sdcard/Zelda64` when storage access is granted
- `APP_NATIVE_LIBS_PATH` to `<app code cache>/native_mods`
- `APP_ANDROID_VERSION_NAME` to the APK `versionName`

If storage access is not granted yet, the app temporarily falls back to app-private data storage until the user grants permission.

## Bundled Android Mods

Android-native mod packages live under:

```text
android/app/src/main/assets/bundled_mods/
```

These are copied into `/sdcard/Zelda64/mods` on launch. The current bundled packages are:

- `ProxyMM_KV.nrm`
- `ProxyRecomp_KV005.so`
- `yazmt_mm_playermodelmanager_fsmodels.nrm`
- `yazmt_mm_playermodelmanager_fsmodels_extlib.so`

They are bundled as complete Android-ready mod pairs because desktop native binaries are not compatible with Android. Keeping the packages in the APK prevents users from accidentally replacing the Android `.so` files with Linux or desktop versions from normal mod downloads.

## Do Not Commit

Do not commit ROMs, decompressed ROMs, generated recompilation sources from private inputs, APKs, AABs, Gradle output, CMake output, app-private saves, configs, or imported user mods.
