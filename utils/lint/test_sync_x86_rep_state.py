#!/usr/bin/env python3
"""Protect full-register REP completion and indivisible DF-window effects."""
import importlib.util
import re
import unittest
from pathlib import Path


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


REP = module("rep_state", "sync-x86-rep-state.py")
NATIVE = module("native", "sync-x86-native-intrinsics.py")


class RepStateTest(unittest.TestCase):
    def setUp(self):
        self.text = (REP.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()

    def test_checked_in_tables_match_and_are_idempotent(self):
        for path, text in REP.synchronize().items():
            self.assertEqual(path.read_text(), text, str(path))
        self.assertEqual(REP.synchronize_table(self.text), self.text)

    def test_old_opcode_ids_are_unchanged(self):
        ids = REP.TABLES.enums(self.text)
        for name, value in (("INVLPGA64", 1442), ("VMLOAD64", 10225),
                            ("AMX_MEMORY_NATIVE", 18538), ("AMX_ROW_NATIVE", 18539)):
            self.assertEqual(ids[name], value)

    def registers(self, name):
        fields = REP.TABLES.descriptor(self.text, name)
        start = self.text.index("    /* 0 */ X86::EFLAGS,")
        end = self.text.index("\n  }\n};", start)
        registers = re.findall(r"X86::(\w+)", self.text[start:end])
        offset = int(fields[7].split(" + ")[1])
        uses, definitions = map(int, fields[5:7])
        return (registers[offset:offset + uses],
                registers[offset + uses:offset + uses + definitions])

    def test_comparison_keeps_the_full_state_and_flag_scratch(self):
        self.assertEqual(self.registers("REP_CMPS_STATE64"),
                         (["RSI", "RDI", "RCX", "RAX"],
                          ["RSI", "RDI", "RCX", "RAX", "EFLAGS", "DF"]))
        self.assertEqual(self.registers("REP_SCAS_STATE64"),
                         (["RDI", "RCX", "RAX", "RDX"],
                          ["RDI", "RCX", "RAX", "RDX", "EFLAGS", "DF"]))

    def test_narrow_addressing_does_not_narrow_the_register_contract(self):
        self.assertEqual(self.registers("REP_LODS_STATE64"),
                         (["RSI", "RCX", "RAX"], ["RSI", "RCX", "RAX", "EFLAGS", "DF"]))
        self.assertEqual(self.registers("REP_MOVS_STATE32"),
                         (["ESI", "EDI", "ECX"], ["ESI", "EDI", "ECX", "EFLAGS", "DF"]))

    def test_all_pseudos_own_direction_and_unknown_memory_effects(self):
        for name, _, _, _ in REP.INSTRUCTIONS:
            fields = REP.TABLES.descriptor(self.text, name)
            self.assertEqual(fields[1:3], ["2", "0"])
            for effect in ("Pseudo", "UnmodeledSideEffects", "MayLoad", "MayStore"):
                self.assertIn("MCID::" + effect, fields[9])
            self.assertIn("DF", self.registers(name)[1])

    def test_lost_store_effect_is_rejected(self):
        corrupted = re.sub(r"\|\(1ULL<<MCID::MayStore\)(?=, 0x0ULL \},  // Inst #\d+ = REP_CMPS_STATE64)", "", self.text)
        self.assertNotEqual(corrupted, self.text)
        with self.assertRaisesRegex(ValueError, "stale.*instruction"):
            REP.synchronize_table(corrupted)

    def test_comparison_signature_retains_zero_count_input_flags(self):
        signature, immediates = NATIVE.ADDRESS_SIGNATURES["rep_cmps_state"]
        self.assertEqual(signature, ("STRUCT4", "I64", "I64", "I64", "I16",
                                     "I64", "I64", "I64", "I8", "I16",
                                     "I32", "I32", "I32", "I32"))
        self.assertEqual(immediates, (5, 6, 7, 8))
        self.assertEqual(NATIVE.ADDRESS_SIGNATURES["rep_ins_state"][0],
                         ("STRUCT2", "I64", "I64", "I64", "I64", "I16", "I8", "I32", "I32"))


if __name__ == "__main__":
    unittest.main()
