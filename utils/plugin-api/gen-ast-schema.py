#!/usr/bin/env python3

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DECL_DEFINITIONS = ROOT / "neverc/include/neverc/Tree/DeclNodes.td.h"
STMT_DEFINITIONS = ROOT / "neverc/include/neverc/Tree/StmtNodes.td.h"
TYPE_DEFINITIONS = ROOT / "neverc/include/neverc/Tree/TypeNodes.td.h"
ATTR_DEFINITIONS = ROOT / "neverc/include/neverc/Foundation/AttrList.td.h"
ATTR_CLASSES = ROOT / "neverc/include/neverc/Tree/Attrs.td.h"
SCHEMA = ROOT / "neverc/include/neverc/Plugin/Schema/ASTSchema.json"
TEMPLATE = (
    ROOT / "neverc/include/neverc/Plugin/Schema/PluginASTSchema.inc.in"
)
OUTPUT = ROOT / "neverc/include/neverc/Plugin/Schema/PluginASTSchema.inc"

NODE_ID_BASES = {
    "decl": 0x31000000,
    "stmt": 0x32000000,
    "type": 0x33000000,
    "type_loc": 0x34000000,
    "attr": 0x35000000,
}
PROPERTY_ID_BASE = 0x36000000
CHILD_SLOT_ID_BASE = 0x37000000
UINT32_MAX = 0xFFFFFFFF

DOMAIN_MACROS = {
    "decl": "DECL",
    "stmt": "STMT",
    "type": "TYPE",
    "type_loc": "TYPE_LOC",
    "attr": "ATTR",
}
VALUE_TYPE_MACROS = {
    "bool": "NEVERC_AST_VALUE_BOOL",
    "i64": "NEVERC_AST_VALUE_I64",
    "u64": "NEVERC_AST_VALUE_U64",
    "string": "NEVERC_AST_VALUE_STRING",
    "source_range": "NEVERC_AST_VALUE_SOURCE_RANGE",
    "node": "NEVERC_AST_VALUE_NODE",
    "decl": "NEVERC_AST_VALUE_DECL",
    "stmt": "NEVERC_AST_VALUE_STMT",
    "expr": "NEVERC_AST_VALUE_EXPR",
    "type": "NEVERC_AST_VALUE_TYPE",
    "type_loc": "NEVERC_AST_VALUE_TYPE_LOC",
    "attr": "NEVERC_AST_VALUE_ATTR",
    "identifier": "NEVERC_AST_VALUE_IDENTIFIER",
    "enum": "NEVERC_AST_VALUE_ENUM",
    "version": "NEVERC_AST_VALUE_VERSION",
    "parameter_index": "NEVERC_AST_VALUE_PARAMETER_INDEX",
    "alignment_operand": "NEVERC_AST_VALUE_ALIGNMENT_OPERAND",
}
ACCESS_MACROS = {
    "read_only": "NEVERC_AST_ACCESS_READ_ONLY",
    "read_write": "NEVERC_AST_ACCESS_READ_WRITE",
    "build_only": "NEVERC_AST_ACCESS_BUILD_ONLY",
}
CARDINALITY_MACROS = {
    "required": "NEVERC_AST_CARDINALITY_REQUIRED",
    "optional": "NEVERC_AST_CARDINALITY_OPTIONAL",
    "many": "NEVERC_AST_CARDINALITY_MANY",
}
SOURCE_RANGE_MACROS = {
    "none": "NEVERC_AST_SOURCE_RANGE_NONE",
    "native": "NEVERC_AST_SOURCE_RANGE_NATIVE",
}


def split_arguments(text):
    arguments = []
    start = 0
    depths = {"(": 0, "<": 0, "[": 0, "{": 0}
    closes = {")": "(", ">": "<", "]": "[", "}": "{"}
    quote = None
    escaped = False
    for index, character in enumerate(text):
        if quote is not None:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == quote:
                quote = None
            continue
        if character in ("'", '"'):
            quote = character
        elif character in depths:
            depths[character] += 1
        elif character in closes:
            depths[closes[character]] -= 1
        elif character == "," and not any(depths.values()):
            arguments.append(text[start:index].strip())
            start = index + 1
    arguments.append(text[start:].strip())
    return [argument for argument in arguments if argument]


def macro_call(text):
    match = re.fullmatch(r"([A-Z][A-Z0-9_]*)\s*\((.*)\)\s*", text)
    if not match:
        return None
    return match.group(1), split_arguments(match.group(2))


def logical_macro_lines(path):
    logical = []
    pending = ""
    depth = 0
    in_directive = False
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        uncommented = raw_line.split("//", 1)[0].rstrip()
        stripped = uncommented.lstrip()
        if in_directive or stripped.startswith("#"):
            in_directive = uncommented.endswith("\\")
            continue
        line = stripped
        if not line:
            continue
        pending = f"{pending} {line}".strip()
        depth += line.count("(") - line.count(")")
        if depth == 0:
            logical.append(pending)
            pending = ""
    if pending or depth != 0:
        raise ValueError(f"unterminated macro invocation in {path}")
    return logical


def public_symbol(name):
    symbol = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    symbol = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", symbol)
    symbol = re.sub(r"[^A-Za-z0-9]+", "_", symbol).strip("_").upper()
    if not re.fullmatch(r"[A-Z_][A-Z0-9_]*", symbol):
        raise ValueError(f"cannot form a public symbol for {name}")
    return symbol


def snake_name(name):
    return public_symbol(name).lower()


