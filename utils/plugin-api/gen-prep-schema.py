#!/usr/bin/env python3

import argparse
import ast
import hashlib
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOKEN_DEFINITIONS = (
    ROOT / "neverc/include/neverc/Foundation/Core/TokenKinds.def"
)
ATTRIBUTE_TOKEN_DEFINITIONS = (
    ROOT / "neverc/include/neverc/Foundation/AttrTokenKinds.td.h"
)
SCHEMA = ROOT / "neverc/include/neverc/Plugin/Schema/PrepSchema.json"
TEMPLATE = (
    ROOT
    / "neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc.in"
)
OUTPUT = (
    ROOT / "neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc"
)

TOKEN_ID_BASE = 0x10000000
PP_KEYWORD_ID_BASE = 0x20000000
UINT32_MAX = 0xFFFFFFFF

CATEGORIES = {
    "special": "NEVERC_TOKEN_CATEGORY_SPECIAL",
    "comment": "NEVERC_TOKEN_CATEGORY_COMMENT",
    "identifier": "NEVERC_TOKEN_CATEGORY_IDENTIFIER",
    "literal": "NEVERC_TOKEN_CATEGORY_LITERAL",
    "punctuator": "NEVERC_TOKEN_CATEGORY_PUNCTUATOR",
    "keyword": "NEVERC_TOKEN_CATEGORY_KEYWORD",
    "annotation": "NEVERC_TOKEN_CATEGORY_ANNOTATION",
}

LITERAL_TOKENS = {
    "numeric_constant",
    "char_constant",
    "wide_char_constant",
    "utf8_char_constant",
    "utf16_char_constant",
    "utf32_char_constant",
    "string_literal",
    "wide_string_literal",
    "header_name",
    "utf8_string_literal",
    "utf16_string_literal",
    "utf32_string_literal",
}


def split_arguments(text):
    arguments = []
    start = 0
    depth = 0
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
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        elif character == "," and depth == 0:
            arguments.append(text[start:index].strip())
            start = index + 1
    arguments.append(text[start:].strip())
    return arguments


def macro_arguments(line, macro):
    match = re.fullmatch(
        rf"{re.escape(macro)}\s*\((.*)\)\s*", line
    )
    return split_arguments(match.group(1)) if match else None


def public_symbol(internal):
    symbol = re.sub(r"[^A-Za-z0-9_]", "_", internal).upper()
    if not re.fullmatch(r"[A-Z_][A-Z0-9_]*", symbol):
        raise ValueError(f"cannot form a public symbol for {internal}")
    return symbol


def token_record(internal, category, spelling=None, pragma=False):
    constructible = (
        category in ("literal", "punctuator")
        or internal in ("eof", "identifier")
    )
    return {
        "internal": internal,
        "symbol": public_symbol(internal),
        "category": category,
        "constructible": constructible,
        "spelling": spelling,
        "pragma_annotation": pragma,
    }


def parse_attribute_tokens(path):
    records = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("//", 1)[0].strip()
        if not line or line.startswith("#"):
            continue
        arguments = macro_arguments(line, "KEYWORD_ATTRIBUTE")
        if arguments is None or len(arguments) != 1:
            raise ValueError(
                f"unrecognized attribute token definition: {raw_line}"
            )
        spelling = arguments[0]
        records.append(
            token_record(f"kw_{spelling}", "keyword", spelling)
        )
    return records


