# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 KaguyaRing
"""Validate native packages, then publish a diagnostic-only GitHub prerelease."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import zipfile

HOSTS = ("macos-15-intel", "macos-26-intel")
MAX_ARCHIVE_BYTES = 512 * 1024 * 1024
MAX_METADATA_BYTES = 8192
MAX_ZIP_ENTRIES = 50000
MAX_UNPACKED_BYTES = 1024 * 1024 * 1024
COMMAND_TIMEOUT = 120


def validate_assets(directory: pathlib.Path, tag: str, sha: str) -> list[pathlib.Path]:
    if tag != "v0.1.0" and not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+-[0-9a-f]{7}", tag):
        raise ValueError("Invalid development tag")
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise ValueError("Invalid source SHA")
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError("Expected a real asset directory")
    expected = {f"MetalVK-{tag}-development-{host}.zip" for host in HOSTS}
    expected |= {name + ".sha256" for name in tuple(expected)}
    if {path.name for path in directory.iterdir()} != expected:
        raise ValueError("Expected exactly two native archives and their checksums")
    assets = []
    for host in HOSTS:
        archive = directory / f"MetalVK-{tag}-development-{host}.zip"
        checksum = directory / (archive.name + ".sha256")
        for path, limit in ((archive, MAX_ARCHIVE_BYTES), (checksum, 256)):
            if path.is_symlink() or not path.is_file() or not 0 < path.stat().st_size <= limit:
                raise ValueError("Missing, oversized or linked release asset")
        with archive.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if checksum.read_text(encoding="ascii").strip() != f"{digest}  {archive.name}":
            raise ValueError("Release checksum mismatch")
        with zipfile.ZipFile(archive) as package:
            entries = package.infolist()
            if len(entries) > MAX_ZIP_ENTRIES:
                raise ValueError("Too many archive entries")
            if sum(entry.file_size for entry in entries) > MAX_UNPACKED_BYTES:
                raise ValueError("Oversized unpacked archive")
            names = [entry.filename for entry in entries]
            if len(set(names)) != len(names):
                raise ValueError("Duplicate archive entries")
            for name in names:
                if ("\\" in name or "\x00" in name or name.startswith("/") or
                        ".." in pathlib.PurePosixPath(name).parts):
                    raise ValueError("Unsafe archive path")
            metadata_entry = package.getinfo("release-stage/BUILD.json")
            if metadata_entry.file_size > MAX_METADATA_BYTES:
                raise ValueError("Oversized build metadata")
            metadata = json.loads(package.read(metadata_entry))
            if not isinstance(metadata, dict):
                raise ValueError("Expected a build metadata object")
            required = {"tag": tag, "source_sha": sha, "build_host": host,
                        "architecture": "x86_64", "signature": "ad-hoc",
                        "capability": "read-only PCI diagnostics", "kext_loaded": False,
                        "gpu_execution_verified": False, "metalvk_rendering_verified": False}
            if any(type(metadata.get(key)) is not type(value) or metadata.get(key) != value
                   for key, value in required.items()):
                raise ValueError("Release metadata disagrees with the source or capability")
            for name in ("LICENSE", "DEVELOPMENT.txt", "README.md", "README_ZN.md",
                         "MetalVK-development.dmg", "MetalVK.kext/Contents/Info.plist",
                         "MetalVK.kext/Contents/MacOS/MetalVK", "MetalVK.app/Contents/MacOS/MetalVK"):
                if "release-stage/" + name not in names:
                    raise ValueError("Incomplete deployment package")
            if package.testzip() is not None:
                raise ValueError("Corrupted deployment package")
        assets.extend((archive, checksum))
    return assets


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", required=True, type=pathlib.Path)
    options = parser.parse_args()
    tag, sha = os.environ.get("RELEASE_TAG", ""), os.environ.get("SOURCE_SHA", "")
    assets = validate_assets(options.assets, tag, sha)
    notes = (
        f"Development diagnostics milestone built from `{sha}`.\n\n"
        "**Not a usable GPU driver. No verified macOS GPU execution, Metal "
        "acceleration or display support.**\n\n"
        "Packages were built on macOS 15/26 Intel, ad-hoc signed and are not "
        "notarized. NVIDIA `10de:28e0` only; read-only BAR0 probe is disabled "
        "by default. No automatic install/load or startup-security changes.\n\n"
        "Archives include source/build metadata, license, warnings and SHA-256 "
        "checksums. Host tests and native builds are not kext-load evidence.\n"
    )
    # Start as draft so an upload failure cannot expose a partial public release.
    # Existing releases are not overwritten; gh create fails and leaves them intact.
    subprocess.run(["gh", "release", "create", tag, "--verify-tag", "--draft",
                    "--prerelease", "--latest=false", "--title", f"{tag} development diagnostics",
                    "--notes", notes, *(str(path) for path in assets)],
                   check=True, timeout=COMMAND_TIMEOUT)
    subprocess.run(["gh", "release", "edit", tag, "--draft=false", "--prerelease",
                    "--latest=false"], check=True, timeout=COMMAND_TIMEOUT)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, zipfile.BadZipFile,
            subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        print(f"Development release refused: {error}", file=sys.stderr)
        raise SystemExit(1)
