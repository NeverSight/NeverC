#!/usr/bin/env python3
"""Append native AMX instructions to NeverC's pruned X86 tables.

The LLVM fork's X86InstrAMX.td owns the instruction definitions. This schema
uses existing operand/printing forms, preserving every preexisting opcode and
feature number. It updates descriptors, operand types, both assembly writers,
both assembler dialects, and subtarget features together.
"""

import argparse
import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "llvm/lib/Target/X86"
# Name, prototype, encoded opcode/map/prefix (X86II values), asm mnemonic.
INSTRUCTIONS = []
for name, prefix in (("TDPBF8PS", 0), ("TDPBHF8PS", 3),
                     ("TDPHBF8PS", 2), ("TDPHF8PS", 1)):
    INSTRUCTIONS += [(name, "TDPBSSD", (0xfd, 9, prefix), name.lower()),
                     ("P" + name, "PTDPBSSD", None, None)]
INSTRUCTIONS += [("TMMULTF32PS", "TDPBSSD", (0x48, 2, 1), "tmmultf32ps"),
                 ("PTMMULTF32PS", "PTDPBSSD", None, None)]
for name, prototype, mnemonic in (
        ("TILELOADDRS", "TILELOADD", "tileloaddrs"),
        ("TILELOADDRST1", "TILELOADDT1", "tileloaddrst1")):
    for suffix in ("", "_EVEX"):
        INSTRUCTIONS.append((name + suffix, prototype + suffix,
                             (0x4a, 2, 3 if not name.endswith("T1") else 1), mnemonic))
    INSTRUCTIONS.append(("P" + name, "P" + prototype, None, None))
FEATURES = ("FP8", "MOVRS", "TF32")


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"expected one table anchor: {old!r}")
    return text.replace(old, new, 1)


def array(text, declaration):
    start = text.index(declaration) + len(declaration)
    end = text.index("};", start)
    return start, end, text[start:end]


def enums(text):
    return {name: int(value) for name, value in
            re.findall(r"^    (\w+)\s*= (\d+),",
                       text[:text.index("INSTRUCTION_LIST_END =")], re.MULTILINE)}


def descriptor(text, name):
    return re.search(r"^    \{ ([^\n]+) \},  // Inst #\d+ = " + name + r"$",
                     text, re.MULTILINE).group(1).split(",\t")


def synchronize_instructions(text):
    for name, prototype, encoding, _ in INSTRUCTIONS:
        ids = enums(text)
        total = int(re.search(r"INSTRUCTION_LIST_END = (\d+)", text).group(1))
        opcode = ids.get(name, total)
        fields = descriptor(text, prototype)
        fields[0] = str(opcode)
        if encoding:
            flags, tsflags = fields[9].split(", ")
            old = int(tsflags.removesuffix("ULL"), 16)
            byte, opmap, prefix = encoding
            mask = (255 << 31) | (15 << 13) | (3 << 11)
            value = (old & ~mask) | (byte << 31) | (opmap << 13) | (prefix << 11)
            fields[9] = f"{flags}, {hex(value)}ULL"
        joined = ",\t".join(fields)
        line = f"    {{ {joined} }},  // Inst #{opcode} = {name}\n"
        # Reuse the prototype's operand sequence. No register-class numbering
        # from the full LLVM build may leak into the pruned tables.
        _, _, offsets_text = array(text, "static const uint16_t Offsets[] = {")
        offsets = [int(v) for v in re.findall(r"\d+", re.sub(r"/\*.*?\*/", "", offsets_text))]
        _, _, name_indices = array(text, "extern const unsigned X86InstrNameIndices[] = {")
        indices = [int(v) for v in re.findall(r"(\d+)U", name_indices)]
        _, _, names = array(text, "extern const char X86InstrNameData[] = {")
        strings = re.findall(r'/\* (\d+) \*/ "(\w+)\\0"', names)
        if len(offsets) != total or len(indices) != total:
            raise ValueError("instruction table lengths disagree")
        if name in ids:
            if (line not in text or offsets[opcode] != offsets[ids[prototype]] or
                    (str(indices[opcode]), name) not in strings or
                    f"CEFBS_In64BitMode, // {name} = {opcode}" not in text):
                raise ValueError(f"stale AMX instruction table: {name}")
            continue
        offset = int(strings[-1][0]) + len(strings[-1][1]) + 1
        _, end, _ = array(text, "extern const char X86InstrNameData[] = {")
        text = text[:end] + f'  /* {offset} */ "{name}\\0"\n' + text[end:]
        _, end, _ = array(text, "extern const unsigned X86InstrNameIndices[] = {")
        text = text[:end] + f"    {offset}U,\n" + text[end:]
        text = replace_once(text, f"    INSTRUCTION_LIST_END = {total}",
                            f"    {name}\t= {opcode},\n    INSTRUCTION_LIST_END = {total + 1}")
        text = replace_once(text, f"MCInstrDesc Insts[{total}];", f"MCInstrDesc Insts[{total + 1}];")
        text = replace_once(text, "extern const X86InstrTable X86Descs = {\n  {\n",
                            "extern const X86InstrTable X86Descs = {\n  {\n" + line)
        old = f"X86InstrNameData, nullptr, nullptr, {total});"
        if text.count(old) != 2:
            raise ValueError("expected two instruction-info constructors")
        text = text.replace(old, f"X86InstrNameData, nullptr, nullptr, {total + 1});")
        _, end, _ = array(text, "static const uint16_t Offsets[] = {")
        text = text[:end] + f"  /* {name} */\n    {offsets[ids[prototype]]},\n  " + text[end:]
        text = replace_once(text, f"  }};\n\n  assert(Opcode < {total});",
                            f"    CEFBS_In64BitMode, // {name} = {opcode}\n  }};\n\n  assert(Opcode < {total + 1});")
    return text


