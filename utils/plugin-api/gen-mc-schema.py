#!/usr/bin/env python3

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OPERAND_DEFINITIONS = ROOT / "llvm/include/llvm/MC/MCInst.h"
EXPRESSION_DEFINITIONS = ROOT / "llvm/include/llvm/MC/MCExpr.h"
FRAGMENT_DEFINITIONS = ROOT / "llvm/include/llvm/MC/MCFragment.h"
FIXUP_DEFINITIONS = ROOT / "llvm/include/llvm/MC/MCFixup.h"
SCHEMA = ROOT / "neverc/include/neverc/Plugin/Schema/MCSchema.json"
TEMPLATE = (
    ROOT / "neverc/include/neverc/Plugin/Schema/PluginMCSchema.inc.in"
)
OUTPUT = ROOT / "neverc/include/neverc/Plugin/Schema/PluginMCSchema.inc"

FAMILIES = {
    "entities": (0x61000000, "ENTITY"),
    "operand_kinds": (0x62000000, "OPERAND"),
    "expression_kinds": (0x63000000, "EXPRESSION"),
    "fragment_kinds": (0x64000000, "FRAGMENT"),
    "fixup_kinds": (0x65000000, "FIXUP"),
    "layout_states": (0x66000000, "LAYOUT_STATE"),
}

ENTITIES = (
    ("MCUnit", "MC_UNIT"),
    ("MCSection", "SECTION"),
    ("MCFragment", "FRAGMENT"),
    ("MCInst", "INST"),
    ("MCOperand", "OPERAND"),
    ("MCExpr", "EXPR"),
    ("MCSymbol", "SYMBOL"),
    ("MCFixup", "FIXUP"),
    ("SourceLoc", "SOURCE_LOC"),
    ("LayoutState", "LAYOUT_STATE"),
)

LAYOUT_STATES = (
    ("Building", "BUILDING"),
    ("Encoding", "ENCODING"),
    ("Relaxing", "RELAXING"),
    ("LaidOut", "LAID_OUT"),
    ("Committed", "COMMITTED"),
)

OPERAND_SYMBOLS = {
    "kInvalid": "INVALID",
    "kRegister": "REGISTER",
    "kImmediate": "IMMEDIATE",
    "kSFPImmediate": "SINGLE_FLOAT",
    "kDFPImmediate": "DOUBLE_FLOAT",
    "kExpr": "EXPRESSION",
    "kInst": "INSTRUCTION",
}

EXPRESSION_SYMBOLS = {
    "Binary": "BINARY",
    "Constant": "CONSTANT",
    "SymbolRef": "SYMBOL_REF",
    "Unary": "UNARY",
    "Target": "TARGET_VARIANT",
}

FRAGMENT_SYMBOLS = {
    "FT_Align": "ALIGN",
    "FT_Data": "DATA",
    "FT_CompactEncodedInst": "ENCODED_WITH_FIXUPS",
    "FT_Fill": "FILL",
    "FT_Nops": "NOP",
    "FT_Relaxable": "RELAXABLE",
    "FT_Org": "ORG",
    "FT_Dwarf": "DEBUG",
    "FT_DwarfFrame": "CFI",
    "FT_LEB": "LEB",
    "FT_BoundaryAlign": "BOUNDARY_ALIGN",
    "FT_SymbolId": "SYMBOL_ID",
    "FT_PseudoProbe": "PSEUDO_PROBE",
    "FT_Dummy": "DUMMY",
}


def enum_body(path, pattern):
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//.*", "", text)
    match = re.search(rf"{pattern}\s*\{{(.*?)\}}\s*;", text, re.DOTALL)
    if not match:
        raise ValueError(f"cannot find {pattern} in {path}")
    return match.group(1)


def parse_simple_enum(path, pattern, stop_before=None):
    body = enum_body(path, pattern)
    records = []
    values = {}
    value = 0
    for raw in body.split(","):
        entry = raw.strip()
        if not entry:
            continue
        if stop_before and re.match(rf"{stop_before}\b", entry):
            break
        match = re.fullmatch(
            r"([A-Za-z_][A-Za-z0-9_]*)"
            r"(?:\s*=\s*([A-Za-z_][A-Za-z0-9_]*|0x[0-9A-Fa-f]+|\d+))?",
            entry,
        )
        if not match:
            raise ValueError(f"cannot parse enum entry in {path}: {entry}")
        name, assigned = match.groups()
        if assigned:
            if assigned in values:
                value = values[assigned]
            else:
                value = int(assigned, 0)
        values[name] = value
        records.append((name, value))
        value += 1
    return records


