#!/usr/bin/env python3
"""Retain the EnhancedTlbi control in the pruned x64 TLBSYNC machine tables.

NeverC supports x64 code generation; RAX carries the complete control value.
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
INSTRUCTIONS = (("TLBSYNC_CONTROL64", "TLBSYNC", None, None),)


def synchronize_table(text):
    text, implicit = EFFECTS.implicit_sequence(text, ("RAX",))
    fields = TABLES.descriptor(text, "TLBSYNC")
    fields[1:4] = ["0", "0", "0"]
    fields[5:8] = ["1", "0", f"X86ImpOpBase + {implicit}"]
    fields[9] = "0" + "".join(f"|(1ULL<<MCID::{flag})" for flag in
                              ("Pseudo", "UnmodeledSideEffects", "MayLoad", "MayStore")) + ", 0x0ULL"
    return TABLES.synchronize_instructions(text, INSTRUCTIONS, {},
                                           {"TLBSYNC_CONTROL64": fields})


def synchronize():
    path = TABLES.TARGET / "X86GenInstrInfo.inc"
    text = synchronize_table(path.read_text())
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
                parser.exit(1, "TLBSYNC control tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
