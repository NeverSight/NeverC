#!/usr/bin/env python3
"""Check native LAR/LSL operands, ZF, failure values, ordering and C ABI.

Controlled completions qualify compiler transport, not a simulated descriptor
table. Memory selectors remain instruction-owned, including unmapped boundary
cases. On Linux x86-64, NeverC and bridge routes additionally compare real user
mode queries with an independent inline-assembly oracle (null and current CS
selectors). That oracle is test code; the builtin probes must contain no asm.
Requires capstone, lief and unicorn. --llc checks both code modes;
--backend-llc checks NeverC IR through the full LLVM fork.
"""

import argparse
import ctypes
import itertools
import platform
import re
import struct
import subprocess
import tempfile
from pathlib import Path

import capstone
import lief
import unicorn
import unicorn.x86_const as regs

CODE, STACK, DATA, PAGE = 0x10000, 0x80000, 0xa0000, 0x1000
STOP, SOURCE, ORDINARY = CODE + PAGE - 1, DATA + 13, DATA + 128
SENTINEL = 0xb1c2d3e4f5a68798
VALUES = ((0, 0, 0), (0xd012345689ab002b, 0xaabbccdd12345678, 0x91726354aabbccdd),
          (0xffffffffffffffff, 0xffffffffffffffff, 0x8000000080000000))
TRIPLES = ("pc-windows-msvc", "unknown-linux-gnu", "apple-darwin")


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(f"command failed: {command}\n{result.stderr}")
    return result.stdout


def text_section(path):
    image = lief.parse(str(path))
    return bytes(next(s for s in image.sections if s.name in (".text", "__text") and len(s.content)).content)


