#!/usr/bin/env python3
"""Qualify native AMX FP8, MOVRS and historical TF32 compiler interfaces.

Check every distinct compute-register triple, all load destinations, both
assembly dialects and three object formats. Controlled instruction completions
check full pointers/strides, persistent tile-bank flow, ordinary memory ordering
and the C ABI. They do not emulate AMX arithmetic or qualify hardware faults,
restart, cache hints, or tile configuration legality.
Requires capstone, lief and unicorn. --llc checks the full LLVM fork;
--backend-llc checks NeverC IR through that fork.
"""

import argparse
import itertools
import re
import struct
import subprocess
import tempfile
from pathlib import Path

import capstone
import lief
import unicorn
import unicorn.x86_const as regs


COMPUTE = {"tdpbf8ps": (5, 0, 0xfd), "tdpbhf8ps": (5, 3, 0xfd),
           "tdphbf8ps": (5, 2, 0xfd), "tdphf8ps": (5, 1, 0xfd),
           "tmmultf32ps": (2, 1, 0x48)}
LOADS = {"tileloaddrs64": "tileloaddrs", "tileloaddrst164": "tileloaddrst1"}
FEATURES = "amx-tile,amx-fp8,amx-movrs,amx-tf32"
TRIPLES = ("x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu", "x86_64-apple-darwin")
GPRS = ("RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI",
        "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15")
CODE, STACK, DATA, PAGE = 0x10000, 0x80000, 0xa0000, 0x1000
STOP, MASK = CODE + PAGE - 1, (1 << 64) - 1
MARKER = 0xa1b2c3d4e5f60718


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(f"command failed: {command}\n{result.stderr}")
    return result.stdout


def text_section(path):
    binary = lief.parse(str(path))
    return bytes(next(s for s in binary.sections if s.name in (".text", "__text") and len(s.content)).content)


def decode(code):
    """Decode the documented AMX fields, using Capstone only for other code."""
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    result = {}
    offset = 0
    while offset < len(code):
        start = offset
        prefix = code[offset]
        vex = prefix == 0xc4 and offset + 5 <= len(code)
        evex = prefix == 0x62 and offset + 6 <= len(code)
        if vex or evex:
            p0, p1 = code[offset + 1:offset + 3]
            op_at = offset + (3 if vex else 4)
            op, modrm = code[op_at:op_at + 2]
            opmap, pp = p0 & (31 if vex else 7), p1 & 3
            arithmetic = next((n for n, encoding in COMPUTE.items()
                               if encoding == (opmap, pp, op) and vex), None)
            memory = opmap == 2 and op in (0x4a, 0x4b) and modrm >> 6 != 3
            zero = vex and opmap == 2 and pp == 3 and op == 0x49 and modrm >> 6 == 3
            if arithmetic:
                assert p0 == 0xe0 | opmap and p1 & 0x84 == 0
                assert modrm >> 6 == 3
                operands = ((modrm >> 3) & 7, modrm & 7, (~p1 >> 3) & 7)
                offset = op_at + 2
                result[start] = (arithmetic, operands, offset)
                continue
            if zero:
                assert p0 == 0xe2 and p1 == 0x7b and modrm & 7 == 0
                offset = op_at + 2
                result[start] = ("tilezero", ((modrm >> 3) & 7,), offset)
                continue
            if memory:
                assert modrm & 7 == 4, "AMX tile memory requires SIB"
                assert pp in (1, 2, 3) and p1 & (0xfc if vex else 0xf8) == 0x78
                sib = code[op_at + 2]
                assert sib >> 6 == 0, "stride must not be scaled"
                base = (sib & 7) | ((~p0 & 0x20) >> 2)
                index = ((sib >> 3) & 7) | ((~p0 & 0x40) >> 3)
                if evex:
                    assert code[start + 3] == 8
                    base |= (p0 & 8) << 1
                    index |= (~p1 & 4) << 2
                offset = op_at + 3
                displacement = 0
                mod = modrm >> 6
                if mod == 1:
                    displacement = struct.unpack_from("b", code, offset)[0]
                    offset += 1
                elif mod == 2 or (mod == 0 and base & 7 == 5):
                    displacement = struct.unpack_from("<i", code, offset)[0]
                    offset += 4
                    if mod == 0:
                        base = None
                name = ({1: "tileloaddt1", 2: "tilestored", 3: "tileloadd"} if op == 0x4b
                        else {1: "tileloaddrst1", 3: "tileloaddrs"})[pp]
                result[start] = (name, ((modrm >> 3) & 7, base, index, displacement), offset)
                continue
        instruction = next(cs.disasm(code[offset:], offset, count=1), None)
        if instruction is None:
            raise AssertionError(f"unknown instruction at {offset}: {code[offset:offset + 16].hex()}")
        offset += instruction.size
    return result