def parse_internal_schema(token_path, attribute_path):
    tokens = []
    pp_keywords = []
    attribute_tokens = parse_attribute_tokens(attribute_path)
    inserted_attribute_tokens = False

    for raw_line in token_path.read_text(encoding="utf-8").splitlines():
        stripped = raw_line.strip()
        if (
            stripped.startswith("#include")
            and "AttrTokenKinds.td.h" in stripped
        ):
            if inserted_attribute_tokens:
                raise ValueError("attribute token definitions included twice")
            tokens.extend(attribute_tokens)
            inserted_attribute_tokens = True
            continue

        line = raw_line.split("//", 1)[0].strip()
        if not line or line.startswith("#"):
            continue

        arguments = macro_arguments(line, "PPKEYWORD")
        if arguments is not None:
            if len(arguments) != 1:
                raise ValueError(
                    f"invalid PP keyword definition: {raw_line}"
                )
            spelling = arguments[0]
            pp_keywords.append(
                {
                    "internal": f"pp_{spelling}",
                    "symbol": public_symbol(spelling),
                    "spelling": spelling,
                }
            )
            continue

        arguments = macro_arguments(line, "PUNCTUATOR")
        if arguments is not None:
            if len(arguments) != 2:
                raise ValueError(
                    f"invalid punctuator definition: {raw_line}"
                )
            try:
                spelling = ast.literal_eval(arguments[1])
            except (SyntaxError, ValueError) as error:
                raise ValueError(
                    f"invalid punctuator spelling: {raw_line}"
                ) from error
            if not isinstance(spelling, str):
                raise ValueError(
                    f"punctuator spelling is not text: {raw_line}"
                )
            tokens.append(
                token_record(arguments[0], "punctuator", spelling)
            )
            continue

        arguments = macro_arguments(line, "PRAGMA_ANNOTATION")
        if arguments is not None:
            if len(arguments) != 1:
                raise ValueError(
                    f"invalid pragma annotation definition: {raw_line}"
                )
            tokens.append(
                token_record(
                    f"annot_{arguments[0]}",
                    "annotation",
                    pragma=True,
                )
            )
            continue

        arguments = macro_arguments(line, "ANNOTATION")
        if arguments is not None:
            if len(arguments) != 1:
                raise ValueError(
                    f"invalid annotation definition: {raw_line}"
                )
            tokens.append(
                token_record(f"annot_{arguments[0]}", "annotation")
            )
            continue

        keyword_arguments = None
        for macro in (
            "KEYWORD",
            "C99_KEYWORD",
            "C23_KEYWORD",
            "UNARY_EXPR_OR_TYPE_TRAIT",
            "TESTING_KEYWORD",
        ):
            keyword_arguments = macro_arguments(line, macro)
            if keyword_arguments is not None:
                break
        if keyword_arguments is not None:
            if len(keyword_arguments) < 2:
                raise ValueError(
                    f"invalid keyword definition: {raw_line}"
                )
            spelling = keyword_arguments[0]
            tokens.append(
                token_record(f"kw_{spelling}", "keyword", spelling)
            )
            continue

        arguments = macro_arguments(line, "TOK")
        if arguments is not None:
            if len(arguments) != 1:
                raise ValueError(f"invalid token definition: {raw_line}")
            internal = arguments[0]
            if internal in ("identifier", "raw_identifier"):
                category = "identifier"
            elif internal in LITERAL_TOKENS:
                category = "literal"
            elif internal == "comment":
                category = "comment"
            else:
                category = "special"
            tokens.append(token_record(internal, category))
            continue

        if any(
            macro_arguments(line, macro) is not None
            for macro in ("ALIAS", "INTERESTING_IDENTIFIER")
        ):
            continue

        raise ValueError(f"unrecognized token definition: {raw_line}")

    if not inserted_attribute_tokens:
        raise ValueError("attribute token definitions were not included")
    validate_internal_records(tokens, "token")
    validate_internal_records(pp_keywords, "PP keyword")
    return tokens, pp_keywords


def validate_internal_records(records, kind):
    internals = set()
    symbols = set()
    for record in records:
        internal = record["internal"]
        symbol = record["symbol"]
        if internal in internals:
            raise ValueError(f"duplicate internal {kind}: {internal}")
        if symbol in symbols:
            raise ValueError(f"duplicate public {kind} symbol: {symbol}")
        internals.add(internal)
        symbols.add(symbol)


def parse_id(value, field):
    if not isinstance(value, str):
        raise ValueError(f"{field} must be a hexadecimal string")
    try:
        parsed = int(value, 0)
    except ValueError as error:
        raise ValueError(f"{field} is not an integer") from error
    if parsed <= 0 or parsed > UINT32_MAX:
        raise ValueError(f"{field} is outside the stable uint32 range")
    return parsed


def normalized_digest_payload(capability, tokens, pp_keywords):
    return {
        "capability": {
            "major": capability["major"],
            "minor": capability["minor"],
        },
        "token_kinds": [
            {
                "internal": entry["internal"],
                "symbol": entry["symbol"],
                "id": entry["id"],
                "category": entry["category"],
                "constructible": entry["constructible"],
                "spelling": entry["spelling"],
                "pragma_annotation": entry["pragma_annotation"],
            }
            for entry in tokens
        ],
        "pp_keywords": [
            {
                "internal": entry["internal"],
                "symbol": entry["symbol"],
                "id": entry["id"],
                "spelling": entry["spelling"],
            }
            for entry in pp_keywords
        ],
    }


