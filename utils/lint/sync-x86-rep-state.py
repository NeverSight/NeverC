#!/usr/bin/env python3
"""Append indivisible REP state pseudos without changing existing opcodes.

The full fork owns the definitions in X86InstrMisc.td. Natural-width registers
retain hardware partial-write behavior; EFLAGS and DF belong to the whole
MC-expanded operation, including its direction and comparison flag handling.
"""

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
OPERATIONS = ("MOVS", "STOS", "LODS", "CMPS", "SCAS", "INS", "OUTS")
INSTRUCTIONS = tuple((f"REP_{op}_STATE{bits}", "WRITE_SYSREG64", None, None)
                     for bits in (64, 32) for op in OPERATIONS)


def synchronize_table(text):
    overrides, offsets = {}, {}
    for bits in (64, 32):
        prefix = "R" if bits == 64 else "E"
        si, di, cx, ax, dx = (prefix + r for r in ("SI", "DI", "CX", "AX", "DX"))
        for operation in OPERATIONS:
            name = f"REP_{operation}_STATE{bits}"
            if operation in ("MOVS", "CMPS"):
                uses, definitions = (si, di, cx), (si, di, cx)
                if operation == "CMPS":
                    uses += (ax,)
                    definitions += (ax,)
            elif operation == "LODS":
                uses = definitions = (si, cx, ax)
            elif operation == "STOS":
                uses, definitions = (di, cx, ax), (di, cx)
            elif operation == "SCAS":
                uses = definitions = (di, cx, ax, dx)
            else:
                index = di if operation == "INS" else si
                uses, definitions = (index, cx, dx), (index, cx)
            definitions += ("EFLAGS", "DF")
            text, implicit = EFFECTS.implicit_sequence(text, uses + definitions)
            text, operands, offsets[name] = MPX.operands(text, ("i32imm", "GR32"))
            fields = TABLES.descriptor(text, "WRITE_SYSREG64")
            fields[1:4] = ["2", "0", "0"]
            fields[4] = TABLES.descriptor(text, "REP_MOVSB_64")[4]
            fields[5:8] = [str(len(uses)), str(len(definitions)),
                            f"X86ImpOpBase + {implicit}"]
            fields[8] = str(operands)
            fields[9] = "0" + "".join(f"|(1ULL<<MCID::{flag})" for flag in
                                      ("Pseudo", "UnmodeledSideEffects", "MayLoad", "MayStore")) + ", 0x0ULL"
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
                parser.exit(1, "REP state tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