def record(internal, symbol, llvm_value, requires_target=False):
    return {
        "internal": internal,
        "symbol": symbol,
        "llvm_value": llvm_value,
        "requires_target_schema": requires_target,
    }


def operand_inventory():
    parsed = parse_simple_enum(
        OPERAND_DEFINITIONS,
        r"enum\s+MachineOperandType\s*:\s*unsigned\s+char",
    )
    expected = list(OPERAND_SYMBOLS)
    if [name for name, _ in parsed] != expected:
        raise ValueError("MCOperand::MachineOperandType inventory changed")
    result = [
        record(name, OPERAND_SYMBOLS[name], value)
        for name, value in parsed
    ]
    result.append(
        record("TargetExtension", "TARGET_EXTENSION", 0xFFFFFFFF, True)
    )
    return result


def expression_inventory():
    parsed = parse_simple_enum(
        EXPRESSION_DEFINITIONS, r"enum\s+ExprKind\s*:\s*uint8_t"
    )
    expected = list(EXPRESSION_SYMBOLS)
    if [name for name, _ in parsed] != expected:
        raise ValueError("MCExpr::ExprKind inventory changed")
    return [
        record(
            name,
            EXPRESSION_SYMBOLS[name],
            value,
            name == "Target",
        )
        for name, value in parsed
    ]


def fragment_inventory():
    parsed = parse_simple_enum(
        FRAGMENT_DEFINITIONS, r"enum\s+FragmentType\s*:\s*uint8_t"
    )
    expected = list(FRAGMENT_SYMBOLS)
    if [name for name, _ in parsed] != expected:
        raise ValueError("MCFragment::FragmentType inventory changed")
    result = [
        record(name, FRAGMENT_SYMBOLS[name], value)
        for name, value in parsed
    ]
    result.append(
        record("FormatExtension", "FORMAT_EXTENSION", 0xFFFFFFFF, True)
    )
    return result


def public_symbol(name):
    name = re.sub(r"^FK_", "", name)
    name = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    name = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    name = re.sub(r"[^A-Za-z0-9]+", "_", name)
    return name.strip("_").upper()


def fixup_inventory():
    parsed = parse_simple_enum(
        FIXUP_DEFINITIONS,
        r"enum\s+MCFixupKind",
        stop_before="FirstTargetFixupKind",
    )
    generic = []
    for name, value in parsed:
        if not name.startswith("FK_"):
            raise ValueError(f"unexpected generic MCFixupKind entry: {name}")
        generic.append(record(name, public_symbol(name), value))
    if not generic or generic[-1]["internal"] != "FK_SecRel_8":
        raise ValueError("generic MCFixupKind inventory changed")
    generic.append(
        record("TargetExtension", "TARGET_EXTENSION", 0xFFFFFFFF, True)
    )
    return generic


def source_inventory():
    return {
        "entities": [
            record(internal, symbol, index)
            for index, (internal, symbol) in enumerate(ENTITIES)
        ],
        "operand_kinds": operand_inventory(),
        "expression_kinds": expression_inventory(),
        "fragment_kinds": fragment_inventory(),
        "fixup_kinds": fixup_inventory(),
        "layout_states": [
            record(internal, symbol, index)
            for index, (internal, symbol) in enumerate(LAYOUT_STATES)
        ],
    }


