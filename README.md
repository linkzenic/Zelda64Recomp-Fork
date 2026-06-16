# Zelda64 Recompiled Android

This fork packages [Zelda64 Recompiled](https://github.com/Zelda64Recomp/Zelda64Recomp) for Android. It is an early beta Android port of Majora's Mask Recompiled with RT64 rendering, SDL input, public device storage, touch controls, Android motion controls, and Android-native mod support.

## Download

Get the current Android beta from this fork's releases:

https://github.com/linkzenic/Zelda64Recomp-Android/releases

The Android release APK does not contain game assets. You must provide your own supported Majora's Mask ROM when the app asks for it.

## Android Requirements

- Android 7.0 or newer
- ARM64 device
- Vulkan-capable GPU and working Vulkan driver
- Enough storage for the app data folder, saves, mods, and texture packs

This port has been tested primarily on Snapdragon/Adreno handhelds. If graphics are incorrect, crashes happen at game start, or Vulkan device creation fails, your device may need a newer or different Vulkan driver.

## What This Android Port Adds

- Android APK packaging for Zelda64 Recompiled
- Fullscreen landscape-only gameplay
- Public app data in `/sdcard/Zelda64`
- Android file picker support for ROM and mod installation
- On-screen controller overlay
- A Controls menu option to disable touch controls completely
- Android accelerometer/gyro input for gyro aim
- Physical controller support through SDL
- Android-native mod `.so` loading support
- Android Vulkan fixes needed by RT64
- Android HUD placement fix for expanded 16:9 HUD
- Android app icon and launcher metadata

## Storage Layout

The app creates and uses:

```text
/sdcard/Zelda64/
  mods/
  saves/
  config/
```

This is intentional. Android scoped storage makes app-private files difficult for users to manage, so this fork keeps the important user-facing folders in a normal device storage location. That makes it easier to add mods, copy saves, back up configs, and troubleshoot files without root access.

On first launch, Android may ask for storage access. Grant it so the app can create and manage `/sdcard/Zelda64`.

## Mods

Mods can be installed from the app's Mods menu. Download manual mod packages, then choose them with the Android file picker.

Android cannot load desktop Linux, Windows, or macOS plugin binaries. Mods that include native code need Android ARM64 `.so` files built specifically for this port. If a mod package overwrites an Android `.so` with a desktop build, the mod will fail to load or the app may crash.

### Bundled Android Mods

This fork includes a small set of Android-ready mods by default:

- Linkzenic Save Editor
- ProxyMM KV
- yazmt Majora's Mask Player Model Manager fsmodels plugin

ProxyMM KV and the player model manager are bundled as complete mod packages, not only as `.so` files. They need Android-native shared libraries, and users would otherwise be likely to download desktop packages whose native binaries are incompatible with Android. Bundling the full Android-ready packages keeps the initial mod experience predictable and prevents Linux `.so` files from replacing the Android builds.

The 2Ship Save Importer and audio API example mods are not bundled in this beta.

## ROMs And Assets

This project is not an emulator and does not include copyrighted game assets. You need a legally obtained supported Majora's Mask ROM. The app will ask you to select it before starting the game.

## Known Beta Notes

- This is an early Android beta and may still have device-specific Vulkan or driver issues.
- The current beta APK is debug-signed.
- External mod compatibility depends on whether the mod is pure `.nrm` content or includes native code that must be rebuilt for Android.
- If a native mod causes crashes, remove it from `/sdcard/Zelda64/mods` and try launching again.

## Building Android

Install Android SDK/NDK and the dependency prefixes, then build from the repository root:

```sh
ANDROID_HOME="$HOME/Library/Android/sdk" tools/ci/setup_android_deps.sh
tools/ci/prepare_android_generated_sources.sh
gradle --no-daemon -p android :app:assembleDebug -PzeldaProbe=false
```

Useful inputs:

- `ANDROID_HOME`, defaulting to `$HOME/Library/Android/sdk`
- `ANDROID_NDK_HOME`, defaulting to `$ANDROID_HOME/ndk/26.0.10792818`
- `ZELDA_ANDROID_SDL2_PREFIX`, defaulting to `$HOME/Android/prefixes/SDL2-2.32.10-android-arm64`
- `ZELDA_ANDROID_FREETYPE_PREFIX`, required for full runtime builds

Do not commit ROMs, decompressed ROMs, generated private game sources, APKs, AABs, Gradle output, CMake output, saves, configs, or user-installed mods.

## Upstream Project

This fork is based on Zelda64 Recompiled:

https://github.com/Zelda64Recomp/Zelda64Recomp

Zelda64 Recompiled uses [N64: Recompiled](https://github.com/Mr-Wiseguy/N64Recomp) to statically recompile Majora's Mask into a native port, with [RT64](https://github.com/rt64/rt64) as the rendering engine.

For general Zelda64 Recompiled information, modding documentation, and desktop releases, see the upstream repository.

## Credits

- Zelda64 Recompiled contributors
- N64: Recompiled contributors
- RT64 contributors
- BanjoRecomp Android port work, which helped prove the Android path
- The Zelda64 Recompiled mod authors whose Android-ready packages are bundled in this fork
