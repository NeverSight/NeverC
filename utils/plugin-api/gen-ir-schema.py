#!/usr/bin/env python3

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LLVM_IR = ROOT / "llvm/include/llvm/IR"
INSTRUCTION_DEFINITIONS = LLVM_IR / "Instruction.def"
VALUE_DEFINITIONS = LLVM_IR / "Value.def"
TYPE_DEFINITIONS = LLVM_IR / "Type.h"
PREDICATE_DEFINITIONS = LLVM_IR / "InstrTypes.h"
GLOBAL_VALUE_DEFINITIONS = LLVM_IR / "GlobalValue.h"
CALLING_CONVENTION_DEFINITIONS = LLVM_IR / "CallingConv.h"
SCHEMA = ROOT / "neverc/include/neverc/Plugin/Schema/IRSchema.json"
TEMPLATE = (
    ROOT / "neverc/include/neverc/Plugin/Schema/PluginIRSchema.inc.in"
)
OUTPUT = ROOT / "neverc/include/neverc/Plugin/Schema/PluginIRSchema.inc"

UINT32_MAX = 0xFFFFFFFF
FAMILIES = {
    "type_kinds": (0x41000000, "TYPE"),
    "value_kinds": (0x42000000, "VALUE"),
    "opcodes": (0x43000000, "OPCODE"),
    "predicates": (0x44000000, "PREDICATE"),
    "linkages": (0x45000000, "LINKAGE"),
    "visibilities": (0x46000000, "VISIBILITY"),
    "calling_conventions": (0x47000000, "CALLING_CONVENTION"),
    "attribute_locations": (0x48000000, "ATTRIBUTE_LOCATION"),
    "properties": (0x49000000, "PROPERTY"),
}
COUNT_NAMES = {
    "type_kinds": "TYPE_KIND",
    "value_kinds": "VALUE_KIND",
}

VALUE_MACRO_CATEGORIES = {
    "GLOBAL_VALUE": "global",
    "CONSTANT": "constant",
    "CONSTANT_EXCLUDE_LLVM_C_API": "constant",
    "VALUE": "value",
    "METADATA_VALUE": "metadata",
    "INLINE_ASM_VALUE": "inline_asm",
    "MEMORY_VALUE": "memory_ssa",
    "INSTRUCTION": "instruction",
}

PROPERTY_DEFINITIONS = [
    ("name", "string"),
    ("fast_math_flags", "flags"),
    ("nuw", "bool"),
    ("nsw", "bool"),
    ("exact", "bool"),
    ("disjoint", "bool"),
    ("volatile", "bool"),
    ("alignment", "u64"),
    ("atomic_ordering", "enum"),
    ("sync_scope", "string"),
    ("predicate", "enum"),
    ("calling_convention", "enum"),
    ("tail_call_kind", "enum"),
    ("indices", "u32_array"),
    ("weak", "bool"),
    ("success_ordering", "enum"),
    ("failure_ordering", "enum"),
    ("inbounds", "bool"),
    ("source_element_type", "type"),
    ("allocated_type", "type"),
    ("attributes", "attribute_set"),
    ("cleanup", "bool"),
    ("nusw", "bool"),
]

ATTRIBUTE_LOCATIONS = [
    ("Return", "RETURN"),
    ("Function", "FUNCTION"),
    ("Parameter", "PARAMETER"),
]

OPERAND_COUNTS = {
    "Ret": (0, 1),
    "Br": (1, 3),
    "Switch": (2, None),
    "IndirectBr": (1, None),
    "Invoke": (3, None),
    "Resume": (1, 1),
    "Unreachable": (0, 0),
    "CleanupRet": (1, 2),
    "CatchRet": (2, 2),
    "CatchSwitch": (2, None),
    "CallBr": (2, None),
    "FNeg": (1, 1),
    "Alloca": (1, 1),
    "Load": (1, 1),
    "Store": (2, 2),
    "GetElementPtr": (2, None),
    "Fence": (0, 0),
    "AtomicCmpXchg": (3, 3),
    "AtomicRMW": (2, 2),
    "CleanupPad": (1, None),
    "CatchPad": (1, None),
    "ICmp": (2, 2),
    "FCmp": (2, 2),
    "PHI": (0, None),
    "Call": (1, None),
    "Select": (3, 3),
    "UserOp1": (0, None),
    "UserOp2": (0, None),
    "VAArg": (1, 1),
    "ExtractElement": (2, 2),
    "InsertElement": (3, 3),
    "ShuffleVector": (2, 2),
    "ExtractValue": (1, 1),
    "InsertValue": (2, 2),
    "LandingPad": (0, None),
    "Freeze": (1, 1),
}