def build(args, directory, name, calls, opt, triple, variant=None, egpr=False):
    stem = f"{name}-{opt}-{TRIPLES.index(triple)}" + ("-egpr" if egpr else "")
    src, ir, obj = (directory / (stem + suffix) for suffix in (".c", ".ll", ".o"))
    features = FEATURES + (",egpr" if egpr else "")
    declarations, body = set(), []
    if variant:
        body.append("store i64 %seed, ptr %out" if args.llc else "*(unsigned long long *)out = seed;")
    for api, operands in calls:
        memory = api.startswith("tileload") or api == "tilestored64"
        if args.llc:
            types = ("i8", "ptr", "i64") if memory else ("i8",) * len(operands)
            declarations.add(f"declare void @llvm.x86.{api}({', '.join(types)})")
            values = ["%" + str(v) if isinstance(v, str) else str(v) for v in operands]
            body.append(f"call void @llvm.x86.{api}({', '.join(t + ' ' + v for t, v in zip(types, values))})")
        else:
            body.append(f"__builtin_ia32_{api}({', '.join(map(str, operands))});")
    if variant:
        if args.llc:
            body += ["%memory = load i64, ptr %out", "%p = ptrtoint ptr %source to i64",
                     "%q = ptrtoint ptr %out to i64", "%r0 = xor i64 %seed, %p",
                     "%r1 = xor i64 %r0, %q", "%r2 = xor i64 %r1, %stride",
                     "%r3 = xor i64 %r2, %memory", "ret i64 %r3"]
        else:
            body.append("return seed ^ (unsigned long long)source ^ stride ^ (unsigned long long)out ^ *(unsigned long long *)out;")
    else:
        body.append("ret void" if args.llc else "return;")
    if args.llc:
        ty = "i64" if variant else "void"
        ir.write_text("\n".join(sorted(declarations)) +
                      f"\ndefine {ty} @probe(ptr %source, i64 %stride, ptr %out, i64 %seed) {{\n" +
                      "\n".join(body) + "\n}\n")
    else:
        ty = "unsigned long long" if variant else "void"
        src.write_text(f'__attribute__((target("{features}")))\n{ty} probe(const void *source, unsigned long long stride, void *out, unsigned long long seed) {{\n' +
                       "\n".join(body) + "\n}\n")
        common = [args.compiler, "--target=" + triple, "-" + opt, "-Werror", "-ffreestanding",
                  "-fno-builtin-mimalloc", "-fno-lto", "-fno-stack-protector", "-mllvm", "-verify-machineinstrs", src]
        run(common + ["-S", "-emit-llvm", "-o", ir])
        if " asm " in ir.read_text():
            raise AssertionError("native AMX API used inline assembly")
        if not args.backend_llc:
            run(common + ["-c", "-o", obj])
    backend = args.llc or args.backend_llc
    if backend:
        backend_common = [backend, "-mtriple=" + triple, "-" + opt, "-verify-machineinstrs",
                          "-mattr=" + ",".join("+" + f for f in features.split(",")), ir]
        run(backend_common + ["-filetype=obj", "-o", obj])
    # Round trips use NeverC's own assembler when checking NeverC codegen.
    # The full fork uses its matching llvm-mc.
    if not variant:
        for dialect in ("att", "intel"):
            assembly = directory / (stem + "-" + dialect + ".s")
            roundtrip = assembly.with_suffix(".o")
            if backend:
                run(backend_common + ["-filetype=asm", "-x86-asm-syntax=" + dialect, "-o", assembly])
                run([Path(backend).parent / "llvm-mc", "-triple=" + triple, "-filetype=obj", assembly, "-o", roundtrip])
            else:
                run(common + ["-S", "-masm=" + dialect, "-o", assembly])
                run([args.compiler, "--target=" + triple, "-c", assembly, "-o", roundtrip])
            assert text_section(roundtrip) == text_section(obj), "assembly round trip changed native code"
    return text_section(obj)