def compile_case(args, directory, operation, source, width, variant, mode, opt, target=TRIPLES[0]):
    name = f"{operation}_{source}{width}_{variant}_{mode}_{opt}-{TRIPLES.index(target)}"
    ir, obj = directory / (name + ".ll"), directory / (name + ".obj")
    legacy, memory = source == "legacy", source == "mem"
    word, narrow = f"i{mode}", f"i{width}"
    api = "llvm.x86." + operation + ("" if legacy else f".{source}{width}")
    ty = narrow if legacy else "{ " + narrow + ", i8 }"
    first = "ptr" if memory else ("i32" if legacy else "i16")
    count = 2 if variant == "twice" else 1
    keep_value = variant not in ("dead", "status")
    keep_status = not legacy and variant not in ("dead", "value")
    triple = ("x86_64" if mode == 64 else "i686") + "-" + target
    if args.llc:
        body = [f"store {word} %seed, ptr %ordinary"] if variant == "memory" else []
        terms = ["%address" if memory else "%field", "%prior", "%seed"]
        if memory:
            body.append(f"%address = ptrtoint ptr %field to {word}")
            selector = "%field"
        elif first != word:
            body.append(f"%selector = trunc {word} %field to {first}")
            selector = "%selector"
        else:
            selector = "%field"
        prior = "%prior"
        if width != mode:
            body.append(f"%prior_value = trunc {word} %prior to {narrow}")
            prior = "%prior_value"
        for i in range(count):
            parameters = f"{first} {selector}" + ("" if legacy else f", {narrow} {prior}")
            body.append(f"%call{i} = call {ty} @{api}({parameters})")
            value = f"%call{i}"
            if not legacy:
                body += [f"%value{i} = extractvalue {ty} %call{i}, 0",
                         f"%status{i} = extractvalue {ty} %call{i}, 1"]
                value = f"%value{i}"
            if keep_value:
                if width != mode:
                    body.append(f"%wide_value{i} = zext {narrow} {value} to {word}")
                    value = f"%wide_value{i}"
                terms.append(value)
            if keep_status:
                body += [f"%wide_status{i} = zext i8 %status{i} to {word}",
                         f"%flag{i} = shl {word} %wide_status{i}, {8 + i * 8}"]
                terms.append(f"%flag{i}")
        if variant == "memory":
            body.append(f"%memory = load {word}, ptr %ordinary")
            terms.append("%memory")
        result = terms[0]
        for i, term in enumerate(terms[1:]):
            body.append(f"%xor{i} = xor {word} {result}, {term}")
            result = f"%xor{i}"
        body.append(f"ret {word} {result}")
        parameters = first + ("" if legacy else ", " + narrow)
        ir.write_text(f"declare {ty} @{api}({parameters})\n"
                      f"define {word} @probe({'ptr' if memory else word} %field, {word} %prior, ptr %ordinary, {word} %seed) {{\n" +
                      "\n".join(body) + "\n}\n")
    else:
        src = directory / (name + ".c")
        body = ["*ordinary = seed;"] if variant == "memory" else []
        terms = ["(unsigned long long)field" if memory else "field", "prior", "seed"]
        for i in range(count):
            body.append(f"unsigned char status{i};")
            call = "__segmentlimit((unsigned int)field)" if legacy else f"__builtin_ia32_{operation}_{source}{width}(field, prior, &status{i})"
            body.append(f"unsigned long long value{i} = {call};")
            if keep_value:
                terms.append(f"value{i}")
            if keep_status:
                terms.append(f"((unsigned long long)status{i} << {8 + i * 8})")
        if variant == "memory":
            terms.append("*ordinary")
        body.append("return " + " ^ ".join(terms) + ";")
        src.write_text(f"unsigned long long probe({'const void *' if memory else 'unsigned long long '}field, "
                       "unsigned long long prior, unsigned long long *ordinary, unsigned long long seed) {\n" +
                       "\n".join(body) + "\n}\n")
        common = [args.compiler, "--target=" + triple, "-" + opt, "-Werror", "-ffreestanding",
                  "-fno-builtin-mimalloc", "-fno-lto", "-fno-stack-protector", "-mllvm", "-verify-machineinstrs", src]
        if legacy:
            common.append("-fms-extensions")
        run(common + ["-S", "-emit-llvm", "-o", ir])
        assert " asm " not in ir.read_text() and re.search(r"\bcall\b[^@\n]*@" + re.escape(api) + r"\(", ir.read_text())
        if not args.backend_llc:
            run(common + ["-c", "-o", obj])
    backend = args.llc or args.backend_llc
    if backend:
        common_backend = [backend, "-mtriple=" + triple, "-" + opt, "-verify-machineinstrs", ir]
        run(common_backend + ["-filetype=obj", "-o", obj])
    if variant == "both":
        for dialect in ("att", "intel"):
            assembly = directory / (name + "-" + dialect + ".s")
            roundtrip = assembly.with_suffix(".o")
            if backend:
                run(common_backend + ["-filetype=asm", "-x86-asm-syntax=" + dialect, "-o", assembly])
                run([Path(backend).parent / "llvm-mc", "-triple=" + triple, "-filetype=obj", assembly, "-o", roundtrip])
            else:
                run(common + ["-S", "-masm=" + dialect, "-o", assembly])
                run([args.compiler, "--target=" + triple, "-c", assembly, "-o", roundtrip])
            assert text_section(obj) == text_section(roundtrip), "descriptor query assembly round trip changed bytes"
    return text_section(obj)


