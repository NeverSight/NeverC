#!/usr/bin/env python3
"""Synchronize the explicit KVM register ABI pseudos without renumbering IDs."""

import argparse
import importlib.util
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "mpx", Path(__file__).with_name("sync-x86-mpx-instructions.py"))
MPX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MPX)
TABLES, EFFECTS = MPX.TABLES, MPX.EFFECTS
INSTRUCTIONS = tuple((f"{op}_KVM{bits}", op, None, None)
                     for op in ("VMCALL", "VMMCALL") for bits in (32, 64))


def synchronize_table(text):
    descriptors = {}
    for name, prototype, _, _ in INSTRUCTIONS:
        prefix = "R" if name.endswith("64") else "E"
        uses = tuple(prefix + reg for reg in ("AX", "BX", "CX", "DX", "SI"))
        definitions = (prefix + "AX", "EFLAGS")
        text, implicit = EFFECTS.implicit_sequence(text, uses + definitions)
        fields = TABLES.descriptor(text, prototype)
        fields[1:4] = ["0", "0", "0"]
        fields[5:8] = ["5", "2", f"X86ImpOpBase + {implicit}"]
        fields[9] = "0" + "".join(f"|(1ULL<<MCID::{flag})" for flag in
                                  ("Pseudo", "UnmodeledSideEffects", "MayLoad", "MayStore")) + ", 0x0ULL"
        descriptors[name] = fields
    return TABLES.synchronize_instructions(text, INSTRUCTIONS, {}, descriptors)


def synchronize():
    path = TABLES.TARGET / "X86GenInstrInfo.inc"
    text = synchronize_table(path.read_text())
    outputs = {path: text}
    for name in ("X86GenAsmWriter.inc", "X86GenAsmWriter1.inc"):
        path = TABLES.TARGET / name
        outputs[path] = TABLES.synchronize_writer(path.read_text(), TABLES.enums(text), INSTRUCTIONS)
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    for path, text in synchronize().items():
        if text != path.read_text():
            if args.write:
                path.write_text(text)
            else:
                parser.exit(1, "KVM hypercall tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
