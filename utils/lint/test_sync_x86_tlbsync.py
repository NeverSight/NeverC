#!/usr/bin/env python3
"""Reject lost TLBSYNC register and memory effects in the pruned tables."""

import importlib.util
from pathlib import Path
import re
import unittest


SPEC = importlib.util.spec_from_file_location(
    "tlbsync", Path(__file__).with_name("sync-x86-tlbsync.py"))
SYNC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SYNC)


class TlbSyncTablesTest(unittest.TestCase):
    def test_generated_tables_are_current(self):
        for path, text in SYNC.synchronize().items():
            self.assertEqual(path.read_text(), text, str(path))

    def test_full_control_is_an_input_without_register_definitions(self):
        text = (SYNC.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        fields = SYNC.TABLES.descriptor(text, "TLBSYNC_CONTROL64")
        self.assertEqual(fields[5:7], ["1", "0"])
        start = text.index("    /* 0 */ X86::EFLAGS,")
        end = text.index("\n  }\n};", start)
        registers = re.findall(r"X86::(\w+)", text[start:end])
        self.assertEqual(registers[int(fields[7].split(" + ")[1])], "RAX")

    def test_lost_ordering_is_rejected(self):
        text = (SYNC.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for effect in ("MayLoad", "MayStore", "UnmodeledSideEffects"):
            corrupted = re.sub(
                r"^(.*\},  // Inst #\d+ = TLBSYNC_CONTROL64)$",
                lambda m: m[0].replace("|(1ULL<<MCID::" + effect + ")", ""),
                text, flags=re.MULTILINE)
            self.assertNotEqual(corrupted, text)
            with self.assertRaisesRegex(ValueError, "stale.*instruction"):
                SYNC.synchronize_table(corrupted)


if __name__ == "__main__":
    unittest.main()