def schema_digest(capability, tokens, pp_keywords):
    encoded = json.dumps(
        normalized_digest_payload(capability, tokens, pp_keywords),
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def validate_capability(document):
    capability = document.get("capability")
    if not isinstance(capability, dict):
        raise ValueError("prep schema has no capability version")
    major = capability.get("major")
    minor = capability.get("minor")
    if (
        not isinstance(major, int)
        or isinstance(major, bool)
        or major <= 0
        or major > 0xFFFF
        or not isinstance(minor, int)
        or isinstance(minor, bool)
        or minor < 0
        or minor > 0xFFFF
    ):
        raise ValueError("prep schema capability version is invalid")
    return {"major": major, "minor": minor}


def validate_schema_entries(entries, expected, kind, id_base):
    if not isinstance(entries, list) or not entries:
        raise ValueError(f"prep schema has no {kind} entries")
    expected_by_internal = {
        entry["internal"]: entry for entry in expected
    }
    normalized = []
    ids = set()
    symbols = set()
    internals = set()
    previous_id = 0
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise ValueError(f"{kind} entry {index} is not an object")
        internal = entry.get("internal")
        symbol = entry.get("symbol")
        if (
            not isinstance(internal, str)
            or internal not in expected_by_internal
        ):
            raise ValueError(f"unknown internal {kind}: {internal}")
        if (
            not isinstance(symbol, str)
            or not re.fullmatch(r"[A-Z_][A-Z0-9_]*", symbol)
        ):
            raise ValueError(f"{kind} {internal} has an invalid symbol")
        stable_id = parse_id(entry.get("id"), f"{kind} {internal} ID")
        if stable_id <= id_base or stable_id >= id_base + 0x10000000:
            raise ValueError(f"{kind} {internal} is outside its ID family")
        if stable_id <= previous_id:
            raise ValueError(f"{kind} schema order is not stable by ID")
        previous_id = stable_id
        if stable_id in ids:
            raise ValueError(f"duplicate stable {kind} ID")
        if symbol in symbols:
            raise ValueError(f"duplicate public {kind} symbol")
        if internal in internals:
            raise ValueError(f"duplicate internal {kind}")
        ids.add(stable_id)
        symbols.add(symbol)
        internals.add(internal)

        expected_entry = expected_by_internal[internal]
        normalized_entry = {**entry, "id": stable_id}
        for field, expected_value in expected_entry.items():
            if normalized_entry.get(field) != expected_value:
                raise ValueError(
                    f"{kind} {internal} has stale {field} metadata"
                )
        normalized.append(normalized_entry)

    missing = set(expected_by_internal) - internals
    if missing:
        raise ValueError(
            f"internal {kind} entries missing from schema: "
            + ", ".join(sorted(missing))
        )
    return normalized


def load_and_validate(
    schema_path=SCHEMA,
    token_path=TOKEN_DEFINITIONS,
    attribute_path=ATTRIBUTE_TOKEN_DEFINITIONS,
):
    document = json.loads(schema_path.read_text(encoding="utf-8"))
    if document.get("schema_version") != 1:
        raise ValueError("unsupported prep schema version")
    capability = validate_capability(document)
    internal_tokens, internal_pp_keywords = parse_internal_schema(
        token_path, attribute_path
    )
    tokens = validate_schema_entries(
        document.get("token_kinds"),
        internal_tokens,
        "token",
        TOKEN_ID_BASE,
    )
    pp_keywords = validate_schema_entries(
        document.get("pp_keywords"),
        internal_pp_keywords,
        "PP keyword",
        PP_KEYWORD_ID_BASE,
    )
    digest = schema_digest(capability, tokens, pp_keywords)
    if document.get("stable_schema_digest") != digest:
        raise ValueError(
            "prep stable schema digest changed; review the ABI and update "
            "the capability major plus golden digest"
        )
    return capability, tokens, pp_keywords, digest


def c_string(value):
    return json.dumps(value, ensure_ascii=True)


def boolean(value):
    return "NEVERC_TRUE" if value else "NEVERC_FALSE"


def generate_body(capability, tokens, pp_keywords, digest):
    lines = [
        "#ifndef NEVERC_PLUGIN_PREP_SCHEMA_CONSTANTS_DEFINED",
        "#define NEVERC_PLUGIN_PREP_SCHEMA_CONSTANTS_DEFINED",
        "",
        "#define NEVERC_PREP_SCHEMA_CAPABILITY_MAJOR "
        f"UINT16_C({capability['major']})",
        "#define NEVERC_PREP_SCHEMA_CAPABILITY_MINOR "
        f"UINT16_C({capability['minor']})",
        f'#define NEVERC_PREP_SCHEMA_DIGEST "{digest}"',
        f"#define NEVERC_TOKEN_KIND_COUNT UINT32_C({len(tokens)})",
        f"#define NEVERC_PP_KEYWORD_COUNT UINT32_C({len(pp_keywords)})",
        "",
    ]
    for entry in tokens:
        lines.append(
            f"#define NEVERC_TOKEN_{entry['symbol']} "
            f"UINT32_C(0x{entry['id']:08x})"
        )
    lines.append("")
    for entry in pp_keywords:
        lines.append(
            f"#define NEVERC_PP_KEYWORD_{entry['symbol']} "
            f"UINT32_C(0x{entry['id']:08x})"
        )
    lines.extend(["", "#define NEVERC_FOR_EACH_TOKEN_KIND(M) \\"])
    for index, entry in enumerate(tokens):
        suffix = " \\" if index + 1 != len(tokens) else ""
        lines.append(
            f"  M({entry['symbol']}, NEVERC_TOKEN_{entry['symbol']}, "
            f"{CATEGORIES[entry['category']]}, "
            f"{boolean(entry['constructible'])}){suffix}"
        )
    lines.extend(["", "#define NEVERC_FOR_EACH_PP_KEYWORD(M) \\"])
    for index, entry in enumerate(pp_keywords):
        suffix = " \\" if index + 1 != len(pp_keywords) else ""
        lines.append(
            f"  M({entry['symbol']}, "
            f"NEVERC_PP_KEYWORD_{entry['symbol']}){suffix}"
        )
    lines.extend(
        [
            "",
            "#endif /* NEVERC_PLUGIN_PREP_SCHEMA_CONSTANTS_DEFINED */",
            "",
            "#ifdef NEVERC_PREP_SCHEMA_INTERNAL_TOKEN",
        ]
    )
    for entry in tokens:
        spelling = entry["spelling"] if entry["spelling"] is not None else ""
        lines.append(
            "NEVERC_PREP_SCHEMA_INTERNAL_TOKEN("
            f"{entry['internal']}, {entry['symbol']}, "
            f"NEVERC_TOKEN_{entry['symbol']}, "
            f"{CATEGORIES[entry['category']]}, "
            f"{boolean(entry['constructible'])}, "
            f"{c_string(entry['internal'])}, {c_string(spelling)}, "
            f"{boolean(entry['spelling'] is not None)}, "
            f"{boolean(entry['pragma_annotation'])})"
        )
    lines.extend(
        [
            "#endif",
            "",
            "#ifdef NEVERC_PREP_SCHEMA_INTERNAL_PP_KEYWORD",
        ]
    )
    for entry in pp_keywords:
        lines.append(
            "NEVERC_PREP_SCHEMA_INTERNAL_PP_KEYWORD("
            f"{entry['internal']}, {entry['symbol']}, "
            f"NEVERC_PP_KEYWORD_{entry['symbol']}, "
            f"{c_string(entry['spelling'])})"
        )
    lines.append("#endif")
    return "\n".join(lines)


def generate(template_path, capability, tokens, pp_keywords, digest):
    template = template_path.read_text(encoding="utf-8")
    marker = "@PREP_SCHEMA_BODY@"
    if template.count(marker) != 1:
        raise ValueError("prep schema template must contain one body marker")
    return template.replace(
        marker,
        generate_body(capability, tokens, pp_keywords, digest),
    )


def bootstrap_schema(schema_path, token_path, attribute_path):
    if schema_path.exists():
        raise ValueError(
            f"refusing to overwrite existing prep schema: {schema_path}"
        )
    tokens, pp_keywords = parse_internal_schema(token_path, attribute_path)
    for index, entry in enumerate(tokens, 1):
        entry["id"] = TOKEN_ID_BASE + index
    for index, entry in enumerate(pp_keywords, 1):
        entry["id"] = PP_KEYWORD_ID_BASE + index
    capability = {"major": 1, "minor": 0}
    digest = schema_digest(capability, tokens, pp_keywords)

    def serialized(entry):
        return {
            **entry,
            "id": f"0x{entry['id']:08x}",
        }

    document = {
        "schema_version": 1,
        "capability": capability,
        "stable_schema_digest": digest,
        "token_kinds": [serialized(entry) for entry in tokens],
        "pp_keywords": [
            serialized(entry) for entry in pp_keywords
        ],
    }
    schema_path.parent.mkdir(parents=True, exist_ok=True)
    schema_path.write_text(
        json.dumps(document, indent=2, ensure_ascii=True) + "\n",
        encoding="utf-8",
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if generated output differs",
    )
    parser.add_argument(
        "--bootstrap",
        action="store_true",
        help="create the initial explicit stable-ID schema",
    )
    parser.add_argument("--schema", type=Path, default=SCHEMA)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--template", type=Path, default=TEMPLATE)
    parser.add_argument(
        "--token-def", type=Path, default=TOKEN_DEFINITIONS
    )
    parser.add_argument(
        "--attr-token-def",
        type=Path,
        default=ATTRIBUTE_TOKEN_DEFINITIONS,
    )
    arguments = parser.parse_args()
    if arguments.check and arguments.bootstrap:
        parser.error("--check and --bootstrap are mutually exclusive")

    try:
        if arguments.bootstrap:
            bootstrap_schema(
                arguments.schema,
                arguments.token_def,
                arguments.attr_token_def,
            )
        capability, tokens, pp_keywords, digest = load_and_validate(
            arguments.schema,
            arguments.token_def,
            arguments.attr_token_def,
        )
        generated = generate(
            arguments.template,
            capability,
            tokens,
            pp_keywords,
            digest,
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"prep schema error: {error}", file=sys.stderr)
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
