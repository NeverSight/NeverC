#!/usr/bin/env python3
"""Qualify descriptor-state native addresses against independent opcode bytes.

Controlled instruction boundaries check addressing, ZF, retained effects,
ordinary-memory ordering, live inputs and the C ABI. They do not establish
privileged execution, descriptor-table validation or native exception delivery.
Requires capstone, lief and unicorn; accepts full LLVM and both NeverC routes.
"""

import argparse
import importlib.util
import itertools
import struct
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
PREFIXES = (0, 0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65)
# Opcode byte, ModRM extension and the memory footprint; INVLPG does not load.
ENCODINGS = {"sldt": (0, 0, 2), "str": (0, 1, 2), "lldt": (0, 2, 2),
             "ltr": (0, 3, 2), "verr": (0, 4, 2), "verw": (0, 5, 2),
             "smsw": (1, 4, 2), "lmsw": (1, 6, 2), "sgdt": (1, 0, 0),
             "sidt": (1, 1, 0), "lgdt": (1, 2, 0), "lidt": (1, 3, 0),
             "invlpg": (1, 7, 0)}
TABLES = ("sgdt", "sidt", "lgdt", "lidt")
VERIFY = ("verr", "verw")


def api_name(op, mode, legacy):
    name = op + ("16" if legacy else "")
    if op not in (*TABLES, "invlpg"):
        name += ".mem"
    return "llvm.x86." + name + ".address" + ("32" if mode == 32 else "")


def compile_case(args, directory, case, mode, opt, target):
    op, width, prefix, legacy, variant = case
    api = api_name(op, mode, legacy)
    name = f"{api}-{width}-{prefix}-{variant}-{opt}-{target}"
    ir, obj = directory / (name + ".ll"), directory / (name + ".o")
    triple = ("x86_64" if mode == 64 else "i386") + "-" + target
    verify, count = op in VERIFY, 2 if variant == "twice" else 1
    word, result = f"i{mode}", "i8" if verify else "void"
    if args.llc:
        body, terms = [], ["%offset", "%seed"]
        if variant == "memory":
            body.append(f"store {word} %seed, ptr %ordinary")
        if variant == "condition":
            body += [f"%cmp = icmp ult {word} %offset, %seed",
                     f"%condition = zext i1 %cmp to {word}"]
            terms.append("%condition")
        for i in range(count):
            body.append(f"%input{i} = xor {word} %offset, {0x40 if i else 0}")
            body.append((f"%call{i} = " if verify else "") +
                        f"call {result} @{api}({word} %input{i}, i32 {width}, i32 {prefix})")
            if verify and variant != "dead":
                body.append(f"%status{i} = zext i8 %call{i} to {word}")
                body.append(f"%shifted{i} = shl {word} %status{i}, {8 + 8 * i}")
                terms.append(f"%shifted{i}")
        if variant == "memory":
            body.append(f"%loaded = load {word}, ptr %ordinary")
            terms.append("%loaded")
        value = terms[0]
        for i, term in enumerate(terms[1:]):
            body.append(f"%xor{i} = xor {word} {value}, {term}")
            value = f"%xor{i}"
        body.append(f"ret {word} {value}")
        ir.write_text(f"declare {result} @{api}({word}, i32, i32)\n"
                      f"define {word} @probe({word} %offset, {word} %seed, ptr %ordinary) {{\n" +
                      "\n".join(body) + "\n}\n")
        if opt == "O2":
            optimized = ir.with_suffix(".opt.ll")
            run([Path(args.llc).parent / "opt", "-S", "-passes=default<O2>", ir, "-o", optimized])
            ir = optimized
    else:
        src = directory / (name + ".c")
        body, terms = [], ["offset", "seed"]
        if variant == "memory":
            body.append("*ordinary = seed;")
        if variant == "condition":
            body.append("unsigned condition = offset < seed;")
            terms.append("condition")
        for i in range(count):
            body.append((f"unsigned char status{i} = " if verify else "") +
                        f"__builtin_ia32_{api[9:].replace('.', '_')}"
                        f"(offset ^ {0x40 if i else 0}, {width}, {prefix});")
            if verify and variant != "dead":
                terms.append(f"((unsigned long long)status{i} << {8 + 8 * i})")
        if variant == "memory":
            terms.append("*ordinary")
        body.append("return " + " ^ ".join(terms) + ";")
        src.write_text("unsigned long long probe(unsigned long long offset, unsigned long long seed, "
                       "unsigned long long *ordinary) {\n" + "\n".join(body) + "\n}\n")
        compiler = [args.compiler, "--target=" + triple, "-" + opt, "-Werror", "-ffreestanding",
                    "-fno-builtin-mimalloc", "-fno-lto", "-fno-stack-protector",
                    "-mllvm", "-verify-machineinstrs", src]
        run(compiler + ["-S", "-emit-llvm", "-o", ir])
        assert " asm " not in ir.read_text() and f"@{api}(" in ir.read_text(), "missing native IR"
        if not args.backend_llc:
            run(compiler + ["-c", "-o", obj])
    backend = args.llc or args.backend_llc
    if backend:
        command = [backend, "-mtriple=" + triple, "-" + opt, "-verify-machineinstrs", ir]
        run(command + ["-filetype=obj", "-o", obj])
    if variant == "live" and prefix in (0, 0x64):
        for dialect in ("att", "intel"):
            asm = directory / (name + "-" + dialect + ".s")
            roundtrip = asm.with_suffix(".o")
            if backend:
                run(command + ["-filetype=asm", "-x86-asm-syntax=" + dialect, "-o", asm])
                run([Path(backend).parent / "llvm-mc", "-triple=" + triple,
                     "-filetype=obj", asm, "-o", roundtrip])
            else:
                run(compiler + ["-S", "-masm=" + dialect, "-o", asm])
                run([args.compiler, "--target=" + triple, "-c", asm, "-o", roundtrip])
            assert text_section(obj) == text_section(roundtrip), "assembly roundtrip changed bytes"
    return text_section(obj)


