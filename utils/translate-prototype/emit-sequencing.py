#!/usr/bin/env python3
"""Bounded scalar fixture emitter; intentionally not a production translator."""

import argparse
import hashlib
import json
import re
from pathlib import Path


class Emitter:
    def __init__(self, document):
        if (document.get("format"), document.get("version")) != (
            "neverc-cpp-feasibility-only", 1
        ):
            raise ValueError("unsupported prototype data")
        if document["records"]:
            raise ValueError("aggregate emission is outside the sequencing experiment")
        self.document = document
        self.lines = []
        self.depth = 0
        self.counter = 0
        self.names = {}
        for function in document["functions"]:
            name = function["name"] if function["c_linkage"] or function["main"] else (
                "nc_proto_f_" + hashlib.sha256(function["id"].encode()).hexdigest()[:16]
            )
            if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name):
                raise ValueError("unsupported exported identifier")
            self.names[function["id"]] = name

    @staticmethod
    def type(node):
        spelling = node["canonical"]
        if spelling not in ("int", "unsigned int", "bool", "void"):
            raise ValueError(f"unsupported prototype type: {spelling}")
        return spelling

    def line(self, value):
        self.lines.append("  " * self.depth + value)

    def temporary(self, type_name, value=None):
        self.counter += 1
        name = f"nc_proto_t_{self.counter}"
        self.line(f"{type_name} {name}" + (f" = {value}" if value else "") + ";")
        return name

    def lvalue(self, node):
        if node["kind"] == "ParenExpr":
            return self.lvalue(node["operand"])
        if node["kind"] != "DeclRefExpr":
            raise ValueError("sequencing prototype supports only direct scalar storage")
        return self.names[node["declaration"]]

    def expression(self, node):
        kind = node["kind"]
        type_name = self.type(node["type"])
        if kind == "IntegerLiteral":
            return f"(({type_name}){node['value']})"
        if kind == "CXXBoolLiteralExpr":
            return "true" if node["value"] else "false"
        if kind == "DeclRefExpr":
            # Every value read is a snapshot before another operand can mutate it.
            return self.temporary(type_name, self.names[node["declaration"]])
        if kind == "ParenExpr":
            return self.expression(node["operand"])
        if kind in ("ImplicitCastExpr", "CStyleCastExpr", "CXXStaticCastExpr"):
            value = self.expression(node["operand"])
            return self.temporary(type_name, f"({type_name})({value})")
        if kind == "CallExpr":
            # C++17 permits this order; each argument completes before the next.
            arguments = [self.expression(arg) for arg in node["arguments"]]
            call = f"{self.names[node['callee']]}({', '.join(arguments)})"
            if type_name == "void":
                self.line(call + ";")
                return None
            return self.temporary(type_name, call)
        if kind == "UnaryOperator":
            operation = node["operation"]
            if operation in ("++", "--"):
                storage = self.lvalue(node["operand"])
                old = self.temporary(type_name, storage)
                self.line(f"{storage} = {old} {'+' if operation == '++' else '-'} 1;")
                return old if node["postfix"] else self.temporary(type_name, storage)
            if operation not in ("+", "-", "!", "~"):
                raise ValueError(f"unsupported unary operation: {operation}")
            value = self.expression(node["operand"])
            return self.temporary(type_name, f"{operation}({value})")
        if kind == "BinaryOperator":
            operation = node["operation"]
            if operation == "=":
                right = self.expression(node["right"])
                storage = self.lvalue(node["left"])
                self.line(f"{storage} = {right};")
                return self.temporary(type_name, storage)
            left = self.expression(node["left"])
            if operation in ("&&", "||"):
                result = self.temporary(type_name, "false" if operation == "&&" else "true")
                self.line(f"if ({'' if operation == '&&' else '!'}({left})) {{")
                self.depth += 1
                right = self.expression(node["right"])
                self.line(f"{result} = (bool)({right});")
                self.depth -= 1
                self.line("}")
                return result
            right = self.expression(node["right"])
            if operation not in (
                "+", "-", "*", "/", "%", "<<", ">>", "&", "|", "^",
                "==", "!=", "<", "<=", ">", ">=",
            ):
                raise ValueError(f"unsupported binary operation: {operation}")
            return self.temporary(type_name, f"({left}) {operation} ({right})")
        raise ValueError(f"unsupported prototype expression: {kind}")

    def statement(self, node):
        kind = node["kind"]
        if kind == "CompoundStmt":
            for child in node["statements"]:
                self.statement(child)
        elif kind == "DeclStmt":
            for var in node["variables"]:
                value = self.expression(var["initializer"]) if "initializer" in var else None
                self.names[var["id"]] = self.temporary(self.type(var["type"]), value)
        elif kind == "ReturnStmt":
            value = self.expression(node["value"]) if "value" in node else ""
            self.line(f"return {value};")
        else:
            self.expression(node)

    def signature(self, function):
        parameters = []
        for index, parameter in enumerate(function["parameters"]):
            name = f"nc_proto_p_{index}"
            self.names[parameter["id"]] = name
            parameters.append(f"{self.type(parameter['type'])} {name}")
        return (
            f"{self.type(function['result_type'])} {self.names[function['id']]}"
            f"({', '.join(parameters) or 'void'})"
        )

    def emit(self):
        self.line("// Generated sequencing feasibility experiment; not supported translation output.")
        self.line(f"static_assert(sizeof(int) * __CHAR_BIT__ == {self.document['int_width']});")
        self.line(f"static_assert(sizeof(void *) * __CHAR_BIT__ == {self.document['pointer_width']});")
        for function in self.document["functions"]:
            self.line(self.signature(function) + ";")
        for function in self.document["functions"]:
            self.line(self.signature(function) + " {")
            self.depth += 1
            self.statement(function["body"])
            self.depth -= 1
            self.line("}")
        return "\n".join(self.lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    output = Emitter(json.loads(args.input.read_text())).emit()
    # No partial source is written if lowering fails.
    args.output.write_text(output)


if __name__ == "__main__":
    main()
