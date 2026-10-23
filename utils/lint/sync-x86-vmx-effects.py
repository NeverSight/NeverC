#!/usr/bin/env python3
"""Maintain VMX access and management effects in NeverC's instruction table.

The full LLVM fork generates these from X86InstrVMX.td. NeverC checks in a
pruned table instead; this focused generator is the authority for explicit
definitions, implicit EFLAGS definitions, and operand memory effects. No opcode,
operand order, instruction encoding, or intrinsic ID is changed.
"""

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TABLE = ROOT / "llvm/lib/Target/X86/X86GenInstrInfo.inc"
# Explicit definitions and the instruction's explicit memory operand effect.
EFFECTS = {
    "VMREAD64rr": (1, None), "VMREAD64mr": (0, "MayStore"),
    "VMWRITE64rr": (0, None), "VMWRITE64rm": (0, "MayLoad"),
    "VMCLEARm": (0, "MayLoad"), "VMPTRLDm": (0, "MayLoad"),
    "VMPTRSTm": (0, "MayStore"), "VMXON": (0, "MayLoad"),
    "VMXOFF": (0, None), "VMLAUNCH": (0, None), "VMRESUME": (0, None),
}


def synchronize(text):
    if not re.search(r"/\* 0 \*/ X86::EFLAGS,", text):
        raise ValueError("implicit-register table no longer starts with EFLAGS")
    for name, (explicit_defs, memory_effect) in EFFECTS.items():
        pattern = re.compile(
            r"^(\s*\{ )([^\n]+?)( \},  // Inst #\d+ = " + name + r")$",
            re.MULTILINE)
        match = pattern.search(text)
        if not match:
            raise ValueError(f"missing instruction descriptor: {name}")
        fields = match.group(2).split(",\t")
        if len(fields) != 10:
            raise ValueError(f"unexpected instruction descriptor: {name}")
        fields[2] = str(explicit_defs)
        fields[5:8] = ["0", "1", "X86ImpOpBase + 0"]
        if "MCID::UnmodeledSideEffects" not in fields[9]:
            raise ValueError(f"missing VMX side effect: {name}")
        flags, encoding = fields[9].split(", ", 1)
        flags = re.sub(r"\|\(1ULL<<MCID::May(?:Load|Store)\)", "", flags)
        if memory_effect:
            flags = flags.replace("0|", f"0|(1ULL<<MCID::{memory_effect})|", 1)
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
        parser.exit(1, "VMX effects are stale; run with --write\n")


if __name__ == "__main__":
    main()
