#!/usr/bin/env python3
"""Synchronize terminal far-jump carriers from the full LLVM fork contract.

Keep existing opcode IDs stable. Returning far calls use existing native MC
records and a platform call-preserved register mask supplied by DAG lowering.
"""
import argparse
import importlib.util
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "mpx", Path(__file__).with_name("sync-x86-mpx-instructions.py"))
MPX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MPX)
TABLES, EFFECTS = MPX.TABLES, MPX.EFFECTS
SCHEMA = (
    ("NATIVE_FAR_JUMP64", ("GR64_NOSP", "GR64", "i32imm")),
)
INSTRUCTIONS = tuple((name, "NATIVE_FRAME_RETURN64", None, None)
                     for name, _ in SCHEMA)


def synchronize():
    path = TABLES.TARGET / "X86GenInstrInfo.inc"
    text = path.read_text()
    overrides, offsets = {}, {}
    for name, kinds in SCHEMA:
        text, operand_offset, offsets[name] = MPX.operands(text, kinds)
        definitions = ("CS", "SS", "DS", "ES", "FS", "GS", "EFLAGS") + tuple(
            "R" + reg for reg in ("AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"))
        text, implicit = EFFECTS.implicit_sequence(text, definitions)
        fields = TABLES.descriptor(text, "NATIVE_FRAME_RETURN64")
        fields[1:4] = [str(len(kinds)), "0", "0"]
        fields[5:8] = ["0", str(len(definitions)), f"X86ImpOpBase + {implicit}"]
        fields[8] = str(operand_offset)
        overrides[name] = fields
    text = TABLES.synchronize_instructions(text, INSTRUCTIONS, {}, overrides, offsets)
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
                parser.exit(1, "Native far-transfer tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
