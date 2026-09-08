#!/usr/bin/env python3
"""Generate bounded NeverC/C protobuf bindings for a proto3 scalar subset."""

from __future__ import annotations

import argparse
import dataclasses
import os
import re
import sys
import tempfile
from pathlib import Path


SCALARS = {
    "double": ("double", "NEVERC_PROTOBUF_TYPE_DOUBLE"),
    "float": ("float", "NEVERC_PROTOBUF_TYPE_FLOAT"),
    "int32": ("int32_t", "NEVERC_PROTOBUF_TYPE_INT32"),
    "int64": ("int64_t", "NEVERC_PROTOBUF_TYPE_INT64"),
    "uint32": ("uint32_t", "NEVERC_PROTOBUF_TYPE_UINT32"),
    "uint64": ("uint64_t", "NEVERC_PROTOBUF_TYPE_UINT64"),
    "sint32": ("int32_t", "NEVERC_PROTOBUF_TYPE_SINT32"),
    "sint64": ("int64_t", "NEVERC_PROTOBUF_TYPE_SINT64"),
    "fixed32": ("uint32_t", "NEVERC_PROTOBUF_TYPE_FIXED32"),
    "fixed64": ("uint64_t", "NEVERC_PROTOBUF_TYPE_FIXED64"),
    "sfixed32": ("int32_t", "NEVERC_PROTOBUF_TYPE_SFIXED32"),
    "sfixed64": ("int64_t", "NEVERC_PROTOBUF_TYPE_SFIXED64"),
    "bool": ("int", "NEVERC_PROTOBUF_TYPE_BOOL"),
    "string": ("neverc_protobuf_bytes_t", "NEVERC_PROTOBUF_TYPE_STRING"),
    "bytes": ("neverc_protobuf_bytes_t", "NEVERC_PROTOBUF_TYPE_BYTES"),
}

IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# Field spellings are preserved in public headers, which support both C and
# C++. Reject keyword spellings through C23 and C++23 consistently.
C_CPP_KEYWORDS = frozenset(
    """
    _Alignas _Alignof _Atomic _BitInt _Bool _Complex _Decimal32 _Decimal64
    _Decimal128 _Generic _Imaginary _Noreturn _Static_assert _Thread_local
    alignas alignof and and_eq asm auto bitand bitor bool break case catch char
    char8_t char16_t char32_t class compl concept const consteval constexpr
    constinit const_cast continue co_await co_return co_yield decltype default
    delete do double dynamic_cast else enum explicit export extern false float
    for friend goto if inline int long mutable namespace new noexcept not not_eq
    nullptr operator or or_eq private protected public register reinterpret_cast
    requires restrict return short signed sizeof static static_assert static_cast
    struct switch template this thread_local throw true try typedef typeid
    typename typeof typeof_unqual union unsigned using virtual void volatile
    wchar_t while xor xor_eq
    """.split()
)

C_FIELD_TYPES = frozenset(ctype for ctype, _descriptor in SCALARS.values())

# Object-like macros from the included headers: standard 8/16/32/64-bit stdint
# families through C23 and the fixed protobuf.h macros. Function-like macros
# remain valid member names; platform and consumer extensions are not modeled.
C_HEADER_MACROS = frozenset({
    "NULL", "NEVERC_ENCODING_PROTOBUF_H", "NEVERC_PROTOBUF_MAX_FIELD_NUMBER",
    "NEVERC_PROTOBUF_DEFAULT_MAX_FIELD_SIZE",
}) | frozenset(
    f"{prefix}{width}_{suffix}"
    for prefix in ("INT", "UINT", "INT_LEAST", "UINT_LEAST", "INT_FAST", "UINT_FAST")
    for width in (8, 16, 32, 64)
    for suffix in (("MAX", "WIDTH") if prefix.startswith("U")
                   else ("MIN", "MAX", "WIDTH"))
) | frozenset(
    f"{prefix}_{suffix}"
    for prefix in ("INTPTR", "UINTPTR", "INTMAX", "UINTMAX", "PTRDIFF",
                   "SIG_ATOMIC", "SIZE", "WCHAR", "WINT")
    for suffix in (("MAX", "WIDTH") if prefix.startswith("U") or prefix == "SIZE"
                   else ("MIN", "MAX", "WIDTH"))
)


