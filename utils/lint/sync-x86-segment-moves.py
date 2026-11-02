#!/usr/bin/env python3
"""Synchronize the MOV-to-segment pseudos, retaining existing opcode IDs.

X86InstrCompiler.td in the full LLVM fork owns the source records. The custom
inserter replaces each pseudo with one MOV and an explicit segment definition.
"""

import argparse
import importlib.util
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "mpx", Path(__file__).with_name("sync-x86-mpx-instructions.py"))
MPX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MPX)
TABLES = MPX.TABLES
SCHEMA = (("WRITE_SEGMENT_REG", ("i32imm", "GR16")),
          ("WRITE_SEGMENT_MEM", ("i32imm", "i16mem")))
INSTRUCTIONS = tuple((name, "WRITE_SYSREG64", None, None) for name, _ in SCHEMA)


def synchronize_table(text):
    descriptors, types = {}, {}
    for name, kinds in SCHEMA:
        text, operand_offset, types[name] = MPX.operands(text, kinds)
        fields = TABLES.descriptor(text, "WRITE_SYSREG64")
        fields[1:4] = [str(sum(5 if k.endswith("mem") else 1 for k in kinds)), "0", "0"]
        fields[5:8] = ["0", "0", "X86ImpOpBase + 0"]
        fields[8] = str(operand_offset)
        flags = ("Pseudo", "UsesCustomInserter", "UnmodeledSideEffects", "MayLoad", "MayStore")
        fields[9] = "0" + "".join(f"|(1ULL<<MCID::{f})" for f in flags) + ", 0x0ULL"
        descriptors[name] = fields
    return TABLES.synchronize_instructions(text, INSTRUCTIONS, {}, descriptors, types)


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
            parser.exit(1, "segment move tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
