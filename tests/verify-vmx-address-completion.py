#!/usr/bin/env python3
"""Qualify explicit VMX offsets, address widths and segment prefixes.

Controlled completions check machine operands, VMfail status, instruction-owned
memory access, ordering and the C ABI. They do not implement VMX operation or
segment descriptor validation. Native Linux CPL3 faults are compared with raw
architectural byte oracles; successful VMX execution needs a CPL0 environment.
Requires capstone, lief and unicorn. --llc covers both code modes;
--backend-llc compiles NeverC IR with the full LLVM fork.
"""

import argparse
import ctypes
import importlib.util
import itertools
import os
import platform
import resource
import signal
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
OPERATIONS = ("vmread", "vmwrite.mem", "invept", "invvpid", "vmclear", "vmptrld", "vmptrst", "vmxon")
SELECTOR = set(OPERATIONS[:4])
SEGMENTS = {0: None, 0x26: "es", 0x2e: "cs", 0x36: "ss", 0x3e: "ds", 0x64: "fs", 0x65: "gs"}
# SDM opcode maps and /r extensions, independent of the compiler tables.
OPCODES = {"vmread": b"\x0f\x78", "vmwrite.mem": b"\x0f\x79",
           "invept": b"\x0f\x38\x80", "invvpid": b"\x0f\x38\x81",
           **{op: b"\x0f\xc7" for op in OPERATIONS[4:]}}
MANDATORY = {"invept": 0x66, "invvpid": 0x66, "vmclear": 0x66, "vmxon": 0xf3}
VALUES = ((0, 0), (0xf1e2d3c44002a5b6, 0x18273645aabbccdd),
          (0xffffffffffffffff, 0x8000000080000000))
PAYLOAD = 0x9172536485a6b7c8