class SchemaError(ValueError):
    pass


def validate_c_member_name(name: str, line: int, header_guard: str) -> None:
    if name in C_CPP_KEYWORDS:
        reason = f"field name {name!r} is a C/C++ keyword"
    elif "__" in name or re.match(r"^_[A-Z]", name):
        reason = (
            f"generated C member {name!r} is reserved to the C/C++ implementation"
        )
    elif name in C_HEADER_MACROS or name == header_guard:
        reason = f"generated C member {name!r} conflicts with a header macro"
    elif name in C_FIELD_TYPES:
        reason = f"generated C member {name!r} shadows a generated field type"
    else:
        return
    raise SchemaError(f"line {line}: {reason}; rename the proto field")


@dataclasses.dataclass(frozen=True)
class Field:
    scalar: str
    name: str
    number: int
    optional: bool


@dataclasses.dataclass(frozen=True)
class Message:
    name: str
    fields: tuple[Field, ...]


@dataclasses.dataclass(frozen=True)
class Schema:
    package: str
    messages: tuple[Message, ...]


@dataclasses.dataclass(frozen=True)
class Token:
    value: str
    line: int


def tokenize(text: str) -> list[Token]:
    tokens: list[Token] = []
    index = 0
    line = 1
    while index < len(text):
        if text[index].isspace():
            if text[index] == "\n":
                line += 1
            index += 1
            continue
        if text.startswith("//", index):
            end = text.find("\n", index + 2)
            index = len(text) if end < 0 else end
            continue
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            if end < 0:
                raise SchemaError(f"line {line}: unterminated block comment")
            line += text.count("\n", index, end + 2)
            index = end + 2
            continue
        character = text[index]
        if character in "{}[]=;." or character == ",":
            tokens.append(Token(character, line))
            index += 1
            continue
        if character in "\"'":
            quote = character
            start_line = line
            end = index + 1
            while end < len(text) and text[end] != quote:
                if text[end] == "\\":
                    end += 2
                    continue
                if text[end] == "\n":
                    line += 1
                end += 1
            if end >= len(text):
                raise SchemaError(f"line {start_line}: unterminated string")
            tokens.append(Token(text[index : end + 1], start_line))
            index = end + 1
            continue
        match = re.match(r"[A-Za-z_][A-Za-z0-9_]*|[0-9]+", text[index:])
        if not match:
            raise SchemaError(
                f"line {line}: unsupported character {text[index]!r}"
            )
        value = match.group(0)
        tokens.append(Token(value, line))
        index += len(value)
    return tokens


