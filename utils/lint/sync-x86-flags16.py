#!/usr/bin/env python3
"""Append word FLAGS pseudos without changing existing NeverC opcode IDs.

The full LLVM fork owns RDFLAGS16/WRFLAGS16 in X86InstrMisc.td. NeverC's
x64-only target retains their GR16 carriers, stack effects and flag clobbers.
"""

import argparse
import importlib.util
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "mpx", Path(__file__).with_name("sync-x86-mpx-instructions.py"))
MPX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MPX)
TABLES, EFFECTS = MPX.TABLES, MPX.EFFECTS
INSTRUCTIONS = (("RDFLAGS16", "RDFLAGS64", None, None),
                ("WRFLAGS16", "WRFLAGS64", None, None))


def synchronize_table(text):
    text, operand_offset, type_offset = MPX.operands(text, ("GR16",))
    overrides, offsets = {}, {}
    for name, prototype, _, _ in INSTRUCTIONS:
        write = name == "WRFLAGS16"
        definitions = ("ESP", "EFLAGS", "DF") if write else ("ESP",)
        text, implicit = EFFECTS.implicit_sequence(text, ("ESP",) + definitions)
        fields = TABLES.descriptor(text, prototype)
        fields[1:4] = ["1", "0" if write else "1", "0"]
        fields[5:8] = ["1", str(len(definitions)), f"X86ImpOpBase + {implicit}"]
        fields[8] = str(operand_offset)
        overrides[name] = fields
        offsets[name] = type_offset
    return TABLES.synchronize_instructions(text, INSTRUCTIONS, {}, overrides, offsets)


def synchronize():
    path = TABLES.TARGET / "X86GenInstrInfo.inc"
    text = synchronize_table(path.read_text())
    outputs = {path: text}
    ids = TABLES.enums(text)
    for name in ("X86GenAsmWriter.inc", "X86GenAsmWriter1.inc"):
        path = TABLES.TARGET / name
        outputs[path] = TABLES.synchronize_writer(path.read_text(), ids, INSTRUCTIONS)
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
            parser.exit(1, "Word FLAGS tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
