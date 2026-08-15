#!/usr/bin/env python3
"""Regression tests for the complete NeverC runtime-layout comparison."""

import importlib.util
from pathlib import Path
import re
import unittest


TOOLS_ROOT = Path(__file__).resolve().parent


def load_tool(name, filename):
    spec = importlib.util.spec_from_file_location(name, TOOLS_ROOT / filename)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {filename}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


COMPAT = load_tool(
    "nvk_compare_compat_test", "generate-compat-table.py"
)
COMPARE = load_tool(
    "nvk_compare_patch_layouts_test", "compare-gki-patch-layouts.py"
)


def layout(size, **members):
    return {
        "size": size,
        "members": dict(members),
        "member_sizes": {name: 8 for name in members},
    }


class CompareGkiPatchLayoutsTests(unittest.TestCase):
    def test_projection_is_derived_from_the_runtime_contract(self):
        self.assertEqual(
            COMPARE.runtime_compare_members_by_struct(),
            COMPAT.neverc_read_members_by_struct(),
        )

    def test_flat_runtime_projection_covers_the_c_layout(self):
        header = (TOOLS_ROOT.parent / "src/nvk_internal.h").read_text(
            encoding="utf-8"
        )
        match = re.search(
            r"struct neverc_krt_gki_layout \{(?P<body>.*?)\n\};",
            header,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        c_fields = set(re.findall(
            r"unsigned long ([a-z0-9_]+);", match.group("body")
        ))

        self.assertEqual(set(COMPAT.RUNTIME_LAYOUT_FIELD_NAMES), c_fields)

    def test_formerly_omitted_runtime_field_difference_is_reported(self):
        left = COMPARE.canonical_fields({
            "cred": layout(176, cap_effective=72, uid=4),
        })
        right = COMPARE.canonical_fields({
            "cred": layout(176, cap_effective=80, uid=4),
        })

        self.assertIn(
            "runtime.cred_cap_effective 72 != 80",
            COMPARE.diff_fields(left, right),
        )

    def test_missing_runtime_member_is_not_silently_ignored(self):
        left = COMPARE.canonical_fields({
            "cred": layout(176, cap_effective=72, uid=4),
        })
        right = COMPARE.canonical_fields({
            "cred": layout(176, uid=4),
        })

        self.assertIn(
            "runtime.cred_cap_effective: missing on one side",
            COMPARE.diff_fields(left, right),
        )

    def test_size_only_runtime_struct_is_compared(self):
        left = COMPARE.canonical_fields({"file_operations": layout(248)})
        right = COMPARE.canonical_fields({"file_operations": layout(256)})

        self.assertIn(
            "source.file_operations.size 248 != 256",
            COMPARE.diff_fields(left, right),
        )

    def test_schema2_certificate_uses_its_full_runtime_layout(self):
        runtime_layout = {
            name: index
            for index, name in enumerate(COMPAT.RUNTIME_LAYOUT_FIELD_NAMES)
        }
        certificate = {
            "release_token": "6.12.45-android16-5-test",
            "identity": {
                "linux_major": 6,
                "linux_minor": 12,
                "linux_patch": 45,
                "android_release": 16,
                "kmi_generation": 5,
                "page_shift": 12,
            },
            "runtime_layout": {"schema": 1, "fields": runtime_layout},
        }

        record = COMPARE.record_from_certificate(certificate)
        expected = {
            f"runtime.{name}": value
            for name, value in runtime_layout.items()
        }
        mismatches, evidence_gaps = COMPARE.compare_field_maps(
            record["fields"], expected
        )

        self.assertEqual(mismatches, [])
        self.assertEqual(evidence_gaps, [])


if __name__ == "__main__":
    unittest.main()
