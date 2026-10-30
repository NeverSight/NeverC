#!/usr/bin/env python3
"""Keep SYSRET's destination stack in a carrier until final MC emission.

The full fork owns these terminals in X86InstrCompiler.td. NeverC appends
their descriptors without changing existing instruction IDs. They abandon
the compiler frame and must never request ordinary ABI return epilogues.
"""

import argparse
import importlib.util
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "mpx", Path(__file__).with_name("sync-x86-mpx-instructions.py"))
MPX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MPX)
TABLES, EFFECTS = MPX.TABLES, MPX.EFFECTS
INSTRUCTIONS = (("SYSRET32_STACK", "SYSRET", None, None),
                ("SYSRET64_STACK", "SYSRET64", None, None))


def synchronize_table(text):
    text, operand_offset, type_offset = MPX.operands(text, ("GR64",))
    overrides, offsets = {}, {}
    for name, prototype, _, _ in INSTRUCTIONS:
        narrow = name == "SYSRET32_STACK"
        uses = ("ECX", "R11D") if narrow else ("RCX", "R11")
        definitions = ("CS", "SS", "EFLAGS", "RAX", "RCX", "RDX", "RBX",
                       "RSP", "RBP", "RSI", "RDI") if narrow else (
                           "CS", "SS", "EFLAGS", "RSP")
        text, implicit = EFFECTS.implicit_sequence(text, uses + definitions)
        fields = TABLES.descriptor(text, prototype)
        fields[1:4] = ["1", "0", "0"]
        fields[5:8] = [str(len(uses)), str(len(definitions)),
                       f"X86ImpOpBase + {implicit}"]
        fields[8] = str(operand_offset)
        fields[9] = "0" + "".join(f"|(1ULL<<MCID::{flag})" for flag in
                                  ("Pseudo", "UnmodeledSideEffects", "MayLoad",
                                   "MayStore", "Terminator", "Barrier")) + ", 0x0ULL"
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
            parser.exit(1, "System-return tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