def schema_digest(document):
    payload = {
        key: value
        for key, value in document.items()
        if key != "stable_schema_digest"
    }
    encoded = json.dumps(
        payload, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def bootstrap_document(inventory):
    document = {
        "schema_version": 1,
        "capability": {"major": 1, "minor": 0},
        "stable_schema_digest": "",
    }
    for family, records in inventory.items():
        base = FAMILIES[family][0]
        document[family] = [
            {**entry, "id": f"0x{base + index:08x}"}
            for index, entry in enumerate(records, 1)
        ]
    document["stable_schema_digest"] = schema_digest(document)
    return document


def validate_document(document, inventory):
    if document.get("schema_version") != 1:
        raise ValueError("MC schema_version must be 1")
    if document.get("capability") != {"major": 1, "minor": 0}:
        raise ValueError("MC schema capability must be 1.0")
    all_ids = set()
    for family, expected in inventory.items():
        records = document.get(family)
        if not isinstance(records, list) or len(records) != len(expected):
            raise ValueError(f"MC {family} inventory size changed")
        base = FAMILIES[family][0]
        for index, (entry, source) in enumerate(zip(records, expected), 1):
            for key, value in source.items():
                if entry.get(key) != value:
                    raise ValueError(
                        f"MC {family} metadata changed for "
                        f"{source['internal']}: {key}"
                    )
            stable_id = entry.get("id")
            if not isinstance(stable_id, str) or not re.fullmatch(
                r"0x[0-9a-fA-F]{8}", stable_id
            ):
                raise ValueError(f"invalid MC {family} stable ID")
            parsed = int(stable_id, 16)
            if parsed != base + index or parsed in all_ids:
                raise ValueError(f"invalid or duplicate MC {family} ID")
            all_ids.add(parsed)
    digest = schema_digest(document)
    if document.get("stable_schema_digest") != digest:
        raise ValueError(
            "MC stable schema digest changed; review the capability "
            f"version before updating it to {digest}"
        )
    return digest


def macro_id(family, entry):
    return f"NEVERC_MC_{FAMILIES[family][1]}_{entry['symbol']}"


def c_uint32(value):
    if value == 0xFFFFFFFF:
        return "UINT32_MAX"
    return f"UINT32_C({value})"


def generate_body(document, digest):
    lines = [
        "#ifndef NEVERC_PLUGIN_MC_SCHEMA_CONSTANTS_DEFINED",
        "#define NEVERC_PLUGIN_MC_SCHEMA_CONSTANTS_DEFINED",
        "",
        "#define NEVERC_MC_SCHEMA_CAPABILITY_MAJOR UINT16_C(1)",
        "#define NEVERC_MC_SCHEMA_CAPABILITY_MINOR UINT16_C(0)",
        f"#define NEVERC_MC_SCHEMA_DIGEST {json.dumps(digest)}",
    ]
    for family in FAMILIES:
        lines.append(
            f"#define NEVERC_MC_{FAMILIES[family][1]}_COUNT "
            f"UINT32_C({len(document[family])})"
        )
    lines.append("")
    for family in FAMILIES:
        for entry in document[family]:
            lines.append(
                f"#define {macro_id(family, entry)} "
                f"UINT32_C({entry['id']})"
            )
        lines.append("")
    lines.append("#endif /* NEVERC_PLUGIN_MC_SCHEMA_CONSTANTS_DEFINED */")
    for family in FAMILIES:
        macro = f"NEVERC_MC_SCHEMA_{FAMILIES[family][1]}"
        lines.extend(["", f"#ifdef {macro}"])
        for entry in document[family]:
            requires_target = (
                "NEVERC_TRUE"
                if entry["requires_target_schema"]
                else "NEVERC_FALSE"
            )
            lines.append(
                f"{macro}({entry['internal']}, {entry['symbol']}, "
                f"{macro_id(family, entry)}, "
                f"{c_uint32(entry['llvm_value'])}, "
                f"{json.dumps(entry['symbol'].lower())}, "
                f"{requires_target})"
            )
        lines.append("#endif")
    return "\n".join(lines) + "\n"


def generate(template, document, digest):
    text = template.read_text(encoding="utf-8")
    marker = "@MC_SCHEMA_BODY@"
    if text.count(marker) != 1:
        raise ValueError("MC schema template must contain one body marker")
    return text.replace(marker, generate_body(document, digest))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--bootstrap", action="store_true")
    parser.add_argument("--schema", type=Path, default=SCHEMA)
    parser.add_argument("--template", type=Path, default=TEMPLATE)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    if args.check and args.bootstrap:
        parser.error("--check and --bootstrap are mutually exclusive")
    try:
        inventory = source_inventory()
        if args.bootstrap:
            if args.schema.exists():
                raise ValueError(
                    f"refusing to overwrite existing MC schema: {args.schema}"
                )
            document = bootstrap_document(inventory)
            args.schema.parent.mkdir(parents=True, exist_ok=True)
            args.schema.write_text(
                json.dumps(document, indent=2, ensure_ascii=True) + "\n",
                encoding="utf-8",
            )
        document = json.loads(args.schema.read_text(encoding="utf-8"))
        digest = validate_document(document, inventory)
        generated = generate(args.template, document, digest)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"MC schema error: {error}", file=sys.stderr)
        return 1
    if args.check:
        existing = (
            args.output.read_text(encoding="utf-8")
            if args.output.exists()
            else ""
        )
        if existing != generated:
            print(f"{args.output} is out of date", file=sys.stderr)
            return 1
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
