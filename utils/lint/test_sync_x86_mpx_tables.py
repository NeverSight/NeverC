#!/usr/bin/env python3
"""Check MPX bank ownership, stable table identities, and effect drift."""

import importlib.util
from pathlib import Path
import re
import unittest


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


REGISTERS = module("mpx_registers", "sync-x86-mpx-registers.py")
INSTRUCTIONS = module("mpx_instructions", "sync-x86-mpx-instructions.py")
TABLES = INSTRUCTIONS.TABLES


class MpxTablesTest(unittest.TestCase):
    def test_register_tables_are_synchronized(self):
        for name, transform in (
                ("X86GenRegisterInfo.inc", REGISTERS.synchronize_registers),
                ("X86GenAsmWriter.inc", REGISTERS.synchronize_writer),
                ("X86GenAsmWriter1.inc", REGISTERS.synchronize_writer),
                ("X86GenAsmMatcher.inc", REGISTERS.synchronize_matcher)):
            text = (TABLES.TARGET / name).read_text()
            self.assertEqual(transform(text), text, name)

    def test_instruction_tables_are_synchronized(self):
        for path, expected in INSTRUCTIONS.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))

    def test_existing_opcode_ids_remain_stable(self):
        ids = TABLES.enums((TABLES.TARGET / "X86GenInstrInfo.inc").read_text())
        for name, value in (("INVLPGA64", 1442), ("VMLOAD64", 10225),
                            ("VMSAVE64", 10809), ("LKGS16m", 18490)):
            self.assertEqual(ids[name], value, name)
        for name, _, _, _ in INSTRUCTIONS.INSTRUCTIONS:
            self.assertGreaterEqual(ids[name], 18510)

    def test_bounds_are_distinct_nonallocatable_registers(self):
        text = (TABLES.TARGET / "X86GenRegisterInfo.inc").read_text()
        for index in range(4):
            self.assertIn(f"  BND{index} = {388 + index},", text)
            self.assertIn(f"  {{ X86::BND{index} }},", text)
        self.assertRegex(text, r"BNDR, BNDRBits, \d+, 4, sizeof\(BNDRBits\), "
                              r"X86::BNDRRegClassID, 128, 1, false")
        self.assertIn("false, false, false, false,\n// MPX bank end", text)
        self.assertIn("{0, 0}, // BNDR", text)

    def test_corrupted_register_ownership_is_detected(self):
        text = (TABLES.TARGET / "X86GenRegisterInfo.inc").read_text()
        for before, after in (("  BND0 = 388,", "  BND0 = 300,"),
                              ("BNDRRegClassID, 128, 1, false", "BNDRRegClassID, 128, 1, true"),
                              ("{ X86::BND3 }", "{ X86::RAX }")):
            corrupted = text.replace(before, after)
            self.assertNotEqual(corrupted, text)
            self.assertEqual(REGISTERS.synchronize_registers(corrupted), text)

    def test_pseudos_keep_the_entire_bank_and_unknown_memory(self):
        text = (TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for name in ("MPX_STATE64", "MPX_MOVE_BOUNDS", "MPX_BRANCH"):
            fields = TABLES.descriptor(text, name)
            self.assertEqual(fields[5:7], ["4", "4"])
            for effect in ("Pseudo", "MayLoad", "MayStore", "UnmodeledSideEffects"):
                self.assertIn(f"MCID::{effect}", fields[9])
            offset = fields[7].removeprefix("X86ImpOpBase + ")
            bank = ", ".join(f"X86::BND{i}" for i in (*range(4), *range(4)))
            self.assertIn(f"/* {offset} */ {bank},", text)

    def test_lost_bank_dependency_or_side_effect_is_rejected(self):
        text = (TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        line = re.search(r"^.*// Inst #\d+ = MPX_STATE64$", text, re.MULTILINE).group(0)
        for corrupted_line in (
                line.replace("|(1ULL<<MCID::MayLoad)", ""),
                line.replace("|(1ULL<<MCID::UnmodeledSideEffects)", ""),
                line.replace(",\t4,\t4,\tX86ImpOpBase", ",\t0,\t0,\tX86ImpOpBase")):
            self.assertNotEqual(corrupted_line, line)
            with self.assertRaisesRegex(ValueError, "stale.*instruction"):
                INSTRUCTIONS.synchronize_table(text.replace(line, corrupted_line))


if __name__ == "__main__":
    unittest.main()