def compile_case(args, directory, operation, width, segment, variant, mode, opt,
                 target=COMMON.TRIPLES[0]):
    name = f"{operation}-{width}-{segment:x}-{variant}-{mode}-{opt}-{COMMON.TRIPLES.index(target)}"
    ir, obj = directory / (name + ".ll"), directory / (name + ".o")
    api = "llvm.x86." + operation + ".address" + ("32" if mode == 32 else "")
    word, result = f"i{mode}", "void" if operation == "vmptrst" else "i8"
    count = 2 if variant == "twice" else 1
    triple = ("x86_64" if mode == 64 else "i686") + "-" + target
    if args.llc:
        body = [f"store {word} %seed, ptr %ordinary"] if variant == "memory" else []
        terms = ["%selector", "%offset", "%seed"]
        if variant == "condition":
            body += [f"%cmp = icmp ult {word} %selector, %seed", f"%condition = zext i1 %cmp to {word}"]
            terms.append("%condition")
        for index in range(count):
            body += [f"%selector{index} = xor {word} %selector, {0x5a31 if index else 0}",
                     f"%offset{index} = xor {word} %offset, {0x40 if index else 0}"]
            call = (f"{word} %selector{index}, " if operation in SELECTOR else "")
            call += f"{word} %offset{index}, i32 {width}, i32 {segment}"
            body.append((f"%native{index} = " if result != "void" else "") + f"call {result} @{api}({call})")
            if result != "void" and variant != "dead":
                body += [f"%status{index} = zext i8 %native{index} to {word}",
                         f"%shift{index} = shl {word} %status{index}, {index * 8}"]
                terms.append(f"%shift{index}")
        if variant == "memory":
            body.append(f"%loaded = load {word}, ptr %ordinary")
            terms.append("%loaded")
        value = terms[0]
        for index, term in enumerate(terms[1:]):
            body.append(f"%xor{index} = xor {word} {value}, {term}")
            value = f"%xor{index}"
        body.append(f"ret {word} {value}")
        types = (word + ", " if operation in SELECTOR else "") + f"{word}, i32, i32"
        ir.write_text(f"declare {result} @{api}({types})\n"
                      f"define {word} @probe({word} %selector, {word} %offset, {word} %seed, ptr %ordinary) {{\n" +
                      "\n".join(body) + "\n}\n")
        if opt == "O2":
            optimized = ir.with_suffix(".opt.ll")
            run([Path(args.llc).parent / "opt", "-S", "-passes=default<O2>", ir, "-o", optimized])
            ir = optimized
    else:
        src = directory / (name + ".c")
        body = ["*ordinary = seed;"] if variant == "memory" else []
        terms = ["selector", "offset", "seed"]
        if variant == "condition":
            body.append("unsigned condition = selector < seed;")
            terms.append("condition")
        for index in range(count):
            call = (f"selector ^ {0x5a31 if index else 0}, " if operation in SELECTOR else "")
            call += f"offset ^ {0x40 if index else 0}, {width}, {segment}"
            body.append((f"unsigned char status{index} = " if result != "void" else "") +
                        f"__builtin_ia32_{operation.replace('.', '_')}_address({call});")
            if result != "void" and variant != "dead":
                terms.append(f"((unsigned long long)status{index} << {index * 8})")
        if variant == "memory":
            terms.append("*ordinary")
        body.append("return " + " ^ ".join(terms) + ";")
        src.write_text("unsigned long long probe(unsigned long long selector, unsigned long long offset, "
                       "unsigned long long seed, unsigned long long *ordinary) {\n" + "\n".join(body) + "\n}\n")
        compiler = [args.compiler, "--target=" + triple, "-" + opt, "-Werror", "-ffreestanding",
                    "-fno-builtin-mimalloc", "-fno-lto", "-fno-stack-protector",
                    "-mllvm", "-verify-machineinstrs", src]
        run(compiler + ["-S", "-emit-llvm", "-o", ir])
        assert " asm " not in ir.read_text() and f"@{api}(" in ir.read_text(), "missing native IR call"
        if not args.backend_llc:
            run(compiler + ["-c", "-o", obj])
    backend = args.llc or args.backend_llc
    if backend:
        backend_command = [backend, "-mtriple=" + triple, "-" + opt, "-verify-machineinstrs", ir]
        run(backend_command + ["-filetype=obj", "-o", obj])
    if variant == "value" and (width, segment) in ((mode, 0), (mode // 2, 0x64)):
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


def accesses(code, operation, width, segment, mode, count):
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if mode == 64 else capstone.CS_MODE_32)
    decoder.detail = True
    instructions = list(decoder.disasm(code, CODE))
    assert sum(i.size for i in instructions) == len(code), "incomplete instruction decoding"
    native = [i for i in instructions if i.mnemonic == operation.split(".")[0]]
    assert len(native) == count, "native effects were lost or merged"
    for instruction in native:
        encoding, cursor = bytes(instruction.bytes), 0
        while encoding[cursor] in (*SEGMENTS.keys(), 0x66, 0x67, 0xf3) or mode == 64 and 0x40 <= encoding[cursor] <= 0x4f:
            cursor += 1
        prefixes = encoding[:cursor]
        assert ([p for p in prefixes if p in SEGMENTS] == ([segment] if segment else [])), "segment override changed"
        assert prefixes.count(0x67) == int(width != mode), "address-size override changed"
        assert [p for p in prefixes if p in (0x66, 0xf3)] == ([MANDATORY[operation]] if operation in MANDATORY else [])
        opcode = OPCODES[operation]
        assert encoding[cursor:cursor + len(opcode)] == opcode, "wrong native opcode"
        modrm = encoding[cursor + len(opcode)]
        assert modrm >> 6 == 0 and (modrm & 7) in ((4, 5) if width == 16 else (0, 1, 2, 3)), "address carrier is not a data-default base"
        if operation not in SELECTOR:
            assert modrm >> 3 & 7 == (7 if operation == "vmptrst" else 6)
        memory, = (o for o in instruction.operands if o.type == capstone.x86.X86_OP_MEM)
        assert instruction.addr_size == width // 8
        assert instruction.reg_name(memory.mem.base) in {16: ("si", "di"),
            32: ("eax", "ebx", "ecx", "edx"), 64: ("rax", "rbx", "rcx", "rdx")}[width]
        assert not memory.mem.index and memory.mem.disp == 0
        assert (instruction.reg_name(memory.mem.segment) if memory.mem.segment else None) == SEGMENTS[segment]
        expected_size = mode // 8 if operation in ("vmread", "vmwrite.mem") else 16 if operation in ("invept", "invvpid") else 8
        assert memory.size == expected_size, "native memory width changed"
    return native


def execute(code, operation, width, segment, variant, mode, values, statuses, mapped=True):
    mask, address_mask = (1 << mode) - 1, (1 << width) - 1
    selector, seed = (v & mask for v in values)
    # High bits deliberately differ from the offset consumed by narrow addressing.
    offset = ((0x9182736400000000 if mode == 64 else 0x81920000) | 0x205) & mask
    if width == mode:
        offset = 0x1205
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
    arguments = selector, offset, seed, ORDINARY
    if mode == 64:
        for name, value in zip(("CX", "DX", "8", "9"), arguments):
            machine.reg_write(reg(name), value)
    else:
        machine.mem_write(sp + 4, b"".join(pack(v) for v in arguments))
    saved = {reg(name): (0xa1b2c3d4e5f60710 + index) & mask for index, name in
             enumerate(("BX", "BP", "SI", "DI", *(("12", "13", "14", "15") if mode == 64 else ())))}
    for number, value in saved.items():
        machine.reg_write(number, value)
    count = 2 if variant == "twice" else 1
    native = accesses(code, operation, width, segment, mode, count)
    # The segment base is applied at the instruction boundary, after truncation.
    # Descriptor validation is explicitly outside this controlled transport test.
    segment_base = 0x400000 if mode == 32 or segment in (0x64, 0x65) else 0
    destinations = [segment_base + ((offset ^ (0x40 if i else 0)) & address_mask) for i in range(count)]
    if mapped:
        for base in set(d & -PAGE for d in destinations):
            machine.mem_map(base, PAGE)
        for destination in destinations:
            machine.mem_write(destination - 1, b"\xa5" + b"\x17" * 16 + b"\x5a")
    touched = []
    def memory_access(machine, access, address, size, value, data):
        if any(d - 1 <= address < d + 17 for d in destinations):
            touched.append((access, address, size))
    machine.hook_add(unicorn.UC_HOOK_MEM_READ | unicorn.UC_HOOK_MEM_WRITE, memory_access)
    result, start = selector ^ offset ^ seed, CODE
    if variant == "condition":
        result ^= int(selector < seed)
    snapshots = {}
    for index, (instruction, status) in enumerate(zip(native, statuses)):
        machine.emu_start(start, instruction.address, count=2000)
        assert machine.reg_read(reg("IP")) == instruction.address
        memory, = (o for o in instruction.operands if o.type == capstone.x86.X86_OP_MEM)
        base = machine.reg_read(getattr(regs, "UC_X86_REG_" + instruction.reg_name(memory.mem.base).upper()))
        assert base == (offset ^ (0x40 if index else 0)) & address_mask, "address truncation changed"
        if operation in SELECTOR:
            operand, = (o for o in instruction.operands if o.type == capstone.x86.X86_OP_REG)
            value = machine.reg_read(getattr(regs, "UC_X86_REG_" + instruction.reg_name(operand.reg).upper()))
            assert value == selector ^ (0x5a31 if index else 0), "selector truncated or swapped"
        assert not touched, "compiler touched native memory outside the instruction"
        if not mapped:
            return
        destination = destinations[index]
        if operation == "vmptrst" or operation == "vmread" and status == 0:
            size = 8 if operation == "vmptrst" else mode // 8
            machine.mem_write(destination, (PAYLOAD ^ index).to_bytes(8, "little")[:size])
        snapshots[destination] = bytes(machine.mem_read(destination - 1, 18))
        if variant == "memory":
            assert bytes(machine.mem_read(ORDINARY, mode // 8)) == pack(seed), "store moved after native effect"
            machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
        flags = machine.reg_read(regs.UC_X86_REG_EFLAGS) & ~0x8d5
        machine.reg_write(regs.UC_X86_REG_EFLAGS, flags | (0x40 if status == 1 else 1 if status == 2 else 0) | 2)
        if operation != "vmptrst" and variant != "dead":
            result ^= status << (index * 8)
        start = instruction.address + instruction.size
    machine.emu_start(start, STOP, count=2000)
    if variant == "memory":
        result ^= COMMON.SENTINEL & mask
    assert machine.reg_read(reg("IP")) == STOP and machine.reg_read(reg("AX")) == result & mask, "completion or live input changed"
    assert all(machine.reg_read(r) == value for r, value in saved.items()), "callee-saved register corrupted"
    assert not touched, "compiler accessed the instruction-owned operand"
    for destination, snapshot in snapshots.items():
        assert bytes(machine.mem_read(destination - 1, 18)) == snapshot, "spurious post-instruction store"


def native_fault_oracle(args, directory):
    if args.llc or platform.system() != "Linux" or platform.machine() != "x86_64":
        return 0
    count = 0
    libc = ctypes.CDLL(None, use_errno=True)
    for opt in ("O0", "O2"):
        base = directory / ("native-" + opt)
        src, ir, obj, library = (base.with_suffix(s) for s in (".c", ".ll", ".o", ".so"))
        body, names = [], []
        for operation, width, segment in itertools.product(OPERATIONS, (32, 64), SEGMENTS):
            name = f"{operation.replace('.', '_')}_{width}_{segment:x}"
            names.append(name)
            prefix = ([segment] if segment else []) + ([0x67] if width == 32 else [])
            if operation in MANDATORY:
                prefix.append(MANDATORY[operation])
            modrm = 3 if operation in SELECTOR else 0x3b if operation == "vmptrst" else 0x33
            encoding = bytes(prefix) + OPCODES[operation] + bytes((modrm,))
            call = ("0xf1e2d3c44002a5b6ULL, " if operation in SELECTOR else "") + f"offset, {width}, {segment}"
            body += [f"void probe_{name}(unsigned long long offset) {{ __builtin_ia32_{operation.replace('.', '_')}_address({call}); }}",
                     f'void original_{name}(unsigned long long offset) {{ __asm__ volatile(".byte ' +
                     ",".join(hex(v) for v in encoding) +
                     '" : : "a"(0xf1e2d3c44002a5b6ULL), "b"(offset) : "cc", "memory"); }']
        src.write_text("\n".join(body) + "\n")
        compiler = [args.compiler, "--target=x86_64-unknown-linux-gnu", "-" + opt, "-Werror", "-ffreestanding",
                    "-fno-lto", "-fno-builtin-mimalloc", "-fPIC", "-mllvm", "-verify-machineinstrs", src]
        if args.backend_llc:
            run(compiler + ["-S", "-emit-llvm", "-o", ir])
            run([args.backend_llc, "-" + opt, "-verify-machineinstrs", "-relocation-model=pic", "-filetype=obj", ir, "-o", obj])
        else:
            run(compiler + ["-c", "-o", obj])
        run([args.compiler, "-shared", "-nostdlib", "-fno-lto", "-fno-builtin-mimalloc", obj, "-o", library])
        native = ctypes.CDLL(str(library.resolve()))
        data = (ctypes.c_ulonglong * 2)(0, 0)
        for name, offset in itertools.product(names, (0xdead000d, ctypes.addressof(data))):
            statuses = []
            for kind in ("original", "probe"):
                function = getattr(native, kind + "_" + name)
                function.argtypes = (ctypes.c_ulonglong,)
                pid = os.fork()
                if not pid:
                    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
                    # A piped core handler can ignore RLIMIT_CORE. These
                    # intentional CPL3 faults do not need system core dumps.
                    assert libc.prctl(4, 0, 0, 0, 0) == 0
                    signal.alarm(5)
                    function(offset)
                    os._exit(0)
                _, status = os.waitpid(pid, 0)
                statuses.append(os.waitstatus_to_exitcode(status))
                count += 1
            assert statuses[0] in (-signal.SIGILL, -signal.SIGSEGV) and statuses[0] == statuses[1], f"{name}/{opt}: CPL3 fault mismatch {statuses}"
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
    with tempfile.TemporaryDirectory(prefix="vmx-address-") as temporary:
        directory = args.keep or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        for mode in ((32, 64) if args.llc else (64,)):
            for operation, width, segment, opt in itertools.product(OPERATIONS, (mode // 2, mode), SEGMENTS, ("O0", "O2")):
                variants = ("value", "dead", "twice", "memory", "condition") if (width, segment) in ((mode, 0), (mode // 2, 0x64)) else ("value",)
                for variant in variants:
                    code = compile_case(args, directory, operation, width, segment, variant, mode, opt)
                    statuses = itertools.product((0,) if operation == "vmptrst" else (0, 1, 2), repeat=2 if variant == "twice" else 1)
                    for values, completions in itertools.product(VALUES, statuses):
                        try:
                            execute(code, operation, width, segment, variant, mode, values, completions)
                        except Exception as error:
                            raise AssertionError(f"{operation}/{width}/{segment:x}/{variant}/{mode}/{opt}/{values}/{completions}: {error}") from error
                        count += 1
                    execute(code, operation, width, segment, variant, mode, VALUES[1], (0,) * (2 if variant == "twice" else 1), False)
                    count += 1
                    if variant == "value":
                        for target in COMMON.TRIPLES[1:]:
                            other = compile_case(args, directory, operation, width, segment, variant, mode, opt, target)
                            accesses(other, operation, width, segment, mode, 1)
                            count += 1
        native = native_fault_oracle(args, directory)
    print(f"PASS: {count} VMX explicit-address checks; {native} real CPL3 fault boundaries")


if __name__ == "__main__":
    main()
