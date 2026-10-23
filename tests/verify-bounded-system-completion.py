#!/usr/bin/env python3
"""Qualify native HRESET, INVLPGB, TLBSYNC and LKGS compiler transport.

Executes surrounding scalar code and intercepts privileged instructions. The
oracle checks exact operands, memory ordering, retention and saved registers;
it does not emulate processor history, descriptor loading or TLB broadcasts.
Requires capstone, lief and unicorn. --llc covers both LLVM code modes;
--backend-llc compiles actual NeverC IR with the full LLVM fork.
"""

import argparse
import importlib.util
import itertools
import struct
import tempfile
from pathlib import Path
from types import SimpleNamespace

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
VALUES = ((0, 0, 0), (0xd012345689ab002b, 0xaabbccdd12345678, 0x91726354aabbccdd),
          (0xffffffffffffffff, 0xffffffffffffffff, 0x8000000080000000))
DELTA = (0x1234, 0x55667788, 0xdeadbeef)


def instructions(code, mode):
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if mode == 64 else capstone.CS_MODE_32)
    decoder.detail = True
    position = 0
    while position < len(code):
        remaining = code[position:]
        for encoding, mnemonic in ((b"\xf3\x0f\x3a\xf0\xc0\x00", "hreset"),
                                    (b"\x0f\x01\xfe", "invlpgb"), (b"\x0f\x01\xff", "tlbsync")):
            if remaining.startswith(encoding):
                instruction = SimpleNamespace(address=CODE + position, size=len(encoding), mnemonic=mnemonic)
                break
        else:
            # Capstone releases without LKGS can decode the identical LLDT
            # operand shape. Require LKGS's independent F2 0F 00 /6 encoding
            # before changing only the ModRM extension for operand decoding.
            index = 1
            if remaining[0] == 0xf2:
                if mode == 64 and 0x40 <= remaining[index] <= 0x4f:
                    index += 1
                assert remaining[index:index + 2] == b"\x0f\x00" and (remaining[index + 2] >> 3) & 7 == 6
                copied = bytearray(remaining[:15])
                copied[index + 2] = (copied[index + 2] & ~0x38) | 0x10
                decoded = next(decoder.disasm(bytes(copied), CODE + position, count=1))
                assert decoded.mnemonic == "lldt"
                instruction = SimpleNamespace(address=decoded.address, size=decoded.size, mnemonic="lkgs",
                                              operands=decoded.operands, reg_name=decoded.reg_name)
            else:
                instruction = next(decoder.disasm(remaining, CODE + position, count=1))
        yield instruction
        position += instruction.size