def execute(code, operation, source, width, variant, mode, values, statuses, mapped=True):
    legacy, memory = source == "legacy", source == "mem"
    mask, result_mask = (1 << mode) - 1, (1 << width) - 1
    field, prior, seed = (value & mask for value in values)
    if memory:
        field = SOURCE if mapped else (seed | (1 << (mode - 1)) | 13)
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64 if mode == 64 else unicorn.UC_MODE_32)
    reg = lambda name: getattr(regs, "UC_X86_REG_" + ("R" if mode == 64 else "E") + name)
    pack = lambda value: struct.pack("<Q" if mode == 64 else "<I", value & mask)
    for base in (CODE, STACK, DATA):
        machine.mem_map(base, PAGE)
    machine.mem_write(CODE, code)
    machine.mem_write(SOURCE, struct.pack("<H", 0x2b))
    machine.mem_write(ORDINARY, pack(SENTINEL))
    stack_pointer = STACK + PAGE - 64 - mode // 8
    machine.reg_write(reg("SP"), stack_pointer)
    machine.mem_write(stack_pointer, pack(STOP))
    arguments = (field, prior, ORDINARY, seed)
    if mode == 64:
        for r, v in zip(("CX", "DX", "8", "9"), arguments):
            machine.reg_write(reg(r), v)
    else:
        machine.mem_write(stack_pointer + 4, b"".join(pack(v) for v in arguments))
    saved = {reg(name): (0xa1b2c3d4e5f60710 + i) & mask
             for i, name in enumerate(("BX", "BP", "SI", "DI", *(("12", "13", "14", "15") if mode == 64 else ())))}
    for r, v in saved.items():
        machine.reg_write(r, v)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if mode == 64 else capstone.CS_MODE_32)
    decoder.detail = True
    instructions = [i for i in decoder.disasm(code, CODE) if i.mnemonic == operation]
    assert len(instructions) == len(statuses), "descriptor reads were removed or merged"
    start, result = CODE, field ^ prior ^ seed
    for i, (instruction, success) in enumerate(zip(instructions, statuses)):
        machine.emu_start(start, instruction.address, count=1000)
        assert machine.reg_read(reg("IP")) == instruction.address
        dest, selector = instruction.operands
        register = lambda op: getattr(regs, "UC_X86_REG_" + instruction.reg_name(op.reg).upper())
        assert dest.type == capstone.x86.X86_OP_REG and dest.size == width // 8
        if memory:
            assert selector.type == capstone.x86.X86_OP_MEM, "selector was eagerly loaded"
            mem = selector.mem
            value = lambda r: machine.reg_read(getattr(regs, "UC_X86_REG_" + instruction.reg_name(r).upper())) if r else 0
            address = (value(mem.base) + value(mem.index) * mem.scale + mem.disp) & mask
            assert not mem.segment and address == field, "selector address changed"
            if not mapped:
                return
            assert bytes(machine.mem_read(SOURCE, 2)) == struct.pack("<H", 0x2b)
        else:
            assert selector.type == capstone.x86.X86_OP_REG and machine.reg_read(register(selector)) & 0xffff == field & 0xffff
        native_value = ((0x00aff300 if operation == "lar" else 0xf1234fff) ^ (i << 20)) & result_mask
        if success:
            machine.reg_write(register(dest), native_value)
        if variant not in ("dead", "status"):
            assert success or not legacy, "legacy failure value is unspecified"
            result ^= native_value if success else prior & result_mask
        if not legacy and variant not in ("dead", "value"):
            result ^= success << (8 + i * 8)
        if variant == "memory":
            assert bytes(machine.mem_read(ORDINARY, mode // 8)) == pack(seed)
            machine.mem_write(ORDINARY, pack(SENTINEL))
            result ^= SENTINEL & mask
        flags = machine.reg_read(regs.UC_X86_REG_EFLAGS)
        machine.reg_write(regs.UC_X86_REG_EFLAGS, (flags & ~0x40) | (success << 6))
        start = instruction.address + instruction.size
    machine.emu_start(start, STOP, count=1000)
    assert machine.reg_read(reg("IP")) == STOP and machine.reg_read(reg("AX")) == result, "descriptor result/ZF/prior/live input changed"
    assert all(machine.reg_read(r) == v for r, v in saved.items()), "callee-saved register corrupted"


def native_oracle(args, directory):
    if args.llc or platform.system() != "Linux" or platform.machine() != "x86_64":
        return 0
    src, ir, obj, library = (directory / ("native-oracle" + suffix) for suffix in (".c", ".ll", ".o", ".so"))
    body = ['int probe(void) { unsigned short selector[2] = {0, 0};',
            '__asm__ volatile("movw %%cs, %0" : "=r"(selector[1]));']
    count = 0
    for operation, source, (width, ctype) in itertools.product(("lar", "lsl"), ("reg", "mem"),
                                                             ((16, "unsigned short"), (32, "unsigned"), (64, "unsigned long long"))):
        for s in (0, 1):
            count += 1
            form = {16: "w", 32: "k", 64: "q"}[width]
            source_constraint = '"r"(selector[' + str(s) + '])' if source == "reg" else '"m"(selector[' + str(s) + '])'
            operand = "%w2" if source == "reg" else "%2"
            argument = f"selector[{s}]" if source == "reg" else f"&selector[{s}]"
            ignored = "0xf0000ULL" if operation == "lar" and width != 16 else "0"
            body += [f"{{ {ctype} prior = ({ctype})0x9182736455aa1234ULL, expected = prior; unsigned char zf, expected_zf;",
                     f'__asm__ volatile("{operation} {operand}, %{form}0; sete %1" : "+r"(expected), "=qm"(expected_zf) : {source_constraint} : "cc", "memory");',
                     f"{ctype} actual = __builtin_ia32_{operation}_{source}{width}({argument}, prior, &zf);",
                     f"if (zf != expected_zf || ((actual ^ expected) & ~(({ctype})(zf ? {ignored} : 0)))) return {count}; }}"]
    body += ['unsigned expected; __asm__ volatile("lsll %1, %0" : "=r"(expected) : "r"((unsigned)selector[1]) : "cc", "memory");',
             f"if (__segmentlimit(selector[1]) != expected) return {count + 1};", "return 0; }"]
    src.write_text("\n".join(body) + "\n")
    common = [args.compiler, "--target=x86_64-unknown-linux-gnu", "-O2", "-Werror", "-fno-lto", "-fms-extensions",
              "-fno-builtin-mimalloc", "-ffreestanding", "-fPIC", "-mllvm", "-verify-machineinstrs", src]
    if args.backend_llc:
        run(common + ["-S", "-emit-llvm", "-o", ir])
        run([args.backend_llc, "-O2", "-verify-machineinstrs", "-relocation-model=pic", "-filetype=obj", ir, "-o", obj])
    else:
        run(common + ["-c", "-o", obj])
    run([args.compiler, "-shared", "-nostdlib", "-fno-lto", "-fno-builtin-mimalloc", obj, "-o", library])
    probe = ctypes.CDLL(str(library.resolve())).probe
    probe.restype = ctypes.c_int
    assert probe() == 0, "native descriptor query disagrees with independent hardware oracle"
    return count + 1


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
    with tempfile.TemporaryDirectory(prefix="descriptor-query-") as temp:
        directory = args.keep or Path(temp)
        directory.mkdir(parents=True, exist_ok=True)
        for mode in ((32, 64) if args.llc else (64,)):
            queries = [(operation, source, width) for operation in ("lar", "lsl") for source in ("reg", "mem")
                       for width in ((16, 32, 64) if mode == 64 else (16, 32))] + [("lsl", "legacy", 32)]
            for (operation, source, width), opt, variant in itertools.product(queries, ("O0", "O2"),
                                                                             ("value", "status", "both", "dead", "twice", "memory")):
                code = compile_case(args, directory, operation, source, width, variant, mode, opt)
                status_values = (0, 1) if source != "legacy" or variant == "dead" else (1,)
                for values, statuses in itertools.product(VALUES, itertools.product(status_values, repeat=2 if variant == "twice" else 1)):
                    try:
                        execute(code, operation, source, width, variant, mode, values, statuses)
                    except Exception as error:
                        raise AssertionError(f"{operation}, {source}{width}, {variant}, {mode}, {opt}, {values}, {statuses}: {error}") from error
                    count += 1
                if source == "mem":
                    execute(code, operation, source, width, variant, mode, VALUES[1], (0,) * (2 if variant == "twice" else 1), False)
                    count += 1
                if variant == "both":
                    for triple in TRIPLES[1:]:
                        compile_case(args, directory, operation, source, width, variant, mode, opt, triple)
                        count += 1
        native = native_oracle(args, directory)
    print(f"PASS: {count} descriptor query encoding/completion checks; {native} real user-mode oracle cases")


if __name__ == "__main__":
    main()
