#!/usr/bin/env python3
"""Qualify native ENCLS/ENCLV register, CF/ZF, memory and ABI transport.

Controlled completions execute surrounding scalar code and inject all four
register outputs plus CF/ZF. They are not an SGX implementation. Separate Linux
children compare the real CPL3 #UD boundary with independently encoded bytes.
Successful enclave/VMX operations require a suitable execution environment.
--bounded-enclu checks constant fallthrough leaves, including rejected
continuations in the compiler regressions; it does not implement an enclave.
Requires capstone, lief and unicorn. --llc tests both LLVM code modes;
--backend-llc tests NeverC IR through the full LLVM fork.
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
STOP, ORDINARY = COMMON.STOP, COMMON.ORDINARY
VALUES = ((4, 0, 0, 0), (0x8800000000000007, 0xd0123456789abcde, 0x798ab6cdaaaaffff, 0x80000000000000c0),
          (0xffffffffffffffff, 0x0102030405060708, 0x8000000180000001, 0xa0009))
DELTA = (0x1234, 0x8796, 0xc0ffee, 0xdeadbeef)
VARIANTS = ("all", "rax", "rbx", "rcx", "rdx", "flags", "dead", "twice", "memory", "condition")
REGISTERS = ("AX", "BX", "CX", "DX")
BOUNDED_LEAVES = (0, 1, 5, 6, 7, 8, 9, 10, 11)


def enclu_control(leaf, mode, index, standard=False):
    return leaf if mode == 32 or standard else (0x8800000000000000 ^ (0x0444000000000000 if index else 0)) | leaf


def observed(variant):
    if variant in ("dead", "dead_standard"):
        return ()
    if variant in ("rax", "rbx", "rcx", "rdx", "flags"):
        return (dict(zip(("rax", "rbx", "rcx", "rdx", "flags"), range(5)))[variant],)
    return range(4 if variant == "standard" else 5)


def compile_case(args, directory, operation, variant, mode, opt, target=COMMON.TRIPLES[0], bounded_leaf=None):
    name = f"{operation}-{variant}-{mode}-{opt}-{COMMON.TRIPLES.index(target)}-{bounded_leaf}"
    ir, obj = directory / (name + ".ll"), directory / (name + ".o")
    word, count = f"i{mode}", 2 if variant == "twice" else 1
    ty = "{ " + ", ".join([word] * 4 + ["i8"]) + " }"
    api = "llvm.x86." + operation + (".bounded" if bounded_leaf is not None else "") + ("32" if mode == 32 else "")
    triple = ("x86_64" if mode == 64 else "i686") + "-" + target
    if args.llc:
        body = [f"store {word} %a, ptr %memory"] if variant == "memory" else []
        terms = ["%a", "%b", "%c", "%d"]
        if variant == "condition":
            body += [f"%cmp = icmp ult {word} %a, %d", f"%condition = zext i1 %cmp to {word}"]
            terms.append("%condition")
        for index in range(count):
            parameters = []
            for parameter, delta in zip(("a", "b", "c", "d"), DELTA):
                value = "%" + parameter
                if index:
                    value += str(index)
                    body.append(f"{value} = xor {word} %{parameter}, {delta}")
                if parameter == "a" and bounded_leaf is not None:
                    value = str(enclu_control(bounded_leaf, mode, index))
                parameters.append(f"{word} {value}")
            body.append(f"%call{index} = call {ty} @{api}({', '.join(parameters)})")
            for field in observed(variant):
                value = f"%v{index}_{field}"
                body.append(f"{value} = extractvalue {ty} %call{index}, {field}")
                if field == 4:
                    body.append(f"%flags{index} = zext i8 {value} to {word}")
                    value = f"%flags{index}"
                terms.append(value)
        if variant == "memory":
            body.append(f"%loaded = load {word}, ptr %memory")
            terms.append("%loaded")
        value = terms[0]
        for index, term in enumerate(terms[1:]):
            body.append(f"%xor{index} = xor {word} {value}, {term}")
            value = f"%xor{index}"
        body.append(f"ret {word} {value}")
        ir.write_text(f"declare {ty} @{api}({', '.join([word] * 4)})\n"
                      f"define {word} @probe({word} %a, {word} %b, {word} %c, {word} %d, ptr %memory) {{\n" +
                      "\n".join(body) + "\n}\n")
    else:
        src = directory / (name + ".c")
        body = ["*memory = a;"] if variant == "memory" else []
        terms = ["a", "b", "c", "d"]
        check_evaluation = bounded_leaf is not None and variant in ("standard", "dead_standard")
        if check_evaluation:
            body.append("volatile unsigned evaluations = 0;")
            terms.append(f"(evaluations != {count})")
        if variant == "condition":
            body.append("unsigned condition = a < d;")
            terms.append("condition")
        for index in range(count):
            arguments = [p + (f" ^ {delta}ULL" if index else "") for p, delta in zip(("a", "b", "c", "d"), DELTA)]
            suffix = operation + ("_bounded" if bounded_leaf is not None else "")
            if bounded_leaf is not None:
                arguments[0] = str(enclu_control(bounded_leaf, mode, index)) + "ULL"
            if variant in ("standard", "dead_standard"):
                leaf = str(bounded_leaf) if bounded_leaf is not None else "(unsigned)a"
                data = f"(evaluations++, out{index})" if check_evaluation else f"out{index}"
                body += [f"__SIZE_TYPE__ out{index}[3] = {{b, c, d}};",
                         f"unsigned long long result{index} = _{suffix}_u32({leaf}, {data});"]
            else:
                body += [f"unsigned long long out{index}[3]; unsigned char flags{index};",
                         f"unsigned long long result{index} = __builtin_ia32_{suffix}({', '.join(arguments)}, out{index}, &flags{index});"]
            for field in observed(variant):
                terms.append(f"flags{index}" if field == 4 else (f"result{index}" if field == 0 else f"out{index}[{field - 1}]"))
        if variant == "memory":
            terms.append("*memory")
        body.append("return " + " ^ ".join(terms) + ";")
        src.write_text('#include <immintrin.h>\n__attribute__((target("sgx")))\n'
                       "unsigned long long probe(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long *memory) {\n" +
                       "\n".join(body) + "\n}\n")
        compiler = [args.compiler, "--target=" + triple, "-" + opt, "-Werror", "-ffreestanding", "-fno-builtin-mimalloc",
                    "-fno-lto", "-fno-stack-protector", "-mllvm", "-verify-machineinstrs", src]
        run(compiler + ["-S", "-emit-llvm", "-o", ir])
        assert " asm " not in ir.read_text() and f"@{api}(" in ir.read_text()
        if not args.backend_llc:
            run(compiler + ["-c", "-o", obj])
    backend = args.llc or args.backend_llc
    if backend:
        backend_command = [backend, "-mtriple=" + triple, "-mattr=+sgx", "-" + opt, "-verify-machineinstrs", ir]
        run(backend_command + ["-filetype=obj", "-o", obj])
    if variant in ("all", "standard"):
        for dialect in ("att", "intel"):
            asm = directory / (name + "-" + dialect + ".s")
            assembled = asm.with_suffix(".o")
            if backend:
                run(backend_command + ["-filetype=asm", "-x86-asm-syntax=" + dialect, "-o", asm])
                run([Path(backend).parent / "llvm-mc", "-triple=" + triple, "-mattr=+sgx", "-filetype=obj", asm, "-o", assembled])
            else:
                run(compiler + ["-S", "-masm=" + dialect, "-o", asm])
                run([args.compiler, "--target=" + triple, "-msgx", "-c", asm, "-o", assembled])
            assert text_section(obj) == text_section(assembled), "assembly round trip changed bytes"
    return text_section(obj)


def execute(code, operation, variant, mode, values, statuses, stop_at_boundary=False, bounded_leaf=None):
    mask = (1 << mode) - 1
    inputs = tuple(value & mask for value in values)
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64 if mode == 64 else unicorn.UC_MODE_32)
    reg = lambda name: getattr(regs, "UC_X86_REG_" + ("R" if mode == 64 else "E") + name)
    pack = lambda value: struct.pack("<Q" if mode == 64 else "<I", value & mask)
    for base in (CODE, STACK, DATA):
        machine.mem_map(base, PAGE)
    machine.mem_write(CODE, code)
    machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
    sp = STACK + PAGE - 64 - mode // 8
    machine.reg_write(reg("SP"), sp)
    machine.mem_write(sp, pack(STOP))
    if mode == 64:
        for r, value in zip(("CX", "DX", "8", "9"), inputs):
            machine.reg_write(reg(r), value)
        machine.mem_write(sp + 40, pack(ORDINARY))
    else:
        machine.mem_write(sp + 4, b"".join(pack(v) for v in (*inputs, ORDINARY)))
    saved = {reg(name): (0xa1b2c3d4e5f60710 + index) & mask for index, name in
             enumerate(("BX", "BP", "SI", "DI", *(("12", "13", "14", "15") if mode == 64 else ())))}
    for r, value in saved.items():
        machine.reg_write(r, value)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if mode == 64 else capstone.CS_MODE_32)
    accesses = [instruction for instruction in decoder.disasm(code, CODE) if instruction.mnemonic == operation]
    assert len(accesses) == len(statuses), "native operations were lost or merged"
    result, start = inputs[0] ^ inputs[1] ^ inputs[2] ^ inputs[3], CODE
    if variant == "condition":
        result ^= int(inputs[0] < inputs[3])
    for index, (instruction, status) in enumerate(zip(accesses, statuses)):
        assert bytes(instruction.bytes) == bytes((0x0f, 0x01, {"encls": 0xcf, "enclv": 0xc0, "enclu": 0xd7}[operation]))
        machine.emu_start(start, instruction.address, count=1000)
        assert machine.reg_read(reg("IP")) == instruction.address
        for field, register in enumerate(REGISTERS):
            expected = inputs[field] ^ (DELTA[field] if index else 0)
            if bounded_leaf is not None and field == 0:
                expected = enclu_control(bounded_leaf, mode, index, variant in ("standard", "dead_standard"))
            if variant in ("standard", "dead_standard") and field == 0:
                expected &= 0xffffffff
            assert machine.reg_read(reg(register)) == expected, f"incorrect SGX {register} input"
        if stop_at_boundary:
            return  # No implicit enclave-address preload occurred before the native boundary.
        outputs = [((0x9182736455aa0123 + field * 0x17659843) ^ (inputs[field] >> 2) ^ (0xfedcba98 if index else 0)) & mask
                   for field in range(4)]
        for register, value in zip(REGISTERS, outputs):
            machine.reg_write(reg(register), value)
        # Defined-status leaves clear the remaining arithmetic flags. The
        # four CF/ZF combinations are diagnostic transport injections.
        flags = machine.reg_read(regs.UC_X86_REG_EFLAGS)
        machine.reg_write(regs.UC_X86_REG_EFLAGS, (flags & ~0x8d5) | (status & 1) | ((status >> 1) << 6))
        if variant == "standard":
            outputs[0] &= 0xffffffff
        for field in observed(variant):
            result ^= status if field == 4 else outputs[field]
        if variant == "memory":
            assert bytes(machine.mem_read(ORDINARY, mode // 8)) == pack(inputs[0])
            machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
            result ^= COMMON.SENTINEL & mask
        start = instruction.address + instruction.size
    machine.emu_start(start, STOP, count=1000)
    assert machine.reg_read(reg("IP")) == STOP and machine.reg_read(reg("AX")) == result, "SGX result/flags/live-input transport changed"
    assert all(machine.reg_read(r) == value for r, value in saved.items()), "callee-saved register corrupted"


def native_fault_oracle(args, directory):
    if args.llc or platform.system() != "Linux" or platform.machine() != "x86_64":
        return 0
    count = 0
    cases = [("enclu", leaf) for leaf in BOUNDED_LEAVES] if args.bounded_enclu else [("encls", 4), ("enclv", 4)]
    for (operation, leaf), opt in itertools.product(cases, ("O0", "O2")):
        base = directory / f"fault-{operation}-{leaf}-{opt}"
        src, ir, obj, library = (base.with_suffix(suffix) for suffix in (".c", ".ll", ".o", ".so"))
        byte = {"encls": "0xcf", "enclv": "0xc0", "enclu": "0xd7"}[operation]
        suffix = operation + ("_bounded" if args.bounded_enclu else "")
        src.write_text(f'''__attribute__((target("sgx"))) unsigned long long probe(void) {{
  unsigned long long outputs[3]; unsigned char flags;
  return __builtin_ia32_{suffix}({leaf}, 0, 0, 0, outputs, &flags);
}}
unsigned long long original(void) {{
  unsigned long long a={leaf},b=0,c=0,d=0;
  __asm__ volatile(".byte 0x0f, 0x01, {byte}" : "+a"(a), "+b"(b), "+c"(c), "+d"(d) : : "cc", "memory");
  return a;
}}
''')
        compiler = [args.compiler, "--target=x86_64-unknown-linux-gnu", "-" + opt, "-Werror", "-ffreestanding",
                    "-fno-lto", "-fno-builtin-mimalloc", "-fPIC", "-mllvm", "-verify-machineinstrs", src]
        if args.backend_llc:
            run(compiler + ["-S", "-emit-llvm", "-o", ir])
            run([args.backend_llc, "-" + opt, "-verify-machineinstrs", "-relocation-model=pic", "-filetype=obj", ir, "-o", obj])
        else:
            run(compiler + ["-c", "-o", obj])
        run([args.compiler, "-shared", "-nostdlib", "-fno-lto", "-fno-builtin-mimalloc", obj, "-o", library])
        outcomes = []
        for function in ("original", "probe"):
            child = subprocess.run([sys.executable, "-c", "import ctypes,sys; getattr(ctypes.CDLL(sys.argv[1]),sys.argv[2])()", str(library.resolve()), function],
                                   capture_output=True, text=True)
            expected = (-signal.SIGILL, -signal.SIGSEGV, -signal.SIGBUS) if args.bounded_enclu else (-signal.SIGILL,)
            assert child.returncode in expected, f"{operation}/{opt}/{function}: expected native fault, got {child.returncode}: {child.stderr}"
            outcomes.append(child.returncode)
            count += 1
        assert outcomes[0] == outcomes[1], "native intrinsic and original bytes disagree on fault"
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path("build-neverc/bin/neverc"))
    backend = parser.add_mutually_exclusive_group()
    backend.add_argument("--llc", type=Path)
    backend.add_argument("--backend-llc", type=Path)
    parser.add_argument("--keep", type=Path)
    parser.add_argument("--bounded-enclu", action="store_true")
    args = parser.parse_args()
    for key in ("compiler", "llc", "backend_llc"):
        if getattr(args, key):
            setattr(args, key, getattr(args, key).resolve())
    count = 0
    with tempfile.TemporaryDirectory(prefix="sgx-supervisor-") as temporary:
        directory = args.keep or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        variants = VARIANTS if args.llc else VARIANTS + ("standard", "dead_standard")
        cases = [("enclu", leaf) for leaf in BOUNDED_LEAVES] if args.bounded_enclu else [("encls", None), ("enclv", None)]
        for mode in ((32, 64) if args.llc else (64,)):
            for (operation, leaf), opt, variant in itertools.product(cases, ("O0", "O2"), variants):
                code = compile_case(args, directory, operation, variant, mode, opt, bounded_leaf=leaf)
                for values, statuses in itertools.product(VALUES, itertools.product(range(4), repeat=2 if variant == "twice" else 1)):
                    try:
                        execute(code, operation, variant, mode, values, statuses, bounded_leaf=leaf)
                    except Exception as error:
                        raise AssertionError(f"{operation}/{leaf}, {variant}, {mode}, {opt}, {values}, {statuses}: {error}") from error
                    count += 1
                execute(code, operation, variant, mode, (4, 0xdead0001, 0xdead0003, 0xdead0005), (0,) * (2 if variant == "twice" else 1), True, leaf)
                count += 1
                if variant in ("all", "standard"):
                    for target in COMMON.TRIPLES[1:]:
                        compile_case(args, directory, operation, variant, mode, opt, target, leaf)
                        count += 1
        native = native_fault_oracle(args, directory)
    print(f"PASS: {count} SGX {'bounded ENCLU' if args.bounded_enclu else 'supervisor'} encoding/completion checks; {native} real CPL3 fault boundaries")


if __name__ == "__main__":
    main()
