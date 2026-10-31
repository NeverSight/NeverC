#!/usr/bin/env python3
"""Guard KVM's five fixed inputs, return register and conservative effects."""

import importlib.util
from pathlib import Path
import re
import unittest

SPEC = importlib.util.spec_from_file_location(
    "kvm", Path(__file__).with_name("sync-x86-kvm-hypercalls.py"))
KVM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(KVM)


class KVMHypercallTableTest(unittest.TestCase):
    def test_checked_in_tables_and_exact_register_order(self):
        for path, expected in KVM.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))
        text = (KVM.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        self.assertEqual(KVM.synchronize_table(text), text)
        for opcode, _, _, _ in KVM.INSTRUCTIONS:
            fields = KVM.TABLES.descriptor(text, opcode)
            self.assertEqual(fields[5:7], ["5", "2"])
            offset = fields[7].removeprefix("X86ImpOpBase + ")
            sequence = re.search(rf"/\* {offset} \*/ (X86::[^\n]+)", text).group(1)
            registers = re.findall(r"X86::(\w+)", sequence)
            prefix = "R" if opcode.endswith("64") else "E"
            self.assertEqual(registers,
                             [prefix + r for r in ("AX", "BX", "CX", "DX", "SI", "AX")] + ["EFLAGS"])
            for flag in ("MayLoad", "MayStore", "UnmodeledSideEffects"):
                self.assertIn(f"MCID::{flag}", fields[9])

    def test_dropped_effect_or_input_is_detected(self):
        text = (KVM.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for opcode, _, _, _ in KVM.INSTRUCTIONS:
            line = re.search(rf"^.*// Inst #\d+ = {opcode}$", text, re.M).group(0)
            for corrupted in (line.replace("|(1ULL<<MCID::MayStore)", ""),
                              line.replace(",\t5,\t2,\tX86ImpOpBase",
                                           ",\t4,\t2,\tX86ImpOpBase")):
                self.assertNotEqual(line, corrupted)
                with self.assertRaisesRegex(ValueError, "stale.*instruction"):
                    KVM.synchronize_table(text.replace(line, corrupted))


if __name__ == "__main__":
    unittest.main()
