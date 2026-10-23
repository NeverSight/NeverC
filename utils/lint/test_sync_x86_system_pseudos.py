#!/usr/bin/env python3
"""Check stable pseudo installation and rejection of split-table drift."""

import importlib.util
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "system_pseudos", Path(__file__).with_name("sync-x86-system-pseudos.py"))
PSEUDOS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PSEUDOS)

BASE = r'''
    PHI = 0,
    INSTRUCTION_LIST_END = 1
MCInstrDesc Insts[1];
MCOperandInfo OperandInfo[1];
extern const X86InstrTable X86Descs = {
  {
    { 0 }, // Existing descriptor must stay unchanged.
  }, {
    /* 0 */ { -1, 0, MCOI::OPERAND_UNKNOWN, 0 },
  }, {
    /* 0 */
    /* 0 */ X86::EFLAGS,
  }
};
extern const char X86InstrNameData[] = {
  /* 0 */ "PHI\0"
};
extern const unsigned X86InstrNameIndices[] = {
    0U,
};
InitMCInstrInfo(X86Descs.Insts, X86InstrNameIndices, X86InstrNameData, nullptr, nullptr, 1);
II->InitMCInstrInfo(X86Descs.Insts, X86InstrNameIndices, X86InstrNameData, nullptr, nullptr, 1);
static const uint16_t Offsets[] = {
    /* PHI */
    0,
};
static const int16_t OpcodeOperandTypes[] = {
    /* PHI */
    -1,
};
static const uint16_t RequiredFeaturesRefs[] = {
    CEFBS_None, // PHI = 0
  };

  assert(Opcode < 1);
'''


class SystemPseudoTests(unittest.TestCase):
    def test_install_keeps_existing_opcode_and_descriptor(self):
        result = PSEUDOS.synchronize(BASE)
        self.assertIn("PHI = 0,", result)
        self.assertIn("{ 0 }, // Existing descriptor must stay unchanged.", result)
        self.assertIn("WRITE_SYSREG64\t= 1,", result)
        self.assertIn("MCInstrDesc Insts[2];", result)
        self.assertIn("MCOperandInfo OperandInfo[3];", result)
        self.assertEqual(PSEUDOS.synchronize(result), result)

    def test_checked_in_table_is_current(self):
        text = PSEUDOS.TABLE.read_text()
        self.assertEqual(PSEUDOS.synchronize(text), text)

    def test_rejects_stale_effects(self):
        text = PSEUDOS.synchronize(BASE).replace("|(1ULL<<MCID::UnmodeledSideEffects)", "")
        with self.assertRaisesRegex(ValueError, "descriptor"):
            PSEUDOS.synchronize(text)

    def test_rejects_stale_name(self):
        text = PSEUDOS.synchronize(BASE).replace("4U,", "0U,")
        with self.assertRaisesRegex(ValueError, "name"):
            PSEUDOS.synchronize(text)

    def test_rejects_stale_operand_type(self):
        text = PSEUDOS.synchronize(BASE).replace("i32imm, GR64,", "i32imm, GR32,")
        with self.assertRaisesRegex(ValueError, "operand types"):
            PSEUDOS.synchronize(text)


if __name__ == "__main__":
    unittest.main()
