#!/usr/bin/env python3
"""Append native MPX opcodes and their reserved-bank machine pseudos.

The full fork's X86InstrMPX.td owns the definitions. NeverC keeps every old
opcode, operand and feature ID; the table helpers reuse existing printer and
assembler operand shapes while this module owns MPX's classes and effects.
"""

import argparse
import importlib.util
import re
from pathlib import Path


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


TABLES = module("tables", "sync-x86-amx-instructions.py")
EFFECTS = module("effects", "sync-x86-system-effects.py")
# Opcode, matching printer shape, native opcode/prefix/form, operand types,
# explicit definitions, memory effects. Prefix values are X86II encodings.
SCHEMA = [
    ("BNDMK64rm", "LEA64r", (0x1b, 2, 25), ("BNDR", "anymem"), 1, ()),
    ("BNDMOVrr", "MOVAPSrr", (0x1a, 1, 41), ("BNDR", "BNDR"), 1, ()),
    ("BNDMOV64rm", "MOVUPSrm", (0x1a, 1, 25), ("BNDR", "i128mem"), 1, ("MayLoad",)),
    ("BNDMOV64mr", "MOVUPSmr", (0x1b, 1, 24), ("i128mem", "BNDR"), 0, ("MayStore",)),
    ("BNDLDXrm", "LEA64r", (0x1a, 0, 25), ("BNDR", "anymem"), 1, ("MayLoad",)),
    ("BNDSTXmr", "TILESTORED", (0x1b, 0, 24), ("anymem", "BNDR"), 0, ("MayStore",)),
]
for name, opcode, prefix in (("BNDCL", 0x1a, 2), ("BNDCU", 0x1a, 3), ("BNDCN", 0x1b, 3)):
    SCHEMA += [(name + "64rr", "MOV64rr", (opcode, prefix, 41), ("BNDR", "GR64"), 0, ()),
               (name + "64rm", "LEA64r", (opcode, prefix, 25), ("BNDR", "anymem"), 0, ())]
SCHEMA += [("MPX_STATE64", "WRITE_SYSREG64", None, ("i32imm", "GR64", "GR64"), 0, ("MayLoad", "MayStore")),
           ("MPX_MOVE_BOUNDS", "WRITE_SYSREG64", None, ("i32imm",), 0, ("MayLoad", "MayStore")),
           ("MPX_BRANCH", "WRITE_SYSREG64", None, ("i32imm",), 0, ("MayLoad", "MayStore"))]
INSTRUCTIONS = tuple((name, prototype, None, re.match(r"[A-Z]+", name).group(0).lower() if encoding else None)
                     for name, prototype, encoding, _, _, _ in SCHEMA)


def operands(text, kinds, ties=None):
    memory = re.search(r"/\* 209 \*/ ([^\n]+)", text).group(1).strip()
    values = []
    for index, kind in enumerate(kinds):
        if kind.endswith("mem"):
            values.append(memory)
        elif kind == "i32imm":
            values.append("{ -1, 0, MCOI::OPERAND_IMMEDIATE, 0 },")
        else:
            tie = f"MCOI_TIED_TO({ties[index]})" if ties and index in ties else "0"
            values.append("{ X86::" + kind + "RegClassID, 0, MCOI::OPERAND_REGISTER, " + tie + " },")
    info = " ".join(values)
    existing = re.search(r"/\* (\d+) \*/ " + re.escape(info), text)
    if existing:
        offset = int(existing.group(1))
    else:
        offset = int(re.search(r"MCOperandInfo OperandInfo\[(\d+)\];", text).group(1))
        count = sum(5 if k.endswith("mem") else 1 for k in kinds)
        text = TABLES.replace_once(text, f"MCOperandInfo OperandInfo[{offset}];",
                                  f"MCOperandInfo OperandInfo[{offset + count}];")
        anchor = "  }, {\n    /* 0 */\n    /* 0 */ X86::EFLAGS,"
        text = TABLES.replace_once(text, anchor, f"    /* {offset} */ {info}\n" + anchor)
    _, end, data = TABLES.array(text, "static const int16_t OpcodeOperandTypes[] = {")
    types = [v.strip() for v in re.sub(r"/\*.*?\*/", "", data).split(",") if v.strip()]
    for type_offset in range(len(types) - len(kinds) + 1):
        if tuple(types[type_offset:type_offset + len(kinds)]) == kinds:
            return text, offset, type_offset
    text = text[:end] + "    " + ", ".join(kinds) + ",\n" + text[end:]
    return text, offset, len(types)


