#!/usr/bin/env python3

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OPERAND_DEFINITIONS = (
    ROOT / "llvm/include/llvm/CodeGen/MachineOperand.h"
)
OPCODE_DEFINITIONS = (
    ROOT / "llvm/include/llvm/Support/TargetOpcodes.def"
)
PROPERTY_DEFINITIONS = (
    ROOT / "llvm/include/llvm/CodeGen/MachineFunction.h"
)
SCHEMA = ROOT / "neverc/include/neverc/Plugin/Schema/MIRSchema.json"
TEMPLATE = (
    ROOT / "neverc/include/neverc/Plugin/Schema/PluginMIRSchema.inc.in"
)
OUTPUT = ROOT / "neverc/include/neverc/Plugin/Schema/PluginMIRSchema.inc"

FAMILIES = {
    "entities": (0x51000000, "ENTITY"),
    "operand_kinds": (0x52000000, "OPERAND"),
    "generic_opcodes": (0x53000000, "GENERIC_OPCODE"),
    "machine_properties": (0x54000000, "PROPERTY"),
}
ENTITIES = (
    ("MachineFunction", "MACHINE_FUNCTION"),
    ("MachineBasicBlock", "MACHINE_BASIC_BLOCK"),
    ("MachineInstr", "MACHINE_INSTR"),
    ("MachineOperand", "MACHINE_OPERAND"),
)


def enum_body(path, pattern):
    text = re.sub(r"//.*", "", path.read_text(encoding="utf-8"))
    match = re.search(rf"{pattern}\s*\{{(.*?)\}}\s*;", text, re.DOTALL)
    if not match:
        raise ValueError(f"cannot find {pattern} in {path}")
    return match.group(1)


def public_symbol(name):
    name = re.sub(r"^MO_", "", name)
    name = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    name = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    name = re.sub(r"[^A-Za-z0-9]+", "_", name)
    return name.strip("_").upper()


def operand_inventory():
    body = enum_body(
        OPERAND_DEFINITIONS,
        r"enum\s+MachineOperandType\s*:\s*unsigned\s+char",
    )
    records = []
    value = 0
    values = {}
    for raw in body.split(","):
        entry = raw.strip()
        if not entry:
            continue
        match = re.fullmatch(
            r"(MO_[A-Za-z0-9_]+)(?:\s*=\s*(MO_[A-Za-z0-9_]+|\d+))?",
            entry,
        )
        if not match:
            raise ValueError(f"cannot parse MachineOperandType entry: {entry}")
        name, assigned = match.groups()
        if assigned:
            value = int(assigned) if assigned.isdigit() else values[assigned]
        values[name] = value
        if name != "MO_Last":
            records.append(
                {
                    "internal": name,
                    "symbol": public_symbol(name),
                    "llvm_value": value,
                }
            )
        value += 1
    if [item["llvm_value"] for item in records] != list(range(len(records))):
        raise ValueError("MachineOperandType inventory is no longer contiguous")
    return records


def opcode_inventory():
    records = []
    pattern = re.compile(
        r"^\s*HANDLE_TARGET_OPCODE\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)"
    )
    for line in OPCODE_DEFINITIONS.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if match:
            name = match.group(1)
            records.append(
                {
                    "internal": name,
                    "symbol": name,
                    "llvm_value": len(records),
                    "requires_target_schema": False,
                }
            )
    if not records:
        raise ValueError("target-independent opcode inventory is empty")
    return records


def property_inventory():
    body = enum_body(
        PROPERTY_DEFINITIONS, r"enum\s+class\s+Property\s*:\s*unsigned"
    )
    records = []
    for raw in body.split(","):
        entry = raw.strip()
        if not entry:
            continue
        match = re.fullmatch(
            r"([A-Za-z_][A-Za-z0-9_]*)(?:\s*=\s*([A-Za-z_][A-Za-z0-9_]*))?",
            entry,
        )
        if not match:
            raise ValueError(f"cannot parse machine property entry: {entry}")
        name, alias = match.groups()
        if name == "LastProperty":
            continue
        if alias:
            raise ValueError(f"unexpected machine property alias: {entry}")
        records.append(
            {
                "internal": name,
                "symbol": public_symbol(name),
                "llvm_value": len(records),
            }
        )
    if not records:
        raise ValueError("machine property inventory is empty")
    return records


