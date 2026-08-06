#!/usr/bin/env python3

"""Generate the complete low-level Python view of the NeverC plugin C ABI.

The distributed single header is the type/signature/constant source of truth.
Clang's JSON AST is used instead of a handwritten C parser; the committed SDK
and ABI manifests supply interface and cross-host layout facts.  Generation is
forced through one freestanding target so checked output is host-independent.
"""

from __future__ import annotations

import argparse
import ast as python_ast
import dataclasses
import hashlib
import json
import operator
import os
import pathlib
import pprint
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Any, Iterable, Mapping


ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "pluginsdk/include/neverc/Plugin/NevercPluginAPI.h"
INCLUDE_DIR = ROOT / "pluginsdk/include"
ABI_MANIFEST = ROOT / "pluginsdk/abi/plugin.json"
SDK_MANIFEST = ROOT / "pluginsdk/manifest/plugin.json"
OUTPUT = ROOT / "pluginsdk/python/neverc_plugin/abi.py"
TRAMPOLINE_OUTPUT = (
    ROOT / "neverc/lib/Plugin/Python/PythonPluginTrampolines.inc"
)
AST_TARGET = "x86_64-unknown-linux-gnu"


@dataclasses.dataclass(frozen=True)
class CType:
    kind: str
    c_spelling: str
    name: str = ""
    target: "CType | None" = None
    length: int | None = None
    result: "CType | None" = None
    arguments: tuple["CType", ...] = ()
    argument_names: tuple[str, ...] = ()


@dataclasses.dataclass(frozen=True)
class Field:
    name: str
    type: CType


@dataclasses.dataclass(frozen=True)
class Record:
    name: str
    kind: str
    fields: tuple[Field, ...]


@dataclasses.dataclass(frozen=True)
class Inventory:
    records: Mapping[str, Record]
    public_records: frozenset[str]
    typedefs: Mapping[str, CType]
    function_fields: Mapping[str, CType]
    callback_typedefs: Mapping[str, CType]
    constants: Mapping[str, int | str]
    unresolved_constants: Mapping[str, str]
    ignored_macros: Mapping[str, str]
    interfaces: frozenset[str]
    interface_specs: Mapping[str, Mapping[str, int | str]]
    abi_layouts: Mapping[str, Any]
    versions: Mapping[str, Any]


def _find_clang(explicit: str | None = None) -> str:
    candidate = explicit or os.environ.get("NEVERC_CLANG") or shutil.which("clang")
    if not candidate:
        raise ValueError("clang is required to generate the Python plugin ABI")
    return candidate


def _clang_command(clang: str, *arguments: str) -> list[str]:
    return [
        clang,
        "-target",
        AST_TARGET,
        "-ffreestanding",
        "-x",
        "c",
        "-std=c11",
        "-I",
        str(INCLUDE_DIR),
        *arguments,
        "-",
    ]


def _translation_unit() -> str:
    return '#include "neverc/Plugin/NevercPluginAPI.h"\n'


