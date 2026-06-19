#!/usr/bin/env bash
set -euo pipefail

APK="${1:?usage: verify_android_apk.sh path/to.apk [runtime|probe]}"
MODE="${2:-runtime}"

if [[ ! -f "$APK" ]]; then
  echo "APK not found: $APK" >&2
  exit 2
fi

python3 - "$APK" "$MODE" <<'PY'
import hashlib
import struct
import sys
import zipfile

apk, mode = sys.argv[1:3]
forbidden = (
    'baserom',
    'decompressed',
    '.z64',
    '.n64',
    '.v64',
    'mm.us',
    'dev-roms/',
    'RecompiledFuncs',
    'RecompiledPatches',
)
with zipfile.ZipFile(apk) as zf:
    names = zf.namelist()
    libs = sorted(n for n in names if n.startswith('lib/'))
    shared_objects = sorted(n for n in names if n.endswith('.so'))
    bad = [n for n in names if any(token in n for token in forbidden)]

if bad:
    print('Forbidden ROM/generated artifact entries found in APK:', file=sys.stderr)
    for name in bad:
        print(f'  {name}', file=sys.stderr)
    sys.exit(1)

required_lib = 'lib/arm64-v8a/libmain.so'
if required_lib not in libs:
    print(f'Missing required native library: {required_lib}', file=sys.stderr)
    sys.exit(1)

if 'lib/arm64-v8a/libSDL2.so' not in libs:
    print('Missing required native library: lib/arm64-v8a/libSDL2.so', file=sys.stderr)
    sys.exit(1)

def load_segment_alignments(blob, name):
    if len(blob) < 64 or blob[:4] != b'\x7fELF':
        raise ValueError(f'{name} is not an ELF file')
    if blob[4] != 2 or blob[5] != 1:
        raise ValueError(f'{name} is not a little-endian ELF64 file')

    e_phoff = struct.unpack_from('<Q', blob, 32)[0]
    e_phentsize = struct.unpack_from('<H', blob, 54)[0]
    e_phnum = struct.unpack_from('<H', blob, 56)[0]
    if e_phentsize < 56:
        raise ValueError(f'{name} has an invalid program header size')

    alignments = []
    for index in range(e_phnum):
        offset = e_phoff + index * e_phentsize
        if offset + 56 > len(blob):
            raise ValueError(f'{name} has a truncated program header table')
        p_type = struct.unpack_from('<I', blob, offset)[0]
        if p_type == 1:
            p_align = struct.unpack_from('<Q', blob, offset + 48)[0]
            alignments.append(p_align)
    return alignments

bad_alignment = []
with zipfile.ZipFile(apk) as zf:
    for name in shared_objects:
        alignments = load_segment_alignments(zf.read(name), name)
        bad_loads = [align for align in alignments if align < 16384]
        if bad_loads:
            formatted = ', '.join(f'0x{align:x}' for align in bad_loads)
            bad_alignment.append((name, formatted))

if bad_alignment:
    print('Shared objects with PT_LOAD alignment below 16 KiB:', file=sys.stderr)
    for name, alignments in bad_alignment:
        print(f'  {name}: {alignments}', file=sys.stderr)
    sys.exit(1)

with open(apk, 'rb') as f:
    digest = hashlib.sha256(f.read()).hexdigest()

print(f'APK: {apk}')
print(f'Mode: {mode}')
print(f'SHA256: {digest}')
print('Shared objects:')
for so in shared_objects:
    print(f'  {so}')
print('Shared object alignment: 16 KiB compatible')
print('ROM/generated artifact scan: clean')
PY
