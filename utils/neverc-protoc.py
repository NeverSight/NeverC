#!/usr/bin/env python3

"""Generate bounded NeverC C bindings for a scalar Protocol Buffers schema."""

from __future__ import annotations

import argparse
import dataclasses
import re
import sys
from pathlib import Path


TOKEN = re.compile(
    r"(?P<space>\s+)|(?P<line>//[^\n]*)|(?P<block>/\*.*?\*/)|"
    r'(?P<string>"(?:\\.|[^"\\])*")|(?P<number>[0-9]+)|'
    r"(?P<ident>[A-Za-z_][A-Za-z0-9_]*)|(?P<punct>[{};=.,\[\]<>-])",
    re.DOTALL,
)

SCALARS = {
    "uint32": ("uint32_t", "NEVERC_PROTOBUF_TYPE_UINT32"),
    "uint64": ("uint64_t", "NEVERC_PROTOBUF_TYPE_UINT64"),
    "int32": ("int32_t", "NEVERC_PROTOBUF_TYPE_INT32"),
    "int64": ("int64_t", "NEVERC_PROTOBUF_TYPE_INT64"),
    "sint32": ("int32_t", "NEVERC_PROTOBUF_TYPE_SINT32"),
    "sint64": ("int64_t", "NEVERC_PROTOBUF_TYPE_SINT64"),
    "bool": ("int", "NEVERC_PROTOBUF_TYPE_BOOL"),
    "fixed32": ("uint32_t", "NEVERC_PROTOBUF_TYPE_FIXED32"),
    "fixed64": ("uint64_t", "NEVERC_PROTOBUF_TYPE_FIXED64"),
    "sfixed32": ("int32_t", "NEVERC_PROTOBUF_TYPE_SFIXED32"),
    "sfixed64": ("int64_t", "NEVERC_PROTOBUF_TYPE_SFIXED64"),
    "float": ("float", "NEVERC_PROTOBUF_TYPE_FLOAT"),
    "double": ("double", "NEVERC_PROTOBUF_TYPE_DOUBLE"),
    "bytes": ("neverc_protobuf_bytes_t", "NEVERC_PROTOBUF_TYPE_BYTES"),
    "string": ("neverc_protobuf_bytes_t", "NEVERC_PROTOBUF_TYPE_STRING"),
}

C_KEYWORDS = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while", "_Alignas", "_Alignof",
    "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary", "_Noreturn",
    "_Static_assert", "_Thread_local",
}


@dataclasses.dataclass(frozen=True)
class Token:
    value: str
    line: int
    column: int


@dataclasses.dataclass
class Field:
    type_name: str
    name: str
    number: int
    presence: bool


@dataclasses.dataclass
class Message:
    name: str
    fields: list[Field]


@dataclasses.dataclass
class Enum:
    name: str
    values: list[tuple[str, int]]


@dataclasses.dataclass
class Schema:
    syntax: str = ""
    package: str = ""
    messages: list[Message] = dataclasses.field(default_factory=list)
    enums: list[Enum] = dataclasses.field(default_factory=list)


class SchemaError(ValueError):
    pass


def tokenize(source: str) -> list[Token]:
    result: list[Token] = []
    position = 0
    line = 1
    column = 1
    for match in TOKEN.finditer(source):
        if match.start() != position:
            raise SchemaError(f"{line}:{column}: unexpected character")
        text = match.group(0)
        token_line, token_column = line, column
        lines = text.splitlines(keepends=True)
        if len(lines) == 1:
            column += len(text)
        else:
            line += len(lines) - 1
            column = len(lines[-1]) + 1
        position = match.end()
        if match.lastgroup not in {"space", "line", "block"}:
            result.append(Token(text, token_line, token_column))
    if position != len(source):
        raise SchemaError(f"{line}:{column}: unexpected character")
    result.append(Token("<eof>", line, column))
    return result


