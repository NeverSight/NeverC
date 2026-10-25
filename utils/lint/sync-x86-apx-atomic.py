#!/usr/bin/env python3
"""Append APX native atomic opcodes and indivisible encoding pseudos.

The full fork owns X86InstrRAOINT.td. Preserve existing pruned opcode IDs,
operand ties, register classes and instruction-writer identities.
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
TABLES = MPX.TABLES
NATIVE = tuple((op + str(bits) + "mr_EVEX", op + str(bits) + "mr", None, op.lower())
               for op in ("AADD", "AAND", "AOR", "AXOR") for bits in (32, 64))
PSEUDOS = tuple((f"APX_{op}_NATIVE{bits}", f"CMPCCXADDmr{bits}_EVEX" if op == "CMPCCXADD" else "WRITE_SYSREG64", None, None)
               for op in ("RAO", "CMPCCXADD") for bits in (32, 64))
INSTRUCTIONS = NATIVE + PSEUDOS


def synchronize_table(text):
    overrides, offsets = {}, {}
    for name, prototype, _, _ in NATIVE:
        fields = TABLES.descriptor(text, prototype)
        tsflags = int(fields[9].split(", ")[1].removesuffix("ULL"), 16)
        # X86II EVEX=3, T_MAP4=8. Other encoding bits retain the legacy widths.
        tsflags = (tsflags & ~((3 << 29) | (15 << 13))) | (3 << 29) | (8 << 13)
        fields[9] = "0|(1ULL<<MCID::MayLoad)|(1ULL<<MCID::MayStore), " + hex(tsflags) + "ULL"
        overrides[name] = fields
    for name, prototype, _, _ in PSEUDOS:
        compare = "CMPCCXADD" in name
        bits = name[-2:]
        kinds = (f"GR{bits}", f"GR{bits}", f"i{bits}mem", f"GR{bits}", "i32imm") if compare else ("i32imm", f"i{bits}mem", f"GR{bits}")
        text, operand_offset, offsets[name] = MPX.operands(text, kinds)
        fields = TABLES.descriptor(text, prototype)
        fields[1:4] = [str(9 if compare else 7), str(int(compare)), "0"]
        fields[4] = TABLES.descriptor(text, "CMPCCXADDmr32_EVEX")[4]
        fields[5:8] = ["0", str(int(compare)), "X86ImpOpBase + 0"]
        # CMP has the native operand sequence, including its tied input 1.
        if not compare:
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
    path = TABLES.TARGET / "X86GenAsmMatcher.inc"
    outputs[path] = TABLES.synchronize_matcher(path.read_text(), NATIVE, {}, require64=True)
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
                parser.exit(1, "APX atomic tables are stale; run with --write\n")


if __name__ == "__main__":
    main()

