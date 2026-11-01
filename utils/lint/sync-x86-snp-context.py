#!/usr/bin/env python3
"""Append SNP completion and SEV-ES opcodes without renumbering old entries."""

import argparse
import importlib.util
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "mpx", Path(__file__).with_name("sync-x86-mpx-instructions.py"))
MPX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MPX)
TABLES, EFFECTS = MPX.TABLES, MPX.EFFECTS
SCHEMA = (
    ("RMPREAD", "RMPUPDATE", (1, 1, 3, 125), "rmpread",
     ("RAX", "RCX"), ("EAX", "EFLAGS")),
    ("RMPOPT", "RMPUPDATE", (1, 1, 3, 124), "rmpopt",
     ("RAX", "RCX"), ("EFLAGS",)),
    ("RMPCHKD", "RMPUPDATE", (1, 1, 2, 124), "rmpchkd",
     ("RAX", "RCX"), ("RAX", "RCX", "EFLAGS")),
    ("VMGEXIT", "VMMCALL", (1, 1, 2), "vmgexit", ("RAX",), ()),
    ("SNP_CONTEXT64", "WRITE_SYSREG64", None, None,
     ("R10", "R11"), ("RAX", "RCX", "RDX", "EFLAGS")),
)
INSTRUCTIONS = tuple(row[:4] for row in SCHEMA)


def synchronize_table(text):
    descriptors, offsets = {}, {}
    for name, prototype, encoding, _, uses, definitions in SCHEMA:
        text, implicit = EFFECTS.implicit_sequence(text, uses + definitions)
        fields = TABLES.descriptor(text, prototype)
        fields[1:4] = ["0" if encoding else "2", "0", "0"]
        fields[5:8] = [str(len(uses)), str(len(definitions)), f"X86ImpOpBase + {implicit}"]
        flags = ("UnmodeledSideEffects", "MayLoad", "MayStore")
        tsflags = fields[9].split(", ")[1] if encoding else "0x0ULL"
        if not encoding:
            flags = ("Pseudo", *flags)
            text, operand_offset, offsets[name] = MPX.operands(text, ("i32imm", "i32imm"))
            fields[8] = str(operand_offset)
        fields[9] = "0" + "".join(f"|(1ULL<<MCID::{f})" for f in flags) + ", " + tsflags
        descriptors[name] = fields
    return TABLES.synchronize_instructions(text, INSTRUCTIONS, {}, descriptors, offsets)


def synchronize():
    path = TABLES.TARGET / "X86GenInstrInfo.inc"
    text = synchronize_table(path.read_text())
    outputs = {path: text}
    for name in ("X86GenAsmWriter.inc", "X86GenAsmWriter1.inc"):
        path = TABLES.TARGET / name
        outputs[path] = TABLES.synchronize_writer(path.read_text(), TABLES.enums(text), INSTRUCTIONS)
    path = TABLES.TARGET / "X86GenAsmMatcher.inc"
    # SNP's existing assembler rows also accept explicit-register aliases.
    # New mnemonics have no printed operands, like canonical VMMCALL.
    matcher = tuple((name, "VMMCALL", encoding, mnemonic)
                    for name, _, encoding, mnemonic in INSTRUCTIONS)
    outputs[path] = TABLES.synchronize_matcher(path.read_text(), matcher, {}, True)
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
            parser.exit(1, "SNP context tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