def compile_case(args, directory, operation, variant, mode, opt, target=COMMON.TRIPLES[0]):
    name = f"{operation}-{variant}-{mode}-{opt}-{COMMON.TRIPLES.index(target)}"
    ir, obj = directory / (name + ".ll"), directory / (name + ".o")
    word = f"i{mode}"
    triple = ("x86_64" if mode == 64 else "i686") + "-" + target
    api = "llvm.x86." + operation + ("32" if operation == "invlpgb" and mode == 32 else "")
    count = 2 if variant == "twice" else 1
    if args.llc:
        body = [f"%pointer = ptrtoint ptr %source to {word}"]
        if variant == "memory":
            body.append(f"store {word} %a, ptr %ordinary")
        if variant == "condition":
            body += [f"%compare = icmp ult {word} %a, %b", f"%condition = zext i1 %compare to {word}"]
        for i in range(count):
            inputs = []
            for parameter, delta in zip(("a", "b", "c"), DELTA):
                value = "%" + parameter
                if i:
                    value += str(i)
                    body.append(f"{value} = xor {word} %{parameter}, {delta}")
                inputs.append(value)
            if mode == 64:
                for j, value in enumerate(inputs):
                    body.append(f"%narrow{i}_{j} = trunc {word} {value} to i32")
                narrow = [f"%narrow{i}_{j}" for j in range(3)]
            else:
                narrow = inputs
            body.append(f"%selector{i} = trunc {word} {inputs[0]} to i16")
            parameters = {"hreset": "i32", "invlpgb": f"{word}, i32, i32", "tlbsync": "",
                          "lkgs.reg": "i16", "lkgs.mem": "ptr"}[operation]
            call = {"hreset": f"i32 {narrow[0]}", "invlpgb": f"{word} {inputs[0]}, i32 {narrow[1]}, i32 {narrow[2]}",
                    "tlbsync": "", "lkgs.reg": f"i16 %selector{i}", "lkgs.mem": "ptr %source"}[operation]
            body.append(f"call void @{api}({call})")
        terms = ["%a", "%b", "%c", "%pointer"]
        if variant == "memory":
            body.append(f"%loaded = load {word}, ptr %ordinary")
            terms.append("%loaded")
        if variant == "condition":
            terms.append("%condition")
        value = terms[0]
        for i, term in enumerate(terms[1:]):
            body.append(f"%xor{i} = xor {word} {value}, {term}")
            value = f"%xor{i}"
        body.append(f"ret {word} {value}")
        ir.write_text(f"declare void @{api}({parameters})\ndefine {word} @probe({word} %a, {word} %b, {word} %c, ptr %source, ptr %ordinary) {{\n" +
                      "\n".join(body) + "\n}\n")
    else:
        src = directory / (name + ".c")
        body = ["*ordinary = a;"] if variant == "memory" else []
        if variant == "condition":
            body.append("unsigned condition = a < b;")
        for i in range(count):
            a, b, c = (p + (f" ^ {delta}ULL" if i else "") for p, delta in zip(("a", "b", "c"), DELTA))
            call = {"hreset": a, "invlpgb": f"{a}, {b}, {c}", "tlbsync": "", "lkgs.reg": a, "lkgs.mem": "source"}[operation]
            body.append(f"__builtin_ia32_{operation.replace('.', '_')}({call});")
        result = "a ^ b ^ c ^ (unsigned long long)source"
        if variant == "memory":
            result += " ^ *ordinary"
        if variant == "condition":
            result += " ^ condition"
        body.append("return " + result + ";")
        src.write_text('__attribute__((target("hreset"))) unsigned long long probe(unsigned long long a, unsigned long long b, unsigned long long c, const void *source, unsigned long long *ordinary) {\n' +
                       "\n".join(body) + "\n}\n")
        common = [args.compiler, "--target=" + triple, "-" + opt, "-Werror", "-ffreestanding", "-fno-builtin-mimalloc",
                  "-fno-lto", "-fno-stack-protector", "-mllvm", "-verify-machineinstrs", src]
        run(common + ["-S", "-emit-llvm", "-o", ir])
        assert " asm " not in ir.read_text() and f"@{api}(" in ir.read_text()
        if not args.backend_llc:
            run(common + ["-c", "-o", obj])
    backend = args.llc or args.backend_llc
    if backend:
        common_backend = [backend, "-mtriple=" + triple, "-mattr=+hreset", "-" + opt, "-verify-machineinstrs", ir]
        run(common_backend + ["-filetype=obj", "-o", obj])
    if variant == "retained":
        for dialect in ("att", "intel"):
            asm = directory / (name + "-" + dialect + ".s")
            assembled = asm.with_suffix(".o")
            if backend:
                run(common_backend + ["-filetype=asm", "-x86-asm-syntax=" + dialect, "-o", asm])
                run([Path(backend).parent / "llvm-mc", "-triple=" + triple, "-mattr=+hreset", "-filetype=obj", asm, "-o", assembled])
            else:
                run(common + ["-S", "-masm=" + dialect, "-o", asm])
                run([args.compiler, "--target=" + triple, "-mhreset", "-c", asm, "-o", assembled])
            assert text_section(obj) == text_section(assembled), "assembly round trip changed bytes"
    return text_section(obj)


