#!/usr/bin/env python3
"""Qualify explicit LAR/LSL/LKGS addresses and descriptor-query completions.

Controlled completions verify operand transport, ZF/prior selection, retained
effects, ordering and ABI preservation. They do not emulate descriptor tables
or successful privileged LKGS. Requires capstone, lief and unicorn.
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
STOP, ORDINARY = COMMON.STOP, COMMON.ORDINARY
SEGMENTS = {0: None, 0x26: "es", 0x2e: "cs", 0x36: "ss", 0x3e: "ds", 0x64: "fs", 0x65: "gs"}


def compile_case(args, directory, op, bits, width, segment, variant, mode, opt,
                 target=COMMON.TRIPLES[0]):
    name = f"{op}{bits}-{width}-{segment:x}-{variant}-{mode}-{opt}-{COMMON.TRIPLES.index(target)}"
    ir, obj = directory / (name + ".ll"), directory / (name + ".o")
    query = op != "lkgs"
    api = "llvm.x86." + op + (f".mem{bits}" if query else "") + ".address" + ("32" if mode == 32 else "")
    word, narrow = f"i{mode}", f"i{bits}"
    result = "{ " + narrow + ", i8 }" if query else "void"
    count = 2 if variant == "twice" else 1
    keep_value = query and variant not in ("dead", "status")
    keep_status = query and variant not in ("dead", "value")
    triple = ("x86_64" if mode == 64 else "i686") + "-" + target
    if args.llc:
        body = [f"store {word} %seed, ptr %ordinary"] if variant == "memory" else []
        terms = ["%offset", "%prior", "%seed"]
        if variant == "condition":
            body += [f"%cmp = icmp ult {word} %prior, %seed", f"%condition = zext i1 %cmp to {word}"]
            terms.append("%condition")
        prior = "%prior"
        if query and bits != mode:
            body.append(f"%input = trunc {word} %prior to {narrow}")
            prior = "%input"
        for i in range(count):
            body.append(f"%offset{i} = xor {word} %offset, {0x40 if i else 0}")
            parameters = f"{word} %offset{i}, i32 {width}, i32 {segment}"
            if query:
                parameters += f", {narrow} {prior}"
            body.append((f"%call{i} = " if query else "") + f"call {result} @{api}({parameters})")
            if keep_value:
                body.append(f"%value{i} = extractvalue {result} %call{i}, 0")
                value = f"%value{i}"
                if bits != mode:
                    body.append(f"%wide{i} = zext {narrow} {value} to {word}")
                    value = f"%wide{i}"
                terms.append(value)
            if keep_status:
                body += [f"%status{i} = extractvalue {result} %call{i}, 1",
                         f"%zf{i} = zext i8 %status{i} to {word}",
                         f"%flag{i} = shl {word} %zf{i}, {8 + i * 8}"]
                terms.append(f"%flag{i}")
        if variant == "memory":
            body.append(f"%loaded = load {word}, ptr %ordinary")
            terms.append("%loaded")
        value = terms[0]
        for i, term in enumerate(terms[1:]):
            body.append(f"%xor{i} = xor {word} {value}, {term}")
            value = f"%xor{i}"
        body.append(f"ret {word} {value}")
        parameters = f"{word}, i32, i32" + (f", {narrow}" if query else "")
        ir.write_text(f"declare {result} @{api}({parameters})\n"
                      f"define {word} @probe({word} %offset, {word} %prior, {word} %seed, ptr %ordinary) {{\n" +
                      "\n".join(body) + "\n}\n")
        if opt == "O2":
            optimized = ir.with_suffix(".opt.ll")
            run([Path(args.llc).parent / "opt", "-S", "-passes=default<O2>", ir, "-o", optimized])
            ir = optimized
    else:
        src = directory / (name + ".c")
        body = ["*ordinary = seed;"] if variant == "memory" else []
        terms = ["offset", "prior", "seed"]
        if variant == "condition":
            body.append("unsigned condition = prior < seed;")
            terms.append("condition")
        for i in range(count):
            if query:
                body.append(f"unsigned char zf{i};")
            call = f"offset ^ {0x40 if i else 0}, {width}, {segment}"
            if query:
                call += f", prior, &zf{i}"
            builtin = op + (f"_mem{bits}" if query else "") + "_address"
            body.append((f"unsigned long long value{i} = " if query else "") + f"__builtin_ia32_{builtin}({call});")
            if keep_value:
                terms.append(f"value{i}")
            if keep_status:
                terms.append(f"((unsigned long long)zf{i} << {8 + i * 8})")
        if variant == "memory":
            terms.append("*ordinary")
        body.append("return " + " ^ ".join(terms) + ";")
        src.write_text("unsigned long long probe(unsigned long long offset, unsigned long long prior, "
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
        command = [backend, "-mtriple=" + triple, "-" + opt, "-verify-machineinstrs", ir]
        run(command + ["-filetype=obj", "-o", obj])
    if variant == "both" and (width, segment) in ((mode, 0), (mode // 2, 0x64)):
        for dialect in ("att", "intel"):
            assembly = directory / (name + "-" + dialect + ".s")
            roundtrip = assembly.with_suffix(".o")
            if backend:
                run(command + ["-filetype=asm", "-x86-asm-syntax=" + dialect, "-o", assembly])
                run([Path(backend).parent / "llvm-mc", "-triple=" + triple, "-filetype=obj", assembly, "-o", roundtrip])
            else:
                run(compiler + ["-S", "-masm=" + dialect, "-o", assembly])
                run([args.compiler, "--target=" + triple, "-c", assembly, "-o", roundtrip])
            assert text_section(obj) == text_section(roundtrip), "assembly round trip changed bytes"
    return text_section(obj)


def decode(code, mode):
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if mode == 64 else capstone.CS_MODE_32)
    decoder.detail = True
    position = 0
    while position < len(code):
        raw = code[position:position + 15]
        cursor = 0
        while raw[cursor] in (0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65, 0x66, 0x67, 0xf2, 0xf3) or mode == 64 and 0x40 <= raw[cursor] <= 0x4f:
            cursor += 1
        # Older Capstone can recover LKGS operands from the identical LLDT
        # shape. First require the independent F2 0F 00 /6 encoding.
        if 0xf2 in raw[:cursor] and raw[cursor:cursor + 2] == b"\x0f\x00" and raw[cursor + 2] >> 3 & 7 == 6:
            copied = bytearray(raw)
            copied[cursor + 2] = (copied[cursor + 2] & ~0x38) | 0x10
            instruction = next(decoder.disasm(bytes(copied), CODE + position, count=1))
            assert instruction.mnemonic == "lldt"
            instruction = SimpleNamespace(address=instruction.address, size=instruction.size,
                                           mnemonic="lkgs", operands=instruction.operands,
                                           reg_name=instruction.reg_name, addr_size=instruction.addr_size,
                                           bytes=raw[:instruction.size])
        else:
            instruction = next(decoder.disasm(raw, CODE + position, count=1))
        yield instruction
        position += instruction.size


def accesses(code, op, bits, width, segment, mode, count):
    native = [i for i in decode(code, mode) if i.mnemonic == op]
    assert len(native) == count, "native effects were removed or merged"
    for instruction in native:
        raw, cursor = bytes(instruction.bytes), 0
        while raw[cursor] != 0x0f:
            cursor += 1
        prefixes = raw[:cursor]
        assert [p for p in prefixes if p in SEGMENTS] == ([segment] if segment else []), "segment override changed"
        assert prefixes.count(0x67) == int(width != mode), "address-size override changed"
        assert prefixes.count(0xf2) == int(op == "lkgs"), "wrong mandatory prefix"
        assert prefixes.count(0x66) == int(op != "lkgs" and bits == 16), "operand width changed"
        assert any(0x48 <= p <= 0x4f for p in prefixes) == (op != "lkgs" and bits == 64), "REX.W changed"
        assert raw[cursor:cursor + 2] == bytes((0x0f, {"lar": 2, "lsl": 3, "lkgs": 0}[op]))
        modrm = raw[cursor + 2]
        assert modrm >> 6 == 0 and (modrm & 7) in ((4, 5) if width == 16 else (0, 1, 2, 3)), "address carrier is not a data-default base"
        if op == "lkgs":
            assert modrm >> 3 & 7 == 6
        memory, = (o for o in instruction.operands if o.type == capstone.x86.X86_OP_MEM)
        assert memory.size == 2, "m16 selector was widened"
        assert instruction.addr_size == width // 8
        assert instruction.reg_name(memory.mem.base) in {16: ("si", "di"),
            32: ("eax", "ebx", "ecx", "edx"), 64: ("rax", "rbx", "rcx", "rdx")}[width]
        assert not memory.mem.index and memory.mem.disp == 0
        assert (instruction.reg_name(memory.mem.segment) if memory.mem.segment else None) == SEGMENTS[segment]
    return native


def execute(code, op, bits, width, segment, variant, mode, values, statuses, mapped=True):
    mask, address_mask, result_mask = (1 << mode) - 1, (1 << width) - 1, (1 << bits) - 1
    _, prior, seed = (v & mask for v in values)
    offset = ((0x9182736400000000 if mode == 64 else 0x81920000) | 0x205) & mask
    if width == mode:
        offset = 0x1205
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64 if mode == 64 else unicorn.UC_MODE_32)
    reg = lambda name: getattr(regs, "UC_X86_REG_" + ("R" if mode == 64 else "E") + name)
    pack = lambda v: struct.pack("<Q" if mode == 64 else "<I", v & mask)
    for base in (CODE, STACK, DATA):
        machine.mem_map(base, PAGE)
    machine.mem_write(CODE, code)
    machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
    sp = STACK + PAGE - 64 - mode // 8
    machine.reg_write(reg("SP"), sp)
    machine.mem_write(sp, pack(STOP))
    arguments = offset, prior, seed, ORDINARY
    if mode == 64:
        for name, value in zip(("CX", "DX", "8", "9"), arguments):
            machine.reg_write(reg(name), value)
    else:
        machine.mem_write(sp + 4, b"".join(pack(v) for v in arguments))
    saved = {reg(name): (0xa1b2c3d4e5f60710 + i) & mask for i, name in
             enumerate(("BX", "BP", "SI", "DI", *(("12", "13", "14", "15") if mode == 64 else ())))}
    for number, value in saved.items():
        machine.reg_write(number, value)
    count = 2 if variant == "twice" else 1
    native = accesses(code, op, bits, width, segment, mode, count)
    # Apply a diagnostic base only at the native instruction boundary. This
    # models transport, not descriptor permissions or segment-limit checks.
    base = 0x400000 if mode == 32 or segment in (0x64, 0x65) else 0
    addresses = [base + ((offset ^ (0x40 if i else 0)) & address_mask) for i in range(count)]
    if mapped:
        for page in {a & -PAGE for a in addresses}:
            machine.mem_map(page, PAGE)
        for address in addresses:
            machine.mem_write(address - 1, b"\xa5\x2b\x00\x5a")
    touched = []
    def memory_access(machine, access, address, size, value, data):
        if any(a - 1 <= address < a + 3 for a in addresses):
            touched.append((access, address, size))
    machine.hook_add(unicorn.UC_HOOK_MEM_READ | unicorn.UC_HOOK_MEM_WRITE, memory_access)
    result, start = offset ^ prior ^ seed, CODE
    if variant == "condition":
        result ^= int(prior < seed)
    for i, (instruction, success) in enumerate(zip(native, statuses)):
        machine.emu_start(start, instruction.address, count=2000)
        assert machine.reg_read(reg("IP")) == instruction.address
        memory, = (o for o in instruction.operands if o.type == capstone.x86.X86_OP_MEM)
        actual = machine.reg_read(getattr(regs, "UC_X86_REG_" + instruction.reg_name(memory.mem.base).upper()))
        assert actual == (offset ^ (0x40 if i else 0)) & address_mask, "offset wrapping changed"
        assert not touched, "selector was accessed before the native instruction"
        if not mapped:
            return
        if op != "lkgs":
            dest = instruction.operands[0]
            assert dest.type == capstone.x86.X86_OP_REG and dest.size == bits // 8
            value = ((0x00aff300 if op == "lar" else 0xf1234fff) ^ (i << 20)) & result_mask
            if success:
                machine.reg_write(getattr(regs, "UC_X86_REG_" + instruction.reg_name(dest.reg).upper()), value)
            if variant not in ("dead", "status"):
                result ^= value if success else prior & result_mask
            if variant not in ("dead", "value"):
                result ^= success << (8 + i * 8)
            flags = machine.reg_read(regs.UC_X86_REG_EFLAGS)
            machine.reg_write(regs.UC_X86_REG_EFLAGS, (flags & ~0x40) | (success << 6))
        if variant == "memory":
            assert bytes(machine.mem_read(ORDINARY, mode // 8)) == pack(seed), "store crossed the native effect"
            machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
        start = instruction.address + instruction.size
    machine.emu_start(start, STOP, count=2000)
    if variant == "memory":
        result ^= COMMON.SENTINEL & mask
    assert machine.reg_read(reg("IP")) == STOP and machine.reg_read(reg("AX")) == result & mask, "result, ZF, prior or live inputs changed"
    assert all(machine.reg_read(r) == value for r, value in saved.items()), "callee-saved register corrupted"
    assert not touched, "compiler accessed the instruction-owned selector"
    for address in addresses:
        assert bytes(machine.mem_read(address - 1, 4)) == b"\xa5\x2b\x00\x5a", "spurious selector write"


def native_oracle(args, directory):
    if args.llc or platform.system() != "Linux" or platform.machine() != "x86_64":
        return 0, 0
    queries, faults = 0, 0
    libc = ctypes.CDLL(None, use_errno=True)
    libc.mmap.argtypes = (ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_long)
    libc.mmap.restype = ctypes.c_void_p
    libc.munmap.argtypes = (ctypes.c_void_p, ctypes.c_size_t)
    libc.syscall.restype = ctypes.c_long
    # A low mapping permits a real default-segment addr32 read. GS uses a
    # separate nonzero base and an offset with deliberately different high bits.
    memory = libc.mmap(None, PAGE, 3, 0x62, -1, 0)
    assert memory != ctypes.c_void_p(-1).value and memory < (1 << 32), \
        f"low descriptor page mmap failed: {os.strerror(ctypes.get_errno())}"
    saved_gs = ctypes.c_ulong()
    assert libc.syscall(ctypes.c_long(158), ctypes.c_ulong(0x1004), ctypes.byref(saved_gs)) == 0
    try:
        assert libc.syscall(ctypes.c_long(158), ctypes.c_ulong(0x1001), ctypes.c_ulong(memory - 0x205)) == 0
        for opt in ("O0", "O2"):
            base = directory / ("native-descriptor-address-" + opt)
            src, ir, obj, library = (base.with_suffix(s) for s in (".c", ".ll", ".o", ".so"))
            body = ['unsigned short current_cs(void) { unsigned short cs; __asm__("movw %%cs, %0" : "=r"(cs)); return cs; }']
            cases, fault_cases = [], []
            for op, bits, width, segment in itertools.product(("lar", "lsl"), (16, 32, 64), (32, 64), (0, 0x65)):
                name = f"{op}{bits}_{width}_{segment:x}"
                cases.append((name, width, segment))
                ctype = {16: "unsigned short", 32: "unsigned", 64: "unsigned long long"}[bits]
                prefix = ([segment] if segment else []) + ([0x67] if width == 32 else []) + ([0x66] if bits == 16 else []) + ([0x48] if bits == 64 else [])
                encoding = prefix + [0x0f, 2 if op == "lar" else 3, 3]
                ignored = "0xf0000ULL" if op == "lar" and bits != 16 else "0"
                body += [f"int {name}(unsigned long long offset, unsigned long long prior) {{",
                         f"{ctype} expected = ({ctype})prior; unsigned char expected_zf, zf;",
                         '__asm__ volatile(".byte ' + ",".join(hex(v) for v in encoding) +
                         '; setz %1" : "+a"(expected), "=qm"(expected_zf) : "b"(offset) : "cc", "memory");',
                         f"{ctype} actual = __builtin_ia32_{op}_mem{bits}_address(offset, {width}, {segment}, prior, &zf);",
                         f"return zf != expected_zf || ((actual ^ expected) & ~(({ctype})(zf ? {ignored} : 0))); }}"]
            for width, segment in itertools.product((32, 64), SEGMENTS):
                name = f"lkgs_{width}_{segment:x}"
                fault_cases.append(name)
                encoding = ([segment] if segment else []) + ([0x67] if width == 32 else []) + [0xf2, 0x0f, 0x00, 0x33]
                body += [f"void probe_{name}(unsigned long long offset) {{ __builtin_ia32_lkgs_address(offset, {width}, {segment}); }}",
                         f'void original_{name}(unsigned long long offset) {{ __asm__ volatile(".byte ' +
                         ",".join(hex(v) for v in encoding) + '" : : "b"(offset) : "memory"); }']
            src.write_text("\n".join(body) + "\n")
            compiler = [args.compiler, "--target=x86_64-unknown-linux-gnu", "-" + opt, "-Werror", "-ffreestanding",
                        "-fno-lto", "-fno-builtin-mimalloc", "-fno-stack-protector", "-fPIC", "-mllvm", "-verify-machineinstrs", src]
            if args.backend_llc:
                run(compiler + ["-S", "-emit-llvm", "-o", ir])
                run([args.backend_llc, "-" + opt, "-verify-machineinstrs", "-relocation-model=pic", "-filetype=obj", ir, "-o", obj])
            else:
                run(compiler + ["-c", "-o", obj])
            run([args.compiler, "-shared", "-nostdlib", "-fno-lto", "-fno-builtin-mimalloc", obj, "-o", library])
            native = ctypes.CDLL(str(library.resolve()))
            native.current_cs.restype = ctypes.c_ushort
            for selector, (name, width, segment) in itertools.product((0, native.current_cs()), cases):
                ctypes.c_ushort.from_address(memory).value = selector
                offset = 0x205 if segment else memory
                if width == 32:
                    offset |= 0x9182736400000000
                function = getattr(native, name)
                function.argtypes = (ctypes.c_ulonglong, ctypes.c_ulonglong)
                function.restype = ctypes.c_int
                assert function(offset, 0xabcdef0123456789) == 0, f"{name}/{opt}: actual descriptor result/ZF/prior mismatch"
                queries += 1
            for name, offset in itertools.product(fault_cases, (memory, 0xdead000d)):
                statuses = []
                for kind in ("original", "probe"):
                    function = getattr(native, kind + "_" + name)
                    function.argtypes = (ctypes.c_ulonglong,)
                    pid = os.fork()
                    if not pid:
                        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
                        # Piped core handlers may ignore RLIMIT_CORE. These
                        # intentional native faults need no system core dump.
                        assert libc.prctl(4, 0, 0, 0, 0) == 0
                        signal.alarm(5)
                        function(offset)
                        os._exit(0)
                    _, status = os.waitpid(pid, 0)
                    statuses.append(os.waitstatus_to_exitcode(status))
                    faults += 1
                assert statuses[0] in (-signal.SIGILL, -signal.SIGSEGV) and statuses[0] == statuses[1], f"{name}/{opt}: LKGS fault mismatch {statuses}"
    finally:
        assert libc.syscall(ctypes.c_long(158), ctypes.c_ulong(0x1001), saved_gs) == 0
        libc.munmap(memory, PAGE)
    return queries, faults


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
    with tempfile.TemporaryDirectory(prefix="descriptor-address-") as temporary:
        directory = args.keep or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        # The emulation matrix grows libc's retained heap through the low 2 GB
        # mapping range on some hosts. Qualify real addr32 accesses first,
        # while a separate low page is still available; restore GS before any
        # controlled execution starts.
        queries, faults = native_oracle(args, directory)
        for mode in ((32, 64) if args.llc else (64,)):
            operations = [(op, bits) for op in ("lar", "lsl") for bits in ((16, 32, 64) if mode == 64 else (16, 32))]
            if mode == 64:
                operations.append(("lkgs", 16))
            for (op, bits), width, segment, opt in itertools.product(operations, (mode // 2, mode), SEGMENTS, ("O0", "O2")):
                variants = ("both", "value", "status", "dead", "twice", "memory", "condition") if (width, segment) in ((mode, 0), (mode // 2, 0x64)) else ("both",)
                for variant in variants:
                    code = compile_case(args, directory, op, bits, width, segment, variant, mode, opt)
                    statuses = tuple(itertools.product((0,) if op == "lkgs" else (0, 1), repeat=2 if variant == "twice" else 1))
                    for values, completions in itertools.product(COMMON.VALUES, statuses):
                        try:
                            execute(code, op, bits, width, segment, variant, mode, values, completions)
                        except Exception as error:
                            raise AssertionError(f"{op}{bits}/{width}/{segment:x}/{variant}/{mode}/{opt}/{values}/{completions}: {error}") from error
                        count += 1
                    execute(code, op, bits, width, segment, variant, mode, COMMON.VALUES[1], statuses[0], False)
                    count += 1
                    if variant == "both":
                        for target in COMMON.TRIPLES[1:]:
                            other = compile_case(args, directory, op, bits, width, segment, variant, mode, opt, target)
                            accesses(other, op, bits, width, segment, mode, 1)
                            count += 1
    print(f"PASS: {count} descriptor explicit-address encoding/completion checks; {queries} real descriptor queries; {faults} LKGS CPL3 fault boundaries")


if __name__ == "__main__":
    main()
