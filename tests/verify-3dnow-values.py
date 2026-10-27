#!/usr/bin/env python3
"""Compare native 3DNow builtins/IR with raw bytes in an isolated QEMU guest.

Checks all 24 instructions (25 builtin spellings), register/foldable-memory
values, O0/O2, x87 control/status/tags and input canaries. The optional context
objects are NeverD's owned FEMMS probes: isa_probe(U64 *) stores binary64 one
at argument + 8 after MOVQ/FEMMS/FLD1/FSTP. Neither this QEMU differential nor
the emitted-byte checks qualify physical hardware or fault continuations.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

FIXTURES = Path(__file__).resolve().parent / "fixtures/x86/3dnow"
# Opcode bytes and source/destination lane types are independent ISA evidence.
OPERATIONS = (
    ("pavgusb", 0xbf, "bytes", "bytes", False, False),
    ("pf2id", 0x1d, "floats", "ints", True, False),
    ("pfacc", 0xae, "floats", "floats", False, False),
    ("pfadd", 0x9e, "floats", "floats", False, False),
    ("pfcmpeq", 0xb0, "floats", "ints", False, False),
    ("pfcmpge", 0x90, "floats", "ints", False, False),
    ("pfcmpgt", 0xa0, "floats", "ints", False, False),
    ("pfmax", 0xa4, "floats", "floats", False, False),
    ("pfmin", 0x94, "floats", "floats", False, False),
    ("pfmul", 0xb4, "floats", "floats", False, False),
    ("pfrcp", 0x96, "floats", "floats", True, False),
    ("pfrcpit1", 0xa6, "floats", "floats", False, False),
    ("pfrcpit2", 0xb6, "floats", "floats", False, False),
    ("pfrsqit1", 0xa7, "floats", "floats", False, False),
    ("pfrsqrt", 0x97, "floats", "floats", True, False),
    ("pfsub", 0x9a, "floats", "floats", False, False),
    ("pfsubr", 0xaa, "floats", "floats", False, False),
    ("pi2fd", 0x0d, "ints", "floats", True, False),
    ("pmulhrw", 0xb7, "words", "words", False, False),
    ("pf2iw", 0x1c, "floats", "ints", True, True),
    ("pfnacc", 0x8a, "floats", "floats", False, True),
    ("pfpnacc", 0x8e, "floats", "floats", False, True),
    ("pi2fw", 0x0c, "ints", "floats", True, True),
    ("pswapdsi", 0xbb, "ints", "ints", True, True),
    ("pswapdsf", 0xbb, "floats", "floats", True, True),
)


def generate(contexts, mode=64, headers=False):
    source = ['#include <x86intrin.h>\n' if headers else '', """typedef unsigned long long U64;
