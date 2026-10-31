#!/usr/bin/env python3
"""Synchronize the Hyper-V page call pseudo, retaining every opcode ID."""

import argparse
import importlib.util
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "mpx", Path(__file__).with_name("sync-x86-mpx-instructions.py"))
MPX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MPX)
TABLES, EFFECTS = MPX.TABLES, MPX.EFFECTS
INSTRUCTIONS = (("HYPERV_CALL64", "WRITE_SYSREG64", None, None),)


def synchronize_table(text):
    text, operand_offset, type_offset = MPX.operands(
        text, ("i32imm",))
    uses = ("R9", "R10", "R11", "RSP")
    definitions = ("RAX", "RCX", "RDX", "R8", "XMM0", "XMM1", "XMM2",
                   "XMM3", "XMM4", "XMM5", "EFLAGS")
    text, implicit = EFFECTS.implicit_sequence(text, uses + definitions)
    fields = TABLES.descriptor(text, "WRITE_SYSREG64")
    fields[1:4] = ["1", "0", "0"]
    fields[5:8] = [str(len(uses)), str(len(definitions)), f"X86ImpOpBase + {implicit}"]
    fields[8] = str(operand_offset)
    fields[9] = "0" + "".join(f"|(1ULL<<MCID::{f})" for f in
                             ("Pseudo", "Call", "UnmodeledSideEffects", "MayLoad", "MayStore")) + ", 0x0ULL"
    return TABLES.synchronize_instructions(
        text, INSTRUCTIONS, {}, {"HYPERV_CALL64": fields},
        {"HYPERV_CALL64": type_offset})


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
            parser.exit(1, "Hyper-V context tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
