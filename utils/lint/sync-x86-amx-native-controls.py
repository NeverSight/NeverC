#!/usr/bin/env python3
"""Append physical AMX address/encoding pseudos, preserving old opcode IDs.

The full fork owns their TableGen definitions in X86InstrAMX.td. These pseudos
survive encoding compression and lower to existing instructions only at MC.
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
INSTRUCTIONS = (("AMX_MEMORY_NATIVE", "WRITE_SYSREG64", None, None),
                ("AMX_ROW_NATIVE", "WRITE_SYSREG64", None, None))


def synchronize_table(text):
    text, bank = EFFECTS.implicit_sequence(text, tuple(f"TMM{i}" for i in range(8)))
    overrides, offsets = {}, {}
    for name, prototype, _, _ in INSTRUCTIONS:
        row = name == "AMX_ROW_NATIVE"
        kinds = ("VR512", "i32imm", "GR32") if row else ("i32imm", "i8mem")
        text, operand_offset, offsets[name] = MPX.operands(text, kinds)
        fields = TABLES.descriptor(text, prototype)
        fields[1:4] = [str(3 if row else 6), str(int(row)), "0"]
        fields[4] = TABLES.descriptor(text, "CLTS")[4]
        fields[5:8] = ["0", "8", f"X86ImpOpBase + {bank}"]
        fields[8] = str(operand_offset)
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
                parser.exit(1, "AMX native control tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
