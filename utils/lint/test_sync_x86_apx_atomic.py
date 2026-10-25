#!/usr/bin/env python3
"""Protect APX native atomic effects, operand ties and generated identities."""
import importlib.util
from pathlib import Path
import re
import unittest


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


APX = module("apx", "sync-x86-apx-atomic.py")
NATIVE = module("native", "sync-x86-native-intrinsics.py")


class APXAtomicTablesTest(unittest.TestCase):
    def setUp(self):
        self.text = (APX.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()

    def test_generated_tables_match(self):
        for path, expected in APX.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))
        self.assertEqual(APX.synchronize_table(self.text), self.text)

    def test_old_identities_and_new_appended_opcodes(self):
        ids = APX.TABLES.enums(self.text)
        for name, value in (("AADD32mr", 351), ("CMPCCXADDmr32_EVEX", 1085),
                            ("ENCLU_STATE64", 18554), ("GETSEC_STATE64", 18555)):
            self.assertEqual(ids[name], value)
        self.assertEqual(len({ids[n] for n, *_ in APX.INSTRUCTIONS}), 12)
        self.assertTrue(all(ids[n] > 18555 for n, *_ in APX.INSTRUCTIONS))

    def test_atomic_transactions_have_both_memory_effects(self):
        for name, prototype, *_ in APX.INSTRUCTIONS:
            fields = APX.TABLES.descriptor(self.text, name)
            for flag in ("MayLoad", "MayStore"):
                self.assertIn("MCID::" + flag, fields[9])
            if name.startswith("APX_"):
                self.assertIn("MCID::UnmodeledSideEffects", fields[9])
                compare = "CMPCC" in name
                self.assertEqual(fields[5:7], ["0", str(int(compare))])
                if compare:
                    native = APX.TABLES.descriptor(self.text, prototype)
                    self.assertEqual(fields[8], native[8], "comparison input must remain tied to result")
            else:
                flags = int(fields[9].split(", ")[1].removesuffix("ULL"), 16)
                self.assertEqual((flags >> 29) & 3, 3)
                self.assertEqual((flags >> 13) & 15, 8)

    def test_mutated_effect_is_rejected(self):
        corrupted = re.sub(r"\|\(1ULL<<MCID::MayLoad\)(?=\|\(1ULL<<MCID::MayStore\), 0x0ULL \},  // Inst #\d+ = APX_RAO_NATIVE32)", "", self.text)
        self.assertNotEqual(corrupted, self.text)
        with self.assertRaisesRegex(ValueError, "stale.*instruction"):
            APX.synchronize_table(corrupted)

    def test_controls_are_immediate_and_signatures_have_no_pointer_alias_promise(self):
        for bits in (32, 64):
            word = f"I{bits}"
            for op in ("aadd", "aand", "aor", "axor"):
                self.assertEqual(NATIVE.ADDRESS_SIGNATURES[f"{op}{bits}_address"],
                                 (("Done", "I64", word, "I32", "I32"), (2, 3)))
            self.assertEqual(NATIVE.ADDRESS_SIGNATURES[f"cmpccxadd{bits}_address"],
                             ((word, "I64", word, word, "I32", "I32", "I32"), (3, 4, 5)))


if __name__ == "__main__":
    unittest.main()

