#!/usr/bin/env python3
"""Maintain the VMCS access effects in NeverC's pruned instruction table.

The full LLVM fork generates these from X86InstrVMX.td. NeverC checks in a
pruned table instead; this focused generator is the authority for its VMREAD
and VMWRITE explicit definitions and implicit EFLAGS definitions. No opcode,
operand order, instruction encoding, or intrinsic ID is changed.
"""

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TABLE = ROOT / "llvm/lib/Target/X86/X86GenInstrInfo.inc"
DEFS = {"VMREAD64rr": 1, "VMREAD64mr": 0,
        "VMWRITE64rr": 0, "VMWRITE64rm": 0}


def synchronize(text):
    if not re.search(r"/\* 0 \*/ X86::EFLAGS,", text):
        raise ValueError("implicit-register table no longer starts with EFLAGS")
    for name, explicit_defs in DEFS.items():
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
            raise ValueError(f"missing VMCS side effect: {name}")
        memory_effect = "MCID::MayStore" if name.endswith("mr") else "MCID::MayLoad"
        if name.endswith(("mr", "rm")) and memory_effect not in fields[9]:
            raise ValueError(f"missing memory effect: {name}")
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
        parser.exit(1, "VMCS effects are stale; run with --write\n")


if __name__ == "__main__":
    main()
