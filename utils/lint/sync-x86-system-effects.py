#!/usr/bin/env python3
"""Maintain native system instruction register and memory effects.

The full LLVM fork owns these in X86InstrSystem.td. NeverC's pruned table
retains its opcode numbers and encodings; this generator owns the matching
implicit register sequences, register definitions and memory effects.
"""

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TABLE = ROOT / "llvm/lib/Target/X86/X86GenInstrInfo.inc"
# Implicit uses, implicit definitions, explicit/implicit memory effects.
EFFECTS = {
    "CLTS": ((), (), ()),
    "WRMSRNS": (("EAX", "ECX", "EDX"), (), ()),
    "RDMSRLIST": (("RSI", "RDI", "RCX"), ("RCX",), ("MayLoad", "MayStore")),
    "WRMSRLIST": (("RSI", "RDI", "RCX"), ("RCX",), ("MayLoad",)),
}


def implicit_sequence(text, sequence):
    start = text.index("    /* 0 */ X86::EFLAGS,")
    end = text.index("\n  }\n};", start)
    registers = re.findall(r"X86::(\w+)", text[start:end])
    declared = int(re.search(r"MCPhysReg ImplicitOps\[(\d+)\];", text).group(1))
    if len(registers) != declared:
        raise ValueError("implicit-register table length disagrees with its declaration")
    for index in range(len(registers) - len(sequence) + 1):
        if tuple(registers[index:index + len(sequence)]) == sequence:
            return text, index
    addition = f"\n    /* {declared} */ " + ", ".join("X86::" + r for r in sequence) + ","
    text = text[:end] + addition + text[end:]
    text = re.sub(r"MCPhysReg ImplicitOps\[\d+\];",
                  f"MCPhysReg ImplicitOps[{declared + len(sequence)}];", text, count=1)
    return text, declared


def synchronize(text):
    for name, (uses, definitions, memory_effects) in EFFECTS.items():
        text, offset = implicit_sequence(text, uses + definitions)
        pattern = re.compile(
            r"^(\s*\{ )([^\n]+?)( \},  // Inst #\d+ = " + name + r")$", re.MULTILINE)
        match = pattern.search(text)
        if not match:
            raise ValueError(f"missing instruction descriptor: {name}")
        fields = match.group(2).split(",\t")
        if len(fields) != 10 or fields[1:3] != ["0", "0"]:
            raise ValueError(f"unexpected explicit operands: {name}")
        fields[5:8] = [str(len(uses)), str(len(definitions)), f"X86ImpOpBase + {offset}"]
        flags, encoding = fields[9].split(", ", 1)
        flags = re.sub(r"\|\(1ULL<<MCID::(?:MayLoad|MayStore|UnmodeledSideEffects)\)", "", flags)
        flags += "".join(f"|(1ULL<<MCID::{flag})" for flag in
                         (*memory_effects, "UnmodeledSideEffects"))
        fields[9] = flags + ", " + encoding
        text = text[:match.start()] + match.group(1) + ",\t".join(fields) + \
            match.group(3) + text[match.end():]
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    original = TABLE.read_text()
    updated = synchronize(original)
    if args.write:
        TABLE.write_text(updated)
    elif original != updated:
        parser.exit(1, "system instruction effects are stale; run with --write\n")


if __name__ == "__main__":
    main()
