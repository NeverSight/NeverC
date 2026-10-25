#!/usr/bin/env python3
"""Check every XABORT byte in native IR and ELF/COFF/Mach-O objects.

The optimized IR must retain the ordinary store before the instruction and
the reload after it. No RTM instruction runs on the host; this does not
qualify transactional rollback, checkpoint recovery or hardware faults.
"""

import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile


TRIPLES = ("x86_64-unknown-linux-gnu", "x86_64-pc-windows-msvc", "x86_64-apple-darwin")


def run(command, directory):
    result = subprocess.run(list(map(str, command)), cwd=directory,
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{command}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def c_source(interface):
    text = "#include <immintrin.h>\n" if interface == "public" else ""
    name = "__builtin_ia32_xabort" if interface == "builtin" else "_xabort"
    for byte in range(256):
        text += (f'__attribute__((target("rtm"))) unsigned abort_{byte}(unsigned *p) '
                 f'{{ *p = 7; {name}({byte}); return *p; }}\n')
    return text


def llvm_source():
    text = 'declare void @llvm.x86.xabort(i8 immarg)\n'
    for byte in range(256):
        text += (f'define i32 @abort_{byte}(ptr %p) #0 {{\n'
                 '  store i32 7, ptr %p\n'
                 f'  call void @llvm.x86.xabort(i8 {byte})\n'
                 '  %value = load i32, ptr %p\n'
                 '  ret i32 %value\n}\n')
    return text + 'attributes #0 = { "target-features"="+rtm" }\n'


def verify_ir(text, optimized):
    assert "asm " not in text, "unexpected inline assembly"
    functions = re.findall(r'define\b[^\n]*@abort_(\d+)\([^\n]*\n(.*?)^}', text, re.M | re.S)
    assert {int(code) for code, _ in functions} == set(range(256)), "missing abort function"
    assert len(functions) == 256, "duplicated abort function"
    for code, body in functions:
        calls = re.findall(r'call void @llvm\.x86\.xabort\(i8(?: [a-z]+)* (-?\d+)\)', body)
        assert len(calls) == 1 and (int(calls[0]) & 255) == int(code), (code, calls)
        if optimized:
            store = body.index("store i32 7,")
            abort = body.index("call void @llvm.x86.xabort(")
            load = body.index("load i32", abort)
            assert store < abort < load, "lost transaction memory ordering"


def verify_object(text):
    current, encoded = None, {}
    for line in text.splitlines():
        label = re.fullmatch(r'[0-9a-f]+ <_?abort_(\d+)>:', line.strip())
        if label:
            current = int(label[1])
            continue
        if re.fullmatch(r'[0-9a-f]+ <.*>:', line.strip()):
            current = None
        if "\txabort" in line:
            instruction = re.search(r':\s+c6 f8 ([a-f0-9]{2})\s', line)
            assert current is not None and instruction and current not in encoded, line
            encoded[current] = int(instruction[1], 16)
        assert not re.search(r'\tcall[qwl]?\s', line), "unexpected helper: " + line
    assert encoded == {byte: byte for byte in range(256)}, "lost or changed abort byte"


def verify(args, directory):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "cases.ll").write_text(llvm_source())
    records = []
    backend = args.llc or args.backend_llc
    for triple in TRIPLES:
        if args.mode == 32:
            triple = triple.replace("x86_64", "i386")
        interfaces = ("public", "builtin", "microsoft") if args.compiler else ("llvm",)
        for interface in interfaces:
            if interface == "microsoft" and "windows" not in triple:
                continue
            source = interface + ".c"
            if args.compiler:
                (directory / source).write_text(c_source(interface))
            for optimization in ("0", "2"):
                stem = f'{triple}-{interface}-O{optimization}'
                ir, obj = stem + ".ll", stem + ".o"
                if args.compiler:
                    command = [args.compiler, "--target=" + triple, "-O" + optimization,
                               "-fno-lto", "-fno-builtin-mimalloc", "-ffreestanding",
                               "-Werror=implicit-function-declaration",
                               "-mllvm", "-verify-machineinstrs", source]
                    run([*command, "-S", "-emit-llvm", "-o", ir], directory)
                else:
                    run([backend.with_name("opt"), "-S", "-mtriple=" + triple,
                         "-passes=default<O" + optimization + ">", "cases.ll", "-o", ir], directory)
                verify_ir((directory / ir).read_text(), optimization == "2")
                if backend:
                    run([backend, "-mtriple=" + triple, "-O" + optimization,
                         "-verify-machineinstrs", "-filetype=obj", ir, "-o", obj], directory)
                else:
                    run([*command, "-c", "-o", obj], directory)
                disassembly = run([args.objdump, "-d", obj], directory)
                (directory / (stem + ".dis")).write_text(disassembly)
                verify_object(disassembly)
                record = {"target": triple, "interface": interface,
                          "optimization": "O" + optimization, "native_calls": 256,
                          "exact_bytes": 256, "optimized_memory_checked": optimization == "2"}
                records.append(record)
                (directory / "results.json").write_text(json.dumps(records, indent=2) + "\n")
                print(json.dumps(record), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--compiler", type=lambda value: Path(value).resolve())
    modes.add_argument("--llc", type=lambda value: Path(value).resolve())
    parser.add_argument("--backend-llc", type=lambda value: Path(value).resolve())
    parser.add_argument("--objdump", required=True, type=lambda value: Path(value).resolve())
    parser.add_argument("--mode", choices=(32, 64), default=64, type=int)
    parser.add_argument("--keep", type=lambda value: Path(value).resolve())
    args = parser.parse_args()
    if args.backend_llc and not args.compiler:
        parser.error("--backend-llc requires --compiler")
    if args.mode == 32 and not args.llc:
        parser.error("NeverC supports x86-64 code; --mode=32 requires --llc")
    if args.keep:
        verify(args, args.keep)
    else:
        with tempfile.TemporaryDirectory(prefix="transaction-encoding-") as temporary:
            verify(args, Path(temporary))


if __name__ == "__main__":
    main()