def accesses(code, case, mode):
    op, width, prefix, legacy, variant = case
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if mode == 64 else capstone.CS_MODE_32)
    decoder.detail = True
    native = [i for i in decoder.disasm(code, CODE) if i.mnemonic == op]
    assert len(native) == (2 if variant == "twice" else 1), "native effect removed or merged"
    expected = bytes(([prefix] if prefix else []) + ([0x66] if legacy else []) +
                     ([0x67] if width != mode else []) +
                     [0x0f, ENCODINGS[op][0], (ENCODINGS[op][1] << 3) | (7 if width == 16 else 3)])
    for instruction in native:
        # Prefix order is immaterial; the opcode and complete addressing bytes are exact.
        raw = bytes(instruction.bytes)
        cursor, wanted = raw.index(0x0f), expected.index(0x0f)
        assert sorted(raw[:cursor]) == sorted(expected[:wanted]), "prefix controls changed"
        assert raw[cursor:] == expected[wanted:], "native opcode/ModRM changed"
        memory, = (o for o in instruction.operands if o.type == capstone.x86.X86_OP_MEM)
        assert instruction.addr_size == width // 8
        assert instruction.reg_name(memory.mem.base) == {16: "bx", 32: "ebx", 64: "rbx"}[width]
        assert not memory.mem.index and memory.mem.disp == 0
    return native


