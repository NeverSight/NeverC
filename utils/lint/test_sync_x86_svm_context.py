#!/usr/bin/env python3
"""Guard the VMRUN pseudo's complete register and unknown-memory effects."""

import importlib.util
from pathlib import Path
import re
import unittest

SPEC = importlib.util.spec_from_file_location(
    "svm", Path(__file__).with_name("sync-x86-svm-context.py"))
SVM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SVM)


class SVMContextTableTest(unittest.TestCase):
    def test_checked_in_table_is_stable(self):
        for path, expected in SVM.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))
        text = (SVM.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        self.assertEqual(SVM.synchronize_table(text), text)
        fields = SVM.TABLES.descriptor(text, "VMRUN_CONTEXT64")
        self.assertEqual(fields[5:7], ["1", "16"])
        offset = fields[7].removeprefix("X86ImpOpBase + ")
        sequence = re.search(rf"/\* {offset} \*/ ([^\n]+)", text).group(1)
        registers = re.findall(r"X86::(\w+)", sequence)
        self.assertEqual(registers[0], "RSP")
        self.assertEqual(set(registers[1:]),
                         {"RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RBP",
                          *(f"R{i}" for i in range(8, 16)), "EFLAGS"})

    def test_dropped_effect_or_register_is_detected(self):
        text = (SVM.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        line = re.search(r"^.*// Inst #\d+ = VMRUN_CONTEXT64$", text, re.M).group(0)
        for corrupted in (line.replace("|(1ULL<<MCID::MayStore)", ""),
                          line.replace(",\t1,\t16,\tX86ImpOpBase",
                                       ",\t1,\t15,\tX86ImpOpBase")):
            self.assertNotEqual(line, corrupted)
            with self.assertRaisesRegex(ValueError, "stale.*instruction"):
                SVM.synchronize_table(text.replace(line, corrupted))


if __name__ == "__main__":
    unittest.main()
