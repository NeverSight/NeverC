#!/usr/bin/env python3
"""Synchronize the terminal SGX transfer context pseudo, retaining opcode IDs.

The full fork owns the source record in X86InstrCompiler.td. The pseudo has no
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
INSTRUCTIONS = tuple((name, "WRITE_SYSREG64", None, None) for name in
                     ("ENCLU_TRANSFER_CONTEXT64", "ENCLU_TRANSFER_AT_CONTEXT64"))


def synchronize_table(text):
    definitions = ("FS", "GS", "EFLAGS", "RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI",
                   "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15")
    text, implicit = EFFECTS.implicit_sequence(text, ("RSP",) + definitions)
    descriptors, types = {}, {}
    for name, _, _, _ in INSTRUCTIONS:
        kinds = ("GR64", "i64imm") if "_AT_" in name else ("GR64",)
        text, operand_offset, types[name] = MPX.operands(text, kinds)
        fields = TABLES.descriptor(text, "WRITE_SYSREG64")
        fields[1:4] = [str(len(kinds)), "0", "0"]
        fields[5:8] = ["1", str(len(definitions)), f"X86ImpOpBase + {implicit}"]
        fields[8] = str(operand_offset)
        fields[9] = "0" + "".join(f"|(1ULL<<MCID::{f})" for f in
                                 ("Pseudo", "UnmodeledSideEffects", "MayLoad", "MayStore", "Terminator", "Barrier")) + ", 0x0ULL"
        descriptors[name] = fields
    return TABLES.synchronize_instructions(
        text, INSTRUCTIONS, {}, descriptors, types)


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
            parser.exit(1, "SGX transfer context tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