VOID_RESULTS = {
    "Ret",
    "Br",
    "Switch",
    "IndirectBr",
    "Resume",
    "Unreachable",
    "CleanupRet",
    "CatchRet",
    "Store",
    "Fence",
}
SAME_OPERAND_ZERO_RESULTS = {
    "FNeg",
    "Add",
    "FAdd",
    "Sub",
    "FSub",
    "Mul",
    "FMul",
    "UDiv",
    "SDiv",
    "FDiv",
    "URem",
    "SRem",
    "FRem",
    "Shl",
    "LShr",
    "AShr",
    "And",
    "Or",
    "Xor",
    "AtomicRMW",
    "PHI",
    "Freeze",
}
I1_RESULTS = {"ICmp", "FCmp"}
POINTER_RESULTS = {"Alloca", "GetElementPtr"}
TOKEN_RESULTS = {"CleanupPad", "CatchPad", "CatchSwitch"}
AGGREGATE_RESULTS = {"AtomicCmpXchg", "LandingPad"}

ALWAYS_SIDE_EFFECTING = {
    "Store",
    "Fence",
    "AtomicCmpXchg",
    "AtomicRMW",
    "Resume",
}
CONDITIONAL_SIDE_EFFECTING = {
    "Load",
    "Invoke",
    "Call",
    "CallBr",
    "VAArg",
    "CatchSwitch",
}

OPCODE_PROPERTIES = {
    "FNeg": {"fast_math_flags"},
    "FAdd": {"fast_math_flags"},
    "FSub": {"fast_math_flags"},
    "FMul": {"fast_math_flags"},
    "FDiv": {"fast_math_flags"},
    "FRem": {"fast_math_flags"},
    "Add": {"nuw", "nsw"},
    "Sub": {"nuw", "nsw"},
    "Mul": {"nuw", "nsw"},
    "Shl": {"nuw", "nsw"},
    "UDiv": {"exact"},
    "SDiv": {"exact"},
    "LShr": {"exact"},
    "AShr": {"exact"},
    "Or": {"disjoint"},
    "Alloca": {"alignment", "allocated_type"},
    "Load": {"volatile", "alignment", "atomic_ordering", "sync_scope"},
    "Store": {"volatile", "alignment", "atomic_ordering", "sync_scope"},
    "GetElementPtr": {
        "inbounds",
        "nuw",
        "nusw",
        "source_element_type",
    },
    "Fence": {"atomic_ordering", "sync_scope"},
    "AtomicCmpXchg": {
        "volatile",
        "weak",
        "success_ordering",
        "failure_ordering",
        "sync_scope",
        "alignment",
    },
    "AtomicRMW": {
        "volatile",
        "atomic_ordering",
        "sync_scope",
        "alignment",
    },
    "ICmp": {"predicate"},
    "FCmp": {"predicate", "fast_math_flags"},
    "Call": {
        "calling_convention",
        "tail_call_kind",
        "attributes",
        "fast_math_flags",
    },
    "Invoke": {"calling_convention", "attributes"},
    "CallBr": {"calling_convention", "attributes"},
    "ExtractValue": {"indices"},
    "InsertValue": {"indices"},
    "LandingPad": {"cleanup"},
}


def public_symbol(name):
    symbol = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    symbol = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", symbol)
    symbol = re.sub(r"[^A-Za-z0-9]+", "_", symbol).strip("_").upper()
    if not re.fullmatch(r"[A-Z_][A-Z0-9_]*", symbol):
        raise ValueError(f"cannot form a public symbol for {name}")
    return symbol


