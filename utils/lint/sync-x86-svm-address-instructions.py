#!/usr/bin/env python3
"""Append SVM address-size pseudos without renumbering existing opcodes.

The full fork owns these in X86InstrSVM.td. They retain exact EAX/ECX inputs
and VMCB memory effects until X86MCInstLower emits the real opcode with 67.
There is no new assembler mnemonic or machine-code encoding.
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
INSTRUCTIONS = tuple((op + "64_ADDR32", op + "64", None, None)
                     for op in ("VMLOAD", "VMSAVE", "INVLPGA"))


def synchronize_table(text):
    overrides = {}
    for name, prototype, _, _ in INSTRUCTIONS:
        uses = ("EAX", "ECX") if prototype == "INVLPGA64" else ("EAX",)
        text, offset = EFFECTS.implicit_sequence(text, uses)
        fields = TABLES.descriptor(text, prototype)
        fields[5:8] = [str(len(uses)), "0", f"X86ImpOpBase + {offset}"]
        flags = fields[9].split(", ")[0]
        fields[9] = flags + "|(1ULL<<MCID::Pseudo), 0x0ULL"
        overrides[name] = fields
    return TABLES.synchronize_instructions(text, INSTRUCTIONS, {}, overrides)


def synchronize():
    target = TABLES.TARGET
    table = target / "X86GenInstrInfo.inc"
    text = synchronize_table(table.read_text())
    outputs = {table: text}
    ids = TABLES.enums(text)
    for name in ("X86GenAsmWriter.inc", "X86GenAsmWriter1.inc"):
        path = target / name
        outputs[path] = TABLES.synchronize_writer(path.read_text(), ids, INSTRUCTIONS)
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
            parser.exit(1, "SVM address-size pseudos are stale; run with --write\n")


if __name__ == "__main__":
    main()
