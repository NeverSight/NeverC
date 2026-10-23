#!/usr/bin/env python3
"""Install/check the system-register write pseudo without renumbering opcodes.

WRITE_SYSREG64 has an immediate physical register number and a GR64 input. Its
custom inserter expands to a real MOV64cr/MOV64dr before machine optimization.
It has no binary encoding or assembler spelling. The LLVM fork's source is in
X86InstrCompiler.td; this schema owns the corresponding pruned NeverC tables.
"""

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TABLE = ROOT / "llvm/lib/Target/X86/X86GenInstrInfo.inc"
NAME = "WRITE_SYSREG64"
OPERANDS = ("{ -1, 0, MCOI::OPERAND_IMMEDIATE, 0 }, "
            "{ X86::GR64RegClassID, 0, MCOI::OPERAND_REGISTER, 0 },")
FLAGS = "0" + "".join(f"|(1ULL<<MCID::{flag})" for flag in
                     ("Pseudo", "UsesCustomInserter", "UnmodeledSideEffects"))


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"expected one table anchor: {old!r}")
    return text.replace(old, new, 1)


def array(text, declaration):
    start = text.index(declaration) + len(declaration)
    end = text.index("};", start)
    return start, end, text[start:end]


def descriptor(opcode, operand_offset):
    return (f"    {{ {opcode},\t2,\t0,\t0,\t8,\t0,\t1,\tX86ImpOpBase + 0,\t"
            f"{operand_offset},\t{FLAGS}, 0x0ULL }},  // Inst #{opcode} = {NAME}\n")


def synchronize(text):
    total = int(re.search(r"INSTRUCTION_LIST_END = (\d+)", text).group(1))
    names_start, names_end, names_text = array(text, "extern const char X86InstrNameData[] = {")
    strings = re.findall(r'/\* (\d+) \*/ "(\w+)\\0"', names_text)
    indices_start, indices_end, indices_text = array(
        text, "extern const unsigned X86InstrNameIndices[] = {")
    indices = [int(value) for value in re.findall(r"(\d+)U", indices_text)]
    if len(indices) != total:
        raise ValueError("instruction-name index count disagrees with opcode count")
    existing = re.search(r"\b" + NAME + r"\s*= (\d+),", text)
    if existing:
        opcode = int(existing.group(1))
        operand_match = re.search(r"/\* (\d+) \*/ " + re.escape(OPERANDS), text)
        if not operand_match or descriptor(opcode, int(operand_match.group(1))) not in text:
            raise ValueError("system-register write descriptor is stale")
        if (str(indices[opcode]), NAME) not in strings:
            raise ValueError("system-register write instruction name is stale")
        if f"CEFBS_In64BitMode, // {NAME} = {opcode}" not in text:
            raise ValueError("system-register write feature requirement is stale")
        _, _, types_text = array(text, "static const int16_t OpcodeOperandTypes[] = {")
        _, _, offsets_text = array(text, "static const uint16_t Offsets[] = {")
        offsets = [int(value) for value in re.findall(r"\d+", re.sub(r"/\*.*?\*/", "", offsets_text))]
        types = [value.strip() for value in re.sub(r"/\*.*?\*/", "", types_text).split(",") if value.strip()]
        if len(offsets) != total or types[offsets[opcode]:offsets[opcode] + 2] != ["i32imm", "GR64"]:
            raise ValueError("system-register write operand types are stale")
        return text

    opcode = total
    operand_offset = int(re.search(r"MCOperandInfo OperandInfo\[(\d+)\];", text).group(1))
    name_offset = int(strings[-1][0]) + len(strings[-1][1]) + 1
    # Append names/indices before changing any earlier offsets in the text.
    text = text[:indices_end] + f"    {name_offset}U,\n" + text[indices_end:]
    text = text[:names_end] + f'  /* {name_offset} */ "{NAME}\\0"\n' + text[names_end:]
    text = replace_once(text, f"    INSTRUCTION_LIST_END = {total}",
                        f"    {NAME}\t= {opcode},\n    INSTRUCTION_LIST_END = {total + 1}")
    text = replace_once(text, f"MCInstrDesc Insts[{total}];", f"MCInstrDesc Insts[{total + 1}];")
    text = replace_once(text, f"MCOperandInfo OperandInfo[{operand_offset}];",
                        f"MCOperandInfo OperandInfo[{operand_offset + 2}];")
    text = replace_once(text, "extern const X86InstrTable X86Descs = {\n  {\n",
                        "extern const X86InstrTable X86Descs = {\n  {\n" + descriptor(opcode, operand_offset))
    text = replace_once(text, "  }, {\n    /* 0 */\n    /* 0 */ X86::EFLAGS,",
                        f"    /* {operand_offset} */ {OPERANDS}\n  }}, {{\n    /* 0 */\n    /* 0 */ X86::EFLAGS,")
    old = f"X86InstrNameData, nullptr, nullptr, {total});"
    if text.count(old) != 2:
        raise ValueError("expected both instruction-info constructors")
    text = text.replace(old, f"X86InstrNameData, nullptr, nullptr, {total + 1});")
    _, types_end, types_text = array(text, "static const int16_t OpcodeOperandTypes[] = {")
    types = [value.strip() for value in re.sub(r"/\*.*?\*/", "", types_text).split(",") if value.strip()]
    text = text[:types_end] + f"  /* {NAME} */\n    i32imm, GR64,\n  " + text[types_end:]
    _, offsets_end, _ = array(text, "static const uint16_t Offsets[] = {")
    text = text[:offsets_end] + f"  /* {NAME} */\n    {len(types)},\n  " + text[offsets_end:]
    anchor = f"  }};\n\n  assert(Opcode < {total});"
    text = replace_once(text, anchor,
                        f"    CEFBS_In64BitMode, // {NAME} = {opcode}\n  }};\n\n  assert(Opcode < {total + 1});")
    synchronize(text)  # Validate every installed cross-reference.
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
        parser.exit(1, "system instruction pseudos are stale; run with --write\n")


if __name__ == "__main__":
    main()
