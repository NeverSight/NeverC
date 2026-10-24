#!/usr/bin/env python3
"""Protect explicit AMX operand effects and append-only plugin identities."""
import importlib.util
import re
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "amx_controls", Path(__file__).with_name("sync-x86-amx-native-controls.py"))
AMX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AMX)


class AMXNativeControlsTest(unittest.TestCase):
    def test_checked_in_tables_match_authority(self):
        for path, expected in AMX.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))

    def test_old_opcode_identities_are_preserved(self):
        text = (AMX.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        ids = AMX.TABLES.enums(text)
        for name, value in (("INVLPGA64", 1442), ("VMLOAD64", 10225),
                            ("VMSAVE64", 10809), ("LKGS16m", 18490)):
            self.assertEqual(ids[name], value)
        self.assertEqual(ids["AMX_MEMORY_NATIVE"], 18538)
        self.assertEqual(ids["AMX_ROW_NATIVE"], 18539)

    def test_native_operands_keep_complete_ambient_bank_effects(self):
        text = (AMX.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for name, operands, outputs in (("AMX_MEMORY_NATIVE", 6, 0),
                                        ("AMX_ROW_NATIVE", 3, 1)):
            fields = AMX.TABLES.descriptor(text, name)
            self.assertEqual(fields[1:3], [str(operands), str(outputs)])
            self.assertEqual(fields[5:7], ["0", "8"])
            for effect in ("Pseudo", "MayLoad", "MayStore", "UnmodeledSideEffects"):
                self.assertIn("MCID::" + effect, fields[9])

    def test_lost_native_effect_is_rejected(self):
        text = (AMX.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        corrupted = re.sub(r"\|\(1ULL<<MCID::MayStore\)(?=, 0x0ULL \},  // Inst #\d+ = AMX_ROW_NATIVE)", "", text)
        self.assertNotEqual(text, corrupted)
        with self.assertRaisesRegex(ValueError, "stale.*instruction"):
            AMX.synchronize_table(corrupted)


if __name__ == "__main__":
    unittest.main()
