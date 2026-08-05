#!/usr/bin/env python3
"""Focused tests for release-ABI patching of GKI smoke modules."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "build_gki_smoke_modules", TOOLS / "build-gki-smoke-modules.py"
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load build-gki-smoke-modules.py")
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)


class PrefixPatchTests(unittest.TestCase):
    def test_patch_writes_little_endian_typeids_at_exact_prefix_offsets(self):
        with tempfile.TemporaryDirectory() as directory:
            module = Path(directory) / "smoke.ko"
            module.write_bytes(bytes(32))
            records = {
                "init_module": {
                    "file_offset": 8,
                    "section_offset": 12,
                    "prefix": bytes(4),
                },
                "cleanup_module": {
                    "file_offset": 20,
                    "section_offset": 24,
                    "prefix": bytes(4),
                },
            }
            expected = {
                "cleanup_module": "0xe5c47d60",
                "init_module": "0x6fbb3035",
            }
            build.patch_entry_prefixes(module, records, expected, "little")
            payload = module.read_bytes()
            self.assertEqual(payload[8:12], bytes.fromhex("3530bb6f"))
            self.assertEqual(payload[20:24], bytes.fromhex("607dc4e5"))

    def test_patch_rejects_nonzero_or_missing_prefix_space(self):
        with tempfile.TemporaryDirectory() as directory:
            module = Path(directory) / "smoke.ko"
            module.write_bytes(bytes(32))
            expected = {
                "cleanup_module": "0xe5c47d60",
                "init_module": "0x6fbb3035",
            }
            records = {
                "init_module": {
                    "file_offset": 8,
                    "section_offset": 12,
                    "prefix": b"used",
                },
                "cleanup_module": {
                    "file_offset": 20,
                    "section_offset": 24,
                    "prefix": bytes(4),
                },
            }
            with self.assertRaisesRegex(RuntimeError, "not zero-filled"):
                build.patch_entry_prefixes(module, records, expected, "little")

            records["init_module"] = {
                "file_offset": None,
                "section_offset": 0,
                "prefix": None,
            }
            with self.assertRaisesRegex(RuntimeError, "no prefix space"):
                build.patch_entry_prefixes(module, records, expected, "little")


if __name__ == "__main__":
    unittest.main()
