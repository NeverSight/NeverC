#!/usr/bin/env python3
"""Compile the NVK kernel runtime to a single LLVM bitcode module and
embed it as a C header.

Usage:
    python3 build_nvk_kernel_bitcode.py <neverc> <runtime_dir> -o <output.h>

Creates a unity source that #include's every .c file under runtime_dir/src/,
compiles it with neverc into one aarch64 bitcode module, and emits a header
with the bitcode embedded as ``static const unsigned char kNvkKernelBC[]``.

The NvkKernelRuntimeLinkerPass then links this single pre-merged module into
each user TU at compile time — no runtime merge step needed.
"""

import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile


FUNCTION_DEFINITION_RE = re.compile(
    r'^\s*define\b.*?@("?[A-Za-z_.$-][A-Za-z0-9_.$-]*"?)\s*\('
)
FUNCTION_DECLARATION_RE = re.compile(
    r'^\s*declare\b.*?@("?[A-Za-z_.$-][A-Za-z0-9_.$-]*"?)\s*\('
)
GLOBAL_DEFINITION_RE = re.compile(
    r'^\s*@("?[A-Za-z_.$-][A-Za-z0-9_.$-]*"?)\s*=\s*'
    r'(?!external\b|extern_weak\b)'
)
GLOBAL_DECLARATION_RE = re.compile(
    r'^\s*@("?[A-Za-z_.$-][A-Za-z0-9_.$-]*"?)\s*=\s*'
    r'(?:external|extern_weak)\b'
)
ALIAS_DEFINITION_RE = re.compile(
    r'^\s*@("?[A-Za-z_.$-][A-Za-z0-9_.$-]*"?)\s*=.*\balias\b'
)
LOCAL_DEFINITION_RE = re.compile(
    r'^\s*(?:define\b[^@]*\b(?:internal|private)\b|'
    r'@"?[A-Za-z_.$-][A-Za-z0-9_.$-]*"?\s*=\s*(?:internal|private)\b)'
)
ATTRIBUTE_GROUP_RE = re.compile(
    r'^\s*attributes\s+#(\d+)\s*=\s*\{(.*)\}\s*$'
)
FUNCTION_ATTRIBUTE_RE = re.compile(r'\s#(\d+)\s*\{\s*$')
REQUIRED_RUNTIME_SYMBOLS = frozenset({
    "neverc_krt_bootstrap",
    "neverc_krt_mem_init",
})
OPTIMIZABLE_RUNTIME_SYMBOLS = frozenset({
    "_neverc_krt_decode_page_shift",
    "neverc_krt_bootstrap",
    "neverc_krt_page_shift",
    "neverc_krt_va_bits",
})


def extract_runtime_symbols(ir_path):
    symbols = set()
    aliases = set()
    with open(ir_path, encoding="utf-8") as ir_file:
        for line in ir_file:
            match = FUNCTION_DEFINITION_RE.match(line)
            if match is None:
                match = GLOBAL_DEFINITION_RE.match(line)
            if match is None:
                continue
            name = match.group(1).strip('"')
            if ALIAS_DEFINITION_RE.match(line):
                aliases.add(name)
            if LOCAL_DEFINITION_RE.match(line):
                continue
            symbols.add(name)
    return sorted(symbols), sorted(aliases)


