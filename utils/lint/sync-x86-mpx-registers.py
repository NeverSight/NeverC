#!/usr/bin/env python3
"""Append the reserved MPX bank without renumbering NeverC's register tables.

X86RegisterInfo.td in the full LLVM fork owns the bank. These independent,
nonallocatable registers have no aliases, subregisters or DWARF mappings. The
existing four-word subclass masks and thirteen-word call masks still fit.
"""

import argparse
import importlib.util
import re
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "tables", Path(__file__).with_name("sync-x86-amx-instructions.py"))
TABLES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TABLES)
BASE_REGISTERS, BASE_CLASSES, BASE_UNITS = 388, 103, 221
REGISTERS = tuple(f"BND{i}" for i in range(4))


def marked(content):
    return "// MPX bank begin\n" + content + "// MPX bank end\n"


def append_array(text, declaration, content):
    _, end, _ = TABLES.array(text, declaration)
    return text[:end] + marked(content) + text[end:]


def append_string(text, declaration, value):
    _, _, data = TABLES.array(text, declaration)
    strings = re.findall(r'/\* (\d+) \*/ "(\w+)\\0"', data)
    offset = int(strings[-1][0]) + len(strings[-1][1]) + 1
    return append_array(text, declaration, f'  /* {offset} */ "{value}\\0"\n'), offset