def source_inventory():
    return {
        "entities": [
            {"internal": internal, "symbol": symbol, "llvm_value": index}
            for index, (internal, symbol) in enumerate(ENTITIES)
        ],
        "operand_kinds": operand_inventory(),
        "generic_opcodes": opcode_inventory(),
        "machine_properties": property_inventory(),
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
            {**record, "id": f"0x{base + index:08x}"}
            for index, record in enumerate(records, 1)
        ]
    document["stable_schema_digest"] = schema_digest(document)
    return document


def validate_document(document, inventory):
    if document.get("schema_version") != 1:
        raise ValueError("MIR schema_version must be 1")
    if document.get("capability") != {"major": 1, "minor": 0}:
        raise ValueError("MIR schema capability must be 1.0")
    all_ids = set()
    for family, expected in inventory.items():
        records = document.get(family)
        if not isinstance(records, list) or len(records) != len(expected):
            raise ValueError(f"MIR {family} inventory size changed")
        base = FAMILIES[family][0]
        for index, (record, source) in enumerate(zip(records, expected), 1):
            for key, value in source.items():
                if record.get(key) != value:
                    raise ValueError(
                        f"MIR {family} metadata changed for "
                        f"{source['internal']}: {key}"
                    )
            stable_id = record.get("id")
            if not isinstance(stable_id, str) or not re.fullmatch(
                r"0x[0-9a-fA-F]{8}", stable_id
            ):
                raise ValueError(f"invalid MIR {family} stable ID")
            parsed = int(stable_id, 16)
            if parsed != base + index or parsed in all_ids:
                raise ValueError(f"invalid or duplicate MIR {family} ID")
            all_ids.add(parsed)
    digest = schema_digest(document)
    if document.get("stable_schema_digest") != digest:
        raise ValueError(
            "MIR stable schema digest changed; review the capability "
            f"version before updating it to {digest}"
        )
    return digest


def macro_id(family, record):
    return f"NEVERC_MIR_{FAMILIES[family][1]}_{record['symbol']}"


def generate_body(document, digest):
    lines = [
        "#ifndef NEVERC_PLUGIN_MIR_SCHEMA_CONSTANTS_DEFINED",
        "#define NEVERC_PLUGIN_MIR_SCHEMA_CONSTANTS_DEFINED",
        "",
        "#define NEVERC_MIR_SCHEMA_CAPABILITY_MAJOR UINT16_C(1)",
        "#define NEVERC_MIR_SCHEMA_CAPABILITY_MINOR UINT16_C(0)",
        f"#define NEVERC_MIR_SCHEMA_DIGEST {json.dumps(digest)}",
    ]
    for family, records in (
        (name, document[name]) for name in FAMILIES
    ):
        lines.append(
            f"#define NEVERC_MIR_{FAMILIES[family][1]}_COUNT "
            f"UINT32_C({len(records)})"
        )
    lines.append("")
    for family, records in (
        (name, document[name]) for name in FAMILIES
    ):
        for record in records:
            lines.append(
                f"#define {macro_id(family, record)} "
                f"UINT32_C({record['id']})"
            )
        lines.append("")
    lines.append("#endif /* NEVERC_PLUGIN_MIR_SCHEMA_CONSTANTS_DEFINED */")
    for family, records in (
        (name, document[name]) for name in FAMILIES
    ):
        macro = f"NEVERC_MIR_SCHEMA_{FAMILIES[family][1]}"
        lines.extend(["", f"#ifdef {macro}"])
        for record in records:
            arguments = (
                f"{record['internal']}, {record['symbol']}, "
                f"{macro_id(family, record)}, "
                f"UINT32_C({record['llvm_value']}), "
                f"{json.dumps(record['symbol'].lower())}"
            )
            if family == "generic_opcodes":
                arguments += ", NEVERC_FALSE"
            lines.append(f"{macro}({arguments})")
        lines.append("#endif")
    return "\n".join(lines) + "\n"


def generate(template, document, digest):
    text = template.read_text(encoding="utf-8")
    marker = "@MIR_SCHEMA_BODY@"
    if text.count(marker) != 1:
        raise ValueError("MIR schema template must contain one body marker")
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
                    f"refusing to overwrite existing MIR schema: {args.schema}"
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
        print(f"MIR schema error: {error}", file=sys.stderr)
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
