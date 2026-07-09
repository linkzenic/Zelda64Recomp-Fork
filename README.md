# Zelda64Recomp Fork

This repository is Linkzenic's desktop fork of [Zelda64 Recompiled](https://github.com/Zelda64Recomp/Zelda64Recomp), focused on macOS, Linux, and Windows builds.

The goal is to keep a clean desktop version of the fork separate from the Android port while preserving the quality-of-life work we have been building: updated menus, built-in editor features, save improvements, and desktop release packaging.

## Downloads

Desktop releases will be published here:

https://github.com/linkzenic/Zelda64Recomp-Fork/releases

Release packages are planned for:

- macOS
- Linux
- Windows

Game assets are not included. You must provide your own supported Majora's Mask ROM when the app asks for it.

## Requirements

- A legally obtained supported Majora's Mask ROM
- A Vulkan-capable GPU and driver
- A modern desktop OS:
  - macOS
  - Linux
  - Windows

This project is not an emulator and does not include copyrighted game assets.

## What This Fork Adds

- Desktop release packaging for macOS, Linux, and Windows
- Built-in save editor work from the Linkzenic fork
- N64 Mode for a stripped-down launch path
- Desktop cleanup for duplicate legacy mods that are now handled by the app
- GitHub Actions workflow foundation for desktop release artifacts

## Mods

Desktop mods should use desktop-compatible packages for your platform.

This fork ignores older external copies of the D-Pad Special Items and Linkzenic Save Editor mods when they would conflict with features already built into the app. This prevents duplicate mod patches from breaking startup while still allowing the app to use the built-in versions.

If a mod includes native code, it must be built for your platform:

- Windows mods need Windows binaries.
- macOS mods need macOS binaries.
- Linux mods need Linux binaries.

Native plugin binaries are not interchangeable across operating systems.

## N64 Mode

N64 Mode is intended as a simpler launch path:

- no optional mods
- no extra fork features that change gameplay behavior
- closer to the raw N64 experience

Use this if you want a minimal run or need to troubleshoot a mod or configuration issue.

## Building

This repo depends on generated Zelda64 Recompiled source files that are not committed to Git. CI restores those generated sources from a private generated-source bundle.

For local development, you need either:

- the generated source folders already present, or
- a decompressed supported ROM at the repository root named `mm.us.rev1.rom_uncompressed.z64`

Then prepare generated desktop sources with:

```sh
tools/ci/prepare_desktop_generated_sources.sh
```

After that, configure and build with CMake for your platform.

## CI Releases

The desktop workflow is:

```text
.github/workflows/desktop-release.yml
```

It builds:

- `Zelda64Recompiled-macOS.zip`
- `Zelda64Recompiled-Linux.tar.gz`
- `Zelda64Recompiled-Windows.zip`

The workflow requires generated-source bundle secrets to be configured on this repository before GitHub Actions can build from a fresh checkout.

## Upstream

This fork is based on Zelda64 Recompiled:

https://github.com/Zelda64Recomp/Zelda64Recomp

Zelda64 Recompiled uses [N64: Recompiled](https://github.com/Mr-Wiseguy/N64Recomp), with [RT64](https://github.com/rt64/rt64) as the rendering engine.

For upstream documentation, modding details, and original desktop releases, see the upstream repository.

## Credits

- Zelda64 Recompiled contributors
- N64: Recompiled contributors
- RT64 contributors
- Zelda64 Recompiled mod authors
- Linkzenic fork contributors and testers
