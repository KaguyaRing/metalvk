# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 KaguyaRing
"""Regression tests for operation catalog extraction, not runtime alignment."""
import importlib.util
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("alignment", ROOT / "tools/check_nvkmd_alignment.py")
assert SPEC is not None and SPEC.loader is not None
ALIGNMENT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ALIGNMENT)


class CatalogTests(unittest.TestCase):
    def test_transport_version_is_read_from_header(self):
        with tempfile.TemporaryDirectory(prefix="metalvk-catalog-") as directory:
            header = pathlib.Path(directory) / "transport.h"
            for version in (1, 2, 17):
                header.write_text(f"#define MTVK_NV_TRANSPORT_VERSION {version}u\n", encoding="utf-8")
                self.assertEqual(ALIGNMENT.transport_version(header), version)

    def test_missing_duplicate_or_invalid_version_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="metalvk-catalog-") as directory:
            header = pathlib.Path(directory) / "transport.h"
            for text in ("", "#define MTVK_NV_TRANSPORT_VERSION 0u\n",
                         "#define MTVK_NV_TRANSPORT_VERSION 4294967296u\n",
                         "#define MTVK_NV_TRANSPORT_VERSION 2u\n" * 2):
                header.write_text(text, encoding="utf-8")
                with self.assertRaises(SystemExit):
                    ALIGNMENT.transport_version(header)

    def test_helpers_outside_enum_are_not_operations(self):
        with tempfile.TemporaryDirectory(prefix="metalvk-catalog-") as directory:
            header = pathlib.Path(directory) / "transport.h"
            source = pathlib.Path(directory) / "transport.c"
            header.write_text(
                "typedef enum mtvk_nv_op { MTVK_NV_OP_DEVICE_QUERY = 0u, "
                "MTVK_NV_OP_COUNT } mtvk_nv_op;\n"
                "#define MTVK_NV_OP_BIT(op) (1u << (op))\n", encoding="utf-8")
            source.write_text(
                "[MTVK_NV_OP_DEVICE_QUERY] = MTVK_NV_CLASS_DISCOVERY,\n", encoding="utf-8")
            names, classes = ALIGNMENT.catalog(header, source)
            self.assertEqual(names, {"DEVICE_QUERY"})
            self.assertEqual(classes, {"DEVICE_QUERY": "discovery"})

    def test_missing_enum_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="metalvk-catalog-") as directory:
            header = pathlib.Path(directory) / "transport.h"
            source = pathlib.Path(directory) / "transport.c"
            header.write_text("#define MTVK_NV_OP_BIT(op) (1u << (op))", encoding="utf-8")
            source.write_text("", encoding="utf-8")
            with self.assertRaises(SystemExit):
                ALIGNMENT.catalog(header, source)


if __name__ == "__main__":
    unittest.main()