def parse_hierarchy(path, domain):
    abstract_macro = {
        "decl": "ABSTRACT_DECL",
        "stmt": "ABSTRACT_STMT",
        "type": "ABSTRACT_TYPE",
    }[domain]
    ignored = {
        "DECL_RANGE",
        "LAST_DECL_RANGE",
        "STMT_RANGE",
        "LAST_STMT_RANGE",
        "LAST_TYPE",
        "LEAF_TYPE",
        "DECL_CONTEXT",
        "DECL_CONTEXT_BASE",
    }
    records = []
    for line in logical_macro_lines(path):
        parsed = macro_call(line)
        if parsed is None:
            raise ValueError(f"unrecognized node definition in {path}: {line}")
        macro, arguments = parsed
        if macro in ignored:
            continue
        abstract = macro == abstract_macro
        if abstract and len(arguments) == 1:
            parsed = macro_call(arguments[0])
            if parsed is None:
                raise ValueError(f"invalid abstract node definition: {line}")
            _, arguments = parsed
        if len(arguments) != 2:
            raise ValueError(f"invalid node definition in {path}: {line}")
        name, parent = arguments
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            raise ValueError(f"invalid node name in {path}: {name}")
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", parent):
            raise ValueError(f"invalid parent name in {path}: {parent}")
        if domain == "decl" and parent != "Decl":
            if not parent.endswith("Decl"):
                raise ValueError(f"invalid declaration parent: {parent}")
            parent = parent[: -len("Decl")]
        if domain == "type" and parent != "Type":
            if not parent.endswith("Type"):
                raise ValueError(f"invalid type parent: {parent}")
            parent = parent[: -len("Type")]
        class_name = {
            "decl": f"{name}Decl",
            "stmt": name,
            "type": f"{name}Type",
        }[domain]
        internal_enum = {
            "decl": name,
            "stmt": f"{name}Class",
            "type": name,
        }[domain]
        records.append(
            {
                "domain": domain,
                "name": name,
                "parent_name": parent,
                "class_name": class_name,
                "internal_enum": internal_enum,
                "abstract": abstract,
            }
        )
    return records


def class_blocks(text):
    pattern = re.compile(
        r"\bclass\s+([A-Za-z_][A-Za-z0-9_]*)Attr\s*"
        r":\s*public\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{"
    )
    blocks = {}
    for match in pattern.finditer(text):
        depth = 1
        index = match.end()
        quote = None
        escaped = False
        while index < len(text) and depth:
            character = text[index]
            if quote is not None:
                if escaped:
                    escaped = False
                elif character == "\\":
                    escaped = True
                elif character == quote:
                    quote = None
            elif character in ("'", '"'):
                quote = character
            elif character == "{":
                depth += 1
            elif character == "}":
                depth -= 1
            index += 1
        if depth:
            raise ValueError(f"unterminated attribute class {match.group(1)}")
        blocks[match.group(1)] = {
            "parent": match.group(2),
            "body": text[match.end() : index - 1],
        }
    return blocks


def parse_parameter(parameter):
    parameter = re.sub(r"\s*=\s*.*$", "", parameter).strip()
    match = re.fullmatch(r"(.+?)([A-Za-z_][A-Za-z0-9_]*)", parameter)
    if not match:
        raise ValueError(f"cannot parse attribute parameter: {parameter}")
    type_name = re.sub(r"\s+", " ", match.group(1)).strip()
    name = match.group(2)
    return type_name, name


def portable_attr_type(type_name):
    normalized = type_name.replace("const ", "").strip()
    normalized = re.sub(r"\s+", " ", normalized)
    if normalized in ("bool",):
        return "bool"
    if normalized in (
        "int",
        "signed",
        "long",
        "long long",
        "int32_t",
        "int64_t",
        "int *",
    ):
        return "i64"
    if normalized in (
        "unsigned",
        "unsigned int",
        "unsigned long",
        "unsigned long long",
        "size_t",
        "uint32_t",
        "uint64_t",
        "unsigned *",
    ):
        return "u64"
    if normalized in (
        "llvm::StringRef",
        "StringRef",
        "char *",
        "llvm::StringRef *",
        "StringRef *",
    ):
        return "string"
    if normalized in ("IdentifierInfo *", "IdentifierInfo **"):
        return "identifier"
    if normalized == "Expr *":
        return "expr"
    if normalized == "Expr **":
        return "expr"
    if normalized == "TypeSourceInfo *":
        return "type_loc"
    if normalized == "QualType":
        return "type"
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*Decl \*+", normalized):
        return "decl"
    if normalized in (
        "ParamIdx",
        "ParamIdx *",
        "VariadicParamIdx",
        "VariadicParamIdx *",
    ):
        return "parameter_index"
    if normalized in ("VersionTuple", "llvm::VersionTuple"):
        return "version"
    if normalized == "void *":
        return "alignment_operand"
    if "*" in normalized or "&" in normalized:
        raise ValueError(
            f"attribute argument type needs an explicit portable mapping: "
            f"{type_name}"
        )
    return "enum"


