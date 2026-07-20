#!/usr/bin/env python3

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "neverc/include/neverc/Plugin/Schema/ObjectSchema.json"
TEMPLATE = (
    ROOT / "neverc/include/neverc/Plugin/Schema/PluginObjectSchema.inc.in"
)
OUTPUT = ROOT / "neverc/include/neverc/Plugin/Schema/PluginObjectSchema.inc"

FAMILIES = {
    "entities": (0x71000000, "ENTITY"),
    "section_kinds": (0x72000000, "SECTION_KIND"),
    "symbol_bindings": (0x73000000, "SYMBOL_BINDING"),
    "symbol_visibilities": (0x73100000, "SYMBOL_VISIBILITY"),
    "symbol_types": (0x73200000, "SYMBOL_TYPE"),
    "symbol_definitions": (0x73300000, "SYMBOL_DEFINITION"),
    "relocation_kinds": (0x74000000, "RELOCATION_KIND"),
    "comdat_selections": (0x75000000, "COMDAT_SELECTION"),
}

INVENTORY = {
    "entities": (
        ("GRAPH", "graph"),
        ("SECTION", "section"),
        ("SYMBOL", "symbol"),
        ("RELOCATION", "relocation"),
        ("COMDAT", "comdat"),
        ("EXTENSION", "extension"),
        ("LAYOUT_PROOF", "layout-proof"),
    ),
    "section_kinds": (
        ("TEXT", "text"),
        ("READ_ONLY_DATA", "read-only-data"),
        ("DATA", "data"),
        ("ZERO_FILL", "zero-fill"),
        ("TLS_DATA", "tls-data"),
        ("TLS_ZERO_FILL", "tls-zero-fill"),
        ("DEBUG", "debug"),
        ("UNWIND", "unwind"),
        ("FORMAT_EXTENSION", "format-extension"),
    ),
    "symbol_bindings": (
        ("LOCAL", "local"),
        ("GLOBAL", "global"),
        ("WEAK", "weak"),
        ("UNIQUE", "unique"),
        ("FORMAT_EXTENSION", "format-extension"),
    ),
    "symbol_visibilities": (
        ("DEFAULT", "default"),
        ("HIDDEN", "hidden"),
        ("PROTECTED", "protected"),
        ("INTERNAL", "internal"),
        ("FORMAT_EXTENSION", "format-extension"),
    ),
    "symbol_types": (
        ("NO_TYPE", "no-type"),
        ("OBJECT", "object"),
        ("FUNCTION", "function"),
        ("SECTION", "section"),
        ("TLS", "tls"),
        ("FILE", "file"),
        ("INDIRECT_FUNCTION", "indirect-function"),
        ("FORMAT_EXTENSION", "format-extension"),
    ),
    "symbol_definitions": (
        ("UNDEFINED", "undefined"),
        ("DEFINED", "defined"),
        ("COMMON", "common"),
        ("ABSOLUTE", "absolute"),
    ),
    "relocation_kinds": (
        ("ABSOLUTE", "absolute"),
        ("PC_RELATIVE", "pc-relative"),
        ("SECTION_RELATIVE", "section-relative"),
        ("GOT_RELATIVE", "got-relative"),
        ("PLT_RELATIVE", "plt-relative"),
        ("TLS", "tls"),
        ("IMAGE_RELATIVE", "image-relative"),
        ("TARGET_EXTENSION", "target-extension"),
    ),
    "comdat_selections": (
        ("ANY", "any"),
        ("EXACT_MATCH", "exact-match"),
        ("SAME_SIZE", "same-size"),
        ("NO_DUPLICATES", "no-duplicates"),
        ("LARGEST", "largest"),
        ("ASSOCIATIVE", "associative"),
    ),
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


def bootstrap_document():
    document = {
        "schema_version": 1,
        "capability": {"major": 1, "minor": 0},
        "stable_schema_digest": "",
    }
    for family, records in INVENTORY.items():
        base = FAMILIES[family][0]
        document[family] = [
            {
                "symbol": symbol,
                "name": name,
                "id": f"0x{base + index:08x}",
            }
            for index, (symbol, name) in enumerate(records, 1)
        ]
    document["stable_schema_digest"] = schema_digest(document)
    return document


def validate_document(document):
    if document.get("schema_version") != 1:
        raise ValueError("Object schema_version must be 1")
    if document.get("capability") != {"major": 1, "minor": 0}:
        raise ValueError("Object schema capability must be 1.0")
    all_ids = set()
    for family, expected in INVENTORY.items():
        records = document.get(family)
        if not isinstance(records, list) or len(records) != len(expected):
            raise ValueError(f"Object {family} inventory size changed")
        base = FAMILIES[family][0]
        for index, (entry, source) in enumerate(zip(records, expected), 1):
            symbol, name = source
            if entry.get("symbol") != symbol or entry.get("name") != name:
                raise ValueError(
                    f"Object {family} metadata changed for {symbol}"
                )
            stable_id = entry.get("id")
            if not isinstance(stable_id, str) or not re.fullmatch(
                r"0x[0-9a-fA-F]{8}", stable_id
            ):
                raise ValueError(f"invalid Object {family} stable ID")
            parsed = int(stable_id, 16)
            if parsed != base + index or parsed in all_ids:
                raise ValueError(
                    f"invalid or duplicate Object {family} stable ID"
                )
            all_ids.add(parsed)
    digest = schema_digest(document)
    if document.get("stable_schema_digest") != digest:
        raise ValueError(
            "Object stable schema digest changed; review the capability "
            f"version before updating it to {digest}"
        )
    return digest


def constant_name(family, symbol):
    if family == "relocation_kinds":
        return f"NEVERC_OBJECT_RELOCATION_{symbol}"
    if family == "comdat_selections":
        return f"NEVERC_OBJECT_COMDAT_{symbol}"
    return f"NEVERC_OBJECT_{FAMILIES[family][1]}_{symbol}"


def generate_body(document, digest):
    lines = [
        "#ifndef NEVERC_PLUGIN_OBJECT_SCHEMA_CONSTANTS_DEFINED",
        "#define NEVERC_PLUGIN_OBJECT_SCHEMA_CONSTANTS_DEFINED",
        "",
        "#define NEVERC_OBJECT_SCHEMA_CAPABILITY_MAJOR UINT16_C(1)",
        "#define NEVERC_OBJECT_SCHEMA_CAPABILITY_MINOR UINT16_C(0)",
        f"#define NEVERC_OBJECT_SCHEMA_DIGEST {json.dumps(digest)}",
    ]
    for family in FAMILIES:
        lines.append(
            f"#define NEVERC_OBJECT_{FAMILIES[family][1]}_COUNT "
            f"UINT32_C({len(document[family])})"
        )
    lines.append("")
    for family in FAMILIES:
        for entry in document[family]:
            lines.append(
                f"#define {constant_name(family, entry['symbol'])} "
                f"UINT32_C({entry['id']})"
            )
        lines.append("")
    lines.append(
        "#endif /* NEVERC_PLUGIN_OBJECT_SCHEMA_CONSTANTS_DEFINED */"
    )
    for family in FAMILIES:
        macro = f"NEVERC_OBJECT_SCHEMA_{FAMILIES[family][1]}"
        lines.extend(["", f"#ifdef {macro}"])
        for entry in document[family]:
            lines.append(
                f"{macro}({entry['symbol']}, "
                f"{constant_name(family, entry['symbol'])}, "
                f"{json.dumps(entry['name'])})"
            )
        lines.append("#endif")
    return "\n".join(lines) + "\n"


def generate(template, document, digest):
    text = template.read_text(encoding="utf-8")
    marker = "@OBJECT_SCHEMA_BODY@"
    if text.count(marker) != 1:
        raise ValueError(
            "Object schema template must contain one body marker"
        )
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
        if args.bootstrap:
            if args.schema.exists():
                raise ValueError(
                    "refusing to overwrite existing Object schema: "
                    f"{args.schema}"
                )
            document = bootstrap_document()
            args.schema.parent.mkdir(parents=True, exist_ok=True)
            args.schema.write_text(
                json.dumps(document, indent=2, ensure_ascii=True) + "\n",
                encoding="utf-8",
            )
        document = json.loads(args.schema.read_text(encoding="utf-8"))
        digest = validate_document(document)
        generated = generate(args.template, document, digest)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"Object schema error: {error}", file=sys.stderr)
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
