#!/usr/bin/env python3
"""Execute native MPX with enabled/disabled state in an isolated QEMU TCG guest.

The guest owns CR4/XCR0, supervisor BNDCFG, bounds tables and exception gates.
This qualifies generated instructions against QEMU's processor model, not real
MPX hardware. The test needs GNU as/ld/objcopy and Clang for the guest oracle.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


FIXTURES = Path(__file__).resolve().parent / "fixtures/mpx"
OPERATIONS = (
    ("bndmk", "MAKE"), ("bndmov_reg", "MOVE"), ("bndmov_load", "LOAD"),
    ("bndmov_store", "STORE"), ("bndmov_store_lock", "STORE_LOCK"),
    ("bndcl_reg", "CHECK_LOWER_REG"), ("bndcu_reg", "CHECK_UPPER_REG"),
    ("bndcn_reg", "CHECK_RAW_REG"), ("bndcl_address", "CHECK_LOWER_ADDRESS"),
    ("bndcu_address", "CHECK_UPPER_ADDRESS"), ("bndcn_address", "CHECK_RAW_ADDRESS"),
    ("bndldx", "LOAD_TABLE"), ("bndstx", "STORE_TABLE"),
)
REGISTER = {"bndmov_reg", "bndcl_reg", "bndcu_reg", "bndcn_reg"}
PAIR = {"bndmk", "bndldx", "bndstx"}
PREFIXES = (0, 0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65)


def generate(mode=64):
    declarations = {}
    ir, source, header, cases = [], ["typedef unsigned long long U64;\n"], [], []
    header.append("enum { " + ", ".join(enum for _, enum in OPERATIONS) + " };\n")

    def call(name, args):
        types = [kind for kind, _ in args]
        native = "llvm.x86." + name.replace("_", ".")
        if mode == 32 and name not in ("bndmov_reg", "bnd_branch"):
            native += "32"
        declaration = "declare void @" + native + "(" + ", ".join(types) + ")"
        if native in declarations:
            assert declarations[native] == declaration
        declarations[native] = declaration
        ir_args = ", ".join(kind + " " + ("%" + value if isinstance(value, str) else str(value)) for kind, value in args)
        c_args = ", ".join(map(str, (value for _, value in args)))
        return f"  call void @{native}({ir_args})\n", f"  __builtin_ia32_{name}({c_args});\n"

    def add_call(name, args):
        a, b = call(name, args)
        ir.append(a); source.append(b)

    def begin(name):
        header.append(f"extern void {name}(U64, U64, U64, U64);\n")
        ir.append(f"define void @{name}(i64 %a, i64 %b, i64 %c, i64 %out) noredzone {{\n")
        source.append(f"void {name}(U64 a, U64 b, U64 c, U64 out) {{\n")

    def end():
        ir.append("  ret void\n}\n"); source.append("}\n")

    for operation, enum in OPERATIONS:
        for bound in range(4):
            for bits in ((mode,) if operation in REGISTER or mode == 32 else (32, 64)):
                for prefix in ((0,) if operation in REGISTER else PREFIXES):
                    name = f"case_{len(cases)}"
                    begin(name)
                    if operation not in ("bndmk", "bndmov_load", "bndldx"):
                        initial = (bound + 1) % 4 if operation == "bndmov_reg" else bound
                        add_call("bndmov_load", [("i32", initial), ("i64", "c"), ("i32", mode), ("i32", 0)])
                    args = [("i32", bound)]
                    if operation == "bndmov_reg":
                        args += [("i32", (bound + 1) % 4)]
                    else:
                        args += [("i64", "a")]
                        if operation in PAIR:
                            args += [("i64", "b")]
                        if operation not in REGISTER:
                            args += [("i32", bits), ("i32", prefix)]
                    add_call(operation, args)
                    add_call("bndmov_store", [("i32", bound), ("i64", "out"), ("i32", mode), ("i32", 0)])
                    end()
                    cases.append(f"  {{ {name}, {enum}, {bound}, {bits}, {prefix} }},\n")
    begin("branch_probe")
    add_call("bndmov_load", [("i32", 0), ("i64", "c"), ("i32", mode), ("i32", 0)])
    add_call("bndmov_load", [("i32", 2), ("i64", "a"), ("i32", mode), ("i32", 0)])
    ir.append("  %pointer = inttoptr i64 %b to ptr\n  %v = load volatile i64, ptr %pointer\n  %test = icmp ne i64 %v, 0\n  br i1 %test, label %yes, label %no\nyes:\n")
    source.append("  if (*(volatile U64 *)b) {\n")
    add_call("bndmov_reg", [("i32", 1), ("i32", 0)])
    ir.append("  br label %done\nno:\n"); source.append("  } else {\n")
    add_call("bndmov_reg", [("i32", 1), ("i32", 2)])
    ir.append("  br label %done\ndone:\n"); source.append("  }\n")
    add_call("bndmov_store", [("i32", 1), ("i64", "out"), ("i32", mode), ("i32", 0)])
    end()
    begin("repeated_probe")
    add_call("bndmov_load", [("i32", 3), ("i64", "c"), ("i32", mode), ("i32", 0)])
    for _ in range(2):
        add_call("bndcl_address", [("i32", 3), ("i64", "a"), ("i32", mode), ("i32", 0)])
    end()
    for kind, name in enumerate(("initialize_probe", "preserve_probe")):
        begin(name)
        add_call("bnd_branch", [("i32", kind)])
        end()
    header += ["static const struct Case cases[] = {\n", *cases, "};\n"]
    llvm_ir = "\n".join(declarations.values()) + "\n" + "".join(ir)
    if mode == 32:
        llvm_ir = llvm_ir.replace("i64", "i32")
    return (llvm_ir,
            "".join(source), "".join(header), len(cases))


def run(args, directory, **kwargs):
    result = subprocess.run(list(map(str, args)), cwd=directory, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)
    if result.returncode:
        raise RuntimeError(f"command failed: {args}\n{result.stdout}\n{result.stderr}")
    return result


def verify(args, directory):
    ir, source, header, count = generate()
    directory.mkdir(parents=True, exist_ok=True)
    for file in FIXTURES.iterdir():
        shutil.copyfile(file, directory / file.name)
    (directory / "cases.ll").write_text(ir)
    (directory / "cases.c").write_text(source)
    (directory / "cases.inc").write_text(header)
    model_flags = ["-DQEMU_MPX_LIMITATIONS"] if args.qemu_mpx_limitations else []
    run([args.clang, *model_flags, "--target=x86_64-unknown-none", "-std=c23", "-ffreestanding",
         "-fno-stack-protector", "-mno-red-zone", "-mgeneral-regs-only", "-O2",
         "-c", "guest.c", "-o", "guest.o"], directory)
    run(["as", "--64", "guest.S", "-o", "entry.o"], directory)
    for opt in ("-O0", "-O2"):
        stem = opt.removeprefix("-")
        if args.compiler:
            command = [args.compiler, "--target=x86_64-unknown-linux-gnu", "-std=c23",
                       "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc",
                       "-fno-stack-protector", "-mno-red-zone", opt]
            if args.backend_llc:
                run([*command, "-S", "-emit-llvm", "cases.c", "-o", stem + ".ll"], directory)
                run([args.backend_llc, "-mtriple=x86_64", "-filetype=obj", "-verify-machineinstrs", opt,
                     stem + ".ll", "-o", "cases.o"], directory)
            else:
                run([*command, "-c", "cases.c", "-o", "cases.o"], directory)
        else:
            input_ir = "cases.ll"
            if opt == "-O2":
                optimizer = Path(args.llc).with_name("opt")
                run([optimizer, "-S", "-passes=default<O2>", input_ir, "-o", "optimized.ll"], directory)
                input_ir = "optimized.ll"
            run([args.llc, "-mtriple=x86_64", "-filetype=obj", "-verify-machineinstrs", opt,
                 input_ir, "-o", "cases.o"], directory)
        shutil.copyfile(directory / "cases.o", directory / (stem + ".o"))
        run(["ld", "-m", "elf_x86_64", "-T", "payload.ld", "entry.o", "guest.o", "cases.o", "-o", "payload.elf"], directory)
        run(["objcopy", "-O", "binary", "payload.elf", "payload.bin"], directory)
        run(["as", "--32", "boot.S", "-o", "boot.o"], directory)
        run(["ld", "-m", "elf_i386", "-T", "boot.ld", "boot.o", "-o", "boot.elf"], directory)
        command = [args.qemu, "-machine", "pc,accel=tcg", "-cpu", "max", "-m", "64M",
                   "-nodefaults", "-display", "none", "-no-reboot", "-monitor", "none", "-serial", "none",
                   "-debugcon", "stdio", "-device", "isa-debug-exit,iobase=0xf4,iosize=4", "-kernel", "boot.elf"]
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=20)
        (directory / (stem + ".log")).write_text(result.stdout + result.stderr)
        # Four addr32 memory forms, both LOCK widths, and three register checks.
        skipped = 4 * (len(PREFIXES) * 6 + 3) if args.qemu_mpx_limitations else 0
        branch_skipped = int(args.qemu_mpx_limitations)
        expected = f"PASS{count - skipped:08x}:{skipped:08x}:{6 - branch_skipped:08x}:{branch_skipped:08x}\n"
        if result.returncode != 1 or result.stdout != expected:
            raise RuntimeError(f"MPX guest {opt}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
        print(f"{opt}: {count - skipped} native MPX cases passed, {skipped} skipped; "
              f"enabled/disabled, faults, tables and CFG; {6 - branch_skipped} original "
              f"branch-state cases passed, {branch_skipped} skipped", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--llc", type=lambda s: str(Path(s).resolve()))
    mode.add_argument("--compiler", type=lambda s: str(Path(s).resolve()))
    parser.add_argument("--backend-llc", type=lambda s: str(Path(s).resolve()))
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--keep", type=lambda s: Path(s).resolve())
    parser.add_argument("--qemu-mpx-limitations", action="store_true",
                        help="explicitly skip QEMU 10.2's addr32 memory, LOCK BNDMOV, register-check and E9 BNDINIT limitations")
    args = parser.parse_args()
    if args.backend_llc and not args.compiler:
        parser.error("--backend-llc requires --compiler")
    if args.keep:
        verify(args, args.keep)
    else:
        with tempfile.TemporaryDirectory(prefix="native-mpx-") as temporary:
            verify(args, Path(temporary))


if __name__ == "__main__":
    main()