class Parser:
    def __init__(self, tokens: list[Token]):
        self.tokens = tokens
        self.index = 0
        self.schema = Schema()

    def current(self) -> Token:
        return self.tokens[self.index]

    def accept(self, value: str) -> bool:
        if self.current().value != value:
            return False
        self.index += 1
        return True

    def expect(self, value: str) -> Token:
        token = self.current()
        if token.value != value:
            self.fail(f"expected {value!r}, got {token.value!r}")
        self.index += 1
        return token

    def expect_ident(self) -> str:
        token = self.current()
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", token.value):
            self.fail(f"expected identifier, got {token.value!r}")
        self.index += 1
        return token.value

    def expect_number(self, *, signed: bool = False) -> int:
        negative = signed and self.accept("-")
        token = self.current()
        if not token.value.isdigit():
            self.fail(f"expected integer, got {token.value!r}")
        self.index += 1
        value = int(token.value)
        return -value if negative else value

    def fail(self, message: str) -> None:
        token = self.current()
        raise SchemaError(f"{token.line}:{token.column}: {message}")

    def qualified_name(self) -> str:
        leading = self.accept(".")
        parts = [self.expect_ident()]
        while self.accept("."):
            parts.append(self.expect_ident())
        return ("." if leading else "") + ".".join(parts)

    def skip_statement(self) -> list[str]:
        values: list[str] = []
        square = 0
        while self.current().value != "<eof>":
            value = self.current().value
            self.index += 1
            if value == "[":
                square += 1
            elif value == "]":
                square -= 1
            elif value == ";" and square == 0:
                return values
            values.append(value)
        self.fail("unterminated statement")
        return values

    def parse_enum(self) -> None:
        name = self.expect_ident()
        self.expect("{")
        values: list[tuple[str, int]] = []
        names: set[str] = set()
        while not self.accept("}"):
            keyword = self.current().value
            if keyword in {"option", "reserved"}:
                self.index += 1
                self.skip_statement()
                continue
            value_name = self.expect_ident()
            self.expect("=")
            value = self.expect_number(signed=True)
            if self.accept("["):
                while not self.accept("]"):
                    if self.current().value == "<eof>":
                        self.fail("unterminated enum option")
                    self.index += 1
            self.expect(";")
            if value_name in names:
                self.fail(f"duplicate enum name {value_name!r}")
            names.add(value_name)
            values.append((value_name, value))
        if not values:
            self.fail(f"enum {name!r} has no values")
        self.accept(";")
        self.schema.enums.append(Enum(name, values))

    def parse_message(self) -> None:
        name = self.expect_ident()
        self.expect("{")
        fields: list[Field] = []
        names: set[str] = set()
        numbers: set[int] = set()
        while not self.accept("}"):
            keyword = self.current().value
            if keyword in {"message", "enum", "oneof", "extensions"}:
                self.fail(f"{keyword} is not supported inside a message")
            if keyword in {"reserved", "option"}:
                self.index += 1
                self.skip_statement()
                continue
            if keyword in {"repeated", "map"}:
                self.fail(f"{keyword} fields are not supported")
            presence = False
            if keyword in {"optional", "required"}:
                presence = True
                if keyword == "required" and self.schema.syntax != "proto2":
                    self.fail("required is only valid with proto2")
                self.index += 1
            type_name = self.qualified_name()
            field_name = self.expect_ident()
            self.expect("=")
            number = self.expect_number()
            options: list[str] = []
            if self.accept("["):
                while not self.accept("]"):
                    if self.current().value == "<eof>":
                        self.fail("unterminated field option")
                    options.append(self.current().value)
                    self.index += 1
            self.expect(";")
            if "default" in options:
                self.fail("proto2 default values are not supported")
            if number <= 0 or number > 536870911 or 19000 <= number <= 19999:
                self.fail(f"invalid or reserved field number {number}")
            if field_name in names:
                self.fail(f"duplicate field name {field_name!r}")
            if number in numbers:
                self.fail(f"duplicate field number {number}")
            names.add(field_name)
            numbers.add(number)
            fields.append(Field(type_name, field_name, number, presence))
        self.accept(";")
        self.schema.messages.append(Message(name, fields))

    def parse(self) -> Schema:
        while self.current().value != "<eof>":
            keyword = self.expect_ident()
            if keyword == "syntax":
                self.expect("=")
                value = self.current().value
                if value not in {'"proto2"', '"proto3"'}:
                    self.fail("syntax must be proto2 or proto3")
                self.index += 1
                self.expect(";")
                syntax = value[1:-1]
                if self.schema.syntax and self.schema.syntax != syntax:
                    self.fail("syntax declared more than once")
                self.schema.syntax = syntax
            elif keyword == "package":
                self.schema.package = self.qualified_name()
                self.expect(";")
            elif keyword == "option":
                self.skip_statement()
            elif keyword == "enum":
                self.parse_enum()
            elif keyword == "message":
                self.parse_message()
            elif keyword in {"import", "service", "extend"}:
                self.fail(f"{keyword} declarations are not supported")
            else:
                self.fail(f"unsupported declaration {keyword!r}")
        if not self.schema.syntax:
            self.fail("schema must declare proto2 or proto3 syntax")
        return self.schema


