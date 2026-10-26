#!/usr/bin/env python3
"""Append native leaf pseudos that preserve the compiler's RBX frame base.

X86InstrCompiler.td in the full fork owns these records. A tied carrier stays
outside RBX and the implicit operands until MC emission; two register XCHGs
transport the input/result without exposing the borrowed base to stack spills.
"""

import argparse
import importlib.util
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "mpx", Path(__file__).with_name("sync-x86-mpx-instructions.py"))
MPX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MPX)
TABLES, EFFECTS = MPX.TABLES, MPX.EFFECTS
INSTRUCTIONS = tuple((name, "ENCLU", None, None) for name in (
    "ENCLS64_SAVE_RBX", "ENCLV64_SAVE_RBX", "ENCLU64_SAVE_RBX",
    "GETSEC64_SAVE_RBX", "ENCLU_STATE64_SAVE_RBX",
    "GETSEC_STATE64_SAVE_RBX", "CPUID64_SAVE_RBX"))


def synchronize_table(text):
    text, operand_offset, type_offset = MPX.operands(
        text, ("GR64_TCW64", "GR64_TCW64"), {1: 0})
    overrides, offsets = {}, {}
    for name, prototype, _, _ in INSTRUCTIONS:
        state = "_STATE" in name
        cpuid = name.startswith("CPUID")
        uses = ("EAX", "ECX") if cpuid else ("RAX", "RCX", "RDX")
        if state:
            uses += ("RDI",)
        definitions = ("EAX", "ECX", "EDX") if cpuid else uses + ("EFLAGS",)
        text, implicit = EFFECTS.implicit_sequence(text, uses + definitions)
        fields = TABLES.descriptor(text, prototype)
        fields[1:4] = ["2", "1", "0"]
        fields[4] = TABLES.descriptor(text, "REP_MOVSB_64")[4]
        fields[5:8] = [str(len(uses)), str(len(definitions)),
                       f"X86ImpOpBase + {implicit}"]
        fields[8] = str(operand_offset)
        fields[9] = "0" + "".join(f"|(1ULL<<MCID::{flag})" for flag in
                                  ("Pseudo", "UnmodeledSideEffects", "MayLoad", "MayStore")) + ", 0x0ULL"
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
            parser.exit(1, "Native register-window tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
