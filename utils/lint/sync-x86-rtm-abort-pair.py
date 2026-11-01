#!/usr/bin/env python3
"""Append the indivisible RTM abort pair and its implicit effects."""

import argparse
import importlib.util
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "context", Path(__file__).with_name("x86_context_tables.py"))
CONTEXT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CONTEXT)
SCHEMA = (("RTM_ABORT_PAIR", "WRITE_SYSREG64", None, None,
           (), ("EAX", "EFLAGS")),)


def synchronize():
    path = CONTEXT.TABLES.TARGET / "X86GenInstrInfo.inc"
    text = CONTEXT.synchronize_table(path.read_text(), SCHEMA, ("i32imm",))
    outputs = {path: text}
    instructions = tuple(row[:4] for row in SCHEMA)
    for name in ("X86GenAsmWriter.inc", "X86GenAsmWriter1.inc"):
        path = CONTEXT.TABLES.TARGET / name
        outputs[path] = CONTEXT.TABLES.synchronize_writer(
            path.read_text(), CONTEXT.TABLES.enums(text), instructions)
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    for path, text in synchronize().items():
        if path.read_text() == text:
            continue
        if args.write:
            path.write_text(text)
        else:
            parser.exit(1, "RTM abort pair tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