def without_comments(text):
    return re.sub(r"//.*", "", text)


def enum_body(path, enum_pattern):
    text = without_comments(path.read_text(encoding="utf-8"))
    match = re.search(
        rf"{enum_pattern}\s*\{{(?P<body>.*?)\}}\s*;",
        text,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"cannot find {enum_pattern} in {path}")
    return match.group("body")


def parse_sequential_enum(path, enum_pattern, suffix, ignored=()):
    body = enum_body(path, enum_pattern)
    records = []
    next_value = 0
    values = {}
    for raw_entry in body.split(","):
        entry = raw_entry.strip()
        if not entry:
            continue
        match = re.fullmatch(
            r"([A-Za-z_][A-Za-z0-9_]*)"
            r"(?:\s*=\s*([A-Za-z_][A-Za-z0-9_]*|\d+))?",
            entry,
        )
        if not match:
            raise ValueError(f"cannot parse enum entry in {path}: {entry}")
        name, assigned = match.groups()
        if assigned is not None:
            next_value = (
                int(assigned)
                if assigned.isdigit()
                else values[assigned]
            )
        values[name] = next_value
        if name not in ignored and (not suffix or name.endswith(suffix)):
            stem = name[: -len(suffix)] if suffix else name
            records.append(
                {
                    "internal": name,
                    "symbol": public_symbol(stem),
                    "llvm_value": next_value,
                }
            )
        next_value += 1
    return records


def parse_instructions(path=INSTRUCTION_DEFINITIONS):
    records = []
    pattern = re.compile(
        r"^HANDLE_(TERM|UNARY|BINARY|MEMORY|CAST|FUNCLETPAD|OTHER|USER)_INST"
        r"\s*\(\s*(\d+)\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*,"
        r"\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)"
    )
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("//", 1)[0].strip()
        match = pattern.fullmatch(line)
        if not match:
            continue
        category, number, internal, class_name = match.groups()
        records.append(
            {
                "internal": internal,
                "symbol": public_symbol(internal),
                "llvm_value": int(number),
                "class_name": class_name,
                "category": category.lower(),
            }
        )
    if not records or [item["llvm_value"] for item in records] != list(
        range(1, len(records) + 1)
    ):
        raise ValueError("LLVM instruction inventory is not contiguous")
    return records


def parse_values(path=VALUE_DEFINITIONS):
    records = []
    pattern = re.compile(
        r"^HANDLE_(GLOBAL_VALUE|CONSTANT|CONSTANT_EXCLUDE_LLVM_C_API|"
        r"VALUE|METADATA_VALUE|INLINE_ASM_VALUE|MEMORY_VALUE|INSTRUCTION)"
        r"\(([A-Za-z_][A-Za-z0-9_]*)\)$"
    )
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("//", 1)[0].strip()
        if line.startswith("#"):
            continue
        match = pattern.fullmatch(line)
        if not match:
            continue
        macro, name = match.groups()
        records.append(
            {
                "internal": f"{name}Val",
                "symbol": public_symbol(name),
                "llvm_value": len(records),
                "category": VALUE_MACRO_CATEGORIES[macro],
            }
        )
    if not records or records[-1]["internal"] != "InstructionVal":
        raise ValueError("LLVM value inventory must end in InstructionVal")
    return records


def parse_predicates(path=PREDICATE_DEFINITIONS):
    body = enum_body(path, r"enum\s+Predicate\s*:\s*unsigned")
    records = []
    for name, value in re.findall(
        r"\b((?:FCMP|ICMP)_[A-Z0-9_]+)\s*=\s*(\d+)", body
    ):
        records.append(
            {
                "internal": name,
                "symbol": name,
                "llvm_value": int(value),
                "category": "float" if name.startswith("FCMP") else "integer",
            }
        )
    if len(records) != 26:
        raise ValueError("LLVM comparison predicate inventory changed")
    return records


