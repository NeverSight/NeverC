#!/usr/bin/env python3
"""Qualify native descriptor/status instructions and INVD.

Controlled completions check operands, exact widths, ZF, dead/repeated effects,
memory ordering and the C ABI. They do not emulate descriptor tables, UMIP,
privilege faults or cache invalidation. Safe user-mode reads and verification
also run in isolated processes against independent inline-assembly oracles.
Requires capstone, lief and unicorn. --llc covers both LLVM code modes;
--backend-llc compiles NeverC's emitted IR with the full LLVM fork.
"""

import argparse
import importlib.util
import itertools
import platform
import signal
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import capstone
import unicorn
import unicorn.x86_const as regs


SPEC = importlib.util.spec_from_file_location(
    "descriptor", Path(__file__).with_name("verify-descriptor-query-completion.py"))
COMMON = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMMON)
run, text_section = COMMON.run, COMMON.text_section
CODE, STACK, DATA, PAGE = COMMON.CODE, COMMON.STACK, COMMON.DATA, COMMON.PAGE
STOP, SOURCE, ORDINARY = COMMON.STOP, COMMON.SOURCE, COMMON.ORDINARY
READS, VERIFY = ("sldt", "str", "smsw"), ("verr", "verw")
CTYPE = {8: "unsigned char", 16: "unsigned short", 32: "unsigned", 64: "unsigned long long"}
# Architectural opcode map and ModRM extensions, independent of backend tables.
ENCODINGS = {"sldt": (0x00, 0), "str": (0x00, 1), "lldt": (0x00, 2),
             "ltr": (0x00, 3), "verr": (0x00, 4), "verw": (0x00, 5),
             "smsw": (0x01, 4), "lmsw": (0x01, 6)}


def cases(mode):
    for operation in READS:
        for bits in ((16, 32, 64) if mode == 64 else (16, 32)):
            yield operation, "reg", bits
        yield operation, "mem", 0
    for operation in (*VERIFY, "lldt", "ltr", "lmsw"):
        for form in ("reg", "mem"):
            yield operation, form, 8 if operation in VERIFY else 0
    yield "invd", "", 0


def api_name(operation, form, bits):
    return operation + ("." + form if form else "") + (str(bits) if operation in READS and form == "reg" else "")