def synchronize_writer(text, ids):
    mask = int(re.search(r"return \{AsmStrs\+\(Bits & (\d+)\)-1, Bits\};", text).group(1))
    start, end, strings = array(text, "  static const char AsmStrs[] = {")
    data = "".join(ast.literal_eval('"' + s + '"') for s in re.findall(r'"([^"\n]*)"', strings))
    offsets = {}
    addition = ""
    for _, _, _, mnemonic in INSTRUCTIONS:
        if mnemonic is None or mnemonic in offsets:
            continue
        value = mnemonic + "\t\0"
        offset = data.find(value)
        if offset < 0:
            offset = len(data)
            addition += f'  /* {offset} */ "{mnemonic}\\t\\0"\n'
            data += value
        if offset + 1 > mask:
            raise ValueError("AMX assembly string does not fit the writer's offset field")
        offsets[mnemonic] = offset
    text = text[:end] + addition + text[end:]
    for declaration in re.findall(r"  static const uint\d+_t OpInfo\d\[\] = \{", text):
        _, end, content = array(text, declaration)
        values = [(int(v), name) for v, name in re.findall(r"(\d+)U,\s*// (\w+)", content)]
        if any(ids.get(name) != index for index, (_, name) in enumerate(values)):
            raise ValueError("assembly writer opcodes are out of order")
        expected = list(values)
        by_id = {value: name for name, value in ids.items()}
        while len(expected) < len(ids):
            expected.append((0, by_id[len(expected)]))
        for name, prototype, _, mnemonic in INSTRUCTIONS:
            value = values[ids[prototype]][0] if mnemonic else 0
            if mnemonic and "OpInfo0" in declaration:
                value = (value & ~mask) | (offsets[mnemonic] + 1)
            expected[ids[name]] = value, name
        for index, old in enumerate(values):
            if old != expected[index]:
                text = replace_once(text, f"    {old[0]}U,\t// {old[1]}\n",
                                    f"    {expected[index][0]}U,\t// {old[1]}\n")
        _, end, _ = array(text, declaration)
        text = text[:end] + "".join(f"    {v}U,\t// {n}\n" for v, n in expected[len(values):]) + text[end:]
    return text


def synchronize_matcher(text):
    start = text.index("static const char MnemonicTable[] =")
    end = text.index(";", start)
    data = "".join(ast.literal_eval('"' + s + '"') for s in re.findall(r'"([^"\n]*)"', text[start:end]))
    offsets = {}
    addition = ""
    for _, _, _, mnemonic in INSTRUCTIONS:
        if mnemonic is None or mnemonic in offsets:
            continue
        value = chr(len(mnemonic)) + mnemonic
        offset = data.find(value)
        if offset < 0:
            offset = len(data)
            addition += f'\n    "\\{len(mnemonic):03o}{mnemonic}"'
            data += value
        offsets[mnemonic] = offset
    text = text[:end] + addition + text[end:]
    for dialect in (0, 1):
        start, end, content = array(text, f"static const MatchEntry MatchTable{dialect}[] = {{")
        lines = [line for line in content.splitlines() if line.strip()]
        for name, prototype, _, mnemonic in INSTRUCTIONS:
            if not mnemonic:
                continue
            matches = [line for line in lines if f"X86::{prototype}," in line]
            if len(matches) != 1:
                raise ValueError(f"expected one assembler prototype: {prototype}")
            line = re.sub(r"\{ \d+ /\* \w+ \*/", f"{{ {offsets[mnemonic]} /* {mnemonic} */", matches[0], count=1)
            line = line.replace(f"X86::{prototype},", f"X86::{name},")
            existing = [value for value in lines if f"X86::{name}," in value]
            if existing and existing != [line]:
                raise ValueError(f"stale AMX assembler entry: {name}")
            if not existing:
                lines.append(line)
        lines.sort(key=lambda line: re.search(r"/\* (\S+) \*/", line).group(1))
        text = text[:start] + "\n" + "\n".join(lines) + "\n" + text[end:]
    return text