def source_inventory():
    return {
        "type_kinds": parse_sequential_enum(
            TYPE_DEFINITIONS, r"enum\s+TypeID", "TyID"
        ),
        "value_kinds": parse_values(),
        "opcodes": parse_instructions(),
        "predicates": parse_predicates(),
        "linkages": parse_sequential_enum(
            GLOBAL_VALUE_DEFINITIONS,
            r"enum\s+LinkageTypes",
            "Linkage",
        ),
        "visibilities": parse_sequential_enum(
            GLOBAL_VALUE_DEFINITIONS,
            r"enum\s+VisibilityTypes",
            "Visibility",
        ),
        "calling_conventions": parse_sequential_enum(
            CALLING_CONVENTION_DEFINITIONS,
            r"enum",
            "",
            ignored=("FirstTargetCC", "MaxID"),
        ),
    }


def default_operand_count(record):
    if record["internal"] in OPERAND_COUNTS:
        return OPERAND_COUNTS[record["internal"]]
    if record["category"] == "binary":
        return 2, 2
    if record["category"] == "cast":
        return 1, 1
    return 0, None


def result_constraint(internal):
    if internal in VOID_RESULTS:
        return "void"
    if internal in SAME_OPERAND_ZERO_RESULTS:
        return "same_as_operand_0"
    if internal in I1_RESULTS:
        return "i1"
    if internal in POINTER_RESULTS:
        return "pointer"
    if internal in TOKEN_RESULTS:
        return "token"
    if internal in AGGREGATE_RESULTS:
        return "aggregate"
    return "declared_type"


def side_effect_class(internal):
    if internal in ALWAYS_SIDE_EFFECTING:
        return "always"
    if internal in CONDITIONAL_SIDE_EFFECTING:
        return "conditional"
    return "never"


def is_terminator(record):
    return record["category"] == "term"


def add_stable_ids(records, family):
    base = FAMILIES[family][0]
    return [
        {**record, "id": f"0x{base + index:08x}"}
        for index, record in enumerate(records, 1)
    ]


def bootstrap_document(inventory):
    opcodes = []
    for record in inventory["opcodes"]:
        minimum, maximum = default_operand_count(record)
        properties = set(OPCODE_PROPERTIES.get(record["internal"], set()))
        if result_constraint(record["internal"]) != "void":
            properties.add("name")
        opcodes.append(
            {
                **record,
                "minimum_operands": minimum,
                "maximum_operands": maximum,
                "result_constraint": result_constraint(record["internal"]),
                "terminator": is_terminator(record),
                "side_effects": side_effect_class(record["internal"]),
                "writable_properties": sorted(properties),
            }
        )

    properties = [
        {
            "internal": name,
            "symbol": public_symbol(name),
            "value_type": value_type,
            "flag": f"0x{1 << index:016x}",
        }
        for index, (name, value_type) in enumerate(PROPERTY_DEFINITIONS)
    ]
    attribute_locations = [
        {"internal": internal, "symbol": symbol}
        for internal, symbol in ATTRIBUTE_LOCATIONS
    ]
    document = {
        "schema_version": 1,
        "capability": {"major": 1, "minor": 0},
        "stable_schema_digest": "",
    }
    for family in FAMILIES:
        if family == "opcodes":
            records = opcodes
        elif family == "properties":
            records = properties
        elif family == "attribute_locations":
            records = attribute_locations
        else:
            records = inventory[family]
        document[family] = add_stable_ids(records, family)
    document["stable_schema_digest"] = schema_digest(document)
    return document


def parse_id(value, description):
    if not isinstance(value, str) or not re.fullmatch(
        r"0x[0-9a-fA-F]{8}", value
    ):
        raise ValueError(f"{description} must be an eight-digit hex string")
    parsed = int(value, 16)
    if parsed == 0 or parsed > UINT32_MAX:
        raise ValueError(f"{description} is outside uint32")
    return parsed


def digest_payload(document):
    return {
        key: value
        for key, value in document.items()
        if key != "stable_schema_digest"
    }


