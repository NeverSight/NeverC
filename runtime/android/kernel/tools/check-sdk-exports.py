#!/usr/bin/env python3
"""Verify that SDK extern declarations resolve in an official GKI KMI."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

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


TOP_LEVEL_DECLARATION = re.compile(r"^(?:\|-|`-)(FunctionDecl|VarDecl)\b")
DECLARATION_NAME = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s+'")


def ast_text_declarations(lines):
    """Extract external top-level declarations from a streamed Clang AST.

    JSON AST dumps for the aggregate SDK translation unit can exceed runner
    memory by many gigabytes.  Clang's text dump preserves the facts needed
    here and can be reduced one line at a time.
    """
    declarations = set()
    current = None

    def finish():
        if current is None:
            return
        if current["kind"] == "FunctionDecl":
            if not current["static"] and not current["defined"]:
                declarations.add(current["name"])
        elif current["extern"]:
            declarations.add(current["name"])

    for raw_line in lines:
        line = raw_line.rstrip("\n")
        match = TOP_LEVEL_DECLARATION.match(line)
        if match is not None:
            finish()
            name_match = DECLARATION_NAME.search(line)
            if name_match is None:
                current = None
                continue
            storage = line[name_match.end() :].rsplit("'", 1)[-1]
            current = {
                "kind": match.group(1),
                "name": name_match.group(1),
                "static": bool(re.search(r"\bstatic\b", storage)),
                "extern": bool(re.search(r"\bextern\b", storage)),
                "defined": False,
            }
        elif current is not None and "CompoundStmt" in line:
            current["defined"] = True
    finish()
    return declarations


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
        "-D__KERNEL__=1",
        "-DMODULE=1",
        "-Wno-everything",
        "-Xclang",
        "-ast-dump",
        "-fsyntax-only",
        "-x",
        "c",
        "-",
    ]
    with tempfile.TemporaryFile(mode="w+t", encoding="utf-8") as errors:
        process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=errors,
            text=True,
        )
        if process.stdin is None or process.stdout is None:
            process.kill()
            raise RuntimeError("failed to open Clang AST pipes")
        try:
            process.stdin.write(aggregate_source())
            process.stdin.close()
            declarations = ast_text_declarations(process.stdout)
            process.stdout.close()
            returncode = process.wait()
        except BaseException:
            process.kill()
            process.wait()
            raise
        errors.seek(0)
        diagnostics = errors.read()
    if returncode:
        sys.stderr.write(diagnostics)
        raise RuntimeError("failed to parse SDK headers")

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
                    exports[symbol.name[len(prefix) :]] = ""
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
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
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