def synchronize_table(text):
    if not re.search(r"^  BNDR = \d+,", text, re.MULTILINE):
        start = text.index("enum OperandType {")
        end = text.index("};", start)
        last = max(map(int, re.findall(r" = (\d+),", text[start:end])))
        text = text[:end] + f"  BNDR = {last + 1},\n" + text[end:]
    bank = tuple(f"BND{i}" for i in range(4))
    text, implicit = EFFECTS.implicit_sequence(text, bank + bank)
    overrides, offsets = {}, {}
    for name, prototype, encoding, kinds, definitions, effects in SCHEMA:
        text, operand_offset, offsets[name] = operands(text, kinds)
        fields = TABLES.descriptor(text, prototype)
        fields[1:4] = [str(sum(5 if k.endswith("mem") else 1 for k in kinds)), str(definitions), "0"]
        fields[4] = TABLES.descriptor(text, "CLTS")[4]
        fields[5:8] = (["0", "0", "X86ImpOpBase + 0"] if encoding else
                       ["4", "4", f"X86ImpOpBase + {implicit}"])
        fields[8] = str(operand_offset)
        flags = ["UnmodeledSideEffects", *effects]
        if encoding:
            opcode, prefix, form = encoding
            tsflags = (opcode << 31) | (1 << 13) | (prefix << 11) | form
        else:
            flags.insert(0, "Pseudo")
            tsflags = 0
        fields[9] = "0" + "".join(f"|(1ULL<<MCID::{flag})" for flag in flags) + f", {hex(tsflags)}ULL"
        overrides[name] = fields
    return TABLES.synchronize_instructions(text, INSTRUCTIONS, {}, overrides, offsets)


def synchronize_matcher(text):
    # Start from the same prototypes on each run, then substitute only the
    # bounds/address roles. Existing non-MPX entries retain their exact text.
    for name, _, _, _ in INSTRUCTIONS:
        text = re.sub(r"^.*X86::" + name + r",[^\n]*\n", "", text, flags=re.MULTILINE)
    text = TABLES.synchronize_matcher(text, INSTRUCTIONS, {}, True)
    for name, _, encoding, kinds, _, _ in SCHEMA:
        if not encoding:
            continue
        for dialect in (0, 1):
            start, end, data = TABLES.array(text, f"static const MatchEntry MatchTable{dialect}[] = {{")
            classes = ["MCK_BNDR" if k == "BNDR" else "MCK_Mem" if k == "anymem"
                       else "MCK_Mem128" if k == "i128mem" else "MCK_GR64" for k in kinds]
            if dialect == 0:
                classes.reverse()
            line = re.search(r"^.*X86::" + name + r",[^\n]*", data, re.MULTILINE).group(0)
            updated = re.sub(r"\{ MCK_[^}]+\}", "{ " + ", ".join(classes) + " }", line)
            text = text[:start] + data.replace(line, updated) + text[end:]
    return text


def synchronize():
    target = TABLES.TARGET
    path = target / "X86GenInstrInfo.inc"
    table = synchronize_table(path.read_text())
    outputs = {path: table}
    ids = TABLES.enums(table)
    for name in ("X86GenAsmWriter.inc", "X86GenAsmWriter1.inc"):
        path = target / name
        outputs[path] = TABLES.synchronize_writer(path.read_text(), ids, INSTRUCTIONS)
    path = target / "X86GenAsmMatcher.inc"
    outputs[path] = synchronize_matcher(path.read_text())
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    for path, updated in synchronize().items():
        if updated != path.read_text():
            if args.write:
                path.write_text(updated)
            else:
                parser.exit(1, "MPX instruction tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