def compile_case(args, directory, case, variant, mode, opt, target=COMMON.TRIPLES[0]):
    operation, form, bits = case
    suffix = api_name(*case)
    name = f"{suffix}-{variant}-{mode}-{opt}-{COMMON.TRIPLES.index(target)}"
    ir, obj = directory / (name + ".ll"), directory / (name + ".o")
    word, result_ty = f"i{mode}", f"i{bits}" if bits else "void"
    parameter = "ptr" if form == "mem" else ("i16" if operation not in READS and form else "")
    count = 2 if variant == "twice" else 1
    memory_output = operation in READS and form == "mem"
    triple = ("x86_64" if mode == 64 else "i686") + "-" + target
    terms = ["%input", "%seed", "%pointer"]
    body = [f"%pointer = ptrtoint ptr %memory to {word}"]
    if args.llc:
        if variant == "memory":
            body.append(f"store {word} %seed, ptr %ordinary")
        if variant == "condition":
            body += [f"%cmp = icmp ult {word} %input, %seed", f"%condition = zext i1 %cmp to {word}"]
            terms.append("%condition")
        for index in range(count):
            if parameter == "i16":
                body.append(f"%input{index} = xor {word} %input, {0x5a31 if index else 0}")
                body.append(f"%selector{index} = trunc {word} %input{index} to i16")
            argument = "ptr %memory" if form == "mem" else (f"i16 %selector{index}" if parameter else "")
            body.append(f"{'%native' + str(index) + ' = ' if bits else ''}call {result_ty} @llvm.x86.{suffix}({argument})")
            width = bits
            if memory_output and variant != "dead":
                body.append(f"%native{index} = load i16, ptr %memory, align 1")
                width = 16
            if width and variant != "dead":
                value = f"%native{index}"
                if width != mode:
                    body.append(f"%value{index} = zext i{width} {value} to {word}")
                    value = f"%value{index}"
                terms.append(value)
        if variant == "memory":
            body.append(f"%loaded = load {word}, ptr %ordinary")
            terms.append("%loaded")
        value = terms[0]
        for index, term in enumerate(terms[1:]):
            body.append(f"%xor{index} = xor {word} {value}, {term}")
            value = f"%xor{index}"
        body.append(f"ret {word} {value}")
        ir.write_text(f"declare {result_ty} @llvm.x86.{suffix}({parameter})\n"
                      f"define {word} @probe({word} %input, ptr %memory, {word} %seed, ptr %ordinary) {{\n" +
                      "\n".join(body) + "\n}\n")
    else:
        src = directory / (name + ".c")
        body = ["*ordinary = seed;"] if variant == "memory" else []
        terms = ["input", "seed", "(unsigned long long)memory"]
        if variant == "condition":
            body.append("unsigned condition = input < seed;")
            terms.append("condition")
        for index in range(count):
            argument = "memory" if form == "mem" else (f"input ^ {0x5a31 if index else 0}" if parameter else "")
            call = f"__builtin_ia32_{suffix.replace('.', '_')}({argument})"
            body.append((f"{CTYPE[bits]} value{index} = " if bits else "") + call + ";")
            if memory_output and variant != "dead":
                # memcpy permits the same unaligned m16 address as the instruction.
                body += [f"unsigned short value{index};", f"__builtin_memcpy(&value{index}, memory, 2);"]
            if (bits or memory_output) and variant != "dead":
                terms.append(f"value{index}")
        if variant == "memory":
            terms.append("*ordinary")
        body.append("return " + " ^ ".join(terms) + ";")
        src.write_text("unsigned long long probe(unsigned long long input, void *memory, unsigned long long seed, unsigned long long *ordinary) {\n" +
                       "\n".join(body) + "\n}\n")
        compiler = [args.compiler, "--target=" + triple, "-" + opt, "-Werror", "-ffreestanding", "-fno-builtin-mimalloc",
                    "-fno-lto", "-fno-stack-protector", "-mllvm", "-verify-machineinstrs", src]
        run(compiler + ["-S", "-emit-llvm", "-o", ir])
        assert " asm " not in ir.read_text() and f"@llvm.x86.{suffix}(" in ir.read_text()
        if not args.backend_llc:
            run(compiler + ["-c", "-o", obj])
    backend = args.llc or args.backend_llc
    if backend:
        backend_command = [backend, "-mtriple=" + triple, "-" + opt, "-verify-machineinstrs", ir]
        run(backend_command + ["-filetype=obj", "-o", obj])
    if variant == "value":
        for dialect in ("att", "intel"):
            asm = directory / (name + "-" + dialect + ".s")
            assembled = asm.with_suffix(".o")
            if backend:
                run(backend_command + ["-filetype=asm", "-x86-asm-syntax=" + dialect, "-o", asm])
                run([Path(backend).parent / "llvm-mc", "-triple=" + triple, "-filetype=obj", asm, "-o", assembled])
            else:
                run(compiler + ["-S", "-masm=" + dialect, "-o", asm])
                run([args.compiler, "--target=" + triple, "-c", asm, "-o", assembled])
            assert text_section(obj) == text_section(assembled), "assembly round trip changed bytes"
    return text_section(obj)