def synchronize_registers(text):
    # Reconstruct and verify owned additions on every invocation. Markers let
    # check mode detect changed descriptors instead of trusting an existing ID.
    installed = "  BND0 = " in text
    text = re.sub(r"// MPX bank begin\n.*?// MPX bank end\n", "", text,
                  flags=re.DOTALL)
    counts = (("NUM_TARGET_REGS // ", BASE_REGISTERS, 4),
              ("X86RegDesc, ", BASE_REGISTERS, 4),
              ("X86MCRegisterClasses, ", BASE_CLASSES, 1),
              ("RegisterClasses+", BASE_CLASSES, 1),
              ("X86RegUnitRoots, ", BASE_UNITS, 4),
              ("                     ", BASE_UNITS, 4),
              ("RegUnit < ", BASE_UNITS, 4))
    if installed:
        for prefix, base, extra in counts:
            text = text.replace(prefix + str(base + extra), prefix + str(base))
    if "NUM_TARGET_REGS // 388" not in text or "  BND0 = " in text:
        raise ValueError("unexpected base register schema")
    text = TABLES.replace_once(text, "  NUM_TARGET_REGS // 388",
                              marked("".join(f"  {r} = {BASE_REGISTERS+i},\n"
                                             for i, r in enumerate(REGISTERS))) +
                              "  NUM_TARGET_REGS // 388")
    text = TABLES.replace_once(text, "  TILERegClassID = 102,\n",
                              "  TILERegClassID = 102,\n" +
                              marked("  BNDRRegClassID = 103,\n"))
    descriptors = ""
    for i, name in enumerate(REGISTERS):
        text, offset = append_string(text, "extern const char X86RegStrings[] = {", name)
        descriptors += f"  {{ {offset}, 2, 2, 2, {8192 + BASE_UNITS + i}, 22 }},\n"
    text = append_array(text, "extern const MCRegisterDesc X86RegDesc[] = {",
                        descriptors)
    text = append_array(text, "extern const MCPhysReg X86RegUnitRoots[][2] = {",
                        "".join(f"  {{ X86::{r} }},\n" for r in REGISTERS))
    text = append_array(text, "extern const uint16_t X86RegEncodingTable[] = {",
                        "  0, 1, 2, 3,\n")
    text, class_name = append_string(text, "extern const char X86RegClassStrings[] = {", "BNDR")
    bits = [0] * ((BASE_REGISTERS + len(REGISTERS) + 7) // 8)
    for i in range(BASE_REGISTERS, BASE_REGISTERS + len(REGISTERS)):
        bits[i // 8] |= 1 << (i % 8)
    definitions = ("const MCPhysReg BNDR[] = { " +
                   ", ".join("X86::" + r for r in REGISTERS) + " };\n" +
                   "const uint8_t BNDRBits[] = { " +
                   ", ".join(hex(b) for b in bits) + " };\n")
    text = TABLES.replace_once(text, "extern const MCRegisterClass X86MCRegisterClasses[] = {",
                              marked(definitions) + "extern const MCRegisterClass X86MCRegisterClasses[] = {")
    text = append_array(text, "extern const MCRegisterClass X86MCRegisterClasses[] = {",
                        f"  {{ BNDR, BNDRBits, {class_name}, 4, sizeof(BNDRBits), X86::BNDRRegClassID, 128, 1, false }},\n")
    text = TABLES.replace_once(text, "  extern const TargetRegisterClass TILERegClass;\n",
                              "  extern const TargetRegisterClass TILERegClass;\n" +
                              marked("  extern const TargetRegisterClass BNDRRegClass;\n"))
    _, _, values = TABLES.array(text, "static const MVT::SimpleValueType VTLists[] = {")
    vt_offset = len(re.findall(r"MVT::\w+", values))
    text = append_array(text, "static const MVT::SimpleValueType VTLists[] = {",
                        f"  /* {vt_offset} */ MVT::v2i64, MVT::Other,\n")
    text = append_array(text, "static const TargetRegisterInfo::RegClassInfo RegClassInfos[] = {",
                        f"  {{ 128, 128, 128, /*VTLists+*/{vt_offset} }}, // BNDR\n")
    text = TABLES.replace_once(text, "static const uint32_t GR8SubClassMask[] = {",
                              marked("static const uint32_t BNDRSubClassMask[] = { 0, 0, 0, 0x80 };\n") +
                              "static const uint32_t GR8SubClassMask[] = {")
    target_class = """  extern const TargetRegisterClass BNDRRegClass = {
    &X86MCRegisterClasses[BNDRRegClassID], BNDRSubClassMask,
    SuperRegIdxSeqs + 1, LaneBitmask(1), 0, false, 0, false, false,
    NullRegClasses, nullptr
  };
"""
    text = TABLES.replace_once(text, "  extern const TargetRegisterClass TILERegClass = {",
                              marked(target_class) + "  extern const TargetRegisterClass TILERegClass = {")
    for declaration, content in (
            ("const TargetRegisterClass *const RegisterClasses[] = {", "    &X86::BNDRRegClass,\n"),
            ("static const uint8_t CostPerUseTable[] = {", "0, 0, 0, 0,\n"),
            ("static const bool InAllocatableClassTable[] = {", "false, false, false, false,\n"),
            ("static const RegClassWeight RCWeightTable[] = {", "    {0, 0}, // BNDR\n"),
            ("static const uint16_t RCSetStartTable[] = {", "1,\n"),
            ("static const uint16_t RUSetStartTable[] = {", "1, 1, 1, 1,\n")):
        text = append_array(text, declaration, content)
    for prefix, base, extra in counts:
        if prefix + str(base) not in text:
            raise ValueError(f"missing register count: {prefix}{base}")
        text = text.replace(prefix + str(base), prefix + str(base + extra))
    return text


def synchronize_writer(text):
    split = text.index("/// getRegisterName -")
    prefix, body = text[:split], text[split:]
    body = re.sub(r"// MPX bank begin\n.*?// MPX bank end\n", "", body,
                  flags=re.DOTALL)
    body = body.replace("RegNo < 392", "RegNo < 388")
    offsets = []
    for name in REGISTERS:
        body, offset = append_string(body, "static const char AsmStrs[] = {", name.lower())
        offsets.append(offset)
    body = append_array(body, "static const uint16_t RegAsmOffset[] = {",
                        "    " + ", ".join(map(str, offsets)) + ",\n")
    body = TABLES.replace_once(body, "RegNo < 388", "RegNo < 392")
    return prefix + body


def synchronize_matcher(text):
    text = re.sub(r"// MPX bank begin\n.*?// MPX bank end\n", "", text,
                  flags=re.DOTALL)
    text = text.replace("MCK_LAST_REGISTER = MCK_BNDR", "MCK_LAST_REGISTER = MCK_GR8")
    text = TABLES.replace_once(text, "static unsigned MatchRegisterName(StringRef Name) {\n",
                              "static unsigned MatchRegisterName(StringRef Name) {\n" + marked(
                                  "  if (Name.size() == 4 && Name.starts_with(\"bnd\") &&\n"
                                  "      Name[3] >= '0' && Name[3] <= '3')\n"
                                  "    return X86::BND0 + Name[3] - '0';\n"))
    text = TABLES.replace_once(text, "  MCK_LAST_REGISTER = MCK_GR8,",
                              marked("  MCK_BNDR, // reserved bounds bank\n") +
                              "  MCK_LAST_REGISTER = MCK_BNDR,")
    text = TABLES.replace_once(text, "    case X86::TMM0: OpKind = MCK_TILE; break;",
                              marked("".join(f"    case X86::{r}: OpKind = MCK_BNDR; break;\n"
                                             for r in REGISTERS)) +
                              "    case X86::TMM0: OpKind = MCK_TILE; break;")
    text = TABLES.replace_once(text, '  case MCK_TILE: return "MCK_TILE";',
                              marked('  case MCK_BNDR: return "MCK_BNDR";\n') +
                              '  case MCK_TILE: return "MCK_TILE";')
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    for name, transform in (("X86GenRegisterInfo.inc", synchronize_registers),
                            ("X86GenAsmWriter.inc", synchronize_writer),
                            ("X86GenAsmWriter1.inc", synchronize_writer),
                            ("X86GenAsmMatcher.inc", synchronize_matcher)):
        path = TABLES.TARGET / name
        original = path.read_text()
        updated = transform(original)
        if updated != original:
            if args.write:
                path.write_text(updated)
            else:
                parser.exit(1, "MPX register tables are stale; run with --write\n")


if __name__ == "__main__":
    main()
