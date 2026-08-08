#!/usr/bin/env python3
"""Unit tests for the final Android module loader-contract checker."""

import copy
import importlib.util
from pathlib import Path
import unittest


TOOLS = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "verify_android_module", TOOLS / "verify-android-module.py"
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load verify-android-module.py")
verify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verify)


def valid_record(alloc_tags_size=0):
    return {
        "elf_class": "ELFCLASS64",
        "data_encoding": "ELFDATA2LSB",
        "machine": "EM_AARCH64",
        "type": "ET_REL",
        "sections": {
            "__versions": [
                {
                    "index": 2,
                    "type": "SHT_PROGBITS",
                    "flags": verify.SHF_ALLOC,
                    "alignment": 8,
                    "size": 0,
                }
            ],
            ".codetag.alloc_tags": [
                {
                    "index": 3,
                    "type": "SHT_PROGBITS",
                    "flags": verify.SHF_ALLOC | verify.SHF_WRITE,
                    "alignment": 8,
                    "size": alloc_tags_size,
                }
            ],
        },
        "symbols": {
            "__start_alloc_tags": [
                {
                    "binding": "STB_GLOBAL",
                    "type": "STT_NOTYPE",
                    "section_index": 3,
                    "value": 0,
                }
            ],
            "__stop_alloc_tags": [
                {
                    "binding": "STB_GLOBAL",
                    "type": "STT_NOTYPE",
                    "section_index": 3,
                    "value": alloc_tags_size,
                }
            ],
        },
    }


class ContractTests(unittest.TestCase):
    def test_accepts_empty_and_populated_ranges(self):
        self.assertEqual(
            verify.validate_module_contract(
                valid_record(), require_empty_alloc_tags=True
            ),
            {"versions_entries": 0, "alloc_tags_size": 0},
        )
        self.assertEqual(
            verify.validate_module_contract(valid_record(24))["alloc_tags_size"],
            24,
        )

    def test_rejects_wrong_elf_identity(self):
        for field, value in (
            ("elf_class", "ELFCLASS32"),
            ("data_encoding", "ELFDATA2MSB"),
            ("machine", "EM_X86_64"),
            ("type", "ET_EXEC"),
        ):
            with self.subTest(field=field):
                record = valid_record()
                record[field] = value
                with self.assertRaises(verify.ValidationError):
                    verify.validate_module_contract(record)

    def test_rejects_missing_or_malformed_versions(self):
        record = valid_record()
        del record["sections"]["__versions"]
        with self.assertRaisesRegex(verify.ValidationError, "__versions"):
            verify.validate_module_contract(record)

        for field, value in (
            ("type", "SHT_NOBITS"),
            ("flags", 0),
            ("flags", verify.SHF_ALLOC | verify.SHF_COMPRESSED),
            ("alignment", 4),
            ("size", 1),
        ):
            with self.subTest(field=field):
                record = valid_record()
                record["sections"]["__versions"][0][field] = value
                with self.assertRaises(verify.ValidationError):
                    verify.validate_module_contract(record)

    def test_rejects_uncollected_or_malformed_alloc_tags(self):
        record = valid_record()
        record["sections"]["alloc_tags"] = [copy.deepcopy(
            record["sections"][".codetag.alloc_tags"][0]
        )]
        with self.assertRaisesRegex(verify.ValidationError, "uncollected"):
            verify.validate_module_contract(record)

        for field, value in (
            ("type", "SHT_NOBITS"),
            ("flags", verify.SHF_ALLOC),
            (
                "flags",
                verify.SHF_ALLOC | verify.SHF_WRITE | verify.SHF_COMPRESSED,
            ),
            ("alignment", 4),
        ):
            with self.subTest(field=field):
                record = valid_record()
                record["sections"][".codetag.alloc_tags"][0][field] = value
                with self.assertRaises(verify.ValidationError):
                    verify.validate_module_contract(record)

    def test_section_name_only_fake_with_undefined_boundaries_is_rejected(self):
        record = valid_record()
        for name in ("__start_alloc_tags", "__stop_alloc_tags"):
            record["symbols"][name][0]["section_index"] = "SHN_UNDEF"
        with self.assertRaisesRegex(verify.ValidationError, "must be defined"):
            verify.validate_module_contract(record)

    def test_rejects_duplicate_wrong_binding_and_wrong_range(self):
        record = valid_record()
        record["symbols"]["__start_alloc_tags"].append(
            copy.deepcopy(record["symbols"]["__start_alloc_tags"][0])
        )
        with self.assertRaisesRegex(verify.ValidationError, "exactly one"):
            verify.validate_module_contract(record)

        record = valid_record()
        record["symbols"]["__start_alloc_tags"][0]["binding"] = "STB_WEAK"
        with self.assertRaisesRegex(verify.ValidationError, "global"):
            verify.validate_module_contract(record)

        record = valid_record()
        record["symbols"]["__stop_alloc_tags"][0]["type"] = "STT_OBJECT"
        with self.assertRaisesRegex(verify.ValidationError, "STT_NOTYPE"):
            verify.validate_module_contract(record)

        record = valid_record(16)
        record["symbols"]["__stop_alloc_tags"][0]["value"] = 8
        with self.assertRaisesRegex(verify.ValidationError, "section size"):
            verify.validate_module_contract(record)

    def test_smoke_mode_rejects_real_tags(self):
        with self.assertRaisesRegex(verify.ValidationError, "must have an empty"):
            verify.validate_module_contract(
                valid_record(8), require_empty_alloc_tags=True
            )

    def test_rejects_neverc_profile_contract_fingerprint(self):
        record = valid_record()
        record["sections"][".neverc.android.kernel.profile"] = [
            {
                "index": 99,
                "type": "SHT_PROGBITS",
                "flags": verify.SHF_ALLOC,
                "alignment": 8,
                "size": 8,
            }
        ]
        with self.assertRaisesRegex(verify.ValidationError, "profile-contract section"):
            verify.validate_module_contract(record)

        record = valid_record()
        record["symbols"]["__neverc_android_kernel_profile_contract"] = [
            {
                "binding": "STB_LOCAL",
                "type": "STT_OBJECT",
                "section_index": 1,
                "value": 0,
            }
        ]
        with self.assertRaisesRegex(verify.ValidationError, "profile-contract symbol"):
            verify.validate_module_contract(record)


if __name__ == "__main__":
    unittest.main()
