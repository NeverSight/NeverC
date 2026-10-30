#!/usr/bin/env python3
"""Append FRED opcodes and the terminal native frame carrier with stable IDs.

X86InstrCompiler.td and X86InstrSystem.td in the full fork own these records.
NeverC targets code64, including the 16/32-bit operand-size return forms.
"""

import argparse
import importlib.util
import re
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "mpx", Path(__file__).with_name("sync-x86-mpx-instructions.py"))
MPX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MPX)
TABLES, EFFECTS = MPX.TABLES, MPX.EFFECTS
INSTRUCTIONS = (("ERETS", "CLAC", (0x01, 1, 3), "erets"),
                ("ERETU", "CLAC", (0x01, 1, 2), "eretu"),
                ("NATIVE_FRAME_RETURN64", "SYSRET64_STACK", None, None))


def synchronize_table(text):
    overrides, offsets = {}, {}
    for name, prototype, encoding, _ in INSTRUCTIONS:
        fields = TABLES.descriptor(text, prototype)
        definitions = ("CS", "SS", "RSP", "EFLAGS")
        uses = ("RSP",)
        if not encoding:
            # Immediate operands have the same MC operand class; the final
            # emitter validates the unsigned 16-bit adjustment before use.
            text, operand_offset, type_offset = MPX.operands(
                text, ("GR64", "i32imm", "i32imm"))
            fields[1:4] = ["3", "0", "0"]
            fields[8] = str(operand_offset)
            offsets[name] = type_offset
            definitions = ("CS", "SS", "DS", "ES", "FS", "GS", "EFLAGS",
                           "RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI")
            uses = ()
        text, implicit = EFFECTS.implicit_sequence(text, uses + definitions)
        fields[5:8] = [str(len(uses)), str(len(definitions)),
                       f"X86ImpOpBase + {implicit}"]
        flags = ("UnmodeledSideEffects", "MayLoad", "MayStore", "Terminator", "Barrier")
        if not encoding:
            flags += ("Pseudo",)
        tsflags = fields[9].split(", ")[1] if encoding else "0x0ULL"
        fields[9] = "0" + "".join(f"|(1ULL<<MCID::{f})" for f in flags) + ", " + tsflags
        overrides[name] = fields
    return TABLES.synchronize_instructions(text, INSTRUCTIONS, {}, overrides, offsets)


def synchronize():
    path = TABLES.TARGET / "X86GenInstrInfo.inc"
    text = synchronize_table(path.read_text())
    outputs = {path: text}
    ids = TABLES.enums(text)
    for name in ("X86GenAsmWriter.inc", "X86GenAsmWriter1.inc"):
        path = TABLES.TARGET / name
        outputs[path] = TABLES.synchronize_writer(path.read_text(), ids, INSTRUCTIONS)
    path = TABLES.TARGET / "X86GenAsmMatcher.inc"
    text = path.read_text()
    # CLAC supplies the fixed ModRM shape, not its SMAP feature requirement.
    text = re.sub(r"^.*X86::ERET[SU],[^\n]*\n", "", text, flags=re.MULTILINE)
    text = TABLES.synchronize_matcher(text, INSTRUCTIONS, {}, True)
    text = re.sub(r"(X86::ERET[SU],[^\n]*?)AMFBS_\w+",
                  r"\1AMFBS_In64BitMode", text)
    outputs[path] = text
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    for path, text in synchronize().items():
        if text == path.read_text():
            continue
        if args.write:
            path.write_text(text)
        else:
            parser.exit(1, "Native frame-return tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
