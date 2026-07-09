#!/usr/bin/env python3
import os
import pathlib
import sys
import urllib.request
import zipfile


def main() -> int:
    url = os.environ.get("ZELDA_GENERATED_SOURCES_URL") or os.environ.get("ZELDA_ANDROID_GENERATED_SOURCES_URL")
    token = os.environ.get("ZELDA_GENERATED_SOURCES_TOKEN") or os.environ.get("ZELDA_ANDROID_GENERATED_SOURCES_TOKEN")

    if not url:
        print("No generated source bundle URL configured; checking repository contents only.")
        return 0

    root = pathlib.Path.cwd().resolve()
    bundle_path = pathlib.Path(os.environ.get("RUNNER_TEMP", os.environ.get("TMPDIR", "/tmp"))) / "zelda64-generated-sources.zip"

    request = urllib.request.Request(url)
    if token:
        request.add_header("Authorization", f"Bearer {token}")
        request.add_header("Accept", "application/octet-stream")

    print(f"Downloading generated source bundle to {bundle_path}")
    with urllib.request.urlopen(request, timeout=120) as response:
        bundle_path.write_bytes(response.read())

    allowed_roots = {
        "RecompiledFuncs",
        "RecompiledPatches",
        "rsp",
    }

    with zipfile.ZipFile(bundle_path) as archive:
        for info in archive.infolist():
            name = pathlib.PurePosixPath(info.filename)
            if info.is_dir():
                continue
            if name.is_absolute() or ".." in name.parts:
                raise SystemExit(f"Refusing unsafe archive path: {info.filename}")
            if not name.parts or name.parts[0] not in allowed_roots:
                raise SystemExit(f"Unexpected archive path: {info.filename}")

        archive.extractall(root)

    print("Generated source bundle restored.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