def constructor_arguments(name, body):
    pattern = re.compile(
        rf"\b{re.escape(name)}Attr\s*\(\s*TreeContext\s*&\s*Ctx\s*,"
        r"\s*const\s+AttributeCommonInfo\s*&\s*CommonInfo"
        r"(?P<tail>.*?)\)\s*;",
        re.DOTALL,
    )
    signatures = []
    for match in pattern.finditer(body):
        tail = match.group("tail").strip()
        if tail.startswith(","):
            tail = tail[1:].strip()
        parameters = (
            [parse_parameter(item) for item in split_arguments(tail)]
            if tail
            else []
        )
        signatures.append(parameters)
    if not signatures:
        raise ValueError(f"cannot find canonical constructor for {name}Attr")
    full = max(signatures, key=len)
    present_counts = {}
    for signature in signatures:
        for _, parameter_name in signature:
            present_counts[parameter_name] = (
                present_counts.get(parameter_name, 0) + 1
            )
    arguments = []
    skip = set()
    for index, (type_name, parameter_name) in enumerate(full):
        if parameter_name in skip:
            continue
        cardinality = (
            "required"
            if present_counts.get(parameter_name) == len(signatures)
            else "optional"
        )
        if index + 1 < len(full):
            next_type, next_name = full[index + 1]
            normalized_next = next_name.replace("_", "").lower()
            expected = f"{parameter_name}size".replace("_", "").lower()
            is_sized_sequence = (
                portable_attr_type(next_type) == "u64"
                and normalized_next == expected
                and "*" in type_name
            )
        else:
            is_sized_sequence = False
        if is_sized_sequence:
            cardinality = "many"
            skip.add(full[index + 1][1])
        arguments.append(
            {
                "name": snake_name(parameter_name),
                "value_type": portable_attr_type(type_name),
                "cardinality": cardinality,
                "access": "read_write",
            }
        )
    return arguments


def parse_attrs(list_path, classes_path):
    text = classes_path.read_text(encoding="utf-8")
    blocks = class_blocks(text)
    records = []
    seen = set()
    for line in logical_macro_lines(list_path):
        parsed = macro_call(line)
        if parsed is None:
            raise ValueError(f"unrecognized attribute definition: {line}")
        macro, arguments = parsed
        if macro in ("ATTR_RANGE", "PRAGMA_SPELLING_ATTR"):
            continue
        if len(arguments) != 1:
            raise ValueError(f"invalid attribute definition: {line}")
        name = arguments[0]
        if name in seen:
            raise ValueError(f"duplicate attribute definition: {name}")
        seen.add(name)
        block = blocks.get(name)
        if block is None:
            raise ValueError(f"missing generated class for {name}Attr")
        records.append(
            {
                "domain": "attr",
                "name": name,
                "parent_name": block["parent"].removesuffix("Attr"),
                "class_name": f"{name}Attr",
                "internal_enum": name,
                "abstract": False,
                "arguments": constructor_arguments(name, block["body"]),
            }
        )
    extra = sorted(set(blocks) - seen)
    if extra:
        raise ValueError(
            "generated attribute classes missing from AttrList.td.h: "
            + ", ".join(extra)
        )
    return records


def source_inventory(paths):
    decls = parse_hierarchy(paths["decl"], "decl")
    stmts = parse_hierarchy(paths["stmt"], "stmt")
    types = parse_hierarchy(paths["type"], "type")
    attrs = parse_attrs(paths["attr"], paths["attr_classes"])
    type_locs = []
    for record in types:
        parent = record["parent_name"]
        type_locs.append(
            {
                "domain": "type_loc",
                "name": record["name"],
                "parent_name": "TypeLoc" if parent == "Type" else parent,
                "class_name": f"{record['name']}TypeLoc",
                "internal_enum": record["internal_enum"],
                "abstract": record["abstract"],
            }
        )
    type_locs.insert(
        0,
        {
            "domain": "type_loc",
            "name": "Qualified",
            "parent_name": "TypeLoc",
            "class_name": "QualifiedTypeLoc",
            "internal_enum": "Qualified",
            "abstract": False,
        },
    )
    return {
        "decl": decls,
        "stmt": stmts,
        "type": types,
        "type_loc": type_locs,
        "attr": attrs,
    }


def node_key(domain, name):
    return f"{domain}.{name}"


def node_macro(node):
    return (
        f"NEVERC_AST_NODE_KIND_{DOMAIN_MACROS[node['domain']]}_"
        f"{node['symbol']}"
    )


def concrete_kind_macro(node):
    prefix = DOMAIN_MACROS[node["domain"]]
    return f"NEVERC_{prefix}_KIND_{node['symbol']}"


def property_macro(prop):
    return f"NEVERC_AST_PROPERTY_{prop['symbol']}"


def child_slot_macro(slot):
    return f"NEVERC_AST_CHILD_SLOT_{slot['symbol']}"


def c_string(value):
    return json.dumps(value, ensure_ascii=True)


def boolean(value):
    return "NEVERC_TRUE" if value else "NEVERC_FALSE"


def parse_id(value, description):
    if not isinstance(value, str) or not re.fullmatch(
        r"0x[0-9a-fA-F]{8}", value
    ):
        raise ValueError(f"{description} must be an eight-digit hex string")
    result = int(value, 16)
    if result == 0 or result > UINT32_MAX:
        raise ValueError(f"{description} is out of range")
    return result


def schema_payload(document):
    return {
        "schema_version": document["schema_version"],
        "capability": document["capability"],
        "node_kinds": document["node_kinds"],
        "properties": document["properties"],
        "child_slots": document["child_slots"],
    }


