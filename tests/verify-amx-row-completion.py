#!/usr/bin/env python3
"""Qualify native AMX row interfaces, encoding, vector results and C ABI.

Decode AMX independently, round-trip both assembly dialects in three object
formats, and inject distinguishable 512-bit completions at each native boundary.
Unicorn executes scalar code; the harness handles AVX-512 move instructions
because Unicorn does not implement them. This checks compiler dataflow, not AMX
arithmetic, exceptions, restart, tile configuration or hardware availability.
Requires capstone, lief and unicorn. --llc checks the full LLVM fork;
--backend-llc checks NeverC IR through that fork.
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
    "amx_native", Path(__file__).with_name("verify-amx-native-completion.py"))
NATIVE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(NATIVE)
run, text_section = NATIVE.run, NATIVE.text_section
TRIPLES, GPRS = NATIVE.TRIPLES, NATIVE.GPRS
CODE, STACK, DATA, PAGE, STOP = NATIVE.CODE, NATIVE.STACK, NATIVE.DATA, NATIVE.PAGE, NATIVE.STOP
# C element, IR vector, register opcode/prefix, immediate opcode/prefix.
ROWS = {
    "tcvtrowd2ps": ("float", "<16 x float>", 0x4a, 2, 0x07, 2),
    "tcvtrowps2bf16h": ("__bf16", "<32 x bfloat>", 0x6d, 3, 0x07, 3),
    "tcvtrowps2bf16l": ("__bf16", "<32 x bfloat>", 0x6d, 2, 0x77, 2),
    "tcvtrowps2phh": ("_Float16", "<32 x half>", 0x6d, 0, 0x07, 0),
    "tcvtrowps2phl": ("_Float16", "<32 x half>", 0x6d, 1, 0x77, 3),
    "tilemovrow": ("int", "<16 x i32>", 0x4a, 1, 0x07, 1),
}
FEATURES = "amx-avx512,avx10.2"
IMMEDIATES = (0, 1, 15, 16, 31, 255)


def decode(code):
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    cs.detail = True
    rows, ordinary, offset = {}, {}, 0
    while offset < len(code):
        if code[offset] == 0x62 and offset + 6 <= len(code):
            p0, p1, p2, opcode, modrm = code[offset + 1:offset + 6]
            opmap, pp = p0 & 7, p1 & 3
            immediate = opmap == 3
            name = next((name for name, (_, _, regop, regpp, immop, immpp) in ROWS.items()
                         if (opcode, pp) == ((immop, immpp) if immediate else (regop, regpp))), None)
            if name and opmap in (2, 3) and modrm >> 6 == 3:
                assert p0 & 0x68 == 0x60 and p1 & 0x84 == 4 and p2 & 0xf7 == 0x40
                zmm = ((modrm >> 3) & 7) | ((~p0 & 0x80) >> 4) | (~p0 & 0x10)
                tile = modrm & 7
                row = code[offset + 6] if immediate else ((~p1 >> 3) & 15) | ((~p2 & 8) << 1)
                if immediate:
                    assert p1 & 0x78 == 0x78 and p2 & 8
                end = offset + (7 if immediate else 6)
                rows[offset] = (name + ("i" if immediate else ""), tile, row, zmm, end)
                offset = end
                continue
        instruction = next(cs.disasm(code[offset:], offset, count=1), None)
        assert instruction, f"unknown instruction at {offset}: {code[offset:offset + 16].hex()}"
        ordinary[offset] = instruction
        offset += instruction.size
    return rows, ordinary


def build(args, directory, stem, calls, opt, triple, live=False):
    stem += f"-{opt}-{TRIPLES.index(triple)}"
    src, ir, obj = (directory / (stem + suffix) for suffix in (".c", ".ll", ".o"))
    declarations, body, stores = set(), [], []
    if live:
        body.append("store i64 %seed, ptr %out" if args.llc else "*(unsigned long long *)out = seed;")
    for i, (api, tile, row) in enumerate(calls):
        name = api[:-1] if api.endswith("i") else api
        ctype, vtype, *_ = ROWS[name]
        if args.llc:
            declarations.add(f"declare {vtype} @llvm.x86.{api}(i8, i32)")
            argument = "%row" if row is None else str(row)
            body.append(f"%v{i} = call {vtype} @llvm.x86.{api}(i8 {tile}, i32 {argument})")
            if live:
                output = stores if live == "pressure" else body
                output += [f"%p{i} = getelementptr i8, ptr %out, i64 {64 + i * 64}",
                           f"store {vtype} %v{i}, ptr %p{i}, align 1"]
        else:
            declarations.add(f"typedef {ctype} V_{name} __attribute__((vector_size(64), aligned(1)));")
            call = f"__builtin_ia32_{api}({tile}, {'row' if row is None else row})"
            if live == "pressure":
                body.append(f"V_{name} v{i} = {call};")
                stores.append(f"*(V_{name} *)((char *)out + {64 + i * 64}) = v{i};")
            else:
                body.append(f"*(V_{name} *)((char *)out + {64 + i * 64}) = {call};" if live else f"(void){call};")
    body += stores
    if live:
        if args.llc:
            body += ["%memory = load i64, ptr %out", "%r = zext i32 %row to i64",
                     "%x = xor i64 %memory, %seed", "%y = xor i64 %x, %r", "ret i64 %y"]
        else:
            body.append("return *(unsigned long long *)out ^ seed ^ row;")
    else:
        body.append("ret void" if args.llc else "return;")
    if args.llc:
        ir.write_text("\n".join(sorted(declarations)) +
                      f"\ndefine {'i64' if live else 'void'} @probe(ptr %out, i32 %row, i64 %seed) {{\n" +
                      "\n".join(body) + "\n}\n")
    else:
        src.write_text("\n".join(sorted(declarations)) +
                       f'\n__attribute__((target("{FEATURES}")))\n' +
                       f"{'unsigned long long' if live else 'void'} probe(void *out, unsigned row, unsigned long long seed) {{\n" +
                       "\n".join(body) + "\n}\n")
        common = [args.compiler, "--target=" + triple, "-" + opt, "-Werror", "-ffreestanding",
                  "-fno-builtin-mimalloc", "-fno-lto", "-fno-stack-protector", "-mllvm", "-verify-machineinstrs", src]
        run(common + ["-S", "-emit-llvm", "-o", ir])
        assert " asm " not in ir.read_text(), "native row interface used assembly"
        if not args.backend_llc:
            run(common + ["-c", "-o", obj])
    backend = args.llc or args.backend_llc
    if backend:
        common_backend = [backend, "-mtriple=" + triple, "-" + opt, "-verify-machineinstrs",
                          "-mattr=+amx-avx512,+avx10.2", ir]
        run(common_backend + ["-filetype=obj", "-o", obj])
    if not live:
        for dialect in ("att", "intel"):
            assembly = directory / (stem + "-" + dialect + ".s")
            roundtrip = assembly.with_suffix(".o")
            if backend:
                run(common_backend + ["-filetype=asm", "-x86-asm-syntax=" + dialect, "-o", assembly])
                run([Path(backend).parent / "llvm-mc", "-triple=" + triple, "-filetype=obj", assembly, "-o", roundtrip])
            else:
                run(common + ["-S", "-masm=" + dialect, "-o", assembly])
                run([args.compiler, "--target=" + triple, "-c", assembly, "-o", roundtrip])
            assert text_section(obj) == text_section(roundtrip), "row assembly round trip changed bytes"
    return text_section(obj)


def complete(code, calls, row_value):
    rows, ordinary = decode(code)
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
    for address in (CODE, STACK, DATA):
        machine.mem_map(address, PAGE)
    machine.mem_write(CODE, code)
    seed, out = 0x81234567fedcba98, DATA + 64
    for name, value in zip(("RCX", "RDX", "R8"), (out, row_value, seed)):
        machine.reg_write(getattr(regs, "UC_X86_REG_" + name), value)
    saved = {name: NATIVE.MARKER + i for i, name in enumerate(("RBX", "RBP", "RSI", "RDI", "R12", "R13", "R14", "R15"))}
    for name, value in saved.items():
        machine.reg_write(getattr(regs, "UC_X86_REG_" + name), value)
    rsp = STACK + PAGE - 72
    machine.reg_write(regs.UC_X86_REG_RSP, rsp)
    machine.mem_write(rsp, struct.pack("<Q", STOP))
    saved_xmm = {i: ((NATIVE.MARKER + i) << 64) | (seed ^ i) for i in range(6, 16)}
    bank = {i: value.to_bytes(16, "little") + bytes(48) for i, value in saved_xmm.items()}
    for i, value in saved_xmm.items():
        machine.reg_write(getattr(regs, "UC_X86_REG_XMM" + str(i)), value)
    vectors = {offset: insn for offset, insn in ordinary.items()
               if insn.mnemonic.startswith("vmov") or insn.mnemonic == "vzeroupper" or
               (insn.mnemonic.startswith("mov") and any(
                   op.type == capstone.x86.X86_OP_REG and insn.reg_name(op.reg).startswith("xmm")
                   for op in insn.operands))}
    boundaries = rows.keys() | vectors.keys()
    machine.hook_add(unicorn.UC_HOOK_CODE, lambda uc, address, size, _: uc.emu_stop()
                     if address - CODE in boundaries else None)

    def address(insn, operand):
        mem = operand.mem
        value = lambda reg: machine.reg_read(getattr(regs, "UC_X86_REG_" + insn.reg_name(reg).upper())) if reg else 0
        return (value(mem.base) + value(mem.index) * mem.scale + mem.disp) & NATIVE.MASK

    def read_vector(insn, operand):
        if operand.type == capstone.x86.X86_OP_MEM:
            return bytes(machine.mem_read(address(insn, operand), operand.size))
        name = insn.reg_name(operand.reg)
        assert name.startswith(("xmm", "ymm", "zmm")), name
        index = int(name[3:])
        if index in bank:
            return bank[index][:operand.size]
        assert index < 16, "uninitialized extended vector register"
        return (machine.reg_read(getattr(regs, "UC_X86_REG_XMM" + str(index))).to_bytes(16, "little") + bytes(48))[:operand.size]

    def write_vector(index, value):
        bank[index] = value.ljust(64, b"\0")
        if index < 16:
            machine.reg_write(getattr(regs, "UC_X86_REG_XMM" + str(index)), int.from_bytes(value[:16], "little"))

    pc, count, expected = CODE, 0, []
    while pc != STOP:
        try:
            machine.emu_start(pc, STOP, count=10000)
        except unicorn.UcError:
            assert machine.reg_read(regs.UC_X86_REG_RIP) - CODE in boundaries
        pc = machine.reg_read(regs.UC_X86_REG_RIP)
        if pc == STOP:
            break
        offset = pc - CODE
        if offset in rows:
            api, tile, row, zmm, end = rows[offset]
            want_api, want_tile, want_row = calls[count]
            assert (api, tile) == (want_api, want_tile)
            actual_row = row if want_row is not None else machine.reg_read(getattr(regs, "UC_X86_REG_" + GPRS[row])) & 0xffffffff
            assert actual_row == (want_row if want_row is not None else row_value), "dynamic row index was truncated"
            if not count:
                assert struct.unpack("<Q", machine.mem_read(out, 8))[0] == seed
            payload = bytes((i * 37 + tile * 13 + count * 71 + actual_row) & 255 for i in range(64))
            write_vector(zmm, payload)
            expected.append(payload)
            # Expose illegal hoisting of ordinary memory across ambient AMX state.
            memory = NATIVE.MARKER ^ count
            machine.mem_write(out, struct.pack("<Q", memory))
            count += 1
        else:
            insn = vectors[offset]
            end = offset + insn.size
            if insn.mnemonic != "vzeroupper":
                assert len(insn.operands) == 2 and insn.operands[0].size in (16, 32, 64)
                dest, src = insn.operands
                payload = read_vector(insn, src)
                if dest.type == capstone.x86.X86_OP_MEM:
                    machine.mem_write(address(insn, dest), payload)
                else:
                    name = insn.reg_name(dest.reg)
                    assert name.startswith(("xmm", "ymm", "zmm")), name
                    write_vector(int(name[3:]), payload)
        pc = CODE + end
        machine.reg_write(regs.UC_X86_REG_RIP, pc)
    assert count == len(calls), "native row reads were removed or merged"
    for i, payload in enumerate(expected):
        assert bytes(machine.mem_read(out + 64 + i * 64, 64)) == payload, "512-bit row result was corrupted"
    assert machine.reg_read(regs.UC_X86_REG_RAX) == memory ^ seed ^ row_value
    for name, value in saved.items():
        assert machine.reg_read(getattr(regs, "UC_X86_REG_" + name)) == value, name
    for index, value in saved_xmm.items():
        assert int.from_bytes(bank[index][:16], "little") == value, "Win64 nonvolatile XMM corrupted"


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
    with tempfile.TemporaryDirectory(prefix="amx-rows-") as temp:
        directory = args.keep or Path(temp)
        directory.mkdir(parents=True, exist_ok=True)
        matrix = [(name, tile, None) for name in ROWS for tile in range(8)]
        matrix += [(name + "i", tile, row) for name in ROWS for tile in range(8) for row in IMMEDIATES]
        count = 0
        for opt, triple in itertools.product(("O0", "O2"), TRIPLES):
            code = build(args, directory, "encoding", matrix, opt, triple)
            decoded = list(decode(code)[0].values())
            assert len(decoded) == len(matrix)
            for (api, tile, row, _, _), (want_api, want_tile, want_row) in zip(decoded, matrix):
                assert (api, tile) == (want_api, want_tile)
                assert want_row is None or row == want_row
            count += len(matrix)
        for api, opt in itertools.product([n + s for n in ROWS for s in ("", "i")], ("O0", "O2")):
            calls = [(api, 7, 255 if api.endswith("i") else None)] * 2
            code = build(args, directory, api, calls, opt, TRIPLES[0], live=True)
            for row in (0, 15, 16, 0x80000010, 0xffffffff):
                complete(code, calls, row)
                count += 1
        for opt in ("O0", "O2"):
            calls = [("tilemovrow", i % 8, None) for i in range(32)]
            code = build(args, directory, "pressure", calls, opt, TRIPLES[0], live="pressure")
            complete(code, calls, 0x80000010)
            count += 1
        for dialect, triple in itertools.product(("att", "intel"), TRIPLES):
            source = directory / f"registers-{dialect}-{TRIPLES.index(triple)}.s"
            obj = source.with_suffix(".o")
            cases = [(name, tile, (zmm + tile) % 32, zmm) for name in ROWS for tile in range(8) for zmm in range(32)]
            cases += [(name + "i", row % 8, row, row % 32) for name in ROWS for row in range(256)]
            lines = [".text", ".intel_syntax noprefix" if dialect == "intel" else ".att_syntax"]
            for api, tile, row, zmm in cases:
                immediate = api.endswith("i")
                name = api[:-1] if immediate else api
                value = str(row) if immediate else ("r" + str(row) + "d" if row >= 8 else ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi")[row])
                operands = f"zmm{zmm}, tmm{tile}, {value}" if dialect == "intel" else f"{'$' if immediate else '%'}{value}, %tmm{tile}, %zmm{zmm}"
                lines.append(name + " " + operands)
            source.write_text("\n".join(lines) + "\n")
            backend = args.llc or args.backend_llc
            if backend:
                run([Path(backend).parent / "llvm-mc", "-triple=" + triple, "-filetype=obj", source, "-o", obj])
            else:
                run([args.compiler, "--target=" + triple, "-c", source, "-o", obj])
            decoded = list(decode(text_section(obj))[0].values())
            assert [v[:4] for v in decoded] == cases, "extended row/ZMM register encoding mismatch"
            count += len(cases)
        print(f"PASS: {count} native AMX row encoding/completion checks")


if __name__ == "__main__":
    main()
