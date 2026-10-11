# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 KaguyaRing
"""Local release validation tests; never creates GitHub releases."""
import hashlib
import importlib.util
import json
import pathlib
import tempfile
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("publisher", ROOT / "tools/publish_development_release.py")
assert SPEC is not None and SPEC.loader is not None
PUBLISHER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PUBLISHER)
TAG = "v0.1.0"
SHA = "a" * 40


def fixture(directory, wrong_source=False, unsafe_path=False):
    for host in PUBLISHER.HOSTS:
        archive = directory / f"MetalVK-{TAG}-development-{host}.zip"
        with zipfile.ZipFile(archive, "w") as package:
            metadata = {"tag": TAG, "source_sha": "b" * 40 if wrong_source else SHA,
                        "build_host": host, "architecture": "x86_64", "signature": "ad-hoc",
                        "capability": "read-only PCI diagnostics", "kext_loaded": False,
                        "gpu_execution_verified": False, "metalvk_rendering_verified": False}
            package.writestr("release-stage/BUILD.json", json.dumps(metadata))
            for name in ("LICENSE", "DEVELOPMENT.txt", "README.md", "README_ZN.md",
                         "MetalVK-development.dmg", "MetalVK.kext/Contents/Info.plist",
                         "MetalVK.kext/Contents/MacOS/MetalVK", "MetalVK.app/Contents/MacOS/MetalVK"):
                package.writestr("release-stage/" + name, "validation fixture only")
            if unsafe_path:
                package.writestr("../outside", "rejected")
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        (directory / (archive.name + ".sha256")).write_text(f"{digest}  {archive.name}\n", encoding="ascii")


class ReleaseTests(unittest.TestCase):
    def test_two_matching_packages_are_accepted(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            fixture(directory)
            self.assertEqual(len(PUBLISHER.validate_assets(directory, TAG, SHA)), 4)

    def test_wrong_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            fixture(directory, wrong_source=True)
            with self.assertRaises(ValueError):
                PUBLISHER.validate_assets(directory, TAG, SHA)

    def test_unsafe_path_is_rejected_without_extraction(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            fixture(directory, unsafe_path=True)
            with self.assertRaises(ValueError):
                PUBLISHER.validate_assets(directory, TAG, SHA)

    def test_tampered_checksum_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            fixture(directory)
            next(directory.glob("*.sha256")).write_text("invalid", encoding="ascii")
            with self.assertRaises(ValueError):
                PUBLISHER.validate_assets(directory, TAG, SHA)

    def test_missing_host_package_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(ValueError):
                PUBLISHER.validate_assets(pathlib.Path(temp), TAG, SHA)

    def test_new_unsuffixed_or_malformed_tag_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            for tag in ("v0.1.1", "--help", "../v0.1.0"):
                with self.assertRaises(ValueError):
                    PUBLISHER.validate_assets(pathlib.Path(temp), tag, SHA)


if __name__ == "__main__":
    unittest.main()