def schema_digest(document):
    encoded = json.dumps(
        schema_payload(document),
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=True,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def inventory_signature(record):
    signature = {
        "domain": record["domain"],
        "name": record["name"],
        "parent_name": record["parent_name"],
        "class_name": record["class_name"],
        "internal_enum": record["internal_enum"],
        "abstract": record["abstract"],
    }
    if record["domain"] == "attr":
        signature["arguments"] = record["arguments"]
    return signature


def validate_document(document, inventory):
    if document.get("schema_version") != 1:
        raise ValueError("AST schema_version must be 1")
    capability = document.get("capability")
    if capability != {"major": 1, "minor": 0}:
        raise ValueError("AST schema capability must be 1.0")
    nodes = document.get("node_kinds")
    properties = document.get("properties")
    child_slots = document.get("child_slots")
    if not isinstance(nodes, list) or not nodes:
        raise ValueError("AST schema node_kinds must be a non-empty array")
    if not isinstance(properties, list) or not properties:
        raise ValueError("AST schema properties must be a non-empty array")
    if not isinstance(child_slots, list) or not child_slots:
        raise ValueError("AST schema child_slots must be a non-empty array")

    node_by_key = {}
    all_ids = set()
    for node in nodes:
        key = node.get("key")
        domain = node.get("domain")
        if domain not in DOMAIN_MACROS:
            raise ValueError(f"invalid AST node domain for {key}")
        if key != node_key(domain, node.get("name", "")):
            raise ValueError(f"invalid AST node key: {key}")
        if key in node_by_key:
            raise ValueError(f"duplicate AST node key: {key}")
        id_value = parse_id(node.get("id"), f"{key} id")
        expected_base = NODE_ID_BASES[domain]
        if not expected_base < id_value < expected_base + 0x01000000:
            raise ValueError(f"{key} id is outside its domain range")
        if id_value in all_ids:
            raise ValueError(f"duplicate stable ID for {key}")
        all_ids.add(id_value)
        if node.get("source_range") not in SOURCE_RANGE_MACROS:
            raise ValueError(f"invalid source range mode for {key}")
        if not isinstance(node.get("properties"), list):
            raise ValueError(f"{key} must declare properties")
        if not isinstance(node.get("child_slots"), list):
            raise ValueError(f"{key} must declare child_slots")
        node_by_key[key] = node
    for node in nodes:
        parent = node.get("parent")
        if parent is not None and parent not in node_by_key:
            raise ValueError(f"unknown parent {parent} for {node['key']}")

    source_records = {}
    for domain, records in inventory.items():
        for record in records:
            source_records[node_key(domain, record["name"])] = record
    schema_concrete = {
        key: inventory_signature(node)
        for key, node in node_by_key.items()
        if not node["synthetic"]
    }
    source_signatures = {
        key: inventory_signature(record)
        for key, record in source_records.items()
    }
    if schema_concrete.keys() != source_signatures.keys():
        missing = sorted(source_signatures.keys() - schema_concrete.keys())
        extra = sorted(schema_concrete.keys() - source_signatures.keys())
        raise ValueError(
            "AST node inventory mismatch; missing="
            + ",".join(missing)
            + " extra="
            + ",".join(extra)
        )
    for key, expected in source_signatures.items():
        actual = schema_concrete[key]
        if actual != expected:
            raise ValueError(
                f"AST source metadata changed for {key}: "
                f"schema={actual!r} source={expected!r}"
            )

    property_by_key = {}
    for prop in properties:
        key = prop.get("key")
        if not isinstance(key, str) or key in property_by_key:
            raise ValueError(f"duplicate or invalid property key: {key}")
        id_value = parse_id(prop.get("id"), f"{key} id")
        if not (
            PROPERTY_ID_BASE
            < id_value
            < PROPERTY_ID_BASE + 0x01000000
        ):
            raise ValueError(f"{key} id is outside the property range")
        if id_value in all_ids:
            raise ValueError(f"duplicate stable ID for {key}")
        all_ids.add(id_value)
        if prop.get("owner") not in node_by_key:
            raise ValueError(f"unknown property owner for {key}")
        if prop.get("value_type") not in VALUE_TYPE_MACROS:
            raise ValueError(f"invalid value type for {key}")
        if prop.get("access") not in ACCESS_MACROS:
            raise ValueError(f"invalid access mode for {key}")
        if prop.get("cardinality") not in CARDINALITY_MACROS:
            raise ValueError(f"invalid cardinality for {key}")
        property_by_key[key] = prop

    child_by_key = {}
    for slot in child_slots:
        key = slot.get("key")
        if not isinstance(key, str) or key in child_by_key:
            raise ValueError(f"duplicate or invalid child slot key: {key}")
        id_value = parse_id(slot.get("id"), f"{key} id")
        if not (
            CHILD_SLOT_ID_BASE
            < id_value
            < CHILD_SLOT_ID_BASE + 0x01000000
        ):
            raise ValueError(f"{key} id is outside the child-slot range")
        if id_value in all_ids:
            raise ValueError(f"duplicate stable ID for {key}")
        all_ids.add(id_value)
        if slot.get("owner") not in node_by_key:
            raise ValueError(f"unknown child slot owner for {key}")
        if slot.get("value_type") not in VALUE_TYPE_MACROS:
            raise ValueError(f"invalid child value type for {key}")
        if slot.get("access") not in ACCESS_MACROS:
            raise ValueError(f"invalid child access mode for {key}")
        if slot.get("cardinality") not in CARDINALITY_MACROS:
            raise ValueError(f"invalid child cardinality for {key}")
        child_by_key[key] = slot

    for node in nodes:
        for key in node["properties"]:
            if key not in property_by_key:
                raise ValueError(f"unknown property {key} on {node['key']}")
        for key in node["child_slots"]:
            if key not in child_by_key:
                raise ValueError(f"unknown child slot {key} on {node['key']}")
        if not node["abstract"] and not node["properties"]:
            raise ValueError(f"concrete AST node has no properties: {node['key']}")

    expected_digest = schema_digest(document)
    if document.get("stable_schema_digest") != expected_digest:
        raise ValueError(
            "stable_schema_digest is stale; an ABI review is required before "
            f"updating it to {expected_digest}"
        )
    return nodes, properties, child_slots, expected_digest


def common_properties():
    return [
        {
            "key": "ast.source_range",
            "owner": "decl.Decl",
            "name": "source_range",
            "value_type": "source_range",
            "access": "read_write",
            "cardinality": "required",
        },
        {
            "key": "decl.is_implicit",
            "owner": "decl.Decl",
            "name": "is_implicit",
            "value_type": "bool",
            "access": "read_write",
            "cardinality": "required",
        },
        {
            "key": "decl.is_invalid",
            "owner": "decl.Decl",
            "name": "is_invalid",
            "value_type": "bool",
            "access": "read_write",
            "cardinality": "required",
        },
        {
            "key": "decl.name",
            "owner": "decl.Named",
            "name": "name",
            "value_type": "identifier",
            "access": "read_write",
            "cardinality": "optional",
        },
        {
            "key": "decl.type",
            "owner": "decl.Value",
            "name": "type",
            "value_type": "type",
            "access": "read_write",
            "cardinality": "required",
        },
        {
            "key": "decl.attributes",
            "owner": "decl.Decl",
            "name": "attributes",
            "value_type": "attr",
            "access": "read_write",
            "cardinality": "many",
        },
        {
            "key": "stmt.expr_type",
            "owner": "stmt.Expr",
            "name": "type",
            "value_type": "type",
            "access": "read_write",
            "cardinality": "required",
        },
        {
            "key": "stmt.value_kind",
            "owner": "stmt.Expr",
            "name": "value_kind",
            "value_type": "enum",
            "access": "read_write",
            "cardinality": "required",
        },
        {
            "key": "type.canonical",
            "owner": "type.Type",
            "name": "canonical_type",
            "value_type": "type",
            "access": "read_only",
            "cardinality": "required",
        },
        {
            "key": "type.qualifiers",
            "owner": "type.Type",
            "name": "qualifiers",
            "value_type": "u64",
            "access": "read_write",
            "cardinality": "required",
        },
        {
            "key": "type.is_dependent",
            "owner": "type.Type",
            "name": "is_dependent",
            "value_type": "bool",
            "access": "read_only",
            "cardinality": "required",
        },
        {
            "key": "type_loc.type",
            "owner": "type_loc.TypeLoc",
            "name": "type",
            "value_type": "type",
            "access": "read_write",
            "cardinality": "required",
        },
        {
            "key": "attr.spelling",
            "owner": "attr.Attr",
            "name": "spelling",
            "value_type": "string",
            "access": "read_only",
            "cardinality": "required",
        },
        {
            "key": "attr.is_implicit",
            "owner": "attr.Attr",
            "name": "is_implicit",
            "value_type": "bool",
            "access": "read_write",
            "cardinality": "required",
        },
        {
            "key": "attr.is_inherited",
            "owner": "attr.Inheritable",
            "name": "is_inherited",
            "value_type": "bool",
            "access": "read_write",
            "cardinality": "required",
        },
    ]


DECL_CHILDREN = {
    "ExternCContext": [("declarations", "decl", "many")],
    "FileScopeAsm": [("assembly", "expr", "required")],
    "Label": [("statement", "stmt", "optional")],
    "Enum": [("declarations", "decl", "many")],
    "Record": [("declarations", "decl", "many")],
    "Field": [
        ("bit_width", "expr", "optional"),
        ("initializer", "expr", "optional"),
    ],
    "Function": [
        ("parameters", "decl", "many"),
        ("body", "stmt", "optional"),
    ],
    "Var": [("initializer", "expr", "optional")],
    "EnumConstant": [("initializer", "expr", "optional")],
    "IndirectField": [("chain", "decl", "many")],
    "StaticAssert": [
        ("assertion", "expr", "required"),
        ("message", "expr", "optional"),
    ],
    "TranslationUnit": [("declarations", "decl", "many")],
}

STMT_CHILDREN = {
    "CompoundStmt": [("body", "stmt", "many")],
    "DeclStmt": [("declarations", "decl", "many")],
    "DoStmt": [("body", "stmt", "required"), ("condition", "expr", "required")],
    "ForStmt": [
        ("initializer", "stmt", "optional"),
        ("condition", "expr", "optional"),
        ("increment", "expr", "optional"),
        ("body", "stmt", "required"),
    ],
    "IfStmt": [
        ("condition", "expr", "required"),
        ("then", "stmt", "required"),
        ("else", "stmt", "optional"),
    ],
    "ReturnStmt": [("value", "expr", "optional")],
    "WhileStmt": [
        ("condition", "expr", "required"),
        ("body", "stmt", "required"),
    ],
    "CaseStmt": [
        ("lhs", "expr", "required"),
        ("rhs", "expr", "optional"),
        ("substatement", "stmt", "required"),
    ],
    "DefaultStmt": [("substatement", "stmt", "required")],
    "SwitchStmt": [
        ("condition", "expr", "required"),
        ("body", "stmt", "required"),
    ],
    "AttributedStmt": [("substatement", "stmt", "required")],
    "BinaryConditionalOperator": [("children", "expr", "many")],
    "ConditionalOperator": [
        ("condition", "expr", "required"),
        ("true_expression", "expr", "required"),
        ("false_expression", "expr", "required"),
    ],
    "ArrayInitLoopExpr": [
        ("common_expression", "expr", "required"),
        ("subexpression", "expr", "required"),
    ],
    "ArraySubscriptExpr": [
        ("base", "expr", "required"),
        ("index", "expr", "required"),
    ],
    "AtomicExpr": [("arguments", "expr", "many")],
    "BinaryOperator": [("lhs", "expr", "required"), ("rhs", "expr", "required")],
    "CompoundAssignOperator": [
        ("lhs", "expr", "required"),
        ("rhs", "expr", "required"),
    ],
    "CallExpr": [
        ("callee", "expr", "required"),
        ("arguments", "expr", "many"),
    ],
    "CStyleCastExpr": [("subexpression", "expr", "required")],
    "ImplicitCastExpr": [("subexpression", "expr", "required")],
    "ChooseExpr": [
        ("condition", "expr", "required"),
        ("lhs", "expr", "required"),
        ("rhs", "expr", "required"),
    ],
    "CompoundLiteralExpr": [("initializer", "expr", "required")],
    "ConvertVectorExpr": [("source", "expr", "required")],
    "DesignatedInitExpr": [("initializer", "expr", "required")],
    "DesignatedInitUpdateExpr": [
        ("base", "expr", "required"),
        ("updater", "expr", "required"),
    ],
    "ExtVectorElementExpr": [("base", "expr", "required")],
    "ConstantExpr": [("subexpression", "expr", "required")],
    "ExprWithCleanups": [("subexpression", "expr", "required")],
    "ImaginaryLiteral": [("subexpression", "expr", "required")],
    "InitListExpr": [("initializers", "expr", "many")],
    "MatrixSubscriptExpr": [
        ("base", "expr", "required"),
        ("row", "expr", "required"),
        ("column", "expr", "required"),
    ],
    "MemberExpr": [("base", "expr", "required")],
    "OpaqueValueExpr": [("source", "expr", "optional")],
    "ParenExpr": [("subexpression", "expr", "required")],
    "ParenListExpr": [("expressions", "expr", "many")],
    "PredefinedExpr": [("function_name", "expr", "required")],
    "PseudoObjectExpr": [("expressions", "expr", "many")],
    "RecoveryExpr": [("subexpressions", "expr", "many")],
    "ShuffleVectorExpr": [("subexpressions", "expr", "many")],
    "StmtExpr": [("substatement", "stmt", "required")],
    "TypoExpr": [("subexpression", "expr", "optional")],
    "UnaryOperator": [("operand", "expr", "required")],
    "VAArgExpr": [("subexpression", "expr", "required")],
    "LabelStmt": [("substatement", "stmt", "required")],
}

TYPE_CHILDREN = {
    "Adjusted": [("original", "type", "required"), ("adjusted", "type", "required")],
    "Decayed": [("pointee", "type", "required")],
    "ConstantArray": [("element", "type", "required")],
    "IncompleteArray": [("element", "type", "required")],
    "VariableArray": [
        ("element", "type", "required"),
        ("size_expression", "expr", "required"),
    ],
    "Atomic": [("value", "type", "required")],
    "Attributed": [
        ("modified", "type", "required"),
        ("equivalent", "type", "required"),
    ],
    "BTFTagAttributed": [("wrapped", "type", "required")],
    "Complex": [("element", "type", "required")],
    "Auto": [("deduced", "type", "optional")],
    "Elaborated": [("named", "type", "required")],
    "FunctionNoProto": [("result", "type", "required")],
    "FunctionProto": [
        ("result", "type", "required"),
        ("parameters", "type", "many"),
    ],
    "MacroQualified": [("underlying", "type", "required")],
    "ConstantMatrix": [("element", "type", "required")],
    "Paren": [("inner", "type", "required")],
    "Pointer": [("pointee", "type", "required")],
    "Enum": [("declaration", "decl", "required")],
    "Record": [("declaration", "decl", "required")],
    "TypeOfExpr": [("expression", "expr", "required")],
    "TypeOf": [("underlying", "type", "required")],
    "Typedef": [("declaration", "decl", "required")],
    "Vector": [("element", "type", "required")],
    "ExtVector": [("element", "type", "required")],
}


def synthetic_roots():
    roots = [
        ("decl", "Decl", None, "Decl"),
        ("stmt", "Stmt", None, "Stmt"),
        ("type", "Type", None, "Type"),
        ("type_loc", "TypeLoc", None, "TypeLoc"),
        ("attr", "Attr", None, "Attr"),
        ("attr", "Type", "Attr", "TypeAttr"),
        ("attr", "Stmt", "Attr", "StmtAttr"),
        ("attr", "Inheritable", "Attr", "InheritableAttr"),
        (
            "attr",
            "DeclOrStmt",
            "Inheritable",
            "DeclOrStmtAttr",
        ),
        (
            "attr",
            "InheritableParam",
            "Inheritable",
            "InheritableParamAttr",
        ),
    ]
    return [
        {
            "domain": domain,
            "name": name,
            "parent_name": parent,
            "class_name": class_name,
            "internal_enum": name,
            "abstract": True,
            "synthetic": True,
        }
        for domain, name, parent, class_name in roots
    ]


def bootstrap_document(inventory):
    records_by_domain = {domain: [] for domain in DOMAIN_MACROS}
    for root in synthetic_roots():
        records_by_domain[root["domain"]].append(root)
    for domain, records in inventory.items():
        records_by_domain[domain].extend(
            {**record, "synthetic": False} for record in records
        )

    nodes = []
    node_lookup = {}
    for domain in DOMAIN_MACROS:
        for index, record in enumerate(records_by_domain[domain], 1):
            parent = (
                node_key(domain, record["parent_name"])
                if record["parent_name"]
                else None
            )
            node = {
                "key": node_key(domain, record["name"]),
                "domain": domain,
                "name": record["name"],
                "symbol": public_symbol(record["name"]),
                "class_name": record["class_name"],
                "internal_enum": record["internal_enum"],
                "id": f"0x{NODE_ID_BASES[domain] + index:08x}",
                "parent": parent,
                "parent_name": record["parent_name"],
                "abstract": record["abstract"],
                "synthetic": record["synthetic"],
                "source_range": (
                    "none" if domain == "type" else "native"
                ),
                "properties": [],
                "child_slots": [],
            }
            if domain == "attr" and not record["synthetic"]:
                node["arguments"] = record["arguments"]
            nodes.append(node)
            node_lookup[node["key"]] = node

    for node in nodes:
        if node["parent"] is not None and node["parent"] not in node_lookup:
            raise ValueError(
                f"bootstrap cannot resolve parent {node['parent']} "
                f"for {node['key']}"
            )

    def derives(node, ancestor_key):
        current = node
        while current is not None:
            if current["key"] == ancestor_key:
                return True
            current = node_lookup.get(current["parent"])
        return False

    properties = common_properties()
    for node in nodes:
        domain = node["domain"]
        if node["source_range"] == "native":
            node["properties"].append("ast.source_range")
        if domain == "decl":
            node["properties"].extend(
                ["decl.is_implicit", "decl.is_invalid", "decl.attributes"]
            )
            if derives(node, "decl.Named"):
                node["properties"].append("decl.name")
            if derives(node, "decl.Value"):
                node["properties"].append("decl.type")
        elif domain == "stmt" and derives(node, "stmt.Expr"):
            node["properties"].extend(
                ["stmt.expr_type", "stmt.value_kind"]
            )
        elif domain == "type":
            node["properties"].extend(
                ["type.canonical", "type.qualifiers", "type.is_dependent"]
            )
        elif domain == "type_loc":
            node["properties"].append("type_loc.type")
        elif domain == "attr":
            node["properties"].extend(
                ["attr.spelling", "attr.is_implicit"]
            )
            if derives(node, "attr.Inheritable"):
                node["properties"].append("attr.is_inherited")
            for argument in node.get("arguments", []):
                key = (
                    f"attr.{node['name']}."
                    f"{argument['name']}"
                )
                properties.append(
                    {
                        "key": key,
                        "owner": node["key"],
                        **argument,
                    }
                )
                node["properties"].append(key)

    for index, prop in enumerate(properties, 1):
        prop["symbol"] = public_symbol(prop["key"])
        prop["id"] = f"0x{PROPERTY_ID_BASE + index:08x}"

    child_slots = []

    def add_child_slots(node, specifications):
        for name, value_type, cardinality in specifications:
            key = f"{node['domain']}.{node['name']}.{name}"
            child_slots.append(
                {
                    "key": key,
                    "symbol": public_symbol(key),
                    "owner": node["key"],
                    "name": name,
                    "value_type": value_type,
                    "access": "read_write",
                    "cardinality": cardinality,
                }
            )
            node["child_slots"].append(key)

    for node in nodes:
        if node["abstract"]:
            continue
        domain = node["domain"]
        if domain == "decl":
            specifications = DECL_CHILDREN.get(node["name"], [])
        elif domain == "stmt":
            specifications = STMT_CHILDREN.get(node["name"], [])
        elif domain == "type":
            specifications = TYPE_CHILDREN.get(node["name"], [])
        elif domain == "type_loc":
            specifications = [("children", "type_loc", "many")]
        else:
            specifications = []
        add_child_slots(node, specifications)

    for index, slot in enumerate(child_slots, 1):
        slot["id"] = f"0x{CHILD_SLOT_ID_BASE + index:08x}"

    document = {
        "schema_version": 1,
        "capability": {"major": 1, "minor": 0},
        "stable_schema_digest": "",
        "node_kinds": nodes,
        "properties": properties,
        "child_slots": child_slots,
    }
    document["stable_schema_digest"] = schema_digest(document)
    return document


def generate_body(capability, nodes, properties, child_slots, digest):
    concrete_counts = {
        domain: sum(
            1
            for node in nodes
            if node["domain"] == domain and not node["abstract"]
        )
        for domain in DOMAIN_MACROS
    }
    lines = [
        "#ifndef NEVERC_PLUGIN_AST_SCHEMA_CONSTANTS_DEFINED",
        "#define NEVERC_PLUGIN_AST_SCHEMA_CONSTANTS_DEFINED",
        "",
        f"#define NEVERC_AST_SCHEMA_CAPABILITY_MAJOR "
        f"UINT16_C({capability['major']})",
        f"#define NEVERC_AST_SCHEMA_CAPABILITY_MINOR "
        f"UINT16_C({capability['minor']})",
        f"#define NEVERC_AST_SCHEMA_DIGEST {c_string(digest)}",
        f"#define NEVERC_AST_SCHEMA_NODE_COUNT UINT32_C({len(nodes)})",
        f"#define NEVERC_AST_PROPERTY_COUNT UINT32_C({len(properties)})",
        f"#define NEVERC_AST_CHILD_SLOT_COUNT UINT32_C({len(child_slots)})",
    ]
    for domain, count in concrete_counts.items():
        prefix = DOMAIN_MACROS[domain]
        lines.append(
            f"#define NEVERC_{prefix}_KIND_COUNT UINT32_C({count})"
        )
    lines.append("")
    for node in nodes:
        lines.append(
            f"#define {node_macro(node)} UINT32_C({node['id']})"
        )
        if not node["abstract"]:
            lines.append(
                f"#define {concrete_kind_macro(node)} {node_macro(node)}"
            )
    lines.append("")
    for prop in properties:
        lines.append(
            f"#define {property_macro(prop)} UINT32_C({prop['id']})"
        )
    lines.append("")
    for slot in child_slots:
        lines.append(
            f"#define {child_slot_macro(slot)} UINT32_C({slot['id']})"
        )
    lines.extend(
        [
            "",
            "#endif /* NEVERC_PLUGIN_AST_SCHEMA_CONSTANTS_DEFINED */",
            "",
            "#ifdef NEVERC_AST_SCHEMA_NODE",
        ]
    )
    node_by_key = {node["key"]: node for node in nodes}
    for node in nodes:
        parent = (
            node_macro(node_by_key[node["parent"]])
            if node["parent"]
            else "NEVERC_AST_NODE_KIND_INVALID"
        )
        lines.append(
            "NEVERC_AST_SCHEMA_NODE("
            f"{DOMAIN_MACROS[node['domain']]}, {node['internal_enum']}, "
            f"{node['symbol']}, {node_macro(node)}, {parent}, "
            f"{boolean(node['abstract'])}, "
            f"{SOURCE_RANGE_MACROS[node['source_range']]}, "
            f"{c_string(node['name'])}, {c_string(node['class_name'])}, "
            f"UINT32_C({len(node['properties'])}), "
            f"UINT32_C({len(node['child_slots'])}))"
        )
    lines.extend(["#endif", "", "#ifdef NEVERC_AST_SCHEMA_PROPERTY"])
    for prop in properties:
        lines.append(
            "NEVERC_AST_SCHEMA_PROPERTY("
            f"{prop['symbol']}, {property_macro(prop)}, "
            f"{node_macro(node_by_key[prop['owner']])}, "
            f"{VALUE_TYPE_MACROS[prop['value_type']]}, "
            f"{ACCESS_MACROS[prop['access']]}, "
            f"{CARDINALITY_MACROS[prop['cardinality']]}, "
            f"{c_string(prop['name'])})"
        )
    lines.extend(["#endif", "", "#ifdef NEVERC_AST_SCHEMA_CHILD_SLOT"])
    for slot in child_slots:
        lines.append(
            "NEVERC_AST_SCHEMA_CHILD_SLOT("
            f"{slot['symbol']}, {child_slot_macro(slot)}, "
            f"{node_macro(node_by_key[slot['owner']])}, "
            f"{VALUE_TYPE_MACROS[slot['value_type']]}, "
            f"{ACCESS_MACROS[slot['access']]}, "
            f"{CARDINALITY_MACROS[slot['cardinality']]}, "
            f"{c_string(slot['name'])})"
        )
    lines.append("#endif")
    for domain in ("decl", "stmt", "type", "attr"):
        macro = f"NEVERC_AST_SCHEMA_INTERNAL_{domain.upper()}"
        lines.extend(["", f"#ifdef {macro}"])
        for node in nodes:
            if node["domain"] != domain or node["abstract"]:
                continue
            lines.append(
                f"{macro}({node['internal_enum']}, {node['symbol']}, "
                f"{concrete_kind_macro(node)})"
            )
        lines.append("#endif")
    for domain in DOMAIN_MACROS:
        macro = f"NEVERC_AST_SCHEMA_VISIT_{DOMAIN_MACROS[domain]}"
        lines.extend(["", f"#ifdef {macro}"])
        for node in nodes:
            if node["domain"] != domain or node["abstract"]:
                continue
            lines.append(
                f"{macro}({node['class_name']}, {node['symbol']}, "
                f"{concrete_kind_macro(node)})"
            )
        lines.append("#endif")
    return "\n".join(lines) + "\n"


def generate(template_path, capability, nodes, properties, child_slots, digest):
    template = template_path.read_text(encoding="utf-8")
    marker = "@AST_SCHEMA_BODY@"
    if template.count(marker) != 1:
        raise ValueError("AST schema template must contain one body marker")
    return template.replace(
        marker,
        generate_body(capability, nodes, properties, child_slots, digest),
    )


def default_paths(arguments):
    return {
        "decl": arguments.decl_def,
        "stmt": arguments.stmt_def,
        "type": arguments.type_def,
        "attr": arguments.attr_def,
        "attr_classes": arguments.attr_classes,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if the checked-in schema or generated output is stale",
    )
    parser.add_argument(
        "--bootstrap",
        action="store_true",
        help="create the initial explicit stable-ID schema",
    )
    parser.add_argument("--schema", type=Path, default=SCHEMA)
    parser.add_argument("--template", type=Path, default=TEMPLATE)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--decl-def", type=Path, default=DECL_DEFINITIONS)
    parser.add_argument("--stmt-def", type=Path, default=STMT_DEFINITIONS)
    parser.add_argument("--type-def", type=Path, default=TYPE_DEFINITIONS)
    parser.add_argument("--attr-def", type=Path, default=ATTR_DEFINITIONS)
    parser.add_argument("--attr-classes", type=Path, default=ATTR_CLASSES)
    arguments = parser.parse_args()
    if arguments.check and arguments.bootstrap:
        parser.error("--check and --bootstrap are mutually exclusive")

    try:
        inventory = source_inventory(default_paths(arguments))
        if arguments.bootstrap:
            if arguments.schema.exists():
                raise ValueError(
                    f"refusing to overwrite existing AST schema: "
                    f"{arguments.schema}"
                )
            document = bootstrap_document(inventory)
            arguments.schema.parent.mkdir(parents=True, exist_ok=True)
            arguments.schema.write_text(
                json.dumps(document, indent=2, ensure_ascii=True) + "\n",
                encoding="utf-8",
            )
        document = json.loads(arguments.schema.read_text(encoding="utf-8"))
        nodes, properties, child_slots, digest = validate_document(
            document, inventory
        )
        generated = generate(
            arguments.template,
            document["capability"],
            nodes,
            properties,
            child_slots,
            digest,
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"AST schema error: {error}", file=sys.stderr)
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
