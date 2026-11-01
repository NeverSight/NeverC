#!/usr/bin/env python3
"""Guard terminal SGX transport effects and the full original-PC operand."""

import importlib.util
from pathlib import Path
import re
import unittest

SPEC = importlib.util.spec_from_file_location(
    "sgx", Path(__file__).with_name("sync-x86-sgx-transfer-context.py"))
SGX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SGX)


class SGXTransferTableTest(unittest.TestCase):
    def test_checked_in_state_and_original_site_contracts(self):
        for path, expected in SGX.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))
        text = (SGX.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for name, _, _, _ in SGX.INSTRUCTIONS:
            fields = SGX.TABLES.descriptor(text, name)
            self.assertEqual(fields[1:4], ["2" if "_AT_" in name else "1", "0", "0"])
            self.assertEqual(fields[5:7], ["1", "19"])
            offset = int(fields[7].removeprefix("X86ImpOpBase + "))
            start = text.index("    /* 0 */ X86::EFLAGS,")
            end = text.index("\n  }\n};", start)
            registers = re.findall(r"X86::(\w+)", text[start:end])
            self.assertEqual(registers[offset], "RSP")
            self.assertEqual(set(registers[offset+1:offset+20]),
                             {"FS", "GS", "EFLAGS", "RAX", "RBX", "RCX", "RDX",
                              "RSI", "RDI", "RBP", "RSP", *(f"R{i}" for i in range(8, 16))})
            for flag in ("MayLoad", "MayStore", "UnmodeledSideEffects", "Terminator", "Barrier"):
                self.assertIn(f"MCID::{flag}", fields[9])
            if "_AT_" in name:
                operands = re.search(rf"/\* {fields[8]} \*/ ([^\n]+)", text).group(1)
                self.assertIn("MCOI::OPERAND_IMMEDIATE", operands)

    def test_dropped_memory_effect_or_terminal_boundary_is_detected(self):
        text = (SGX.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for name, _, _, _ in SGX.INSTRUCTIONS:
            line = re.search(r"^.*// Inst #\d+ = " + name + "$", text, re.M).group(0)
            for flag in ("MayStore", "Barrier", "Terminator"):
                damaged = line.replace(f"|(1ULL<<MCID::{flag})", "")
                self.assertNotEqual(line, damaged)
                with self.assertRaisesRegex(ValueError, "stale.*instruction"):
                    SGX.synchronize_table(text.replace(line, damaged))


if __name__ == "__main__":
    unittest.main()
