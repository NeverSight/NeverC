#!/usr/bin/env python3
"""Keep the native memory carrier class until final MC lowering.

The full fork defines NATIVE_ADDRESS64 in X86InstrCompiler.td. NeverC keeps
only its x64 form and appends it without changing existing opcode identities.
This is not a copy for machine optimization: widening its result class could
change a native operand's default segment or corrupt a reserved stack base.
"""

import argparse
import importlib.util
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "mpx", Path(__file__).with_name("sync-x86-mpx-instructions.py"))
MPX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MPX)
TABLES = MPX.TABLES
INSTRUCTIONS = (("NATIVE_ADDRESS64", "MOV64rr", None, None),)


def synchronize_table(text):
    text, operand_offset, type_offset = MPX.operands(text, ("GR64_ABCD", "GR64"))
    fields = TABLES.descriptor(text, "MOV64rr")
    fields[1:4] = ["2", "1", "0"]
    fields[5:8] = ["0", "0", "X86ImpOpBase + 0"]
    fields[8] = str(operand_offset)
    fields[9] = "0|(1ULL<<MCID::Pseudo), 0x0ULL"
    return TABLES.synchronize_instructions(
        text, INSTRUCTIONS, {}, {"NATIVE_ADDRESS64": fields},
        {"NATIVE_ADDRESS64": type_offset})


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
            parser.exit(1, "Native address carrier tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
