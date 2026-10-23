#!/usr/bin/env python3
"""Append native LKGS register/memory forms without renumbering old opcodes.

X86InstrSystem.td in the full fork owns the instruction definitions. Reuse the
existing append-only table machinery and LLDT's identical operand/printer
shapes, with LKGS's F2 0F 00 /6 encoding and long-mode requirement. The table
engine takes an explicit schema; this module does not modify AMX's schema.
"""

import argparse
import importlib.util
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "x86_tables", Path(__file__).with_name("sync-x86-amx-instructions.py"))
TABLES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TABLES)
INSTRUCTIONS = (
    ("LKGS16r", "LLDT16r", (0x00, 1, 3, 54), "lkgs"),
    ("LKGS16m", "LLDT16m", (0x00, 1, 3, 38), "lkgs"),
)


def synchronize():
    target = TABLES.TARGET
    table = target / "X86GenInstrInfo.inc"
    updated = TABLES.synchronize_instructions(table.read_text(), INSTRUCTIONS, {})
    ids = TABLES.enums(updated)
    outputs = {table: updated}
    for name in ("X86GenAsmWriter.inc", "X86GenAsmWriter1.inc"):
        path = target / name
        outputs[path] = TABLES.synchronize_writer(path.read_text(), ids, INSTRUCTIONS)
    path = target / "X86GenAsmMatcher.inc"
    outputs[path] = TABLES.synchronize_matcher(path.read_text(), INSTRUCTIONS, {}, True)
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    for path, updated in synchronize().items():
        if updated == path.read_text():
            continue
        if args.write:
            path.write_text(updated)
        else:
            parser.exit(1, "LKGS instruction tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