def check_encoding(instruction, case, mode):
    operation, form, bits = case
    encoding = bytes(instruction.bytes)
    if operation == "invd":
        assert encoding == b"\x0f\x08"
        return
    index = 0
    while encoding[index] in (0x66, 0x67) or mode == 64 and 0x40 <= encoding[index] <= 0x4f:
        index += 1
    opcode, extension = ENCODINGS[operation]
    assert encoding[index:index + 2] == bytes((0x0f, opcode))
    assert encoding[index + 2] >> 3 & 7 == extension
    assert (encoding[index + 2] >> 6 == 3) == (form == "reg")
    assert 0x67 not in encoding[:index], "unexpected address-size override"
    operand, = instruction.operands
    assert operand.size == (bits // 8 if operation in READS and form == "reg" else 2)


def execute(code, case, variant, mode, values, statuses, mapped=True):
    operation, form, bits = case
    mask = (1 << mode) - 1
    input_value, seed, payload = (v & mask for v in values)
    address = SOURCE if mapped else 0xdead000d
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64 if mode == 64 else unicorn.UC_MODE_32)
    reg = lambda name: getattr(regs, "UC_X86_REG_" + ("R" if mode == 64 else "E") + name)
    pack = lambda value: struct.pack("<Q" if mode == 64 else "<I", value & mask)
    for base in (CODE, STACK, DATA):
        machine.mem_map(base, PAGE)
    machine.mem_write(CODE, code)
    initial_memory = b"\xa5\x2b\x00\x5a"
    machine.mem_write(SOURCE - 1, initial_memory)
    machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
    sp = STACK + PAGE - 64 - mode // 8
    machine.reg_write(reg("SP"), sp)
    machine.mem_write(sp, pack(STOP))
    arguments = input_value, address, seed, ORDINARY
    if mode == 64:
        for r, value in zip(("CX", "DX", "8", "9"), arguments):
            machine.reg_write(reg(r), value)
    else:
        machine.mem_write(sp + 4, b"".join(pack(v) for v in arguments))
    saved = {reg(name): (0xa1b2c3d4e5f60710 + index) & mask for index, name in
             enumerate(("BX", "BP", "SI", "DI", *(("12", "13", "14", "15") if mode == 64 else ())))}
    for r, value in saved.items():
        machine.reg_write(r, value)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if mode == 64 else capstone.CS_MODE_32)
    decoder.detail = True
    accesses = [i for i in decoder.disasm(code, CODE) if i.mnemonic == operation]
    assert len(accesses) == len(statuses), "native effects were lost or merged"
    result, start = input_value ^ seed ^ address, CODE
    if variant == "condition":
        result ^= int(input_value < seed)
    for index, (instruction, status) in enumerate(zip(accesses, statuses)):
        check_encoding(instruction, case, mode)
        machine.emu_start(start, instruction.address, count=1000)
        assert machine.reg_read(reg("IP")) == instruction.address
        register = lambda r: getattr(regs, "UC_X86_REG_" + instruction.reg_name(r).upper())
        value = lambda r: machine.reg_read(register(r)) if r else 0
        if form == "mem":
            operand, = instruction.operands
            mem = operand.mem
            assert operand.type == capstone.x86.X86_OP_MEM and not mem.segment
            assert (value(mem.base) + value(mem.index) * mem.scale + mem.disp) & mask == address
            if not mapped:
                return  # No eager C/IR access occurred before the native boundary.
            if index == 0 or operation not in READS:
                assert bytes(machine.mem_read(SOURCE - 1, 4)) == initial_memory
        elif form == "reg" and operation not in READS:
            operand, = instruction.operands
            assert value(operand.reg) == (input_value ^ (0x5a31 if index else 0)) & 0xffff
        if operation in READS:
            width = bits if form == "reg" else 16
            native = (payload ^ (0x8796 if index else 0)) & ((1 << width) - 1)
            if operation != "smsw":
                native &= 0xffff
            if form == "reg":
                destination = instruction.operands[0].reg
                # Real 32-bit GPR writes clear the upper half in long mode.
                name = instruction.reg_name(destination)
                if mode == 64 and width == 32:
                    wide = "r" + name[1:] if name.startswith("e") else name[:-1]
                    machine.reg_write(getattr(regs, "UC_X86_REG_" + wide.upper()), native)
                else:
                    machine.reg_write(register(destination), native)
            else:
                machine.mem_write(SOURCE, struct.pack("<H", native))
            if variant != "dead":
                result ^= native
        elif operation in VERIFY:
            flags = machine.reg_read(regs.UC_X86_REG_EFLAGS)
            machine.reg_write(regs.UC_X86_REG_EFLAGS, (flags & ~0x40) | (status << 6))
            if variant != "dead":
                result ^= status
        if variant == "memory":
            assert bytes(machine.mem_read(ORDINARY, mode // 8)) == pack(seed)
            machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
            result ^= COMMON.SENTINEL & mask
        start = instruction.address + instruction.size
    machine.emu_start(start, STOP, count=1000)
    assert machine.reg_read(reg("IP")) == STOP and machine.reg_read(reg("AX")) == result, "native result, ZF, memory or live inputs changed"
    assert all(machine.reg_read(r) == value for r, value in saved.items()), "callee-saved register corrupted"
    assert machine.mem_read(SOURCE - 1, 1) == b"\xa5" and machine.mem_read(SOURCE + 2, 1) == b"\x5a", "m16 access widened"


def native_oracle(args, directory):
    if args.llc or platform.system() != "Linux" or platform.machine() != "x86_64":
        return 0
    count = 0
    for operation in (*VERIFY, *READS):
        src, ir, obj, library = (directory / (f"native-{operation}" + suffix) for suffix in (".c", ".ll", ".o", ".so"))
        body = ["int probe(void) {"]
        checks = 0
        if operation in VERIFY:
            body += ["unsigned short selectors[4] = {0, 0, 0, 65535};",
                     '__asm__ volatile("movw %%cs, %0; movw %%ss, %1" : "=r"(selectors[1]), "=r"(selectors[2]));']
            for form, selector in itertools.product(("reg", "mem"), range(4)):
                checks += 1
                argument = f"selectors[{selector}]"
                operand, constraint = ("%w1", "r") if form == "reg" else ("%1", "m")
                body += [f'{{ unsigned char expected; __asm__ volatile("{operation} {operand}; sete %0" : "=qm"(expected) : "{constraint}"({argument}) : "cc", "memory");',
                         f"if (__builtin_ia32_{operation}_{form}({'&' if form == 'mem' else ''}{argument}) != expected) return {checks}; }}"]
        else:
            for bits in (16, 32, 64):
                checks += 1
                modifier = {16: "w", 32: "k", 64: "q"}[bits]
                body += [f'{{ {CTYPE[bits]} expected; __asm__ volatile("{operation} %{modifier}0" : "=r"(expected) : : "memory");',
                         f"if (__builtin_ia32_{operation}_reg{bits}() != expected) return {checks}; }}"]
            checks += 1
            body += [f'{{ unsigned short expected = 0, actual = 65535; __asm__ volatile("{operation} %0" : "=m"(expected) : : "memory");',
                     f"__builtin_ia32_{operation}_mem(&actual); if (actual != expected) return {checks}; }}"]
        body.append("return 0; }")
        src.write_text("\n".join(body) + "\n")
        compiler = [args.compiler, "--target=x86_64-unknown-linux-gnu", "-O2", "-Werror", "-ffreestanding",
                    "-fno-lto", "-fno-builtin-mimalloc", "-fPIC", "-mllvm", "-verify-machineinstrs", src]
        if args.backend_llc:
            run(compiler + ["-S", "-emit-llvm", "-o", ir])
            run([args.backend_llc, "-O2", "-verify-machineinstrs", "-relocation-model=pic", "-filetype=obj", ir, "-o", obj])
        else:
            run(compiler + ["-c", "-o", obj])
        run([args.compiler, "-shared", "-nostdlib", "-fno-lto", "-fno-builtin-mimalloc", obj, "-o", library])
        # UMIP may forbid the read on a particular host; do not crash the runner.
        child = subprocess.run([sys.executable, "-c", "import ctypes,sys; sys.exit(ctypes.CDLL(sys.argv[1]).probe())", str(library.resolve())])
        if operation in READS and child.returncode in (-signal.SIGSEGV, -signal.SIGILL):
            print(f"SKIP: native {operation} unavailable (signal {-child.returncode})")
            continue
        assert child.returncode == 0, f"{operation} native oracle failed: {child.returncode}"
        count += checks
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path("build-neverc/bin/neverc"))
    backend = parser.add_mutually_exclusive_group()
    backend.add_argument("--llc", type=Path)
    backend.add_argument("--backend-llc", type=Path)
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    for key in ("compiler", "llc", "backend_llc"):
        if getattr(args, key):
            setattr(args, key, getattr(args, key).resolve())
    count = 0
    with tempfile.TemporaryDirectory(prefix="descriptor-state-") as temporary:
        directory = args.keep or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        for mode in ((32, 64) if args.llc else (64,)):
            for case, opt, variant in itertools.product(cases(mode), ("O0", "O2"), ("value", "dead", "twice", "memory", "condition")):
                code = compile_case(args, directory, case, variant, mode, opt)
                statuses = itertools.product((0, 1) if case[0] in VERIFY else (0,), repeat=2 if variant == "twice" else 1)
                for values, completions in itertools.product(COMMON.VALUES, statuses):
                    try:
                        execute(code, case, variant, mode, values, completions)
                    except Exception as error:
                        raise AssertionError(f"{case}, {variant}, {mode}, {opt}, {values}, {completions}: {error}") from error
                    count += 1
                if case[1] == "mem":
                    execute(code, case, variant, mode, COMMON.VALUES[1], (0,) * (2 if variant == "twice" else 1), False)
                    count += 1
                if variant == "value":
                    for target in COMMON.TRIPLES[1:]:
                        compile_case(args, directory, case, variant, mode, opt, target)
                        count += 1
        native = native_oracle(args, directory)
    print(f"PASS: {count} descriptor state encoding/completion checks; {native} real user-mode oracle cases")


if __name__ == "__main__":
    main()
