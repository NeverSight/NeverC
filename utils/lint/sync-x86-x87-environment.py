#!/usr/bin/env python3
"""Keep the four operand-size-16 x87 environment encodings synchronized.

The full fork owns these records in X86InstrFPStack.td. Preserve NeverC's
existing opcode numbers, operand shapes and the shared system-effect owner.
"""

import argparse
import importlib.util
from pathlib import Path


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


TABLES = module("tables", "sync-x86-amx-instructions.py")
EFFECTS = module("effects", "sync-x86-system-effects.py")
INSTRUCTIONS = tuple((op + "16m", op + "m", None, mnemonic + "s")
                     for op, mnemonic in (("FSTENV", "fnstenv"), ("FLDENV", "fldenv"),
                                          ("FSAVE", "fnsave"), ("FRSTOR", "frstor")))


def synchronize_table(text):
    overrides = {}
    for name, prototype, _, _ in INSTRUCTIONS:
        _, uses, definitions, effects = EFFECTS.EFFECTS[name]
        text, offset = EFFECTS.implicit_sequence(text, uses + definitions)
        fields = TABLES.descriptor(text, prototype)
        fields[5:8] = [str(len(uses)), str(len(definitions)), f"X86ImpOpBase + {offset}"]
        encoding = int(fields[9].split(", ")[1].removesuffix("ULL"), 16)
        # X86II::OpSize16. The opcode, ModRM extension and address width stay
        # those of the original instruction; this supplies exactly one 66.
        encoding = (encoding & ~(3 << 7)) | (1 << 7)
        fields[9] = "0" + "".join(f"|(1ULL<<MCID::{flag})" for flag in
                                  (*effects, "UnmodeledSideEffects")) + f", {hex(encoding)}ULL"
        overrides[name] = fields
    return TABLES.synchronize_instructions(text, INSTRUCTIONS, {}, overrides)


def synchronize():
    path = TABLES.TARGET / "X86GenInstrInfo.inc"
    table = synchronize_table(path.read_text())
    outputs = {path: table}
    ids = TABLES.enums(table)
    for name in ("X86GenAsmWriter.inc", "X86GenAsmWriter1.inc"):
        path = TABLES.TARGET / name
        outputs[path] = TABLES.synchronize_writer(path.read_text(), ids, INSTRUCTIONS)
    path = TABLES.TARGET / "X86GenAsmMatcher.inc"
    outputs[path] = TABLES.synchronize_matcher(path.read_text(), INSTRUCTIONS, {}, True)
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    for path, updated in synchronize().items():
        if path.read_text() != updated:
            if args.write:
                path.write_text(updated)
            else:
                parser.exit(1, "x87 environment tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
