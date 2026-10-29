#!/usr/bin/env python3
"""Verify x87 prefix encodings and the generated-table corruption checks."""

import importlib.util
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "environment", Path(__file__).with_name("sync-x86-x87-environment.py"))
ENV = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ENV)


class EnvironmentTablesTest(unittest.TestCase):
    def test_checked_in_tables_are_synchronized(self):
        for path, expected in ENV.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))

    def test_format_prefix_changes_no_opcode_or_address_bits(self):
        table = (ENV.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for name, original, _, _ in ENV.INSTRUCTIONS:
            source = ENV.TABLES.descriptor(table, original)
            target = ENV.TABLES.descriptor(table, name)
            old, new = [int(row[9].split(", ")[1].removesuffix("ULL"), 16)
                        for row in (source, target)]
            self.assertEqual(old ^ new, 1 << 7)
            self.assertEqual(source[1:9], target[1:9])
            self.assertEqual(source[9].split(", ")[0], target[9].split(", ")[0])

    def test_removing_operand_prefix_is_rejected(self):
        table = (ENV.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for name, original, _, _ in ENV.INSTRUCTIONS:
            row = next(line for line in table.splitlines()
                       if line.endswith(" = " + name))
            new = ENV.TABLES.descriptor(table, original)[9].split(", ")[1]
            old = ENV.TABLES.descriptor(table, name)[9].split(", ")[1]
            bad = table.replace(row, row.replace(old, new))
            with self.assertRaisesRegex(ValueError, "stale.*instruction"):
                ENV.synchronize_table(bad)


if __name__ == "__main__":
    unittest.main()
