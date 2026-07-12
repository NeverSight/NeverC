#!/usr/bin/env python3
"""Verify that SDK extern declarations resolve in an official GKI KMI."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

from elftools.elf.elffile import ELFFile


RUNTIME_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = RUNTIME_ROOT.parents[2]
ARM64_INCLUDE = RUNTIME_ROOT / "arm64" / "include"
PUBLIC_INCLUDE = RUNTIME_ROOT / "include"
NEVERC_HEADERS = REPO_ROOT / "neverc" / "lib" / "Headers"
LOCAL_DEFINITIONS = {"__this_module"}
LOCAL_PREFIXES = ("__atomic_", "__builtin_", "neverc_krt_")


def aggregate_source():
    headers = sorted(
        path.relative_to(ARM64_INCLUDE).as_posix()
        for path in ARM64_INCLUDE.rglob("*.h")
    )
    return "".join(f"#include <{header}>\n" for header in headers)


def find_builtin_include(compiler):
    result = subprocess.run(
        [compiler, "-print-resource-dir"],
        check=True,
        capture_output=True,
        text=True,
    )
    return Path(result.stdout.strip()) / "include"


def ast_declarations(node, declarations):
    kind = node.get("kind")
    name = node.get("name")
    storage = node.get("storageClass")
    children = node.get("inner", ())

    if kind == "FunctionDecl" and name and storage != "static":
        if not any(child.get("kind") == "CompoundStmt" for child in children):
            declarations.add(name)
    elif kind == "VarDecl" and name and storage == "extern":
        declarations.add(name)

    for child in children:
        ast_declarations(child, declarations)


def resolve_ast_compiler(compiler):
    """Return a compiler that supports Clang's JSON AST dump."""
    name = Path(compiler).name
    if "neverc" in name:
        clang = shutil.which("clang")
        if clang is None:
            raise RuntimeError(
                f"{compiler} does not support -Xclang -ast-dump=json; "
                "install clang or pass --compiler clang"
            )
        return clang
    return compiler


def sdk_declarations(compiler, kernel):
    compiler = resolve_ast_compiler(compiler)
    builtin_include = find_builtin_include(compiler)
    command = [
        compiler,
        "--target=aarch64-linux-android",
        "-std=gnu2x",
        "-nostdinc",
        f"-I{builtin_include}",
        f"-I{ARM64_INCLUDE}",
        f"-I{PUBLIC_INCLUDE}",
        f"-I{NEVERC_HEADERS}",
        f"-DNVK_KERNEL={kernel}",
        "-Wno-everything",
        "-Xclang",
        "-ast-dump=json",
        "-fsyntax-only",
        "-x",
        "c",
        "-",
    ]
    result = subprocess.run(
        command,
        input=aggregate_source(),
        capture_output=True,
        text=True,
    )
    if result.returncode:
        sys.stderr.write(result.stderr)
        raise RuntimeError("failed to parse SDK headers")

    declarations = set()
    ast_declarations(json.loads(result.stdout), declarations)
    return {
        name
        for name in declarations - LOCAL_DEFINITIONS
        if not name.startswith(LOCAL_PREFIXES)
    }


def exported_symbols(path):
    exports = {}
    with path.open("rb") as stream:
        if stream.read(4) == b"\x7fELF":
            stream.seek(0)
            symbols = ELFFile(stream).get_section_by_name(".symtab")
            if symbols is None:
                raise RuntimeError(f"{path}: ELF has no symbol table")
            for symbol in symbols.iter_symbols():
                prefix = "__ksymtab_"
                if symbol.name.startswith(prefix):
                    exports[symbol.name[len(prefix):]] = ""
            return exports

    with path.open(encoding="utf-8", errors="replace") as stream:
        for line in stream:
            fields = line.split()
            if len(fields) >= 2:
                exports[fields[1]] = fields[4] if len(fields) >= 5 else ""
    return exports


def main():
    parser = argparse.ArgumentParser(
        description="compare SDK extern declarations with a GKI Module.symvers"
    )
    parser.add_argument("--compiler", default="clang")
    parser.add_argument("--kernel", required=True, type=int)
    parser.add_argument("symvers", type=Path)
    args = parser.parse_args()

    try:
        declarations = sdk_declarations(args.compiler, args.kernel)
        exports = exported_symbols(args.symvers)
    except (OSError, RuntimeError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"check-sdk-exports: {error}", file=sys.stderr)
        return 2

    missing = sorted(declarations - set(exports))
    namespaced = sorted(
        (name, exports[name])
        for name in declarations.intersection(exports)
        if exports[name]
    )
    for name in missing:
        print(name)
    for name, namespace in namespaced:
        print(f"{name} [namespace: {namespace}]")
    if missing or namespaced:
        print(
            f"check-sdk-exports: {len(missing)} unresolved and "
            f"{len(namespaced)} namespaced declaration(s) for GKI "
            f"{args.kernel}",
            file=sys.stderr,
        )
        return 1

    print(
        f"check-sdk-exports: {len(declarations)} declarations resolve "
        f"for GKI {args.kernel}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
