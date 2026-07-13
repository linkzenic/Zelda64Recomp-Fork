#!/usr/bin/env bash
set -euo pipefail

N64RECOMP_SOURCE_DIR="${ZELDA_N64RECOMP_SOURCE_DIR:-lib/N64ModernRuntime/N64Recomp}"
N64RECOMP_BUILD_DIR="${ZELDA_N64RECOMP_BUILD_DIR:-${RUNNER_TEMP:-${TMPDIR:-/tmp}}/zelda64-n64recomp-build}"
FILE_TO_C="${ZELDA_FILE_TO_C:-${RUNNER_TEMP:-${TMPDIR:-/tmp}}/zelda64-file-to-c}"
DECOMPRESSED_ROM="${ZELDA_DECOMPRESSED_ROM:-mm.us.rev1.rom_uncompressed.z64}"

if [[ "${OS:-}" == "Windows_NT" ]]; then
  EXE_SUFFIX=".exe"
else
  EXE_SUFFIX=""
fi

have_runtime_sources() {
  (compgen -G 'RecompiledFuncs/*.c' >/dev/null || compgen -G 'RecompiledFuncs/*.cpp' >/dev/null) && \
  [[ -f rsp/aspMain.cpp ]] && \
  [[ -f rsp/njpgdspMain.cpp ]] && \
  [[ -f RecompiledPatches/patches.c ]] && \
  [[ -f RecompiledPatches/patches_bin.c ]]
}

build_recomp_tools() {
  if [[ -x "./N64Recomp${EXE_SUFFIX}" && -x "./RSPRecomp${EXE_SUFFIX}" ]]; then
    echo "N64Recomp and RSPRecomp are already present."
    return
  fi

  if [[ ! -d "$N64RECOMP_SOURCE_DIR" ]]; then
    echo "N64Recomp source directory is missing: $N64RECOMP_SOURCE_DIR" >&2
    exit 2
  fi

  echo "Building N64Recomp/RSPRecomp from $N64RECOMP_SOURCE_DIR."
  cmake \
    -S "$N64RECOMP_SOURCE_DIR" \
    -B "$N64RECOMP_BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_MAKE_PROGRAM=ninja
  cmake --build "$N64RECOMP_BUILD_DIR" --config Release --target N64RecompCLI RSPRecomp -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 2)"

  local n64recomp_path
  local rsprecomp_path
  n64recomp_path="$(find "$N64RECOMP_BUILD_DIR" -type f -name "N64Recomp${EXE_SUFFIX}" -print -quit)"
  rsprecomp_path="$(find "$N64RECOMP_BUILD_DIR" -type f -name "RSPRecomp${EXE_SUFFIX}" -print -quit)"
  if [[ -z "$n64recomp_path" || -z "$rsprecomp_path" ]]; then
    echo "Failed to find built N64Recomp/RSPRecomp binaries in $N64RECOMP_BUILD_DIR" >&2
    exit 2
  fi

  cp "$n64recomp_path" "./N64Recomp${EXE_SUFFIX}"
  cp "$rsprecomp_path" "./RSPRecomp${EXE_SUFFIX}"
  chmod +x "./N64Recomp${EXE_SUFFIX}" "./RSPRecomp${EXE_SUFFIX}"
}

build_file_to_c() {
  if [[ -x "$FILE_TO_C" ]]; then
    return
  fi

  local source_path="lib/rt64/src/tools/file_to_c/file_to_c.cpp"
  if [[ ! -f "$source_path" ]]; then
    echo "file_to_c source is missing: $source_path" >&2
    exit 2
  fi

  echo "Building host file_to_c helper."
  "${ZELDA_FILE_TO_C_CXX:-${CXX:-c++}}" -std=c++17 -O2 "$source_path" -o "$FILE_TO_C"
}

refresh_patch_sources() {
  build_recomp_tools
  build_file_to_c

  make -C patches clean
  CC="${PATCHES_C_COMPILER:-clang}" LD="${PATCHES_LD:-ld.lld}" make -C patches ZELDA_ANDROID_BUILTIN_DPAD=1
  "./N64Recomp${EXE_SUFFIX}" patches.toml
  "$FILE_TO_C" patches/patches.bin mm_patches_bin RecompiledPatches/patches_bin.c RecompiledPatches/patches_bin.h
}

if have_runtime_sources; then
  if [[ "${OS:-}" == "Windows_NT" && "${ZELDA_FORCE_PATCH_REFRESH:-0}" != "1" ]]; then
    echo "Generated runtime sources are already present; using restored patch artifacts on Windows."
    echo "Set ZELDA_FORCE_PATCH_REFRESH=1 to force local patch regeneration."
    exit 0
  fi

  echo "Generated runtime sources are already present; refreshing desktop patch artifacts."
  refresh_patch_sources
  exit 0
fi

if [[ ! -f "$DECOMPRESSED_ROM" ]]; then
  cat >&2 <<MSG
Generated Zelda64 runtime sources are missing, and the decompressed ROM input was not found:
  $DECOMPRESSED_ROM

Place the decompressed NTSC-U Majora's Mask ROM at the repository root as:
  mm.us.rev1.rom_uncompressed.z64

or set:
  ZELDA_DECOMPRESSED_ROM=/path/to/mm.us.rev1.rom_uncompressed.z64

Do not commit ROM files or generated private-source artifacts.
MSG
  exit 2
fi

if [[ "$DECOMPRESSED_ROM" != "mm.us.rev1.rom_uncompressed.z64" ]]; then
  cp "$DECOMPRESSED_ROM" mm.us.rev1.rom_uncompressed.z64
fi

build_recomp_tools
build_file_to_c

"./N64Recomp${EXE_SUFFIX}" us.rev1.toml
"./RSPRecomp${EXE_SUFFIX}" aspMain.us.rev1.toml
"./RSPRecomp${EXE_SUFFIX}" njpgdspMain.us.rev1.toml
refresh_patch_sources

if ! have_runtime_sources; then
  echo "Runtime source generation completed, but required generated files are still missing." >&2
  exit 2
fi

echo "Generated Zelda64 runtime sources are ready."
