#!/usr/bin/env python3
"""Append SNP completion and SEV-ES opcodes without renumbering old entries."""

import argparse
import importlib.util
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "context", Path(__file__).with_name("x86_context_tables.py"))
CONTEXT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CONTEXT)
TABLES = CONTEXT.TABLES
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
    return CONTEXT.synchronize_table(text, SCHEMA)


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
