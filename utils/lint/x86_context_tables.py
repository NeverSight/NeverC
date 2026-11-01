"""Shared exact instruction/context table construction for native x86 APIs."""
import importlib.util
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "mpx", Path(__file__).with_name("sync-x86-mpx-instructions.py"))
MPX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MPX)
TABLES, EFFECTS = MPX.TABLES, MPX.EFFECTS


def synchronize_table(text, schema, operand_types=("i32imm", "i32imm")):
    descriptors, offsets = {}, {}
    for name, prototype, encoding, _, uses, definitions in schema:
        text, implicit = EFFECTS.implicit_sequence(text, uses + definitions)
        fields = TABLES.descriptor(text, prototype)
        fields[1:4] = ["0" if encoding else str(len(operand_types)), "0", "0"]
        fields[5:8] = [str(len(uses)), str(len(definitions)), f"X86ImpOpBase + {implicit}"]
        flags = ("UnmodeledSideEffects", "MayLoad", "MayStore")
        tsflags = fields[9].split(", ")[1] if encoding else "0x0ULL"
        if not encoding:
            flags = ("Pseudo", *flags)
            text, operand_offset, offsets[name] = MPX.operands(text, operand_types)
            fields[8] = str(operand_offset)
        fields[9] = "0" + "".join(f"|(1ULL<<MCID::{f})" for f in flags) + ", " + tsflags
        descriptors[name] = fields
    instructions = tuple(row[:4] for row in schema)
    return TABLES.synchronize_instructions(text, instructions, {}, descriptors, offsets)
