#!/usr/bin/env python3
"""Focused tests for compiler-emitted GKI module entry ABI prefixes."""

import importlib.util
from pathlib import Path
import unittest


TOOLS = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "build_gki_smoke_modules", TOOLS / "build-gki-smoke-modules.py"
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load build-gki-smoke-modules.py")
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)


class CompilerPrefixTests(unittest.TestCase):
    EXPECTED = {
        "cleanup_module": "0xe5c47d60",
        "init_module": "0x6fbb3035",
    }

    @staticmethod
    def records(init_prefix, cleanup_prefix):
        return {
            "init_module": {"prefix": init_prefix},
            "cleanup_module": {"prefix": cleanup_prefix},
        }

    def test_accepts_exact_compiler_emitted_typeids(self):
        records = self.records(
            bytes.fromhex("3530bb6f"), bytes.fromhex("607dc4e5")
        )
        self.assertEqual(
            build.validate_compiler_kcfi_typeids(
                records, self.EXPECTED, "little"
            ),
            self.EXPECTED,
        )

    def test_rejects_missing_or_incorrect_compiler_typeids(self):
        missing = self.records(None, bytes.fromhex("607dc4e5"))
        with self.assertRaisesRegex(RuntimeError, "compiler omitted"):
            build.validate_compiler_kcfi_typeids(
                missing, self.EXPECTED, "little"
            )

        incorrect = self.records(bytes(4), bytes.fromhex("607dc4e5"))
        with self.assertRaisesRegex(RuntimeError, "compiler-emitted"):
            build.validate_compiler_kcfi_typeids(
                incorrect, self.EXPECTED, "little"
            )

    def test_non_kcfi_profiles_require_zero_or_absent_prefixes(self):
        self.assertIsNone(
            build.validate_compiler_kcfi_typeids(
                self.records(bytes(4), None), None, "little"
            )
        )
        with self.assertRaisesRegex(RuntimeError, "non-KCFI"):
            build.validate_compiler_kcfi_typeids(
                self.records(bytes.fromhex("01000000"), None), None, "little"
            )


if __name__ == "__main__":
    unittest.main()
