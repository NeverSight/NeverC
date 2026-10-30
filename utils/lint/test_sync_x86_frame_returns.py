#!/usr/bin/env python3
"""Check frame-return terminal metadata, native encodings and signature attrs."""

import importlib.util
import unittest
from pathlib import Path


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


FRAME = module("frame", "sync-x86-frame-returns.py")
NATIVE = module("native", "sync-x86-native-intrinsics.py")


class FrameReturnsTest(unittest.TestCase):
    def test_generated_tables_match_authority(self):
        for path, expected in FRAME.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))

    def test_terminal_metadata_has_no_abi_return_epilogue(self):
        text = (FRAME.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for name in ("NATIVE_FRAME_RETURN64", "ERETS", "ERETU", "SYSENTER", "RSM"):
            fields = FRAME.TABLES.descriptor(text, name)
            for flag in ("Terminator", "Barrier", "UnmodeledSideEffects", "MayLoad", "MayStore"):
                self.assertIn(f"MCID::{flag}", fields[9], name)
            self.assertNotIn("MCID::Return", fields[9], name)
        ids = FRAME.TABLES.enums(text)
        self.assertEqual(ids["IRET16"], 1449)
        self.assertEqual(ids["LRET16"], 1705)
        self.assertEqual(ids["UIRET"], 3611)

    def test_fred_encodings_are_distinct_and_require_code64(self):
        text = (FRAME.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for name, prefix in (("ERETS", 3), ("ERETU", 2)):
            flags = int(FRAME.TABLES.descriptor(text, name)[9].split(", ")[1][:-3], 16)
            self.assertEqual((flags >> 31) & 255, 1)
            self.assertEqual((flags >> 13) & 15, 1)
            self.assertEqual((flags >> 11) & 3, prefix)
        matcher = (FRAME.TABLES.TARGET / "X86GenAsmMatcher.inc").read_text()
        for line in matcher.splitlines():
            if "X86::ERETS," in line or "X86::ERETU," in line:
                self.assertIn("AMFBS_In64BitMode", line)
                self.assertNotIn("SMAP", line)

    def test_retf_immediate_and_noreturn_attributes_coexist(self):
        impl = NATIVE.SORTER.INC_PATH.read_text()
        recipe, updated = NATIVE.nonreturning_transfer_attribute(impl, (1,))
        self.assertEqual(updated, impl)
        plain, updated = NATIVE.nonreturning_transfer_attribute(impl)
        self.assertNotEqual(plain, recipe)
        body = impl.split(f"    case {recipe}: {{\n", 1)[1].split("    }", 1)[0]
        self.assertIn("AS[0] = {2, getIntrinsicArgAttributeSet(C, 0)}", body)
        for width in (16, 32, 64):
            self.assertEqual(NATIVE.DECLARATIONS[f"retf{width}_imm"], ("Done", "PTR", "I16"))
        NATIVE.synchronize(False)


if __name__ == "__main__":
    unittest.main()