def synchronize_features(text):
    for name in FEATURES:
        cpp = "FeatureAMX" + name
        if not re.search(r"\b" + cpp + r" = \d+", text):
            count = int(re.search(r"NumSubtargetFeatures = (\d+)", text).group(1))
            text = replace_once(text, f"  NumSubtargetFeatures = {count}",
                                f"  {cpp} = {count},\n  NumSubtargetFeatures = {count + 1}")
        macro = f"GET_SUBTARGETINFO_MACRO(HasAMX{name}, false, hasAMX{name})\n"
        if macro not in text:
            text = replace_once(text, "GET_SUBTARGETINFO_MACRO(HasAMXTILE, false, hasAMXTILE)\n",
                                macro + "GET_SUBTARGETINFO_MACRO(HasAMXTILE, false, hasAMXTILE)\n")
        key = "amx-" + name.lower()
        entry = (f'  {{ "{key}", "Support AMX-{name} instructions", X86::{cpp}, '
                 '{ { { 0x100ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, } } } },\n')
        if f'{{ "{key}",' in text and entry not in text:
            raise ValueError(f"stale AMX feature dependency: {key}")
        if f'{{ "{key}",' not in text:
            start, end, contents = array(text, "extern const llvm::SubtargetFeatureKV X86FeatureKV[] = {")
            lines = (contents + entry).strip().splitlines()
            lines.sort(key=lambda line: re.search(r'"([^"]+)"', line).group(1))
            text = text[:start] + "\n" + "\n".join("  " + s.strip() for s in lines) + "\n" + text[end:]
        assignment = f"  if (Bits[X86::{cpp}]) HasAMX{name} = true;\n"
        if assignment not in text:
            text = replace_once(text, "  if (Bits[X86::FeatureAMXTILE]) HasAMXTILE = true;\n",
                                assignment + "  if (Bits[X86::FeatureAMXTILE]) HasAMXTILE = true;\n")
    _, _, contents = array(text, "extern const llvm::SubtargetFeatureKV X86FeatureKV[] = {")
    count = len(re.findall(r'\{ "', contents))
    text = re.sub(r"ArrayRef\(X86FeatureKV, \d+\)", f"ArrayRef(X86FeatureKV, {count})", text)
    return re.sub(r"(ArrayRef\(X86FeatureKV,[^\n]+?) +$", r"\1", text, flags=re.MULTILINE)


def synchronize_compression(text):
    _, end, content = array(text, "static const X86EvexToVexCompressTableEntry X86EvexToVex128CompressTable[] = {")
    for name in ("TILELOADDRS", "TILELOADDRST1"):
        line = f"  {{ X86::{name}_EVEX, X86::{name} }},\n"
        if f"{{ X86::{name}_EVEX," in content and line not in content:
            raise ValueError(f"stale AMX compression mapping: {name}")
        if line not in content:
            text = text[:end] + line + text[end:]
            end += len(line)
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    table = TARGET / "X86GenInstrInfo.inc"
    updated = synchronize_instructions(table.read_text())
    ids = enums(updated)
    outputs = {table: updated}
    for name in ("X86GenAsmWriter.inc", "X86GenAsmWriter1.inc"):
        path = TARGET / name
        outputs[path] = synchronize_writer(path.read_text(), ids)
    path = TARGET / "X86GenAsmMatcher.inc"
    outputs[path] = synchronize_matcher(path.read_text())
    path = TARGET / "X86GenSubtargetInfo.inc"
    outputs[path] = synchronize_features(path.read_text())
    path = TARGET / "X86GenEVEX2VEXTables.inc"
    outputs[path] = synchronize_compression(path.read_text())
    for path, content in outputs.items():
        if path.read_text() == content:
            continue
        if args.write:
            path.write_text(content)
        else:
            parser.exit(1, "AMX instruction tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
