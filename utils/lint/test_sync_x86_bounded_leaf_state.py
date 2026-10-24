#!/usr/bin/env python3
"""Keep bounded leaf register/flag windows indivisible and append-only."""

import importlib.util
from pathlib import Path
import re
import unittest


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


STATE = module("leaf_state", "sync-x86-bounded-leaf-state.py")
NATIVE = module("native", "sync-x86-native-intrinsics.py")


class BoundedLeafStateTest(unittest.TestCase):
    def setUp(self):
        self.text = (STATE.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()

    def test_generated_tables_match(self):
        for path, text in STATE.synchronize().items():
            self.assertEqual(path.read_text(), text, str(path))
        self.assertEqual(STATE.synchronize_table(self.text), self.text)

    def test_old_opcodes_are_stable(self):
        ids = STATE.TABLES.enums(self.text)
        self.assertEqual(ids["REP_MOVS_STATE64"], 18540)
        self.assertEqual(ids["REP_OUTS_STATE32"], 18553)
        self.assertEqual(ids["ENCLU_STATE64"], 18554)
        self.assertEqual(ids["GETSEC_STATE64"], 18555)

    def test_whole_register_and_arithmetic_flag_effects(self):
        start = self.text.index("    /* 0 */ X86::EFLAGS,")
        end = self.text.index("\n  }\n};", start)
        registers = re.findall(r"X86::(\w+)", self.text[start:end])
        for name, _, _, _ in STATE.INSTRUCTIONS:
            fields = STATE.TABLES.descriptor(self.text, name)
            self.assertEqual(fields[1:4], ["0", "0", "0"])
            uses, definitions = map(int, fields[5:7])
            offset = int(fields[7].split(" + ")[1])
            self.assertEqual(registers[offset:offset + uses],
                             ["RAX", "RBX", "RCX", "RDX", "RDI"])
            self.assertEqual(registers[offset + uses:offset + uses + definitions],
                             ["RAX", "RBX", "RCX", "RDX", "RDI", "RSI", "EFLAGS"])
            for effect in ("Pseudo", "UnmodeledSideEffects", "MayLoad", "MayStore"):
                self.assertIn("MCID::" + effect, fields[9])

    def test_lost_effect_is_rejected(self):
        corrupted = re.sub(
            r"\|\(1ULL<<MCID::MayStore\)(?=, 0x0ULL \},  // Inst #\d+ = ENCLU_STATE64)",
            "", self.text)
        self.assertNotEqual(corrupted, self.text)
        with self.assertRaisesRegex(ValueError, "stale.*instruction"):
            STATE.synchronize_table(corrupted)

    def test_native_signature_carries_both_flag_words(self):
        for name in ("enclu", "getsec"):
            self.assertEqual(
                NATIVE.FIRST_IMMEDIATE_SIGNATURES[name + "_bounded_state"],
                ("STRUCT5", "I64", "I64", "I64", "I64", "I16",
                 "I64", "I64", "I64", "I64", "I16"))


if __name__ == "__main__":
    unittest.main()