def execute(code, operation, variant, mode, values, mapped=True):
    mask = (1 << mode) - 1
    a, b, c = (v & mask for v in values)
    source = SOURCE if mapped else ((1 << (mode - 1)) | (c & mask) | 13)
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64 if mode == 64 else unicorn.UC_MODE_32)
    reg = lambda name: getattr(regs, "UC_X86_REG_" + ("R" if mode == 64 else "E") + name)
    pack = lambda value: struct.pack("<Q" if mode == 64 else "<I", value & mask)
    for base in (CODE, STACK, DATA):
        machine.mem_map(base, PAGE)
    machine.mem_write(CODE, code)
    machine.mem_write(SOURCE, b"\x2b\x00")
    machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
    sp = STACK + PAGE - 64 - mode // 8
    machine.reg_write(reg("SP"), sp)
    machine.mem_write(sp, pack(STOP))
    arguments = (a, b, c, source, ORDINARY)
    if mode == 64:
        for r, v in zip(("CX", "DX", "8", "9"), arguments):
            machine.reg_write(reg(r), v)
        machine.mem_write(sp + 40, pack(ORDINARY))
    else:
        machine.mem_write(sp + 4, b"".join(pack(v) for v in arguments))
    saved = {reg(name): (0xa1b2c3d4e5f60710 + i) & mask for i, name in
             enumerate(("BX", "BP", "SI", "DI", *(("12", "13", "14", "15") if mode == 64 else ())))}
    for r, value in saved.items():
        machine.reg_write(r, value)
    accesses = [i for i in instructions(code, mode) if i.mnemonic == operation.split('.')[0]]
    assert len(accesses) == (2 if variant == "twice" else 1), "native effects were lost or merged"
    start = CODE
    for index, instruction in enumerate(accesses):
        machine.emu_start(start, instruction.address, count=1000)
        assert machine.reg_read(reg("IP")) == instruction.address
        av, bv, cv = (v ^ (d if index else 0) for v, d in zip((a, b, c), DELTA))
        if operation == "hreset":
            assert machine.reg_read(regs.UC_X86_REG_EAX) == av & 0xffffffff
        elif operation == "invlpgb":
            assert machine.reg_read(reg("AX")) == av
            assert machine.reg_read(regs.UC_X86_REG_EDX) == bv & 0xffffffff
            assert machine.reg_read(regs.UC_X86_REG_ECX) == cv & 0xffffffff
        elif operation.startswith("lkgs"):
            operand, = instruction.operands
            value = lambda r: machine.reg_read(getattr(regs, "UC_X86_REG_" + instruction.reg_name(r).upper())) if r else 0
            if operation == "lkgs.reg":
                assert operand.type == capstone.x86.X86_OP_REG and operand.size == 2
                assert value(operand.reg) == av & 0xffff
            else:
                assert operand.type == capstone.x86.X86_OP_MEM and operand.size == 2, "selector was eagerly loaded"
                mem = operand.mem
                assert not mem.segment and (value(mem.base) + value(mem.index) * mem.scale + mem.disp) & mask == source
                if not mapped:
                    return
                assert bytes(machine.mem_read(SOURCE, 2)) == b"\x2b\x00"
        if variant == "memory":
            assert bytes(machine.mem_read(ORDINARY, mode // 8)) == pack(a)
            machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
        start = instruction.address + instruction.size
    machine.emu_start(start, STOP, count=1000)
    result = a ^ b ^ c ^ source
    if variant == "memory":
        result ^= COMMON.SENTINEL & mask
    if variant == "condition":
        result ^= int(a < b)
    assert machine.reg_read(reg("IP")) == STOP and machine.reg_read(reg("AX")) == result, "live inputs or ordinary memory changed"
    assert all(machine.reg_read(r) == value for r, value in saved.items()), "callee-saved register corrupted"


def lkgs_encoding_matrix(args, directory):
    small = ("ax", "cx", "dx", "bx", "sp", "bp", "si", "di") + tuple(f"r{i}w" for i in range(8, 16))
    bases = ("rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi") + tuple(f"r{i}" for i in range(8, 16))
    count = 0
    for target, dialect in itertools.product(COMMON.TRIPLES, ("att", "intel")):
        triple = "x86_64-" + target
        path = directory / f"lkgs-all-registers-{COMMON.TRIPLES.index(target)}-{dialect}.s"
        obj = path.with_suffix(".o")
        lines = [".text"] + ([".intel_syntax noprefix"] if dialect == "intel" else [])
        lines += ["lkgs " + ("%" if dialect == "att" else "") + register for register in small]
        lines += ["lkgs " + (f"13(%{register})" if dialect == "att" else f"word ptr [{register} + 13]") for register in bases]
        path.write_text("\n".join(lines) + "\n")
        backend = args.llc or args.backend_llc
        if backend:
            run([Path(backend).parent / "llvm-mc", "-triple=" + triple, "-filetype=obj", path, "-o", obj])
        else:
            run([args.compiler, "--target=" + triple, "-c", path, "-o", obj])
        decoded = list(instructions(text_section(obj), 64))
        assert len(decoded) == 32 and all(i.mnemonic == "lkgs" for i in decoded)
        for instruction, expected in zip(decoded[:16], small):
            operand, = instruction.operands
            assert operand.type == capstone.x86.X86_OP_REG and operand.size == 2
            assert instruction.reg_name(operand.reg) == expected
            count += 1
        for instruction, expected in zip(decoded[16:], bases):
            operand, = instruction.operands
            assert operand.type == capstone.x86.X86_OP_MEM and operand.size == 2
            assert instruction.reg_name(operand.mem.base) == expected
            assert operand.mem.disp == 13 and not operand.mem.index and not operand.mem.segment
            count += 1
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
    with tempfile.TemporaryDirectory(prefix="bounded-system-") as temp:
        directory = args.keep or Path(temp)
        directory.mkdir(parents=True, exist_ok=True)
        for mode in ((32, 64) if args.llc else (64,)):
            operations = ("hreset", "invlpgb", "tlbsync", "lkgs.reg", "lkgs.mem") if mode == 64 else ("hreset", "invlpgb", "tlbsync")
            for operation, opt, variant in itertools.product(operations, ("O0", "O2"), ("retained", "twice", "memory", "condition")):
                code = compile_case(args, directory, operation, variant, mode, opt)
                for values in VALUES:
                    try:
                        execute(code, operation, variant, mode, values)
                    except Exception as error:
                        raise AssertionError(f"{operation}, {variant}, {mode}, {opt}, {values}: {error}") from error
                    count += 1
                if operation == "lkgs.mem":
                    execute(code, operation, variant, mode, VALUES[1], False)
                    count += 1
                if variant == "retained":
                    for triple in COMMON.TRIPLES[1:]:
                        compile_case(args, directory, operation, variant, mode, opt, triple)
                        count += 1
        count += lkgs_encoding_matrix(args, directory)
    print(f"PASS: {count} bounded system encoding/completion checks")


if __name__ == "__main__":
    main()