def validate_runtime_ir(ir_path):
    attribute_groups = {}
    function_groups = {}
    has_optnone = False
    unresolved_runtime_symbols = set()

    with open(ir_path, encoding="utf-8") as ir_file:
        for line in ir_file:
            if re.search(r"\boptnone\b", line):
                has_optnone = True

            declaration_match = FUNCTION_DECLARATION_RE.match(line)
            if declaration_match is None:
                declaration_match = GLOBAL_DECLARATION_RE.match(line)
            if declaration_match is not None:
                name = declaration_match.group(1).strip('"')
                if name.startswith(("neverc_krt_", "_neverc_krt_")):
                    unresolved_runtime_symbols.add(name)

            attribute_match = ATTRIBUTE_GROUP_RE.match(line)
            if attribute_match:
                attribute_groups[attribute_match.group(1)] = (
                    attribute_match.group(2)
                )
                continue

            function_match = FUNCTION_DEFINITION_RE.match(line)
            if not function_match:
                continue
            group_match = FUNCTION_ATTRIBUTE_RE.search(line)
            if group_match:
                function_groups[
                    function_match.group(1).strip('"')
                ] = group_match.group(1)

    errors = []
    if has_optnone:
        errors.append("embedded runtime IR contains optnone")
    if unresolved_runtime_symbols:
        errors.append(
            "embedded runtime IR has undefined runtime symbols: "
            + ", ".join(sorted(unresolved_runtime_symbols))
        )

    for symbol in sorted(OPTIMIZABLE_RUNTIME_SYMBOLS):
        group = function_groups.get(symbol)
        if group is None:
            errors.append(f"{symbol} has no inspectable attribute group")
            continue
        attributes = attribute_groups.get(group, "")
        if re.search(r"\b(?:noinline|optnone)\b", attributes):
            errors.append(f"{symbol} is not available to downstream inlining")

    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("neverc", help="Path to neverc compiler binary")
    parser.add_argument("runtime_dir",
                        help="Path to runtime/android/kernel directory")
    parser.add_argument("-o", "--output", required=True,
                        help="Output .h header file")
    args = parser.parse_args()

    src_dir = os.path.join(args.runtime_dir, "src")
    inc_kern = os.path.join(args.runtime_dir, "arm64", "include")
    inc_nvk = os.path.join(args.runtime_dir, "include")
    boundary_checker = os.path.join(
        args.runtime_dir, "tools", "check-source-boundaries.py"
    )

    sources = sorted(glob.glob(os.path.join(src_dir, "*.c")))

    if not sources:
        print(f"error: no .c files found in {src_dir}", file=sys.stderr)
        sys.exit(1)

    for d in (inc_kern, inc_nvk):
        if not os.path.isdir(d):
            print(f"error: include dir not found: {d}", file=sys.stderr)
            sys.exit(1)
    if not os.path.isfile(boundary_checker):
        print(f"error: boundary checker not found: {boundary_checker}",
              file=sys.stderr)
        sys.exit(1)

    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)

    print("  [check] source boundaries", file=sys.stderr)
    try:
        subprocess.check_call([sys.executable, boundary_checker])
    except subprocess.CalledProcessError as e:
        print(f"error: source-boundary check failed (exit {e.returncode})",
              file=sys.stderr)
        sys.exit(1)

    src_dir_abs = os.path.abspath(src_dir)
    inc_kern_abs = os.path.abspath(inc_kern)
    inc_nvk_abs = os.path.abspath(inc_nvk)
    common_args = [
        "-fno-lto",
        "--target=aarch64-linux-android",
        "-mbranch-protection=bti+pac-ret",
        "-ffreestanding", "-std=gnu11",
        "-D__KERNEL__", "-DMODULE",
        f"-I{inc_kern_abs}",
        f"-I{inc_nvk_abs}",
        f"-fdebug-prefix-map={src_dir_abs}=runtime/android/kernel/src",
        f"-fdebug-prefix-map={inc_kern_abs}=runtime/android/kernel/arm64/include",
        f"-fdebug-prefix-map={inc_nvk_abs}=runtime/android/kernel/include",
        "-Wall", "-Wextra", "-Werror",
    ]

    syntax_cmd = [
        args.neverc,
        "-fsyntax-only",
        "-O0",
        *common_args,
        *(os.path.abspath(source) for source in sources),
    ]
    print(f"  [check] independent ({len(sources)} sources)", file=sys.stderr)
    try:
        subprocess.check_call(syntax_cmd)
    except subprocess.CalledProcessError as e:
        print(f"error: independent source check failed (exit {e.returncode})",
              file=sys.stderr)
        sys.exit(1)

    with tempfile.TemporaryDirectory(prefix="neverc_nvk_bc_") as tmpdir:
        unity_src = os.path.join(tmpdir, "nvk_kernel_unity.c")
        with open(unity_src, "w") as f:
            for src in sources:
                f.write(f'#include "{os.path.abspath(src)}"\n')

        bc_path = os.path.join(tmpdir, "nvk_kernel_runtime.bc")
        ir_path = os.path.join(tmpdir, "nvk_kernel_runtime.ll")

        cmd = [
            args.neverc,
            "-c", "-emit-llvm", "-O0",
            # Preserve the unoptimized call/data graph for runtime pruning,
            # but leave it available to the consumer TU/LTO optimizer.
            "-disable-O0-optnone", "-finline-functions",
            *common_args,
            f"-fdebug-prefix-map={tmpdir}=runtime/android/kernel",
            unity_src, "-o", bc_path,
        ]
        print(f"  [bc] unity ({len(sources)} sources)", file=sys.stderr)
        try:
            subprocess.check_call(cmd)
        except subprocess.CalledProcessError as e:
            print(f"error: unity compilation failed (exit {e.returncode})",
                  file=sys.stderr)
            sys.exit(1)

        with open(bc_path, "rb") as f:
            bc_data = f.read()

        ir_cmd = cmd.copy()
        ir_cmd[ir_cmd.index("-c")] = "-S"
        ir_cmd[-1] = ir_path
        try:
            subprocess.check_call(ir_cmd)
        except subprocess.CalledProcessError as e:
            print(f"error: textual IR compilation failed (exit {e.returncode})",
                  file=sys.stderr)
            sys.exit(1)
        ir_errors = validate_runtime_ir(ir_path)
        if ir_errors:
            print("error: invalid embedded runtime optimization attributes: "
                  + "; ".join(ir_errors), file=sys.stderr)
            sys.exit(1)
        runtime_symbols, aliases = extract_runtime_symbols(ir_path)
        if aliases:
            print("error: runtime aliases are unsupported: "
                  + ", ".join(aliases), file=sys.stderr)
            sys.exit(1)
        missing_symbols = REQUIRED_RUNTIME_SYMBOLS.difference(runtime_symbols)
        if missing_symbols:
            print("error: runtime definition index is incomplete: "
                  + ", ".join(sorted(missing_symbols))
                  + " missing", file=sys.stderr)
            sys.exit(1)

    lines = []
    lines.append("/* Auto-generated by build_nvk_kernel_bitcode.py — DO NOT EDIT. */")
    lines.append("")
    lines.append("static const unsigned char kNvkKernelBC[] = {")
    for i in range(0, len(bc_data), 16):
        chunk = bc_data[i:i + 16]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"  {hex_vals},")
    lines.append("};")
    lines.append("")
    lines.append(f"static const unsigned int kNvkKernelBCLen = sizeof(kNvkKernelBC);")
    lines.append("")
    lines.append("static const char *const kNvkKernelSymbols[] = {")
    for symbol in runtime_symbols:
        lines.append(f'  "{symbol}",')
    lines.append("};")
    lines.append("")
    lines.append("static const unsigned int kNvkKernelSymbolCount =")
    lines.append("    sizeof(kNvkKernelSymbols) / sizeof(kNvkKernelSymbols[0]);")
    lines.append("")

    with open(args.output, "w") as f:
        f.write("\n".join(lines))

    print(f"build_nvk_kernel_bitcode: {len(sources)} sources, "
          f"{len(runtime_symbols)} symbols -> "
          f"{len(bc_data)} bytes -> {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