def schema_digest(document):
    encoded = json.dumps(
        digest_payload(document),
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=True,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def validate_source_family(document, inventory, family, all_ids):
    entries = document.get(family)
    expected = inventory[family]
    if not isinstance(entries, list) or len(entries) != len(expected):
        raise ValueError(f"IR {family} inventory size changed")
    base = FAMILIES[family][0]
    for index, (entry, source) in enumerate(zip(entries, expected), 1):
        for key, value in source.items():
            if entry.get(key) != value:
                raise ValueError(
                    f"IR {family} source metadata changed for "
                    f"{source['internal']}: {key}"
                )
        stable_id = parse_id(entry.get("id"), f"{family} ID")
        if not base < stable_id < base + 0x01000000:
            raise ValueError(f"IR {family} ID is outside its family")
        if stable_id in all_ids:
            raise ValueError("duplicate IR stable ID")
        all_ids.add(stable_id)
        if stable_id != base + index:
            raise ValueError(f"IR {family} schema order is not stable")
    return entries


def validate_document(document, inventory):
    if document.get("schema_version") != 1:
        raise ValueError("IR schema_version must be 1")
    if document.get("capability") != {"major": 1, "minor": 0}:
        raise ValueError("IR schema capability must be 1.0")

    all_ids = set()
    validated = {}
    for family in (
        "type_kinds",
        "value_kinds",
        "opcodes",
        "predicates",
        "linkages",
        "visibilities",
        "calling_conventions",
    ):
        validated[family] = validate_source_family(
            document, inventory, family, all_ids
        )

    property_names = {
        name: (public_symbol(name), value_type)
        for name, value_type in PROPERTY_DEFINITIONS
    }
    properties = document.get("properties")
    if not isinstance(properties, list) or len(properties) != len(
        property_names
    ):
        raise ValueError("IR property inventory changed")
    for index, entry in enumerate(properties):
        internal = entry.get("internal")
        if internal not in property_names:
            raise ValueError(f"unknown IR property {internal}")
        symbol, value_type = property_names[internal]
        if entry.get("symbol") != symbol or entry.get(
            "value_type"
        ) != value_type:
            raise ValueError(f"stale IR property metadata for {internal}")
        expected_flag = f"0x{1 << index:016x}"
        if entry.get("flag") != expected_flag:
            raise ValueError(f"stale IR property flag for {internal}")
    validated["properties"] = properties

    locations = document.get("attribute_locations")
    expected_locations = [
        {"internal": internal, "symbol": symbol}
        for internal, symbol in ATTRIBUTE_LOCATIONS
    ]
    if not isinstance(locations, list) or len(locations) != len(
        expected_locations
    ):
        raise ValueError("IR attribute-location inventory changed")
    for entry, expected in zip(locations, expected_locations):
        for key, value in expected.items():
            if entry.get(key) != value:
                raise ValueError("stale IR attribute-location metadata")
    validated["attribute_locations"] = locations

    for family in ("properties", "attribute_locations"):
        base = FAMILIES[family][0]
        for index, entry in enumerate(validated[family], 1):
            stable_id = parse_id(entry.get("id"), f"{family} ID")
            if stable_id != base + index or stable_id in all_ids:
                raise ValueError(f"invalid or duplicate IR {family} ID")
            all_ids.add(stable_id)

    valid_properties = set(property_names)
    for entry in validated["opcodes"]:
        minimum = entry.get("minimum_operands")
        maximum = entry.get("maximum_operands")
        if (
            not isinstance(minimum, int)
            or isinstance(minimum, bool)
            or minimum < 0
            or (
                maximum is not None
                and (
                    not isinstance(maximum, int)
                    or isinstance(maximum, bool)
                    or maximum < minimum
                )
            )
        ):
            raise ValueError(f"invalid operand contract for {entry['internal']}")
        if entry.get("result_constraint") not in {
            "void",
            "declared_type",
            "same_as_operand_0",
            "i1",
            "pointer",
            "token",
            "aggregate",
        }:
            raise ValueError(
                f"invalid result constraint for {entry['internal']}"
            )
        if not isinstance(entry.get("terminator"), bool):
            raise ValueError(f"invalid terminator flag for {entry['internal']}")
        if entry.get("side_effects") not in {
            "never",
            "conditional",
            "always",
        }:
            raise ValueError(
                f"invalid side-effect class for {entry['internal']}"
            )
        writable = entry.get("writable_properties")
        if (
            not isinstance(writable, list)
            or len(writable) != len(set(writable))
            or not set(writable) <= valid_properties
        ):
            raise ValueError(
                f"invalid writable properties for {entry['internal']}"
            )

    digest = schema_digest(document)
    if document.get("stable_schema_digest") != digest:
        raise ValueError(
            "IR stable schema digest changed; review the capability version "
            f"before updating it to {digest}"
        )
    return validated, digest


def c_string(value):
    return json.dumps(value, ensure_ascii=True)


def boolean(value):
    return "NEVERC_TRUE" if value else "NEVERC_FALSE"


def macro_id(family, entry):
    prefix = FAMILIES[family][1]
    return f"NEVERC_IR_{prefix}_{entry['symbol']}"


def integer_macro(value):
    if value is None:
        return "NEVERC_IR_OPERAND_VARIADIC"
    return f"UINT32_C({value})"


def property_flags(entry, properties):
    by_name = {item["internal"]: item for item in properties}
    flags = [
        f"NEVERC_IR_PROPERTY_FLAG_{by_name[name]['symbol']}"
        for name in entry["writable_properties"]
    ]
    return " | ".join(flags) if flags else "NEVERC_IR_PROPERTY_FLAG_NONE"


def opcode_arguments(entry, properties):
    category = entry["category"].upper()
    result = entry["result_constraint"].upper()
    side_effects = entry["side_effects"].upper()
    return (
        f"{entry['symbol']}, {macro_id('opcodes', entry)}, "
        f"{entry['internal']}, NEVERC_IR_OPCODE_CATEGORY_{category}, "
        f"{integer_macro(entry['minimum_operands'])}, "
        f"{integer_macro(entry['maximum_operands'])}, "
        f"NEVERC_IR_RESULT_{result}, {boolean(entry['terminator'])}, "
        f"NEVERC_IR_SIDE_EFFECT_{side_effects}, "
        f"{property_flags(entry, properties)}, "
        f"{c_string(entry['symbol'].lower())}"
    )


def generate_body(document, validated, digest):
    lines = [
        "#ifndef NEVERC_PLUGIN_IR_SCHEMA_CONSTANTS_DEFINED",
        "#define NEVERC_PLUGIN_IR_SCHEMA_CONSTANTS_DEFINED",
        "",
        "#define NEVERC_IR_SCHEMA_CAPABILITY_MAJOR UINT16_C(1)",
        "#define NEVERC_IR_SCHEMA_CAPABILITY_MINOR UINT16_C(0)",
        f"#define NEVERC_IR_SCHEMA_DIGEST {c_string(digest)}",
    ]
    for family in FAMILIES:
        count_name = COUNT_NAMES.get(family, FAMILIES[family][1])
        lines.append(
            f"#define NEVERC_IR_{count_name}_COUNT "
            f"UINT32_C({len(validated[family])})"
        )
    lines.append("")
    for family in FAMILIES:
        for entry in validated[family]:
            lines.append(
                f"#define {macro_id(family, entry)} "
                f"UINT32_C({entry['id']})"
            )
        if family == "properties":
            lines.append(
                "#define NEVERC_IR_PROPERTY_FLAG_NONE UINT64_C(0)"
            )
            for entry in validated[family]:
                lines.append(
                    f"#define NEVERC_IR_PROPERTY_FLAG_{entry['symbol']} "
                    f"UINT64_C({entry['flag']})"
                )
        lines.append("")
    lines.append("#endif /* NEVERC_PLUGIN_IR_SCHEMA_CONSTANTS_DEFINED */")

    internal_families = {
        "type_kinds": "TYPE",
        "value_kinds": "VALUE",
        "opcodes": "OPCODE",
        "predicates": "PREDICATE",
        "linkages": "LINKAGE",
        "visibilities": "VISIBILITY",
        "calling_conventions": "CALLING_CONVENTION",
    }
    for family, macro_suffix in internal_families.items():
        macro = f"NEVERC_IR_SCHEMA_INTERNAL_{macro_suffix}"
        lines.extend(["", f"#ifdef {macro}"])
        for entry in validated[family]:
            lines.append(
                f"{macro}({entry['internal']}, {entry['symbol']}, "
                f"{macro_id(family, entry)})"
            )
        lines.append("#endif")

    lines.extend(["", "#ifdef NEVERC_IR_SCHEMA_ATTRIBUTE_LOCATION"])
    for entry in validated["attribute_locations"]:
        lines.append(
            "NEVERC_IR_SCHEMA_ATTRIBUTE_LOCATION("
            f"{entry['internal']}, {entry['symbol']}, "
            f"{macro_id('attribute_locations', entry)})"
        )
    lines.append("#endif")

    lines.extend(["", "#ifdef NEVERC_IR_SCHEMA_PROPERTY"])
    for entry in validated["properties"]:
        lines.append(
            "NEVERC_IR_SCHEMA_PROPERTY("
            f"{entry['symbol']}, {macro_id('properties', entry)}, "
            f"NEVERC_IR_PROPERTY_FLAG_{entry['symbol']}, "
            f"{c_string(entry['value_type'])})"
        )
    lines.append("#endif")

    lines.extend(["", "#ifdef NEVERC_IR_SCHEMA_OPCODE"])
    for entry in validated["opcodes"]:
        lines.append(
            f"NEVERC_IR_SCHEMA_OPCODE("
            f"{opcode_arguments(entry, validated['properties'])})"
        )
    lines.append("#endif")

    lines.extend(["", "#ifdef NEVERC_IR_SCHEMA_VERIFY_OPCODE"])
    for entry in validated["opcodes"]:
        lines.append(
            f"NEVERC_IR_SCHEMA_VERIFY_OPCODE("
            f"{opcode_arguments(entry, validated['properties'])})"
        )
    lines.append("#endif")
    return "\n".join(lines) + "\n"


def generate(template_path, document, validated, digest):
    template = template_path.read_text(encoding="utf-8")
    marker = "@IR_SCHEMA_BODY@"
    if template.count(marker) != 1:
        raise ValueError("IR schema template must contain one body marker")
    return template.replace(
        marker, generate_body(document, validated, digest)
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if the checked-in schema or output is stale",
    )
    parser.add_argument(
        "--bootstrap",
        action="store_true",
        help="create the initial explicit stable-ID schema",
    )
    parser.add_argument("--schema", type=Path, default=SCHEMA)
    parser.add_argument("--template", type=Path, default=TEMPLATE)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    arguments = parser.parse_args()
    if arguments.check and arguments.bootstrap:
        parser.error("--check and --bootstrap are mutually exclusive")

    try:
        inventory = source_inventory()
        if arguments.bootstrap:
            if arguments.schema.exists():
                raise ValueError(
                    f"refusing to overwrite existing IR schema: "
                    f"{arguments.schema}"
                )
            document = bootstrap_document(inventory)
            arguments.schema.parent.mkdir(parents=True, exist_ok=True)
            arguments.schema.write_text(
                json.dumps(document, indent=2, ensure_ascii=True) + "\n",
                encoding="utf-8",
            )
        document = json.loads(arguments.schema.read_text(encoding="utf-8"))
        validated, digest = validate_document(document, inventory)
        generated = generate(
            arguments.template, document, validated, digest
        )
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"IR schema error: {error}", file=sys.stderr)
        return 1

    if arguments.check:
        try:
            existing = arguments.output.read_text(encoding="utf-8")
        except OSError:
            existing = ""
        if existing != generated:
            print(f"{arguments.output} is out of date", file=sys.stderr)
            return 1
        return 0

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(generated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
