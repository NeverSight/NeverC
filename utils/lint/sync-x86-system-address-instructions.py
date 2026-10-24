#!/usr/bin/env python3
"""Append system address-size pseudos without renumbering existing opcodes.

The full fork owns these in X86InstrSVM.td, X86InstrMisc.td, X86InstrVMX.td
and X86InstrSystem.td.
They retain exact inputs and effects until MC lowering adds the native 67.
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
IMPLICIT = tuple(op + "64" for op in ("VMLOAD", "VMSAVE", "INVLPGA", "INVLPGB"))
VMX = ("VMREAD64mr", "VMWRITE64rm", "INVEPT64", "INVVPID64",
       "VMCLEARm", "VMPTRLDm", "VMPTRSTm", "VMXON")
DESCRIPTOR = ("LKGS16m", *(f"{op}{bits}rm" for op in ("LAR", "LSL")
                           for bits in (16, 32, 64)))
STATE = ("VERRm", "VERWm", "SLDT16m", "STRm", "SMSW16m", "LLDT16m", "LTRm", "LMSW16m", "SGDT64m", "SIDT64m", "LGDT64m", "LIDT64m", "INVLPG")
INSTRUCTIONS = tuple((op + "_ADDR32", op, None, None)
                     for op in (*IMPLICIT, *VMX, *DESCRIPTOR, *STATE))


def synchronize_table(text):
    overrides = {}
    for name, prototype, _, _ in INSTRUCTIONS:
        fields = TABLES.descriptor(text, prototype)
        if prototype in IMPLICIT:
            uses = (("EAX", "EDX", "ECX") if prototype == "INVLPGB64" else
                    ("EAX", "ECX") if prototype == "INVLPGA64" else ("EAX",))
            text, offset = EFFECTS.implicit_sequence(text, uses)
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
            parser.exit(1, "System address-size pseudos are stale; run with --write\n")


if __name__ == "__main__":
    main()
