#!/usr/bin/env python3
"""Regressions for stable native instruction effects and implicit registers."""

import importlib.util
from pathlib import Path
import unittest


SPEC = importlib.util.spec_from_file_location(
    "system_effects", Path(__file__).with_name("sync-x86-system-effects.py"))
EFFECTS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EFFECTS)


class SystemEffectsTest(unittest.TestCase):
    def test_appended_sequence_uses_the_first_overlapping_match(self):
        text = """struct Table {
  MCPhysReg ImplicitOps[4];
  {
    /* 0 */ X86::EFLAGS, X86::FPCW, X86::FPSW, X86::FPSW,
  }
};"""
        sequence = ("FPCW", "FPSW", "FPSW", "FPCW")
        updated, offset = EFFECTS.implicit_sequence(text, sequence)
        self.assertEqual(offset, 1)
        self.assertEqual(EFFECTS.implicit_sequence(updated, sequence),
                         (updated, offset))

    def test_checked_in_effects_are_stable(self):
        original = EFFECTS.TABLE.read_text()
        self.assertEqual(EFFECTS.synchronize(original), original)


if __name__ == "__main__":
    unittest.main()