def complete(code, calls, source, stride):
    instructions = decode(code)
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
    for address in (CODE, STACK, DATA):
        machine.mem_map(address, PAGE)
    machine.mem_write(CODE, code)
    seed, out = 0x81234567fedcba98, DATA + 256
    arguments = (source, stride, out, seed)
    for name, value in zip(("RCX", "RDX", "R8", "R9"), arguments):
        machine.reg_write(getattr(regs, "UC_X86_REG_" + name), value)
    saved = {name: MARKER + i for i, name in enumerate(("RBX", "RBP", "RSI", "RDI", "R12", "R13", "R14", "R15"))}
    for name, value in saved.items():
        machine.reg_write(getattr(regs, "UC_X86_REG_" + name), value)
    rsp = STACK + PAGE - 72
    machine.reg_write(regs.UC_X86_REG_RSP, rsp)
    machine.mem_write(rsp, struct.pack("<Q", STOP))
    machine.hook_add(unicorn.UC_HOOK_CODE, lambda uc, address, size, _: uc.emu_stop()
                     if address - CODE in instructions else None)
    bank = [MARKER ^ i for i in range(8)]
    pc, count = CODE, 0
    while pc != STOP:
        try:
            machine.emu_start(pc, STOP, count=10000)
        except unicorn.UcError:
            if machine.reg_read(regs.UC_X86_REG_RIP) - CODE not in instructions:
                raise
        pc = machine.reg_read(regs.UC_X86_REG_RIP)
        if pc == STOP:
            break
        if count >= len(calls) or pc - CODE not in instructions:
            raise AssertionError("unexpected execution boundary")
        name, operands, end = instructions[pc - CODE]
        api, expected = calls[count]
        assert name == LOADS.get(api, api.removesuffix("64")), (name, api)
        if not count:
            assert struct.unpack("<Q", machine.mem_read(out, 8))[0] == seed
        if name in COMPUTE:
            assert operands == expected
            # Unique injected completion depends on all three incoming tiles.
            d, a, b = operands
            bank[d] = ((bank[d] * 3) ^ (bank[a] * 5) ^ (bank[b] * 7)) & MASK
        elif name == "tilezero":
            assert operands == expected
            bank[operands[0]] = 0
        else:
            tile, base, index, displacement = operands
            value = lambda r: machine.reg_read(getattr(regs, "UC_X86_REG_" + GPRS[r]))
            pointer = ((value(base) if base is not None else 0) + displacement) & MASK
            assert tile == expected[0]
            assert pointer == (out if expected[1] == "out" else source)
            assert value(index) == stride, "truncated native tile stride"
            if name == "tilestored":
                machine.mem_write(out, struct.pack("<Q", bank[tile]))
            else:
                bank[tile] = MARKER ^ source ^ stride
        # Native ambient effects must prevent C loads moving before the call.
        memory = bank[operands[0]] if name == "tilestored" else MARKER ^ count
        machine.mem_write(out, struct.pack("<Q", memory))
        count += 1
        pc = CODE + end
        machine.reg_write(regs.UC_X86_REG_RIP, pc)
    assert count == len(calls), "AMX operations were removed or merged"
    assert machine.reg_read(regs.UC_X86_REG_RAX) == seed ^ source ^ stride ^ out ^ memory
    for name, value in saved.items():
        assert machine.reg_read(getattr(regs, "UC_X86_REG_" + name)) == value, name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path("build-neverc/bin/neverc"))
    backend = parser.add_mutually_exclusive_group()
    backend.add_argument("--llc", type=Path)
    backend.add_argument("--backend-llc", type=Path)
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    for name in ("compiler", "llc", "backend_llc"):
        if getattr(args, name):
            setattr(args, name, getattr(args, name).resolve())
    with tempfile.TemporaryDirectory(prefix="amx-native-") as temp:
        directory = args.keep or Path(temp)
        directory.mkdir(parents=True, exist_ok=True)
        matrix = [(name, triple) for name in COMPUTE for triple in itertools.permutations(range(8), 3)]
        matrix += [(name, (tile, "source", "stride")) for name in LOADS for tile in range(8)]
        count = 0
        for opt, triple in itertools.product(("O0", "O2"), TRIPLES):
            code = build(args, directory, "encoding", matrix, opt, triple)
            decoded = list(decode(code).values())
            assert len(decoded) == len(matrix)
            for (name, operands, _), (api, expected) in zip(decoded, matrix):
                assert name == LOADS.get(api, api)
                assert operands == expected if api in COMPUTE else operands[0] == expected[0]
            count += len(matrix)
        for api, opt, variant in itertools.product((*COMPUTE, *LOADS), ("O0", "O2"), ("single", "twice", "bank")):
            operands = (0, 1, 2) if api in COMPUTE else (3, "source", "stride")
            calls = [(api, operands)] * (2 if variant == "twice" else 1)
            if variant == "bank":
                if api in COMPUTE:
                    calls = [("tilezero", (0,)), ("tileloadd64", (1, "source", "stride")),
                             ("tileloadd64", (2, "source", "stride"))] + calls
                calls += [("tilestored64", (operands[0], "out", "stride"))]
            code = build(args, directory, api + "-" + variant, calls, opt, TRIPLES[0], variant)
            for source, stride in ((DATA + 13, 64), (0x123456780000000d, 0x8765432100000040), (MASK, MASK)):
                complete(code, calls, source, stride)
                count += 1
        for opt, triple in itertools.product(("O0", "O2"), TRIPLES):
            calls = [(name, (7, "source", "stride")) for name in LOADS]
            code = build(args, directory, "evex", calls, opt, triple, egpr=True)
            decoded = decode(code)
            # EGPR enables extended addressing; low registers may still use VEX.
            assert len(decoded) == 2
            count += 2
        for dialect, triple in itertools.product(("att", "intel"), TRIPLES):
            source = directory / f"extended-{dialect}-{TRIPLES.index(triple)}.s"
            obj = source.with_suffix(".o")
            cases = [(name, tile, base, index) for name in LOADS.values() for tile in range(8)
                     for base, index in ((0, 1), (8, 15), (16, 17), (20, 21), (31, 30))]
            lines = [".text", ".intel_syntax noprefix" if dialect == "intel" else ".att_syntax"]
            for name, tile, base, index in cases:
                a, b = ("r" + str(r) if r >= 16 else GPRS[r].lower() for r in (base, index))
                operands = f"tmm{tile}, [{a} + {b}]" if dialect == "intel" else f"(%{a},%{b}), %tmm{tile}"
                lines.append("{evex} " + name + " " + operands)
            source.write_text("\n".join(lines) + "\n")
            backend = args.llc or args.backend_llc
            if backend:
                run([Path(backend).parent / "llvm-mc", "-triple=" + triple, "-filetype=obj", source, "-o", obj])
            else:
                run([args.compiler, "--target=" + triple, "-c", source, "-o", obj])
            code = text_section(obj)
            decoded = decode(code)
            assert len(decoded) == len(cases)
            for (offset, (name, operands, _)), (expected, tile, base, index) in zip(decoded.items(), cases):
                assert code[offset] == 0x62 and name == expected
                assert operands == (tile, base, index, 0)
            count += len(cases)
        print(f"PASS: {count} native AMX encoding/completion checks")


if __name__ == "__main__":
    main()
