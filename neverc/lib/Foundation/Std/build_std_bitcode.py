#!/usr/bin/env python3
"""Compile all NeverC std library .c files to individual LLVM bitcode and
generate an embedded C header with a lookup table.

Usage:
    python3 build_std_bitcode.py <neverc> <std_src_dir> <std_include_dir> -o <output.h>

Each .c is compiled to .bc in a temporary directory.  The script then
emits a single C header containing:

  1. One ``static const unsigned char kStdBC_<name>[]`` per module.
  2. A ``StdBitcodeEntry`` table for runtime iteration.

No external linker (llvm-link) is needed — the compiler's
StdRuntimeLinkerPass merges the modules at compile time using
llvm::Linker::linkModules.
"""

import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile


C_IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")


def sanitize_name(rel_path: str) -> str:
    """Convert a relative path like 'crypto/sha256/sha256.c' to 'crypto_sha256_sha256'."""
    if rel_path.endswith(".c"):
        rel_path = rel_path[:-2]
    return rel_path.replace(os.sep, "_").replace("/", "_")


def write_header_atomically(output_path: str, contents: str) -> None:
    """Write contents beside output_path, then atomically replace it."""
    output_abs = os.path.abspath(output_path)
    output_dir = os.path.dirname(output_abs)
    os.makedirs(output_dir, exist_ok=True)

    temp_fd = None
    temp_path = None
    try:
        temp_fd, temp_path = tempfile.mkstemp(
            prefix=f".{os.path.basename(output_abs)}.",
            suffix=".tmp",
            dir=output_dir,
            text=True,
        )
        stream = os.fdopen(
            temp_fd, "w", encoding="utf-8", newline="\n")
        temp_fd = None
        with stream:
            stream.write(contents)
            stream.flush()
        os.replace(temp_path, output_abs)
        temp_path = None
    finally:
        if temp_fd is not None:
            try:
                os.close(temp_fd)
            except OSError:
                pass
        if temp_path is not None:
            try:
                os.unlink(temp_path)
            except FileNotFoundError:
                pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("neverc", help="Path to neverc compiler binary")
    parser.add_argument("std_src_dir", help="Path to std/src directory")
    parser.add_argument("std_include_dir", help="Path to std/include directory")
    parser.add_argument("--prefix-map", action="append", default=[],
                        metavar="FROM=TO",
                        help="Base path remapping applied ahead of this "
                             "runtime's own source mappings (repeatable)")
    parser.add_argument("--target", required=True,
                        help="Target triple to compile the modules for")
    parser.add_argument("--sysroot",
                        help="Target SDK root forwarded to the compiler")
    parser.add_argument("--tag", required=True,
                        help="Identifier suffix for the emitted symbols, "
                             "e.g. linux_x64")
    parser.add_argument("-o", "--output", required=True,
                        help="Output .h header file")
    args = parser.parse_args()

    if C_IDENTIFIER.fullmatch(args.tag) is None:
        print(f"error: tag is not a C identifier: {args.tag!r}",
              file=sys.stderr)
        sys.exit(1)

    sources = sorted(glob.glob(
        os.path.join(args.std_src_dir, "**", "*.c"), recursive=True))
    sources = [s for s in sources if not s.endswith(".h")]

    if not sources:
        print(f"error: no .c files found in {args.std_src_dir}",
              file=sys.stderr)
        sys.exit(1)

    source_specs = []  # (source_path, relative_path, sanitized_name)
    names = {}
    for src in sources:
        rel = os.path.relpath(src, args.std_src_dir)
        name = sanitize_name(rel)
        symbol = f"kStdBC_{args.tag}_{name}"
        if not name or C_IDENTIFIER.fullmatch(symbol) is None:
            print(f"error: emitted module symbol for {rel!r} is not a "
                  f"C identifier: {symbol!r}", file=sys.stderr)
            sys.exit(1)
        previous = names.get(name)
        if previous is not None:
            print(f"error: sanitized module name collision for {name!r}: "
                  f"{previous!r} and {rel!r}", file=sys.stderr)
            sys.exit(1)
        names[name] = rel
        source_specs.append((src, rel, name))

    entries = []  # (sanitized_name, bc_bytes)

    src_dir_abs = os.path.abspath(args.std_src_dir)
    inc_dir_abs = os.path.abspath(args.std_include_dir)

    # Clang resolves prefix maps last-match-wins, so the caller's general
    # roots go first and the install-tree names below override them.  Those
    # names are what lets a debugger find std sources under the install
    # prefix, so they must survive.
    path_mapping = [f"-ffile-prefix-map={m}" for m in args.prefix_map] + [
        f"-fdebug-prefix-map={src_dir_abs}=runtime/std/src",
        f"-fdebug-prefix-map={inc_dir_abs}=runtime/std/include",
    ]
    sysroot_args = (["-isysroot", args.sysroot]
                    if args.sysroot is not None else [])

    with tempfile.TemporaryDirectory(prefix="neverc_std_bc_") as tmpdir:
        for src, rel, name in source_specs:
            bc_path = os.path.join(tmpdir, name + ".bc")

            cmd = [
                args.neverc,
                "-c", "-emit-llvm", "-O2",
                "-gline-tables-only",
                "-fno-builtin-std",
                "-fno-lto",
                "-ffreestanding", "-std=gnu11", "-w",
                f"--target={args.target}",
                f"-I{inc_dir_abs}",
                *path_mapping,
                *sysroot_args,
                os.path.abspath(src), "-o", bc_path,
            ]
            print(f"  [bc] {rel}", file=sys.stderr)
            try:
                built = subprocess.run(cmd, capture_output=True, text=True)
            except OSError as exc:
                print(f"error: failed to invoke compiler for {rel}: {exc}",
                      file=sys.stderr)
                sys.exit(1)
            if built.returncode != 0:
                diag = next((l for l in (built.stdout + built.stderr).splitlines()
                             if "error" in l), "compilation failed")
                print(f"error: failed to compile {rel}: {diag.strip()}",
                      file=sys.stderr)
                sys.exit(1)

            if not os.path.isfile(bc_path):
                print(f"error: compiler produced no bitcode for {rel}",
                      file=sys.stderr)
                sys.exit(1)
            try:
                with open(bc_path, "rb") as f:
                    bc_data = f.read()
            except OSError as exc:
                print(f"error: could not read bitcode for {rel}: {exc}",
                      file=sys.stderr)
                sys.exit(1)
            if not bc_data:
                print(f"error: compiler produced empty bitcode for {rel}",
                      file=sys.stderr)
                sys.exit(1)
            entries.append((name, bc_data))

    if len(entries) != len(source_specs):
        print(f"error: expected {len(source_specs)} std modules, built "
              f"{len(entries)}", file=sys.stderr)
        sys.exit(1)

    # Emit the header.  StdBitcodeEntry itself is declared by BuiltinStd.cpp,
    # which includes one of these per target and so may only see it once.
    lines = ["/* Auto-generated by build_std_bitcode.py — DO NOT EDIT. */", ""]

    total_bytes = 0
    for name, data in entries:
        var = f"kStdBC_{args.tag}_{name}"
        lines.append(f"static const unsigned char {var}[] = {{")
        for i in range(0, len(data), 16):
            chunk = data[i:i + 16]
            lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
        lines.append("};")
        lines.append("")
        total_bytes += len(data)

    lines.append(f"static const StdBitcodeEntry kStdBitcodeEntries_{args.tag}[] = {{")
    for name, _ in entries:
        var = f"kStdBC_{args.tag}_{name}"
        lines.append(f'  {{"{name}", {var}, sizeof({var})}},')
    lines.append("};")
    lines.append("")
    lines.append(f"static const unsigned int kStdBitcodeEntryCount_{args.tag} = "
                 f"{len(entries)};")
    lines.append("")

    try:
        write_header_atomically(args.output, "\n".join(lines))
    except Exception as exc:
        print(f"error: could not atomically write {args.output}: {exc}",
              file=sys.stderr)
        sys.exit(1)

    print(f"build_std_bitcode: {len(entries)} modules for {args.tag} "
          f"({args.target}), {total_bytes} bytes -> {args.output}",
          file=sys.stderr)


if __name__ == "__main__":
    main()
