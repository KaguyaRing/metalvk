#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Regression tests for research QA only; no git/network/device access."""
import contextlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import audit_sources as audit


class ResearchAuditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="metalvk-research-qa-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        for name in audit.REQUIRED_DOCUMENTS:
            (self.directory / f"{name}.md").write_text("# 研究\n", encoding="utf-8")
        self.metadata = {"evidence": {"L01": {}}}

    def check(self):
        with contextlib.redirect_stdout(io.StringIO()):
            audit.check_documents(self.directory, self.metadata)

    def write_readme(self, text):
        (self.directory / "README.md").write_text(text, encoding="utf-8")

    def test_valid_local_links_and_known_evidence_are_accepted(self):
        self.write_readme("[L01] [target](target-baseline.md) [online](https://example.org)\n")
        self.check()

    def test_missing_required_document_is_rejected(self):
        (self.directory / "decision.md").unlink()
        with self.assertRaisesRegex(ValueError, "Missing deliverable"):
            self.check()

    def test_unknown_evidence_identifier_is_rejected(self):
        self.write_readme("[M99]\n")
        with self.assertRaisesRegex(ValueError, "Unknown evidence ID"):
            self.check()

    def test_broken_local_link_is_rejected(self):
        self.write_readme("[missing](absent.md)\n")
        with self.assertRaisesRegex(ValueError, "Broken local link"):
            self.check()

    def test_unresolved_placeholder_is_rejected(self):
        self.write_readme("FIXME\n")
        with self.assertRaisesRegex(ValueError, "Unresolved placeholder"):
            self.check()

    def test_utf8_and_crlf_normalize_to_lf(self):
        path = self.directory / "text.txt"
        path.write_bytes("来源\r\n".encode("utf-8"))
        self.assertEqual(audit.read_text(path), "来源\n")

    def test_oversized_text_is_rejected_before_read(self):
        with patch.object(audit, "MAX_TEXT_BYTES", 1):
            with self.assertRaisesRegex(ValueError, "Oversized research input"):
                audit.read_text(self.directory / "README.md")

    def test_rendered_evidence_preserves_pinned_source_location(self):
        metadata = {"evidence": {"L01": {"folder": "source", "path": "driver.c", "start": 4,
                    "end": 12, "url": "https://example.org/pinned/driver.c#L4-L12", "needle": "probe("}}}
        text = audit.render_evidence(metadata)
        self.assertIn("source/driver.c:4-12", text)
        self.assertIn("https://example.org/pinned/driver.c#L4-L12", text)
        self.assertIn("`probe(`", text)
        self.assertEqual(text, audit.render_evidence(metadata))


if __name__ == "__main__":
    unittest.main()
