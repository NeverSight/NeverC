#!/usr/bin/env python3
"""Synchronize the compiler-owned VMRUN context pseudo, retaining opcode IDs.

The full fork owns the source record in X86InstrSVM.td. The pseudo has no
assembler spelling; its final emitter owns register transport and the opcode.
"""

import argparse
import importlib.util
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "mpx", Path(__file__).with_name("sync-x86-mpx-instructions.py"))
MPX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MPX)
TABLES, EFFECTS = MPX.TABLES, MPX.EFFECTS
INSTRUCTIONS = (("VMRUN_CONTEXT64", "WRITE_SYSREG64", None, None),)


def synchronize_table(text):
    text, operand_offset, type_offset = MPX.operands(
        text, ("GR64", "i64mem", "i32imm"))
    definitions = ("RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RBP",
                   "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15", "EFLAGS")
    text, implicit = EFFECTS.implicit_sequence(text, ("RSP",) + definitions)
    fields = TABLES.descriptor(text, "WRITE_SYSREG64")
    fields[1:4] = ["7", "0", "0"]
    fields[5:8] = ["1", str(len(definitions)), f"X86ImpOpBase + {implicit}"]
    fields[8] = str(operand_offset)
    fields[9] = "0" + "".join(f"|(1ULL<<MCID::{f})" for f in
                             ("Pseudo", "UnmodeledSideEffects", "MayLoad", "MayStore")) + ", 0x0ULL"
    return TABLES.synchronize_instructions(
        text, INSTRUCTIONS, {}, {"VMRUN_CONTEXT64": fields},
        {"VMRUN_CONTEXT64": type_offset})


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
            parser.exit(1, "VMRUN context tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
