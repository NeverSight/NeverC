#!/usr/bin/env python3
"""Append indivisible bounded SGX/SMX state pseudos, retaining old opcodes."""

import argparse
import importlib.util
from pathlib import Path


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


MPX = module("mpx", "sync-x86-mpx-instructions.py")
TABLES, EFFECTS = MPX.TABLES, MPX.EFFECTS
INSTRUCTIONS = tuple((name + "_STATE64", "ENCLU", None, None)
                     for name in ("ENCLU", "GETSEC"))


def synchronize_table(text):
    uses = ("RAX", "RBX", "RCX", "RDX", "RDI")
    definitions = uses + ("RSI", "EFLAGS")
    text, implicit = EFFECTS.implicit_sequence(text, uses + definitions)
    overrides = {}
    for name, prototype, _, _ in INSTRUCTIONS:
        fields = TABLES.descriptor(text, prototype)
        fields[1:4] = ["0", "0", "0"]
        fields[4] = TABLES.descriptor(text, "REP_MOVSB_64")[4]
        fields[5:8] = [str(len(uses)), str(len(definitions)),
                        f"X86ImpOpBase + {implicit}"]
        fields[9] = "0" + "".join(f"|(1ULL<<MCID::{flag})" for flag in
                                  ("Pseudo", "UnmodeledSideEffects", "MayLoad", "MayStore")) + ", 0x0ULL"
        overrides[name] = fields
    return TABLES.synchronize_instructions(text, INSTRUCTIONS, {}, overrides)


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
        if text != path.read_text():
            if args.write:
                path.write_text(text)
            else:
                parser.exit(1, "bounded leaf state tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