typedef char V8C __attribute__((vector_size(8)));
typedef short V4S __attribute__((vector_size(8)));
typedef int V2I __attribute__((vector_size(8)));
typedef float V2F __attribute__((vector_size(8)));
typedef union { U64 bits; V8C bytes; V4S words; V2I ints; V2F floats; } Value;
"""]
    ir, declarations, header, table, oracle = [], set(), [], [], [".text\n"]
    for index, (name, opcode, arg, result, unary, enhanced) in enumerate(OPERATIONS):
        native = "llvm.x86." + ("3dnowa." if enhanced else "3dnow.") + (
            "pswapd" if name.startswith("pswapd") else name)
        types = "x86_mmx" if unary else "x86_mmx, x86_mmx"
        declarations.add(f"declare x86_mmx @{native}({types})\n")
        header.append(f"extern U64 reference_{index}(U64, U64);\n")
        oracle.append(f".global reference_{index}\nreference_{index}:\n")
        oracle.append(" movq %rdi, %mm0\n movq %rsi, %mm1\n" if mode == 64 else
                      " movq 4(%esp), %mm0\n movq 12(%esp), %mm1\n")
        oracle.append(f" .byte 0x0f,0x0f,0xc1,{opcode}\n")
        oracle.append(" movq %mm0, %rax\n" if mode == 64 else
                      " movd %mm0, %eax\n psrlq $32, %mm0\n movd %mm0, %edx\n")
        oracle.append(" femms\n ret\n")
        for memory in (False, True):
            fn = name + ("_mem" if memory else "_reg")
            header.append(f"extern U64 {fn}(U64, U64, const U64 *);\n")
            table.append(f"  {{ {fn}, reference_{index} }},\n")
            left = ("*p" if memory else "b") if unary else "a"
            right = "*p" if memory else "b"
            if headers:
                args = "(__m64)left.bits" + ("" if unary else ", (__m64)right.bits")
                public = "pfrsqrtit1" if name == "pfrsqit1" else name
                assignment = f"out.bits = (U64)_m_{public}({args});"
            else:
                args = f"left.{arg}" + ("" if unary else f", right.{arg}")
                assignment = f"out.{result} = __builtin_ia32_{name}({args});"
            cleanup = "_m_femms" if headers else "__builtin_ia32_femms"
            source.append(f'__attribute__((target("3dnowa"), noinline))\n'
                          f"U64 {fn}(U64 a, U64 b, const U64 *p) {{\n"
                          f" Value left = {{{left}}}, right = {{{right}}}, out;\n"
                          f" {assignment}\n"
                          f" U64 bits = out.bits; {cleanup}(); return bits;\n}}\n")
            ir.append(f"define i64 @{fn}(i64 %a, i64 %b, ptr %p) #0 {{\n")
            if memory:
                ir.append("  %loaded = load i64, ptr %p, align 8\n")
            left = ("%loaded" if memory else "%b") if unary else "%a"
            right = "%loaded" if memory else "%b"
            ir.append(f"  %left = bitcast i64 {left} to x86_mmx\n"
                      f"  %right = bitcast i64 {right} to x86_mmx\n")
            args = "x86_mmx %left" + ("" if unary else ", x86_mmx %right")
            ir.append(f"  %value = call x86_mmx @{native}({args})\n"
                      "  %bits = bitcast x86_mmx %value to i64\n"
                      "  call void @llvm.x86.mmx.femms()\n  ret i64 %bits\n}\n")
    ir += sorted(declarations) + ['declare void @llvm.x86.mmx.femms()\n',
                                  'attributes #0 = { noredzone "target-features"="+3dnowa" }\n']
    header += ["static const struct Case cases[] = {\n", *table, "};\n"]
    for index in range(contexts):
        header.append(f"extern void context_{index}(U64 *);\n")
    header.append("static void (*const contexts[])(U64 *) = {" +
                  ",".join(f"context_{i}" for i in range(contexts)) + "};\n")
    oracle.append('.section .note.GNU-stack,"",@progbits\n')
    return "".join(source), "".join(ir), "".join(header), "".join(oracle)


def run(command, directory):
    result = subprocess.run(list(map(str, command)), cwd=directory, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{command}\n{result.stdout}\n{result.stderr}")
    if "not a recognized feature" in result.stderr:
        raise RuntimeError(result.stderr)


def verify(args, directory):
    directory.mkdir(parents=True, exist_ok=True)
    for fixture in FIXTURES.iterdir():
        shutil.copyfile(fixture, directory / fixture.name)
    source, ir, header, oracle = generate(len(args.context_object), args.mode, args.headers)
    for name, text in (("cases.c", source), ("cases.ll", ir),
                       ("cases.inc", header), ("oracle.S", oracle)):
        (directory / name).write_text(text)
    objects = []
    for index, path in enumerate(args.context_object):
        output = f"context-{index}.o"
        run(["objcopy", "--redefine-sym", f"isa_probe=context_{index}", path, output], directory)
        objects.append(output)
    target = "x86_64" if args.mode == 64 else "i386"
    elf = "elf_x86_64" if args.mode == 64 else "elf_i386"
    asm_flags = [] if args.mode == 64 else ["--defsym", "I386=1"]
    run([args.clang, f"--target={target}-unknown-none", "-ffreestanding", "-fno-stack-protector",
         "-mno-red-zone", "-mgeneral-regs-only", "-O2", "-c", "guest.c", "-o", "guest.o"], directory)
    for name in ("entry", "oracle"):
        run(["as", f"--{args.mode}", *asm_flags, name + ".S", "-o", name + ".o"], directory)
    evidence = []
    for opt in ("-O0", "-O2"):
        stem = opt[1:]
        if args.compiler:
            command = [args.compiler, "--target=x86_64-unknown-linux-gnu", "-std=c23",
                       "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc",
                       "-fno-stack-protector", "-mno-red-zone", opt]
            if args.backend_llc:
                suffix = ".bc" if args.bitcode else ".ll"
                output_mode = "-c" if args.bitcode else "-S"
                run([*command, output_mode, "-emit-llvm", "cases.c", "-o", stem + suffix], directory)
                run([args.backend_llc, "-filetype=obj", "-verify-machineinstrs", opt,
                     stem + suffix, "-o", "cases.o"], directory)
            else:
                run([*command, "-c", "cases.c", "-o", "cases.o"], directory)
        else:
            input_ir = "cases.ll"
            if opt == "-O2":
                run([Path(args.llc).with_name("opt"), "-S", "-passes=default<O2>",
                     input_ir, "-o", "optimized.ll"], directory)
                input_ir = "optimized.ll"
            run([args.llc, f"-mtriple={target}-unknown-linux-gnu", "-filetype=obj",
                 "-verify-machineinstrs", opt, input_ir, "-o", "cases.o"], directory)
        shutil.copyfile(directory / "cases.o", directory / (stem + ".o"))
        run(["ld", "-m", elf, "-T", "payload.ld", "entry.o", "oracle.o", "guest.o",
             "cases.o", *objects, "-o", "payload.elf"], directory)
        run(["objcopy", "-O", "binary", "payload.elf", "payload.bin"], directory)
        run(["as", "--32", *asm_flags, "boot.S", "-o", "boot.o"], directory)
        run(["ld", "-m", "elf_i386", "-T", "boot.ld", "boot.o", "-o", "boot.elf"], directory)
        command = [args.qemu, "-machine", "pc,accel=tcg", "-cpu", "max", "-m", "64M",
                   "-nodefaults", "-display", "none", "-no-reboot", "-monitor", "none", "-serial", "none",
                   "-debugcon", "stdio", "-device", "isa-debug-exit,iobase=0xf4,iosize=4", "-kernel", "boot.elf"]
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=30)
        (directory / (stem + ".log")).write_text(result.stdout + result.stderr)
        count = len(OPERATIONS) * 2 * 8 * 4
        expected = f"PASS{count:08x}:{len(objects) * 4:08x}\n"
        if result.returncode != 1 or result.stdout != expected:
            raise RuntimeError(f"3DNow guest {opt}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
        shutil.copyfile(directory / "boot.elf", directory / (stem + "-boot.elf"))
        evidence.append({"optimization": opt, "mode": args.mode, "value_comparisons": count,
                         "owned_context_comparisons": len(objects) * 4})
        print(f"{opt}: {count} native 3DNow and {len(objects) * 4} owned FEMMS comparisons passed", flush=True)
    (directory / "qualification.json").write_text(json.dumps(evidence, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--compiler", type=lambda p: str(Path(p).resolve()))
    mode.add_argument("--llc", type=lambda p: str(Path(p).resolve()))
    parser.add_argument("--backend-llc", type=lambda p: str(Path(p).resolve()))
    parser.add_argument("--bitcode", action="store_true",
                        help="Use NeverC bitcode instead of text IR with --backend-llc")
    parser.add_argument("--headers", action="store_true",
                        help="Use the standard _m_* interfaces through <x86intrin.h>")
    parser.add_argument("--mode", type=int, choices=(32, 64), default=64)
    parser.add_argument("--context-object", action="append", default=[], type=lambda p: str(Path(p).resolve()))
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--keep", type=lambda p: Path(p).resolve())
    args = parser.parse_args()
    if args.backend_llc and not args.compiler:
        parser.error("--backend-llc requires --compiler")
    if args.bitcode and not args.backend_llc:
        parser.error("--bitcode requires --backend-llc")
    if args.headers and not args.compiler:
        parser.error("--headers requires --compiler")
    if args.mode == 32 and (args.compiler or args.context_object):
        parser.error("i386 qualification requires --llc and no x64 context objects")
    if args.keep:
        verify(args, args.keep)
    else:
        with tempfile.TemporaryDirectory(prefix="native-3dnow-") as directory:
            verify(args, Path(directory))


if __name__ == "__main__":
    main()