class Parser:
    def __init__(self, tokens: list[Token], header_guard: str):
        self.tokens = tokens
        self.position = 0
        self.header_guard = header_guard

    def current(self) -> Token:
        if self.position >= len(self.tokens):
            line = self.tokens[-1].line if self.tokens else 1
            return Token("<eof>", line)
        return self.tokens[self.position]

    def take(self) -> Token:
        token = self.current()
        if token.value != "<eof>":
            self.position += 1
        return token

    def accept(self, value: str) -> bool:
        if self.current().value != value:
            return False
        self.position += 1
        return True

    def expect(self, value: str) -> Token:
        token = self.take()
        if token.value != value:
            raise SchemaError(
                f"line {token.line}: expected {value!r}, got {token.value!r}"
            )
        return token

    def expect_identifier(self) -> Token:
        token = self.take()
        if not IDENTIFIER.match(token.value):
            raise SchemaError(
                f"line {token.line}: expected identifier, got {token.value!r}"
            )
        return token

    def skip_statement(self) -> None:
        depth = 0
        while self.current().value != "<eof>":
            token = self.take()
            if token.value == "[":
                depth += 1
            elif token.value == "]":
                depth -= 1
                if depth < 0:
                    raise SchemaError(f"line {token.line}: unmatched ']' ")
            elif token.value == ";" and depth == 0:
                return
        raise SchemaError("unterminated statement")

    def dotted_name(self) -> str:
        parts = [self.expect_identifier().value]
        while self.accept("."):
            parts.append(self.expect_identifier().value)
        return ".".join(parts)

    def parse(self) -> Schema:
        self.expect("syntax")
        self.expect("=")
        syntax = self.take()
        if syntax.value not in {'"proto3"', "'proto3'"}:
            raise SchemaError(
                f"line {syntax.line}: only syntax = \"proto3\" is supported"
            )
        self.expect(";")
        package = ""
        messages: list[Message] = []
        names: set[str] = set()
        while self.current().value != "<eof>":
            keyword = self.current()
            if self.accept("package"):
                if package:
                    raise SchemaError(f"line {keyword.line}: duplicate package")
                package = self.dotted_name()
                self.expect(";")
            elif self.accept("option"):
                self.skip_statement()
            elif self.accept("message"):
                message = self.parse_message()
                if message.name in names:
                    raise SchemaError(
                        f"line {keyword.line}: duplicate message {message.name!r}"
                    )
                names.add(message.name)
                messages.append(message)
            elif keyword.value in {"import", "enum", "service", "extend"}:
                raise SchemaError(
                    f"line {keyword.line}: {keyword.value} is outside the "
                    "minimal scalar generator subset"
                )
            else:
                raise SchemaError(
                    f"line {keyword.line}: unexpected top-level token "
                    f"{keyword.value!r}"
                )
        if not messages:
            raise SchemaError("schema contains no messages")
        return Schema(package, tuple(messages))

    def parse_message(self) -> Message:
        name = self.expect_identifier()
        self.expect("{")
        fields: list[Field] = []
        field_names: set[str] = set()
        field_numbers: set[int] = set()
        member_names: set[str] = set()
        while not self.accept("}"):
            token = self.current()
            if token.value == "<eof>":
                raise SchemaError(f"line {name.line}: unterminated message")
            if self.accept("option") or self.accept("reserved"):
                self.skip_statement()
                continue
            optional = self.accept("optional")
            if self.accept("repeated"):
                raise SchemaError(
                    f"line {token.line}: repeated fields are not supported"
                )
            scalar = self.take()
            if scalar.value not in SCALARS:
                raise SchemaError(
                    f"line {scalar.line}: field type {scalar.value!r} is not "
                    "a supported scalar"
                )
            field_name = self.expect_identifier()
            self.expect("=")
            number = self.take()
            if not number.value.isdigit():
                raise SchemaError(
                    f"line {number.line}: expected positive field number"
                )
            field_number = int(number.value)
            if (
                field_number <= 0
                or field_number > 536870911
                or 19000 <= field_number <= 19999
            ):
                raise SchemaError(
                    f"line {number.line}: invalid protobuf field number "
                    f"{field_number}"
                )
            if self.accept("["):
                depth = 1
                while depth:
                    option_token = self.take()
                    if option_token.value == "<eof>":
                        raise SchemaError("unterminated field option")
                    if option_token.value == "[":
                        depth += 1
                    elif option_token.value == "]":
                        depth -= 1
            self.expect(";")
            if field_name.value in field_names:
                raise SchemaError(
                    f"line {field_name.line}: duplicate field name "
                    f"{field_name.value!r}"
                )
            if field_number in field_numbers:
                raise SchemaError(
                    f"line {number.line}: duplicate field number {field_number}"
                )
            emitted_names = [field_name.value]
            if optional:
                emitted_names.append(f"has_{field_name.value}")
            for member_name in emitted_names:
                validate_c_member_name(
                    member_name, field_name.line, self.header_guard
                )
                if member_name in member_names:
                    raise SchemaError(
                        f"line {field_name.line}: generated C member "
                        f"{member_name!r} conflicts with another field; "
                        "rename the proto field"
                    )
            member_names.update(emitted_names)
            field_names.add(field_name.value)
            field_numbers.add(field_number)
            fields.append(
                Field(scalar.value, field_name.value, field_number, optional)
            )
        return Message(name.value, tuple(fields))