def _run_clang_ast(clang: str) -> dict[str, Any]:
    process = subprocess.run(
        _clang_command(clang, "-Xclang", "-ast-dump=json", "-fsyntax-only"),
        input=_translation_unit(),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if process.returncode != 0:
        raise ValueError(f"clang AST extraction failed:\n{process.stderr.strip()}")
    try:
        return json.loads(process.stdout)
    except json.JSONDecodeError as error:
        raise ValueError(f"clang emitted invalid AST JSON: {error}") from error


def _run_clang_macros(clang: str) -> str:
    process = subprocess.run(
        _clang_command(clang, "-dM", "-E"),
        input=_translation_unit(),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if process.returncode != 0:
        raise ValueError(f"clang macro extraction failed:\n{process.stderr.strip()}")
    return process.stdout


def _walk(node: Mapping[str, Any]) -> Iterable[Mapping[str, Any]]:
    yield node
    for child in node.get("inner", ()):
        yield from _walk(child)


def _split_arguments(text: str) -> list[str]:
    if not text.strip() or text.strip() == "void":
        return []
    pieces: list[str] = []
    start = 0
    depth = 0
    for index, character in enumerate(text):
        if character in "([":
            depth += 1
        elif character in ")]":
            depth -= 1
        elif character == "," and depth == 0:
            pieces.append(text[start:index].strip())
            start = index + 1
    pieces.append(text[start:].strip())
    if depth != 0 or any(not piece for piece in pieces):
        raise ValueError(f"cannot split C function arguments: {text!r}")
    return pieces


_ANONYMOUS_LOCATION = re.compile(
    r"unnamed(?: struct| union)? at .*:(?P<line>\d+):(?P<column>\d+)\)"
)


def _anonymous_location(spelling: str) -> tuple[int, int] | None:
    match = _ANONYMOUS_LOCATION.search(spelling)
    if not match:
        return None
    return int(match.group("line")), int(match.group("column"))


_PRIMITIVES = {
    "_Bool": "c_bool",
    "char": "c_char",
    "signed char": "c_int8",
    "unsigned char": "c_uint8",
    "short": "c_int16",
    "short int": "c_int16",
    "signed short": "c_int16",
    "signed short int": "c_int16",
    "unsigned short": "c_uint16",
    "unsigned short int": "c_uint16",
    "int": "c_int32",
    "signed": "c_int32",
    "signed int": "c_int32",
    "unsigned": "c_uint32",
    "unsigned int": "c_uint32",
    # AST extraction is pinned to x86_64 Linux. Public ABI integers are
    # fixed-width aliases, so normalize target spellings to fixed ctypes.
    "long": "c_int64",
    "long int": "c_int64",
    "signed long": "c_int64",
    "signed long int": "c_int64",
    "unsigned long": "c_uint64",
    "unsigned long int": "c_uint64",
    "long long": "c_int64",
    "long long int": "c_int64",
    "signed long long": "c_int64",
    "signed long long int": "c_int64",
    "unsigned long long": "c_uint64",
    "unsigned long long int": "c_uint64",
    "float": "c_float",
    "double": "c_double",
}
_FIXED_PRIMITIVE_ALIASES = {
    "int8_t": "c_int8",
    "uint8_t": "c_uint8",
    "int16_t": "c_int16",
    "uint16_t": "c_uint16",
    "int32_t": "c_int32",
    "uint32_t": "c_uint32",
    "int64_t": "c_int64",
    "uint64_t": "c_uint64",
    "intptr_t": "c_int64",
    "uintptr_t": "c_uint64",
    "ptrdiff_t": "c_int64",
    "size_t": "c_uint64",
}


def _normalize_type_spelling(spelling: str) -> str:
    spelling = re.sub(r"\b__cdecl\b", "", spelling)
    spelling = re.sub(r"\s*__attribute__\s*\(\(cdecl\)\)\s*", " ", spelling)
    return " ".join(spelling.split())


def _parse_type(
    c_spelling: str,
    desugared: str | None,
    anonymous_names: Mapping[tuple[int, int], str],
    typedef_underlyings: Mapping[str, str],
) -> CType:
    original = _normalize_type_spelling(c_spelling)
    underlying = _normalize_type_spelling(desugared or c_spelling)

    # Clang prints all official callbacks as Return (*)(Arguments).
    function_pattern = r"(.+?)\s*\(\s*\*\s*\)\s*\((.*)\)"
    function_match = re.fullmatch(function_pattern, original)
    if function_match is None:
        function_match = re.fullmatch(function_pattern, underlying)
    if function_match:
        result = _parse_type(
            function_match.group(1), None, anonymous_names, typedef_underlyings
        )
        arguments = tuple(
            _parse_type(argument, None, anonymous_names, typedef_underlyings)
            for argument in _split_arguments(function_match.group(2))
        )
        return CType(
            "function_pointer",
            original,
            result=result,
            arguments=arguments,
        )

    array_match = re.fullmatch(r"(.+?)\s*\[(\d+)\]", underlying)
    if array_match:
        target_spelling = array_match.group(1).strip()
        return CType(
            "array",
            original,
            target=_parse_type(
                target_spelling, None, anonymous_names, typedef_underlyings
            ),
            length=int(array_match.group(2)),
        )

    # Top-level qualifiers do not affect ctypes representation.
    unqualified = re.sub(r"^(?:const|volatile|restrict)\s+", "", underlying)
    unqualified = re.sub(r"\s+(?:const|volatile|restrict)$", "", unqualified)
    unqualified = unqualified.strip()

    if unqualified.endswith("*"):
        target_spelling = unqualified[:-1].strip()
        return CType(
            "pointer",
            original,
            target=_parse_type(
                target_spelling, None, anonymous_names, typedef_underlyings
            ),
        )

    location = _anonymous_location(unqualified)
    if location is not None:
        try:
            name = anonymous_names[location]
        except KeyError as error:
            raise ValueError(
                f"anonymous C record at {location} has no generated name"
            ) from error
        return CType("record", original, name=name)

    record_match = re.fullmatch(r"(?:struct|union)\s+([A-Za-z_]\w*)", unqualified)
    if record_match:
        return CType("record", original, name=record_match.group(1))

    if unqualified == "void":
        return CType("void", original)
    if unqualified in _FIXED_PRIMITIVE_ALIASES:
        return CType(
            "primitive", original, name=_FIXED_PRIMITIVE_ALIASES[unqualified]
        )
    if unqualified in _PRIMITIVES:
        return CType("primitive", original, name=_PRIMITIVES[unqualified])
    if unqualified in typedef_underlyings:
        resolved = _parse_type(
            typedef_underlyings[unqualified],
            None,
            anonymous_names,
            typedef_underlyings,
        )
        return dataclasses.replace(resolved, c_spelling=original)

    raise ValueError(
        f"unsupported public C type {original!r} (desugared {underlying!r})"
    )


def _assign_anonymous_names(
    root: Mapping[str, Any],
) -> dict[tuple[int, int], str]:
    referenced_names: dict[tuple[int, int], str] = {}
    for node in _walk(root):
        if node.get("kind") != "FieldDecl" or not node.get("name"):
            continue
        type_info = node.get("type", {})
        for spelling in (
            type_info.get("qualType", ""),
            type_info.get("desugaredQualType", ""),
        ):
            location = _anonymous_location(spelling)
            if location is not None:
                referenced_names[location] = node["name"]

    names: dict[tuple[int, int], str] = {}

    def visit(record_node: Mapping[str, Any], parent_name: str) -> None:
        for child in record_node.get("inner", ()):
            if child.get("kind") != "RecordDecl" or not child.get(
                "completeDefinition"
            ):
                continue
            child_name = child.get("name")
            if child_name:
                visit(child, child_name)
                continue
            location = (
                int(child.get("loc", {}).get("line", 0)),
                int(child.get("loc", {}).get("col", 0)),
            )
            field_name = referenced_names.get(location)
            if not field_name:
                raise ValueError(
                    f"anonymous record at {location} is not attached to a named field"
                )
            generated = f"_{parent_name.lstrip('_')}_{field_name}"
            names[location] = generated
            visit(child, generated)

    for node in _walk(root):
        name = node.get("name", "")
        if (
            node.get("kind") == "RecordDecl"
            and node.get("completeDefinition")
            and name.startswith("Neverc")
        ):
            visit(node, name)
    return names


def _collect_records(
    root: Mapping[str, Any],
    anonymous_names: Mapping[tuple[int, int], str],
    typedef_underlyings: Mapping[str, str],
) -> tuple[dict[str, Record], frozenset[str]]:
    nodes: dict[str, Mapping[str, Any]] = {}
    public: set[str] = set()
    for node in _walk(root):
        if node.get("kind") != "RecordDecl" or not node.get("completeDefinition"):
            continue
        name = node.get("name", "")
        if name.startswith("Neverc"):
            nodes[name] = node
            public.add(name)
            continue
        location = (
            int(node.get("loc", {}).get("line", 0)),
            int(node.get("loc", {}).get("col", 0)),
        )
        if location in anonymous_names:
            nodes[anonymous_names[location]] = node

    records: dict[str, Record] = {}
    for name in sorted(nodes):
        node = nodes[name]
        fields: list[Field] = []
        for child in node.get("inner", ()):
            if child.get("kind") != "FieldDecl":
                continue
            if child.get("isBitfield"):
                raise ValueError(f"public bitfield is unsupported: {name}.{child.get('name')}")
            field_name = child.get("name")
            if not field_name:
                raise ValueError(f"unnamed public field in {name}")
            type_info = child.get("type", {})
            fields.append(
                Field(
                    field_name,
                    _parse_type(
                        type_info["qualType"],
                        type_info.get("desugaredQualType"),
                        anonymous_names,
                        typedef_underlyings,
                    ),
                )
            )
        records[name] = Record(
            name=name,
            kind="union" if node.get("tagUsed") == "union" else "struct",
            fields=tuple(fields),
        )
    return records, frozenset(public)


def _collect_typedefs(
    root: Mapping[str, Any],
    anonymous_names: Mapping[tuple[int, int], str],
    typedef_underlyings: Mapping[str, str],
) -> dict[str, CType]:
    typedefs: dict[str, CType] = {}
    header_text = HEADER.read_text(encoding="utf-8")
    for node in _walk(root):
        name = node.get("name", "")
        if node.get("kind") != "TypedefDecl" or not name.startswith("Neverc"):
            continue
        type_info = node.get("type", {})
        parsed = _parse_type(
            type_info["qualType"],
            type_info.get("desugaredQualType"),
            anonymous_names,
            typedef_underlyings,
        )
        if parsed.kind == "function_pointer":
            begin = node.get("range", {}).get("begin", {}).get("offset")
            end_info = node.get("range", {}).get("end", {})
            end = end_info.get("offset")
            if begin is None or end is None:
                raise ValueError(f"callback typedef {name} has no source range")
            end += int(end_info.get("tokLen", 0))
            source = header_text[int(begin) : int(end)]
            match = re.search(
                rf"\*\s*{re.escape(name)}\s*\)\s*\((.*)\)\s*$",
                source,
                re.DOTALL,
            )
            if not match:
                raise ValueError(f"cannot recover callback parameter names for {name}")
            parameters = _split_arguments(match.group(1))
            argument_names = []
            for index, parameter in enumerate(parameters):
                parameter = re.sub(r"/\*.*?\*/", " ", parameter, flags=re.DOTALL)
                name_match = re.search(
                    r"([A-Za-z_]\w*)\s*(?:\[[^]]*\])?\s*$", parameter
                )
                argument_names.append(
                    name_match.group(1) if name_match else f"Argument{index}"
                )
            if len(argument_names) != len(parsed.arguments):
                raise ValueError(
                    f"callback typedef {name} has mismatched parameter names"
                )
            parsed = dataclasses.replace(
                parsed, argument_names=tuple(argument_names)
            )
        typedefs[name] = parsed
    return dict(sorted(typedefs.items()))


def _collect_typedef_underlyings(root: Mapping[str, Any]) -> dict[str, str]:
    underlyings: dict[str, str] = {}
    for node in _walk(root):
        name = node.get("name", "")
        if node.get("kind") != "TypedefDecl" or not name.startswith("Neverc"):
            continue
        type_info = node.get("type", {})
        underlyings[name] = type_info.get("desugaredQualType", type_info["qualType"])
    return underlyings


def _apply_callback_typedefs(
    records: Mapping[str, Record], typedefs: Mapping[str, CType]
) -> dict[str, Record]:
    updated: dict[str, Record] = {}
    for name, record in records.items():
        fields: list[Field] = []
        for field in record.fields:
            alias = typedefs.get(field.type.c_spelling)
            if (
                field.type.kind == "function_pointer"
                and alias is not None
                and alias.kind == "function_pointer"
            ):
                fields.append(
                    Field(
                        field.name,
                        dataclasses.replace(alias, c_spelling=field.type.c_spelling),
                    )
                )
            else:
                fields.append(field)
        updated[name] = dataclasses.replace(record, fields=tuple(fields))
    return updated


_MACRO_LINE = re.compile(
    r"^#define\s+(?P<name>NEVERC_[A-Za-z0-9_]+)"
    r"(?P<parameters>\([^)]*\))?(?:\s+(?P<value>.*))?$"
)
_CONSTANT_WRAPPER = re.compile(
    r"\b_*(?:UINT|INT)(?:8|16|32|64)_C\(\s*([^()]*)\s*\)"
)
_INTEGER_LIMITS = {
    "UINT8_MAX": (1 << 8) - 1,
    "UINT16_MAX": (1 << 16) - 1,
    "UINT32_MAX": (1 << 32) - 1,
    "UINT64_MAX": (1 << 64) - 1,
    "INT8_MAX": (1 << 7) - 1,
    "INT16_MAX": (1 << 15) - 1,
    "INT32_MAX": (1 << 31) - 1,
    "INT64_MAX": (1 << 63) - 1,
    "INT8_MIN": -(1 << 7),
    "INT16_MIN": -(1 << 15),
    "INT32_MIN": -(1 << 31),
    "INT64_MIN": -(1 << 63),
}
_BINARY_OPERATORS = {
    python_ast.Add: operator.add,
    python_ast.Sub: operator.sub,
    python_ast.Mult: operator.mul,
    python_ast.FloorDiv: operator.floordiv,
    python_ast.Mod: operator.mod,
    python_ast.LShift: operator.lshift,
    python_ast.RShift: operator.rshift,
    python_ast.BitOr: operator.or_,
    python_ast.BitAnd: operator.and_,
    python_ast.BitXor: operator.xor,
}
_UNARY_OPERATORS = {
    python_ast.UAdd: operator.pos,
    python_ast.USub: operator.neg,
    python_ast.Invert: operator.invert,
}
_IGNORED_OBJECT_MACROS = {
    "NEVERC_ABI_PACK_BEGIN",
    "NEVERC_ABI_PACK_END",
    "NEVERC_CALL",
    "NEVERC_EXPORT",
}


def _normalize_constant_expression(expression: str) -> str:
    previous = None
    while expression != previous:
        previous = expression
        expression = _CONSTANT_WRAPPER.sub(r"(\1)", expression)
    return expression.strip()


def _evaluate_macros(
    macro_text: str,
) -> tuple[dict[str, int | str], dict[str, str], dict[str, str]]:
    raw: dict[str, str] = {}
    ignored: dict[str, str] = {}
    for line in macro_text.splitlines():
        match = _MACRO_LINE.match(line)
        if not match:
            continue
        name = match.group("name")
        value = match.group("value") or ""
        if match.group("parameters") is not None:
            ignored[name] = value
        elif not value or name in _IGNORED_OBJECT_MACROS or name.endswith("_DEFINED"):
            ignored[name] = value
        elif re.fullmatch(r"NEVERC_PLUGIN_[A-Z0-9_]+_H", name):
            ignored[name] = value
        else:
            raw[name] = value

    resolved: dict[str, int | str] = {}
    resolving: set[str] = set()

    def evaluate_node(node: python_ast.AST) -> int | str:
        if isinstance(node, python_ast.Expression):
            return evaluate_node(node.body)
        if isinstance(node, python_ast.Constant) and isinstance(node.value, (int, str)):
            return node.value
        if isinstance(node, python_ast.Name):
            if node.id in _INTEGER_LIMITS:
                return _INTEGER_LIMITS[node.id]
            return resolve(node.id)
        if isinstance(node, python_ast.BinOp) and type(node.op) in _BINARY_OPERATORS:
            left = evaluate_node(node.left)
            right = evaluate_node(node.right)
            if not isinstance(left, int) or not isinstance(right, int):
                raise ValueError("non-integer operand in integer constant")
            return _BINARY_OPERATORS[type(node.op)](left, right)
        if isinstance(node, python_ast.UnaryOp) and type(node.op) in _UNARY_OPERATORS:
            operand = evaluate_node(node.operand)
            if not isinstance(operand, int):
                raise ValueError("non-integer unary operand")
            return _UNARY_OPERATORS[type(node.op)](operand)
        raise ValueError(f"unsupported expression node {type(node).__name__}")

    def resolve(name: str) -> int | str:
        if name in resolved:
            return resolved[name]
        if name not in raw:
            raise ValueError(f"unknown constant {name}")
        if name in resolving:
            raise ValueError(f"cyclic macro alias involving {name}")
        resolving.add(name)
        expression = _normalize_constant_expression(raw[name])
        try:
            tree = python_ast.parse(expression, mode="eval")
            value = evaluate_node(tree)
        finally:
            resolving.remove(name)
        resolved[name] = value
        return value

    unresolved: dict[str, str] = {}
    for name in sorted(raw):
        try:
            resolve(name)
        except (SyntaxError, ValueError, TypeError, ZeroDivisionError) as error:
            unresolved[name] = f"{raw[name]} ({error})"
    return dict(sorted(resolved.items())), unresolved, dict(sorted(ignored.items()))


def collect_inventory(clang: str | None = None) -> Inventory:
    clang_path = _find_clang(clang)
    ast_root = _run_clang_ast(clang_path)
    anonymous_names = _assign_anonymous_names(ast_root)
    typedef_underlyings = _collect_typedef_underlyings(ast_root)
    typedefs = _collect_typedefs(
        ast_root, anonymous_names, typedef_underlyings
    )
    records, public_records = _collect_records(
        ast_root, anonymous_names, typedef_underlyings
    )
    records = _apply_callback_typedefs(records, typedefs)

    function_fields = {
        f"{record.name}.{field.name}": field.type
        for record in records.values()
        for field in record.fields
        if field.type.kind == "function_pointer"
    }
    callback_typedefs = {
        name: typedef
        for name, typedef in typedefs.items()
        if typedef.kind == "function_pointer"
    }
    constants, unresolved, ignored = _evaluate_macros(_run_clang_macros(clang_path))

    abi = json.loads(ABI_MANIFEST.read_text(encoding="utf-8"))
    manifest = json.loads(SDK_MANIFEST.read_text(encoding="utf-8"))
    interface_entries = [
        interface
        for module in manifest["modules"]
        for interface in module["interfaces"]
    ]
    interfaces = frozenset(interface["name"] for interface in interface_entries)

    def normalized_interface_table(name: str) -> str:
        return re.sub(r"[^A-Z0-9]", "", name.upper())

    api_tables: dict[str, list[str]] = {}
    for record_name in records:
        if not record_name.startswith("Neverc") or not record_name.endswith("API"):
            continue
        key = normalized_interface_table(record_name[len("Neverc") : -len("API")])
        api_tables.setdefault(key, []).append(record_name)
    interface_specs: dict[str, dict[str, int | str]] = {}
    for interface in interface_entries:
        key = normalized_interface_table(interface["name"])
        candidates = api_tables.get(key, [])
        if len(candidates) != 1:
            raise ValueError(
                f"interface {interface['name']} maps to API tables {candidates}"
            )
        interface_specs[interface["name"]] = {
            "table": candidates[0],
            "high": int(interface["id_high"], 16),
            "low": int(interface["id_low"], 16),
            "major": int(interface["major"]),
            "minor": int(interface["minor"]),
        }
    return Inventory(
        records=dict(sorted(records.items())),
        public_records=public_records,
        typedefs=typedefs,
        function_fields=dict(sorted(function_fields.items())),
        callback_typedefs=dict(sorted(callback_typedefs.items())),
        constants=constants,
        unresolved_constants=unresolved,
        ignored_macros=ignored,
        interfaces=interfaces,
        interface_specs=dict(sorted(interface_specs.items())),
        abi_layouts=abi["abi_keys"],
        versions=abi.get("versions", {}),
    )


def _ctype_expression(c_type: CType, *, field: bool = False) -> str:
    if c_type.kind == "void":
        return "None"
    if c_type.kind == "primitive":
        return f"ctypes.{c_type.name}"
    if c_type.kind == "record":
        return c_type.name
    if c_type.kind == "pointer":
        if c_type.target is None or c_type.target.kind == "void":
            return "ctypes.c_void_p"
        if c_type.target.kind == "function_pointer":
            return "ctypes.c_void_p"
        return f"ctypes.POINTER({_ctype_expression(c_type.target)})"
    if c_type.kind == "array":
        if c_type.target is None or c_type.length is None:
            raise ValueError(f"incomplete array type {c_type.c_spelling}")
        return f"({_ctype_expression(c_type.target)} * {c_type.length})"
    if c_type.kind == "function_pointer":
        if field:
            return "ctypes.c_void_p"
        if c_type.result is None:
            raise ValueError(f"incomplete function type {c_type.c_spelling}")
        arguments = ", ".join(_ctype_expression(arg) for arg in c_type.arguments)
        suffix = f", {arguments}" if arguments else ""
        return f"ctypes.CFUNCTYPE({_ctype_expression(c_type.result)}{suffix})"
    raise ValueError(f"cannot render C type kind {c_type.kind}")


def _signature_literal(c_type: CType) -> str:
    if c_type.kind != "function_pointer" or c_type.result is None:
        raise ValueError(f"not a complete function signature: {c_type.c_spelling}")
    arguments = tuple(argument.c_spelling for argument in c_type.arguments)
    return repr((c_type.result.c_spelling, arguments))


def _record_definition_order(records: Mapping[str, Record]) -> list[str]:
    """Order `_fields_` assignments by by-value record dependencies."""

    def dependencies(c_type: CType) -> Iterable[str]:
        if c_type.kind == "record":
            yield c_type.name
        elif c_type.kind == "array" and c_type.target is not None:
            yield from dependencies(c_type.target)
        # Pointers and function-pointer storage do not require a complete type.

    ordered: list[str] = []
    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(name: str) -> None:
        if name in visited:
            return
        if name in visiting:
            raise ValueError(f"recursive by-value public record involving {name}")
        visiting.add(name)
        for field in records[name].fields:
            for dependency in dependencies(field.type):
                if dependency == name:
                    raise ValueError(f"record {name} contains itself by value")
                if dependency in records:
                    visit(dependency)
        visiting.remove(name)
        visited.add(name)
        ordered.append(name)

    for record_name in sorted(records):
        visit(record_name)
    return ordered


def _digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def render_python(inventory: Inventory) -> str:
    if inventory.unresolved_constants:
        details = ", ".join(
            f"{name}={value}" for name, value in inventory.unresolved_constants.items()
        )
        raise ValueError(f"unresolved public constants: {details}")

    lines = [
        "# Generated by utils/plugin-api/gen-python-abi.py; DO NOT EDIT.",
        f"# header-sha256: {_digest(HEADER)}",
        f"# sdk-manifest-sha256: {_digest(SDK_MANIFEST)}",
        f"# abi-manifest-sha256: {_digest(ABI_MANIFEST)}",
        "from __future__ import annotations",
        "",
        "import ctypes",
        "import platform",
        "import sys",
        "",
        f"AST_TARGET = {AST_TARGET!r}",
        "",
    ]

    for name, record in inventory.records.items():
        base = "ctypes.Union" if record.kind == "union" else "ctypes.Structure"
        lines.extend(
            [
                f"class {name}({base}):",
                "    _pack_ = 8",
                "    pass",
                "",
            ]
        )

    for name in _record_definition_order(inventory.records):
        record = inventory.records[name]
        lines.append(f"{name}._fields_ = [")
        for field in record.fields:
            lines.append(
                f"    ({field.name!r}, {_ctype_expression(field.type, field=True)}),"
            )
        lines.extend(["]", ""])

    function_signatures = {
        **inventory.callback_typedefs,
        **inventory.function_fields,
    }
    lines.append("FUNCTION_SIGNATURES = {")
    for name in sorted(function_signatures):
        lines.append(f"    {name!r}: {_signature_literal(function_signatures[name])},")
    lines.extend(["}", ""])

    lines.extend(
        [
            "_FUNCTION_CTYPES = {",
        ]
    )
    for name in sorted(function_signatures):
        lines.append(
            f"    {name!r}: {_ctype_expression(function_signatures[name])},"
        )
    lines.extend(
        [
            "}",
            "",
            "def function_type(symbol: str):",
            "    \"\"\"Return the exact outbound ctypes prototype for an SDK symbol.\"\"\"",
            "    try:",
            "        return _FUNCTION_CTYPES[symbol]",
            "    except KeyError as error:",
            "        raise KeyError(f'unknown NeverC function symbol: {symbol}') from error",
            "",
            "def bind_function(table, field: str):",
            "    \"\"\"Bind one non-null API-table slot after its table was size-checked.\"\"\"",
            "    if hasattr(table, 'contents'):",
            "        table = table.contents",
            "    symbol = f'{type(table).__name__}.{field}'",
            "    prototype = function_type(symbol)",
            "    address = getattr(table, field)",
            "    if not address:",
            "        raise RuntimeError(f'NeverC API slot is null: {symbol}')",
            "    return prototype(address)",
            "",
        ]
    )

    # Export all non-function aliases. Record typedefs already share their
    # class name; callback aliases are assigned after function_type exists.
    for name, c_type in inventory.typedefs.items():
        if name in inventory.records or c_type.kind == "function_pointer":
            continue
        lines.append(f"{name} = {_ctype_expression(c_type)}")
    lines.append("")
    for name in sorted(inventory.callback_typedefs):
        lines.append(f"{name} = function_type({name!r})")
    lines.append("")

    for name, value in inventory.constants.items():
        lines.append(f"{name} = {value!r}")
    lines.append("")

    lines.append(
        "ABI_LAYOUTS = "
        + pprint.pformat(dict(inventory.abi_layouts), width=100, sort_dicts=True)
    )
    lines.append(f"ABI_VERSIONS = {pprint.pformat(dict(inventory.versions), sort_dicts=True)}")
    lines.append(f"PUBLIC_RECORDS = {tuple(sorted(inventory.public_records))!r}")
    lines.append(
        "INTERFACE_SPECS = "
        + pprint.pformat(dict(inventory.interface_specs), width=100, sort_dicts=True)
    )
    lines.extend(
        [
            "",
            "def native_abi_key() -> str:",
            "    machine = platform.machine().lower()",
            "    if machine in {'amd64', 'x86_64'}:",
            "        arch = 'x86_64'",
            "    elif machine in {'aarch64', 'arm64'}:",
            "        arch = 'aarch64'",
            "    else:",
            "        raise RuntimeError(f'unsupported NeverC plugin host architecture: {machine}')",
            "    convention = 'win' if sys.platform == 'win32' else 'sysv'",
            "    return f'{arch}-le-{ctypes.sizeof(ctypes.c_void_p) * 8}-{convention}'",
            "",
            "def validate_layout(abi_key: str | None = None) -> None:",
            "    \"\"\"Validate size/alignment/offset/field-size for one committed ABI.\"\"\"",
            "    key = abi_key or native_abi_key()",
            "    try:",
            "        expected = ABI_LAYOUTS[key]['structs']",
            "    except KeyError as error:",
            "        raise RuntimeError(f'no committed NeverC ABI layout for {key}') from error",
            "    actual_names = set(PUBLIC_RECORDS)",
            "    expected_names = set(expected)",
            "    if actual_names != expected_names:",
            "        missing = sorted(actual_names - expected_names)",
            "        extra = sorted(expected_names - actual_names)",
            "        raise RuntimeError(f'ABI record inventory mismatch: missing={missing}, extra={extra}')",
            "    for name in PUBLIC_RECORDS:",
            "        record = globals()[name]",
            "        layout = expected[name]",
            "        if ctypes.sizeof(record) != layout['size']:",
            "            raise RuntimeError(f'{name}: sizeof {ctypes.sizeof(record)} != {layout[\"size\"]}')",
            "        if ctypes.alignment(record) != layout['align']:",
            "            raise RuntimeError(f'{name}: align {ctypes.alignment(record)} != {layout[\"align\"]}')",
            "        field_types = dict(record._fields_)",
            "        for field_name, field_layout in layout['fields'].items():",
            "            field = getattr(record, field_name)",
            "            if field.offset != field_layout['offset']:",
            "                raise RuntimeError(f'{name}.{field_name}: offset {field.offset} != {field_layout[\"offset\"]}')",
            "            field_size = ctypes.sizeof(field_types[field_name])",
            "            if field_size != field_layout['size']:",
            "                raise RuntimeError(f'{name}.{field_name}: size {field_size} != {field_layout[\"size\"]}')",
            "",
            "def validate_all_layouts() -> None:",
            "    for key in sorted(ABI_LAYOUTS):",
            "        validate_layout(key)",
            "",
        ]
    )

    exports = sorted(
        set(inventory.public_records)
        | set(inventory.typedefs)
        | set(inventory.constants)
        | {
            "ABI_LAYOUTS",
            "ABI_VERSIONS",
            "AST_TARGET",
            "FUNCTION_SIGNATURES",
            "INTERFACE_SPECS",
            "PUBLIC_RECORDS",
            "bind_function",
            "function_type",
            "native_abi_key",
            "validate_all_layouts",
            "validate_layout",
        }
    )
    lines.append(f"__all__ = {exports!r}")
    lines.append("")
    return "\n".join(lines)


def _userdata_callback_records(
    inventory: Inventory,
) -> dict[str, tuple[Field, ...]]:
    result: dict[str, tuple[Field, ...]] = {}
    for name, record in inventory.records.items():
        if not any(field.name == "UserData" for field in record.fields):
            continue
        callbacks = tuple(
            field for field in record.fields if field.type.kind == "function_pointer"
        )
        if callbacks:
            result[name] = callbacks
    return result


def _trampoline_name(record: str, field: str) -> str:
    return f"pythonTrampoline_{record}_{field}"


def render_trampolines(inventory: Inventory) -> str:
    """Render exact native trampolines for every UserData-bearing callback."""

    callback_records = _userdata_callback_records(inventory)
    lines = [
        "// Generated by utils/plugin-api/gen-python-abi.py; DO NOT EDIT.",
        f"// header-sha256: {_digest(HEADER)}",
        "",
    ]
    for record_name, fields in callback_records.items():
        for field in fields:
            callback = field.type
            if callback.result is None:
                raise ValueError(f"incomplete callback {record_name}.{field.name}")
            if len(callback.argument_names) != len(callback.arguments):
                raise ValueError(
                    f"callback {record_name}.{field.name} has no parameter names"
                )
            try:
                user_data_index = callback.argument_names.index("UserData")
            except ValueError as error:
                raise ValueError(
                    f"callback {record_name}.{field.name} has no UserData parameter"
                ) from error
            symbol = f"{record_name}.{field.name}"
            result_spelling = callback.result.c_spelling
            if result_spelling not in {"NevercStatus", "void"}:
                raise ValueError(
                    f"callback {symbol} has unsupported return {result_spelling}"
                )
            parameters = ", ".join(
                f"{argument.c_spelling} Argument{index}"
                for index, argument in enumerate(callback.arguments)
            )
            lines.append(f"// {symbol}")
            lines.append(
                f"static {result_spelling} NEVERC_CALL "
                f"{_trampoline_name(record_name, field.name)}({parameters}) {{"
            )
            visible = [
                (index, argument)
                for index, argument in enumerate(callback.arguments)
                if index != user_data_index
            ]
            if visible:
                lines.append("  const PythonCallbackArgument Arguments[] = {")
                for index, argument in visible:
                    lines.append(
                        "      makePythonCallbackArgument("
                        f"{json.dumps(argument.c_spelling)}, Argument{index}),"
                    )
                lines.append("  };")
                arguments_expression = "Arguments, std::size(Arguments)"
            else:
                arguments_expression = "nullptr, 0"
            dispatch = (
                "invokePythonStatusCallback"
                if result_spelling == "NevercStatus"
                else "invokePythonVoidCallback"
            )
            call = (
                f"{dispatch}(Argument{user_data_index}, {json.dumps(symbol)}, "
                f"{arguments_expression})"
            )
            if result_spelling == "NevercStatus":
                lines.append(f"  return {call};")
            else:
                lines.append(f"  {call};")
                if field.name == "DestroyUserData":
                    lines.append(
                        f"  pythonCallbackUserDataDestroyed(Argument{user_data_index});"
                    )
            lines.extend(["}", ""])

    lines.extend(
        [
            "static bool configurePythonCallbackRecord(",
            "    PythonCallbackBinding &Binding, const std::string &RecordName,",
            "    std::vector<uint8_t> &Bytes, std::string &Error) {",
        ]
    )
    for record_name, fields in callback_records.items():
        lines.append(f"  if (RecordName == {json.dumps(record_name)}) {{")
        lines.append(f"    if (Bytes.size() != sizeof({record_name})) {{")
        lines.append(
            "      Error = "
            f"{json.dumps('descriptor byte size does not match ' + record_name)};"
        )
        lines.append("      return false;")
        lines.append("    }")
        allowed = ", ".join(json.dumps(field.name) for field in fields)
        lines.append(
            f"    if (!Binding.validateCallbacks({{{allowed}}}, Error))"
        )
        lines.append("      return false;")
        lines.append(
            f"    auto *Descriptor = reinterpret_cast<{record_name} *>(Bytes.data());"
        )
        destroy = next(
            (field for field in fields if field.name == "DestroyUserData"), None
        )
        if destroy is not None:
            lines.append(
                "    Binding.setOriginalUserData(Descriptor->UserData, "
                "Descriptor->DestroyUserData);"
            )
        else:
            lines.append(
                "    Binding.setOriginalUserData(Descriptor->UserData, nullptr);"
            )
        for field in fields:
            if field.name == "DestroyUserData":
                lines.append(
                    "    Descriptor->DestroyUserData = "
                    f"{_trampoline_name(record_name, field.name)};"
                )
                continue
            lines.append(
                f"    if (Descriptor->{field.name} && "
                f"!Binding.hasCallback({json.dumps(field.name)})) {{"
            )
            lines.append(
                "      Error = "
                f"{json.dumps('native callback requires a Python replacement: ' + record_name + '.' + field.name)};"
            )
            lines.append("      return false;")
            lines.append("    }")
            lines.append(
                f"    Descriptor->{field.name} = Binding.hasCallback({json.dumps(field.name)}) "
                f"? {_trampoline_name(record_name, field.name)} : nullptr;"
            )
        lines.append("    Descriptor->UserData = &Binding;")
        lines.append("    return true;")
        lines.append("  }")
    lines.extend(
        [
            "  Error = \"record is not an official callback-bearing NeverC descriptor: \" + RecordName;",
            "  return false;",
            "}",
            "",
        ]
    )
    return "\n".join(lines)


def _atomic_write(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=path.parent, text=True
    )
    temporary = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(content)
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if output is stale")
    parser.add_argument("--clang", default=None, help="clang executable")
    parser.add_argument("--output", type=pathlib.Path, default=OUTPUT)
    parser.add_argument(
        "--trampolines-output", type=pathlib.Path, default=TRAMPOLINE_OUTPUT
    )
    arguments = parser.parse_args()
    try:
        inventory = collect_inventory(arguments.clang)
        generated = render_python(inventory)
        generated_trampolines = render_trampolines(inventory)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"Python ABI generation error: {error}", file=sys.stderr)
        return 1

    if arguments.check:
        stale = []
        for path, content in (
            (arguments.output, generated),
            (arguments.trampolines_output, generated_trampolines),
        ):
            try:
                existing = path.read_text(encoding="utf-8")
            except OSError:
                existing = ""
            if existing != content:
                stale.append(path)
        if stale:
            print(
                f"{', '.join(map(str, stale))} is out of date; "
                "run gen-python-abi.py",
                file=sys.stderr,
            )
            return 1
        return 0
    _atomic_write(arguments.output, generated)
    _atomic_write(arguments.trampolines_output, generated_trampolines)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