def execute(code, case, mode, status, mapped=True):
    op, width, prefix, legacy, variant = case
    mask, address_mask = (1 << mode) - 1, (1 << width) - 1
    offset = 0x1205 if width == mode else (0x9182736481920205 & mask)
    seed = 0xf123456789abcdef & mask
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
        for name, value in zip(("CX", "DX", "8"), (offset, seed, ORDINARY)):
            machine.reg_write(reg(name), value)
    else:
        machine.mem_write(sp + 4, b"".join(pack(v) for v in (offset, seed, ORDINARY)))
    saved = {reg(name): (0xa1b2c3d4e5f60710 + i) & mask for i, name in
             enumerate(("BX", "BP", "SI", "DI", *(("12", "13", "14", "15") if mode == 64 else ())))}
    for number, value in saved.items():
        machine.reg_write(number, value)
    native = accesses(code, case, mode)
    # Segment-base addition is diagnostic transport only; no descriptor model.
    base = 0x400000 if mode == 32 or prefix in (0x64, 0x65) else 0
    addresses = [base + ((offset ^ (0x40 if i else 0)) & address_mask) for i in range(len(native))]
    footprint = (10 if mode == 64 else 6) if op in TABLES else ENCODINGS[op][2]
    if mapped:
        for page in {address & -PAGE for address in addresses}:
            machine.mem_map(page, PAGE)
        for address in addresses:
            machine.mem_write(address - 1, b"\xa5" * (footprint + 2))
    touched = []
    def access(machine, kind, address, size, value, data):
        if any(a - 1 <= address < a + footprint + 1 for a in addresses):
            touched.append((kind, address, size))
    machine.hook_add(unicorn.UC_HOOK_MEM_READ | unicorn.UC_HOOK_MEM_WRITE, access)
    result, start = offset ^ seed, CODE
    if variant == "condition":
        result ^= int(offset < seed)
    for i, instruction in enumerate(native):
        machine.emu_start(start, instruction.address, count=2000)
        assert machine.reg_read(reg("IP")) == instruction.address
        assert machine.reg_read(reg("BX")) == (offset ^ (0x40 if i else 0)) & address_mask, "offset wrapping changed"
        assert not touched, "memory operand escaped the native instruction"
        if not mapped:
            return
        if op in VERIFY:
            zf = status ^ (i & 1)
            flags = machine.reg_read(regs.UC_X86_REG_EFLAGS)
            machine.reg_write(regs.UC_X86_REG_EFLAGS, (flags & ~0x40) | (zf << 6))
            if variant != "dead":
                result ^= zf << (8 + i * 8)
        if variant == "memory":
            assert bytes(machine.mem_read(ORDINARY, mode // 8)) == pack(seed), "store crossed native effect"
            machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
        start = instruction.address + instruction.size
    machine.emu_start(start, STOP, count=2000)
    if variant == "memory":
        result ^= COMMON.SENTINEL & mask
    assert machine.reg_read(reg("IP")) == STOP and machine.reg_read(reg("AX")) == result & mask, "result/ZF/live inputs changed"
    assert all(machine.reg_read(r) == value for r, value in saved.items()), "callee-saved register corrupted"
    assert not touched, "compiler accessed native memory operand"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path("build-neverc/bin/neverc"))
    backend = parser.add_mutually_exclusive_group()
    backend.add_argument("--llc", type=Path)
    backend.add_argument("--backend-llc", type=Path)
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="descriptor-state-address-") as temp:
        directory = args.keep or Path(temp)
        directory.mkdir(parents=True, exist_ok=True)
        count = 0
        for mode in ((64, 32) if args.llc else (64,)):
            cases = [(op, width, prefix, legacy, "live")
                     for op, width, prefix in itertools.product(ENCODINGS, (mode, mode // 2), PREFIXES)
                     for legacy in ((False, True) if mode == 32 and op in TABLES else (False,))]
            cases += [(op, mode // 2, 0x65, False, variant)
                      for op, variant in itertools.product(ENCODINGS, ("dead", "twice", "memory", "condition"))]
            for opt, case in itertools.product(("O0", "O2"), cases):
                for target in COMMON.TRIPLES:
                    try:
                        code = compile_case(args, directory, case, mode, opt, target)
                        accesses(code, case, mode)
                        if target == COMMON.TRIPLES[0]:
                            for status in (0, 1):
                                execute(code, case, mode, status)
                            execute(code, case, mode, 0, False)
                    except Exception as error:
                        raise AssertionError(f"{mode} {opt} {target} {case}: {error}") from error
                    count += 1
            print(f"descriptor-state addresses: {mode}-bit complete", flush=True)
        print(f"PASS: {count} object/encoding checks; controlled boundaries on COFF; ATT/Intel roundtrips")


if __name__ == "__main__":
    main()
