#!/usr/bin/env python3

"""Generate NeverC's private Unicode tables from the official Go snapshot.

The generated C data intentionally contains only the tables used by
``std/src/unicode/unicode.c``.  It is not a general Go-source parser: every
consumed initializer is parsed strictly so an upstream format change fails
closed instead of silently producing incomplete Unicode data.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / "std/src/unicode/unicode_tables.h"

GO_TAG = "go1.27.0"
GO_COMMIT = "8af21751f066eced273ca3ce49506b366847c623"
GO_UNICODE_VERSION = "17.0.0"
GO_SOURCE_URL = (
    "https://raw.githubusercontent.com/golang/go/"
    f"{GO_COMMIT}/src/unicode/tables.go"
)
GO_SOURCE_SHA256 = "ac0e5d5d1b58ed4b02d7cb4a0085bdefbdb4f7aae6fadddbc9477f8463da8bd8"

MAX_ASCII = 0x7F
MAX_LATIN1 = 0xFF
MAX_RUNE = 0x10FFFF
UPPER_LOWER_SENTINEL = MAX_RUNE + 1


class GenerationError(ValueError):
    """Raised when the pinned Go input does not have the expected shape."""


@dataclass(frozen=True)
class RangeTable:
    r16: tuple[tuple[int, int], ...]
    r32: tuple[tuple[int, int], ...]
    latin_offset: int


TABLE_SPECS = (
    ("letter", "_L"),
    ("upper", "_Lu"),
    ("lower", "_Ll"),
    ("title", "_Lt"),
    ("digit", "_Nd"),
    ("number", "_N"),
    ("punct", "_P"),
    ("symbol", "_S"),
    ("mark", "_M"),
    ("space", "_White_Space"),
    ("zs", "_Zs"),
)

PROPERTY_VALUES = {
    "pC": 1 << 0,
    "pP": 1 << 1,
    "pN": 1 << 2,
    "pS": 1 << 3,
    "pZ": 1 << 4,
    "pLu": 1 << 5,
    "pLl": 1 << 6,
    "pp": 1 << 7,
    "pLo": (1 << 5) | (1 << 6),
    "pLmask": (1 << 5) | (1 << 6),
    "pg": (1 << 7) | (1 << 4),
}


def extract_braced(text: str, opening: int) -> str:
    """Return a balanced Go braced block without its outer braces."""
    if opening >= len(text) or text[opening] != "{":
        raise GenerationError("internal error: expected opening brace")

    depth = 0
    index = opening
    quote: Optional[str] = None
    escaped = False
    line_comment = False
    block_comment = False

    while index < len(text):
        char = text[index]
        next_char = text[index + 1] if index + 1 < len(text) else ""

        if line_comment:
            if char == "\n":
                line_comment = False
            index += 1
            continue
        if block_comment:
            if char == "*" and next_char == "/":
                block_comment = False
                index += 2
            else:
                index += 1
            continue
        if quote is not None:
            if quote != "`" and escaped:
                escaped = False
            elif quote != "`" and char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            index += 1
            continue

        if char == "/" and next_char == "/":
            line_comment = True
            index += 2
            continue
        if char == "/" and next_char == "*":
            block_comment = True
            index += 2
            continue
        if char in ('"', "'", "`"):
            quote = char
            index += 1
            continue
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1 : index]
            if depth < 0:
                break
        index += 1

    raise GenerationError("unterminated braced initializer")


def initializer(source: str, declaration: str) -> str:
    start = source.find(declaration)
    if start < 0:
        raise GenerationError(f"missing Go declaration: {declaration}")
    opening = source.find("{", start + len(declaration))
    if opening < 0:
        raise GenerationError(f"missing initializer for: {declaration}")
    return extract_braced(source, opening)


def data_lines(body: str):
    for line_number, raw_line in enumerate(body.splitlines(), 1):
        line = raw_line.partition("//")[0].strip()
        if line:
            yield line_number, line


RANGE_ENTRY = re.compile(
    r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,"
    r"\s*([0-9]+)\s*\},?"
)


def parse_range_entries(body: str, label: str) -> list[tuple[int, int, int]]:
    entries: list[tuple[int, int, int]] = []
    previous_hi = -1
    for line_number, line in data_lines(body):
        match = RANGE_ENTRY.fullmatch(line)
        if match is None:
            raise GenerationError(
                f"unsupported {label} entry on initializer line {line_number}: {line}"
            )
        lo, hi, stride = (
            int(match.group(1), 16),
            int(match.group(2), 16),
            int(match.group(3), 10),
        )
        if stride <= 0 or lo > hi or (hi - lo) % stride != 0:
            raise GenerationError(f"invalid {label} range: {line}")
        if lo <= previous_hi:
            raise GenerationError(f"unsorted or overlapping {label} range: {line}")
        previous_hi = hi
        entries.append((lo, hi, stride))
    return entries


def parse_range_field(
    table_body: str, field: str, go_type: str, label: str
) -> list[tuple[int, int, int]]:
    match = re.search(rf"\b{field}\s*:\s*\[\]{go_type}\s*\{{", table_body)
    if match is None:
        return []
    opening = match.end() - 1
    return parse_range_entries(extract_braced(table_body, opening), label)


def normalize_ranges(
    entries: list[tuple[int, int, int]], minimum: int, maximum: int, label: str
) -> tuple[tuple[int, int], ...]:
    normalized: list[list[int]] = []
    for lo, hi, stride in entries:
        if lo < minimum or hi > maximum:
            raise GenerationError(
                f"{label} range U+{lo:04X}..U+{hi:04X} is outside its C width"
            )
        for codepoint in range(lo, hi + 1, stride):
            if normalized and codepoint == normalized[-1][1] + 1:
                normalized[-1][1] = codepoint
            else:
                normalized.append([codepoint, codepoint])
    return tuple((lo, hi) for lo, hi in normalized)


def parse_range_table(source: str, go_name: str) -> RangeTable:
    declaration = f"var {go_name} = &RangeTable"
    body = initializer(source, declaration)
    r16_source = parse_range_field(body, "R16", "Range16", f"{go_name}.R16")
    r32_source = parse_range_field(body, "R32", "Range32", f"{go_name}.R32")
    if not r16_source and not r32_source:
        raise GenerationError(f"{go_name} has no ranges")

    latin_match = re.search(r"\bLatinOffset\s*:\s*([0-9]+)\s*,?", body)
    source_latin_offset = int(latin_match.group(1)) if latin_match else 0
    expected_source_offset = sum(1 for _, hi, _ in r16_source if hi <= MAX_LATIN1)
    if source_latin_offset != expected_source_offset:
        raise GenerationError(
            f"{go_name} LatinOffset is {source_latin_offset}, expected "
            f"{expected_source_offset} from its R16 data"
        )

    r16 = normalize_ranges(r16_source, 0, 0xFFFF, f"{go_name}.R16")
    r32 = normalize_ranges(r32_source, 0x10000, MAX_RUNE, f"{go_name}.R32")
    latin_offset = sum(1 for _, hi in r16 if hi <= MAX_LATIN1)
    return RangeTable(r16=r16, r32=r32, latin_offset=latin_offset)


def evaluate_property(expression: str) -> int:
    value = 0
    for term in expression.split("|"):
        token = term.strip()
        if token in PROPERTY_VALUES:
            value |= PROPERTY_VALUES[token]
        else:
            try:
                value |= int(token, 0)
            except ValueError as error:
                raise GenerationError(
                    f"unsupported Latin-1 property expression: {expression}"
                ) from error
    if not 0 <= value <= 0xFF:
        raise GenerationError(f"Latin-1 property exceeds uint8_t: {expression}")
    return value


PROPERTY_ENTRY = re.compile(r"(0x[0-9A-Fa-f]+)\s*:\s*([^,]+),?")


def parse_properties(source: str) -> tuple[int, ...]:
    body = initializer(source, "var properties = [MaxLatin1 + 1]uint8")
    properties: dict[int, int] = {}
    for line_number, line in data_lines(body):
        match = PROPERTY_ENTRY.fullmatch(line)
        if match is None:
            raise GenerationError(
                f"unsupported properties entry on initializer line {line_number}: {line}"
            )
        index = int(match.group(1), 16)
        if index in properties:
            raise GenerationError(f"duplicate Latin-1 property index: U+{index:02X}")
        properties[index] = evaluate_property(match.group(2).strip())
    expected = set(range(MAX_LATIN1 + 1))
    if set(properties) != expected:
        missing = sorted(expected - set(properties))
        if missing:
            raise GenerationError(
                "Latin-1 property table is incomplete; first missing index "
                f"U+{missing[0]:02X}"
            )
        unexpected = sorted(set(properties) - expected)
        raise GenerationError(
            "Latin-1 property table has an out-of-range index; first is "
            f"U+{unexpected[0]:04X}"
        )
    return tuple(properties[index] for index in range(MAX_LATIN1 + 1))


HEX_VALUE = re.compile(r"(0x[0-9A-Fa-f]+),?")


def parse_ascii_fold(source: str) -> tuple[int, ...]:
    body = initializer(source, "var asciiFold = [MaxASCII + 1]uint16")
    values: list[int] = []
    for line_number, line in data_lines(body):
        match = HEX_VALUE.fullmatch(line)
        if match is None:
            raise GenerationError(
                f"unsupported asciiFold entry on initializer line {line_number}: {line}"
            )
        values.append(int(match.group(1), 16))
    if len(values) != MAX_ASCII + 1:
        raise GenerationError(f"asciiFold has {len(values)} entries, expected 128")
    return tuple(values)


CASE_RANGE_ENTRY = re.compile(
    r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*d\{"
    r"\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^}]+)\s*\}\s*\},?"
)


def parse_delta(token: str) -> int:
    token = token.strip()
    if token == "UpperLower":
        return UPPER_LOWER_SENTINEL
    try:
        return int(token, 0)
    except ValueError as error:
        raise GenerationError(f"unsupported case delta: {token}") from error


def parse_case_map(source: str) -> tuple[tuple[int, int, int, int], ...]:
    body = initializer(source, "var _CaseRanges = []CaseRange")
    result: list[tuple[int, int, int, int]] = []
    previous_hi = -1
    for line_number, line in data_lines(body):
        match = CASE_RANGE_ENTRY.fullmatch(line)
        if match is None:
            raise GenerationError(
                f"unsupported CaseRange on initializer line {line_number}: {line}"
            )
        lo, hi = int(match.group(1), 16), int(match.group(2), 16)
        deltas = tuple(parse_delta(match.group(index)) for index in (3, 4, 5))
        if lo > hi or lo <= previous_hi or hi > MAX_RUNE:
            raise GenerationError(f"invalid or unsorted CaseRange: {line}")
        previous_hi = hi

        alternating = all(delta == UPPER_LOWER_SENTINEL for delta in deltas)
        if not alternating and any(delta == UPPER_LOWER_SENTINEL for delta in deltas):
            raise GenerationError(f"partially alternating CaseRange is unsupported: {line}")

        for codepoint in range(lo, hi + 1):
            if alternating:
                offset = codepoint - lo
                mapped = tuple(lo + ((offset & ~1) | (case_index & 1)) for case_index in range(3))
            else:
                mapped = tuple(codepoint + delta for delta in deltas)
            if any(value < 0 or value > MAX_RUNE for value in mapped):
                raise GenerationError(f"CaseRange maps outside Unicode: {line}")
            result.append((codepoint, mapped[0], mapped[1], mapped[2]))
    if not result:
        raise GenerationError("CaseRanges is empty")
    return tuple(result)


FOLD_PAIR_ENTRY = re.compile(
    r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\},?"
)


def parse_case_orbit(
    source: str, case_map: tuple[tuple[int, int, int, int], ...]
) -> tuple[tuple[int, int], ...]:
    body = initializer(source, "var caseOrbit = []foldPair")
    official: list[tuple[int, int]] = []
    previous_from = -1
    for line_number, line in data_lines(body):
        match = FOLD_PAIR_ENTRY.fullmatch(line)
        if match is None:
            raise GenerationError(
                f"unsupported caseOrbit pair on initializer line {line_number}: {line}"
            )
        source_rune, target_rune = int(match.group(1), 16), int(match.group(2), 16)
        if source_rune <= previous_from:
            raise GenerationError(f"caseOrbit is not strictly sorted: {line}")
        if source_rune > MAX_RUNE or target_rune > MAX_RUNE:
            raise GenerationError(f"caseOrbit pair is outside Unicode: {line}")
        previous_from = source_rune
        official.append((source_rune, target_rune))

    mappings = {entry[0]: entry[1:] for entry in case_map}
    exceptions: list[tuple[int, int]] = []
    for source_rune, target_rune in official:
        if source_rune <= MAX_ASCII:
            continue
        upper, lower, _title = mappings.get(
            source_rune, (source_rune, source_rune, source_rune)
        )
        fallback = lower if lower != source_rune else upper
        if target_rune != fallback:
            exceptions.append((source_rune, target_rune))
    return tuple(exceptions)


def parse_version(source: str) -> str:
    match = re.search(r'^const Version = "([^"]+)"$', source, re.MULTILINE)
    if match is None:
        raise GenerationError("missing Go unicode.Version declaration")
    return match.group(1)


def load_source(input_path: Optional[Path]) -> str:
    if input_path is not None:
        try:
            raw = input_path.read_bytes()
        except OSError as error:
            raise GenerationError(f"cannot read {input_path}: {error}") from error
    else:
        request = urllib.request.Request(
            GO_SOURCE_URL,
            headers={"User-Agent": "NeverC-Unicode-Table-Generator/1"},
        )
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                raw = response.read()
        except (OSError, urllib.error.URLError) as error:
            raise GenerationError(f"cannot download pinned Go tables: {error}") from error

    digest = hashlib.sha256(raw).hexdigest()
    if digest != GO_SOURCE_SHA256:
        raise GenerationError(
            f"Go tables SHA-256 mismatch: got {digest}, expected {GO_SOURCE_SHA256}"
        )
    try:
        source = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise GenerationError("Go tables are not valid UTF-8") from error
    version = parse_version(source)
    if version != GO_UNICODE_VERSION:
        raise GenerationError(
            f"Go tables declare Unicode {version}, expected {GO_UNICODE_VERSION}"
        )
    return source


def append_properties(lines: list[str], properties: tuple[int, ...]) -> None:
    lines.append("static const uint8_t nci_uprops[256] = {")
    for index in range(0, len(properties), 8):
        values = ",    ".join(f"0x{value:02X}" for value in properties[index : index + 8])
        suffix = "," if index + 8 < len(properties) else ""
        lines.append(f"    {values}{suffix} /* U+{index:02X} */")
    lines.append("};")
    lines.append("")


def append_ascii_fold(lines: list[str], values: tuple[int, ...]) -> None:
    lines.append("static const uint32_t nci_ascii_fold[128] = {")
    for index in range(0, len(values), 8):
        chunk = ",    ".join(f"0x{value:04X}" for value in values[index : index + 8])
        suffix = "," if index + 8 < len(values) else ""
        lines.append(f"    {chunk}{suffix}")
    lines.append("};")
    lines.append("")


def append_case_orbit(lines: list[str], pairs: tuple[tuple[int, int], ...]) -> None:
    lines.append("static const nci_ufold nci_case_orbit[] = {")
    for source_rune, target_rune in pairs:
        lines.append(f"    {{0x{source_rune:X}, 0x{target_rune:X}}},")
    lines.append("};")
    lines.append(f"#define NCI_CASE_ORBIT_N {len(pairs)}")
    lines.append("")


def append_case_map(
    lines: list[str], mappings: tuple[tuple[int, int, int, int], ...]
) -> None:
    lines.append("static const nci_ucase nci_case_map[] = {")
    for codepoint, upper, lower, title in mappings:
        lines.append(
            f"    {{0x{codepoint:X}, 0x{upper:X}, 0x{lower:X}, 0x{title:X}}},"
        )
    lines.append("};")
    lines.append(f"#define NCI_CASE_MAP_N {len(mappings)}")
    lines.append("")


def append_range_array(
    lines: list[str], c_name: str, width: int, ranges: tuple[tuple[int, int], ...]
) -> None:
    lines.append(f"static const nci_ur{width} nci_{c_name}_r{width}[] = {{")
    if ranges:
        for lo, hi in ranges:
            lines.append(f"    {{0x{lo:04X}, 0x{hi:04X}, 1}},")
    elif width == 16:
        lines.append("    {0x0000, 0x0000, 1},")
    else:
        lines.append("    {0x10000, 0x10000, 1},")
    lines.append("};")


def append_range_table(lines: list[str], c_name: str, table: RangeTable) -> None:
    append_range_array(lines, c_name, 16, table.r16)
    append_range_array(lines, c_name, 32, table.r32)
    lines.append(
        f"static const nci_utable nci_tab_{c_name} = {{ "
        f"nci_{c_name}_r16, {len(table.r16)}, "
        f"nci_{c_name}_r32, {len(table.r32)}, {table.latin_offset} }};"
    )
    lines.append("")


def generate(source: str) -> str:
    properties = parse_properties(source)
    ascii_fold = parse_ascii_fold(source)
    case_map = parse_case_map(source)
    case_orbit = parse_case_orbit(source, case_map)
    tables = {c_name: parse_range_table(source, go_name) for c_name, go_name in TABLE_SPECS}

    if properties[0xAA] != 0xE0 or properties[0xBA] != 0xE0:
        raise GenerationError("Latin-1 Other_Letter properties lost pLo | pp bits")

    lines = [
        "/*",
        " * GENERATED FILE - DO NOT EDIT.",
        " * Generator: utils/unicode/gen-unicode-tables.py",
        f" * Source: Go {GO_TAG} src/unicode/tables.go (Unicode {GO_UNICODE_VERSION})",
        f" * Source commit: {GO_COMMIT}",
        f" * Source SHA-256: {GO_SOURCE_SHA256}",
        " */",
        "#ifndef NEVERC_UNICODE_TABLES_H",
        "#define NEVERC_UNICODE_TABLES_H",
        "",
        "#include <stdint.h>",
        "",
        "typedef struct { uint16_t lo, hi, stride; } nci_ur16;",
        "typedef struct { uint32_t lo, hi, stride; } nci_ur32;",
        "typedef struct {",
        "    const nci_ur16 *r16;",
        "    int n16;",
        "    const nci_ur32 *r32;",
        "    int n32;",
        "    int latin_offset;",
        "} nci_utable;",
        "typedef struct { uint32_t r, u, l, t; } nci_ucase;",
        "typedef struct { uint32_t from, to; } nci_ufold;",
        "",
        "enum {",
        "    NCI_PC  = 1 << 0,",
        "    NCI_PP  = 1 << 1,",
        "    NCI_PN  = 1 << 2,",
        "    NCI_PS  = 1 << 3,",
        "    NCI_PZ  = 1 << 4,",
        "    NCI_PLU = 1 << 5,",
        "    NCI_PLL = 1 << 6,",
        "    NCI_PPR = 1 << 7",
        "};",
        "",
    ]
    append_properties(lines, properties)
    append_ascii_fold(lines, ascii_fold)
    append_case_orbit(lines, case_orbit)
    append_case_map(lines, case_map)
    for c_name, _go_name in TABLE_SPECS:
        append_range_table(lines, c_name, tables[c_name])
    lines.append("#endif /* NEVERC_UNICODE_TABLES_H */")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        help="read the pinned tables.go from disk instead of downloading it",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"generated header path (default: {DEFAULT_OUTPUT.relative_to(ROOT)})",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if the output file differs from the generated header",
    )
    arguments = parser.parse_args()

    try:
        generated = generate(load_source(arguments.input))
        generated_bytes = generated.encode("utf-8")
        if arguments.check:
            try:
                existing = arguments.output.read_bytes()
            except OSError:
                existing = b""
            if existing != generated_bytes:
                raise GenerationError(
                    f"{arguments.output} is out of date; run {Path(__file__).name}"
                )
            return 0
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        # Keep generated output byte-for-byte reproducible on Windows too.
        arguments.output.write_bytes(generated_bytes)
    except (GenerationError, OSError) as error:
        print(f"unicode table generation error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