def c_identifier(name: str) -> str:
    value = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if not value or value[0].isdigit():
        value = "_" + value
    if value in C_KEYWORDS:
        value += "_"
    return value


def symbol_prefix(schema: Schema) -> str:
    return c_identifier(schema.package.replace(".", "_") + "_") if schema.package else ""


def resolve_fields(schema: Schema) -> None:
    enum_names = {enum.name for enum in schema.enums}
    for message in schema.messages:
        for field in message.fields:
            short_name = field.type_name.rsplit(".", 1)[-1]
            if field.type_name not in SCALARS and short_name not in enum_names:
                raise SchemaError(
                    f"message {message.name}: field {field.name} uses unsupported "
                    f"message or imported type {field.type_name!r}"
                )


def generate(schema: Schema, source_name: str, output_name: str) -> str:
    resolve_fields(schema)
    prefix = symbol_prefix(schema)
    guard = c_identifier(output_name.upper()) + "_INCLUDED"
    enum_names = {enum.name for enum in schema.enums}
    lines = [
        f"/* Generated from {source_name} by utils/neverc-protoc.py. */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        '#include "neverc/std/encoding/protobuf.h"',
        "",
    ]
    for enum in schema.enums:
        enum_type = prefix + c_identifier(enum.name)
        lines.append(f"typedef enum {enum_type} {{")
        for name, value in enum.values:
            lines.append(f"    {prefix}{c_identifier(name)} = {value},")
        lines.append(f"}} {enum_type};")
        lines.append("")
    for message in schema.messages:
        message_type = prefix + c_identifier(message.name)
        lines.append(f"typedef struct {message_type} {{")
        if not message.fields:
            lines.append("    unsigned char _neverc_empty;")
        for field in message.fields:
            member = c_identifier(field.name)
            if field.presence:
                lines.append(f"    int has_{member};")
            short_name = field.type_name.rsplit(".", 1)[-1]
            c_type = "int32_t" if short_name in enum_names else SCALARS[field.type_name][0]
            lines.append(f"    {c_type} {member};")
        lines.append(f"}} {message_type};")
        lines.append("")
        descriptor_name = message_type + "_fields"
        if message.fields:
            lines.append(
                f"static const neverc_protobuf_field_descriptor_t "
                f"{descriptor_name}[] = {{"
            )
            for field in message.fields:
                member = c_identifier(field.name)
                short_name = field.type_name.rsplit(".", 1)[-1]
                scalar = (
                    "NEVERC_PROTOBUF_TYPE_ENUM"
                    if short_name in enum_names
                    else SCALARS[field.type_name][1]
                )
                presence = (
                    f"offsetof({message_type}, has_{member})"
                    if field.presence else "SIZE_MAX"
                )
                lines.append(
                    f"    {{{field.number}, {scalar}, "
                    f"offsetof({message_type}, {member}), {presence}}},"
                )
            lines.append("};")
        lines.extend([
            f"static const neverc_protobuf_message_descriptor_t "
            f"{message_type}_descriptor = {{",
            f"    sizeof({message_type}),",
            f"    {descriptor_name if message.fields else 'NULL'},",
            f"    {len(message.fields)}",
            "};",
            "",
            f"static inline int {message_type}_encode(",
            f"    const {message_type} *message, void *output,",
            "    size_t output_capacity, size_t *output_length) {",
            "    return neverc_protobuf_message_encode(",
            f"        &{message_type}_descriptor, message, output,",
            "        output_capacity, output_length);",
            "}",
            "",
            f"static inline int {message_type}_decode(",
            "    const void *input, size_t input_length, size_t max_field_size,",
            f"    {message_type} *message) {{",
            "    return neverc_protobuf_message_decode(",
            f"        &{message_type}_descriptor, input, input_length,",
            f"        max_field_size, message, sizeof({message_type}));",
            "}",
            "",
        ])
    lines.extend([f"#endif /* {guard} */", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate NeverC C bindings for a bounded protobuf subset"
    )
    parser.add_argument("schema", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()
    output = args.output or args.schema.with_suffix(".neverc.pb.h")
    try:
        source = args.schema.read_text(encoding="utf-8")
        schema = Parser(tokenize(source)).parse()
        generated = generate(schema, args.schema.name, output.name)
        output.parent.mkdir(parents=True, exist_ok=True)
        temporary = output.with_name(output.name + ".tmp")
        temporary.write_text(generated, encoding="utf-8", newline="\n")
        temporary.replace(output)
    except (OSError, SchemaError) as error:
        print(f"neverc-protoc: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