def c_prefix(package: str, message: str) -> str:
    return "_".join(part for part in package.split(".") + [message] if part)


def include_guard(stem: str) -> str:
    return re.sub(r"[^A-Za-z0-9]", "_", stem).upper() + "_PB_H"


def render_header(schema: Schema, stem: str) -> str:
    guard = include_guard(stem)
    lines = [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <neverc/std/encoding/protobuf.h>",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ]
    for message in schema.messages:
        prefix = c_prefix(schema.package, message.name)
        lines.append("typedef struct {")
        for field in message.fields:
            if field.optional:
                lines.append(f"    int has_{field.name};")
            lines.append(f"    {SCALARS[field.scalar][0]} {field.name};")
        if not message.fields:
            lines.append("    unsigned char _empty;")
        lines.extend(
            [
                f"}} {prefix}_t;",
                "",
                f"int {prefix}_encode(const {prefix}_t *message,",
                "                       void *output, size_t output_capacity,",
                "                       size_t *output_length);",
                f"int {prefix}_decode(const void *input, size_t input_length,",
                "                       size_t max_field_size,",
                f"                       {prefix}_t *message);",
                "",
            ]
        )
    lines.extend(
        [
            "#ifdef __cplusplus",
            "}",
            "#endif",
            "",
            f"#endif /* {guard} */",
            "",
        ]
    )
    return "\n".join(lines)


def render_source(schema: Schema, stem: str) -> str:
    lines = [
        f'#include "{stem}.pb.h"',
        "",
        "#include <stddef.h>",
        "",
    ]
    for message in schema.messages:
        prefix = c_prefix(schema.package, message.name)
        if message.fields:
            lines.append(
                f"static const neverc_protobuf_field_descriptor_t "
                f"{prefix}_fields[] = {{"
            )
            for field in message.fields:
                presence = (
                    f"offsetof({prefix}_t, has_{field.name})"
                    if field.optional
                    else "SIZE_MAX"
                )
                lines.append(
                    f"    {{{field.number}U, {SCALARS[field.scalar][1]}, "
                    f"offsetof({prefix}_t, {field.name}), {presence}}},"
                )
            lines.extend(["};", ""])
            field_pointer = f"{prefix}_fields"
        else:
            field_pointer = "NULL"
        lines.extend(
            [
                f"static const neverc_protobuf_message_descriptor_t "
                f"{prefix}_descriptor = {{",
                f"    sizeof({prefix}_t), {field_pointer}, "
                f"{len(message.fields)}U",
                "};",
                "",
                f"int {prefix}_encode(const {prefix}_t *message,",
                "                       void *output, size_t output_capacity,",
                "                       size_t *output_length) {",
                "    return neverc_protobuf_message_encode(",
                f"        &{prefix}_descriptor, message, output,",
                "        output_capacity, output_length);",
                "}",
                "",
                f"int {prefix}_decode(const void *input, size_t input_length,",
                "                       size_t max_field_size,",
                f"                       {prefix}_t *message) {{",
                "    return neverc_protobuf_message_decode(",
                f"        &{prefix}_descriptor, input, input_length,",
                f"        max_field_size, message, sizeof(*message));",
                "}",
                "",
            ]
        )
    return "\n".join(lines)


def write_atomic(path: Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=path.name + ".", dir=path.parent, text=True
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as file:
            file.write(contents)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="generate NeverC/C bindings for bounded proto3 scalars"
    )
    parser.add_argument("schema", type=Path)
    parser.add_argument("--out-dir", type=Path, default=Path("."))
    arguments = parser.parse_args(argv)
    try:
        text = arguments.schema.read_text(encoding="utf-8")
        stem = arguments.schema.stem
        schema = Parser(tokenize(text), include_guard(stem)).parse()
        write_atomic(arguments.out_dir / f"{stem}.pb.h",
                     render_header(schema, stem))
        write_atomic(arguments.out_dir / f"{stem}.pb.c",
                     render_source(schema, stem))
    except (OSError, UnicodeError, SchemaError) as error:
        print(f"neverc-protoc: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
