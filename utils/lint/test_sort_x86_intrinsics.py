#!/usr/bin/env python3
"""Check that intrinsic identities retain their type metadata when renumbered."""

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


SCRIPT = Path(__file__).with_name("sort-x86-intrinsics.py")
SPEC = importlib.util.spec_from_file_location("sort_x86_intrinsics", SCRIPT)
SORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SORTER)


class IntrinsicMetadataTests(unittest.TestCase):
    def sort_fixture(self, additions=()):
        enum = """enum X86Intrinsics : unsigned {
    x86_z = 2, // llvm.x86.z
    x86_a, // llvm.x86.a
}; // enum
"""
        tables = """{llvm::StringLiteral("x86"), 1, 2},
#ifdef GET_INTRINSIC_NAME_TABLE
  "llvm.prefix",
  "llvm.x86.z",
  "llvm.x86.a",
#endif
static const unsigned IIT_Table[] = {0, 11, 22};
IntrinsicsToAttributesMap[] = {
    7, // llvm.prefix
    8, // llvm.x86.z
    9, // llvm.x86.a
};
static const uint8_t OTable[] = {
  0 | (1<<1) | (1<<2)
};
"""
        with tempfile.TemporaryDirectory() as temp:
            enum_path, inc_path = Path(temp) / "enum.h", Path(temp) / "impl.inc"
            enum_path.write_text(enum)
            inc_path.write_text(tables)
            with patch.object(SORTER, "ENUM_PATH", enum_path), \
                 patch.object(SORTER, "INC_PATH", inc_path):
                return SORTER.sort_x86_intrinsics(additions)

    def test_sort_preserves_each_intrinsics_signature_and_overload_flag(self):
        new_enum, new_tables, count = self.sort_fixture()
        self.assertEqual(count, 2)
        self.assertEqual(SORTER.parse_enum_names(new_enum), ["x86_a", "x86_z"])
        self.assertEqual(SORTER.parse_name_table(new_tables),
                         ["llvm.prefix", "llvm.x86.a", "llvm.x86.z"])
        self.assertEqual(SORTER.parse_iit_table(new_tables), ["0", "22", "11"])
        self.assertEqual([value for value, _ in SORTER.parse_attr_map(new_tables)],
                         ["7", "9", "8"])
        self.assertEqual(SORTER.parse_overload_table(new_tables)[:4],
                         [False, True, False, True])

    def test_addition_keeps_old_signatures_and_moves_their_overload_bits(self):
        record = {"cpp": "x86_invept", "name": "llvm.x86.invept", "iit": "33",
                  "attr_val": "10", "overloaded": False}
        new_enum, tables, count = self.sort_fixture([record])
        self.assertEqual(count, 3)
        self.assertIn('{llvm::StringLiteral("x86"), 1, 3}', tables)
        self.assertEqual(SORTER.parse_enum_names(new_enum), ["x86_a", "x86_invept", "x86_z"])
        self.assertEqual(SORTER.parse_iit_table(tables), ["0", "22", "33", "11"])
        self.assertEqual([value for value, _ in SORTER.parse_attr_map(tables)],
                         ["7", "9", "10", "8"])
        self.assertEqual(SORTER.parse_overload_table(tables)[:5],
                         [False, True, False, False, True])

    def test_bitmap_keeps_bits_across_byte_boundaries(self):
        flags = [i in {1, 7, 8, 13, 16} for i in range(19)]
        text = SORTER.replace_overload_table("static const uint8_t OTable[] = {0};", flags)
        actual = SORTER.parse_overload_table(text)
        self.assertEqual(actual[:len(flags)], flags)
        self.assertFalse(any(actual[len(flags):]))

    def test_bitmap_rejects_unrecognized_encoding(self):
        with self.assertRaises(ValueError):
            SORTER.parse_overload_table("static const uint8_t OTable[] = {0 | (1<<8)};")

    def test_dag_remaps_chained_and_chainless_intrinsic_ids(self):
        text = ("OPC_CheckChild0Integer, 16|128,45,\n"
                "OPC_CheckChild1Integer, 16|128,45,\n"
                "OPC_CheckChild0Integer, 3,\n")
        actual, count = SORTER._patch_dagisel(text, {2888: 2913})
        self.assertEqual(count, 2)
        self.assertEqual(actual, text.replace("16|128,45", "66|128,45"))

    def test_dag_rejects_remapping_that_invalidates_branch_offsets(self):
        with self.assertRaisesRegex(ValueError, "matcher width"):
            SORTER._patch_dagisel("OPC_CheckChild0Integer, 126,\n", {63: 64})

    def test_sync_point_enum_and_dag_use_the_same_revision(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            enum = root / "enum.h"
            dag = root / "dag.inc"

            def git(*args):
                return subprocess.check_output(
                    ["git", "-C", str(root), "-c", "user.name=Intrinsic Test",
                     "-c", "user.email=intrinsic-test@example.invalid",
                     "-c", "commit.gpgsign=false", "-c", "core.hooksPath=" + str(root / "hooks"),
                     *args], text=True, stderr=subprocess.DEVNULL)

            git("init", "-q")
            enum.write_text("x86_a,\nx86_b,\n")
            dag.write_text("old pattern table\n")
            git("add", ".")
            git("commit", "-qm", "initial metadata")
            enum.write_text("x86_a,\nx86_b,\nx86_c,\n")
            dag.write_text("synchronized pattern table\n")
            git("add", ".")
            git("commit", "-qm", "synchronize added intrinsic")
            with patch.object(SORTER, "REPO", root), \
                 patch.object(SORTER, "ENUM_PATH", enum), \
                 patch.object(SORTER, "DAGISEL_PATH", dag):
                names, reference = SORTER._find_baseline(4)
                self.assertEqual(names, ["x86_a", "x86_b", "x86_c"])
                self.assertEqual(SORTER._get_original_dagisel(reference),
                                 "synchronized pattern table\n")


if __name__ == "__main__":
    unittest.main()
