#!/usr/bin/env python3
"""Check native MPX encoding, operand transport and ABI without MPX hardware.

MPX instructions are inspected and skipped at controlled boundaries. This is
not a processor emulator: enable state, bounds tables and faults are separately
tested by verify-mpx-state.py. Requires Unicorn and the full fork's objdump/mc.
"""

import argparse
import importlib.util
from pathlib import Path
import re
import struct
import subprocess
import tempfile

import unicorn
import unicorn.x86_const as regs


SPEC = importlib.util.spec_from_file_location("mpx", Path(__file__).with_name("verify-mpx-state.py"))
MPX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MPX)
TRIPLES = ("x86_64-unknown-linux-gnu", "x86_64-pc-windows-msvc", "x86_64-apple-darwin")
CODE, STACK, STOP = 0x100000, 0x200000, 0x300000
PAGE = 0x10000
GPRS = ("RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI",
        "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15")
NATIVE_PREFIX = {"bndmk": 0xf3, "bndmov_reg": 0x66, "bndmov_load": 0x66,
                 "bndmov_store": 0x66, "bndmov_store_lock": 0x66,
                 "bndcl_reg": 0xf3, "bndcu_reg": 0xf2, "bndcn_reg": 0xf2,
                 "bndcl_address": 0xf3, "bndcu_address": 0xf2, "bndcn_address": 0xf2,
                 "bndldx": 0, "bndstx": 0}
OPCODE = {name: 0x1b if name in ("bndmk", "bndmov_store", "bndmov_store_lock",
                                "bndcn_reg", "bndcn_address", "bndstx") else 0x1a
          for name in NATIVE_PREFIX}
SEGMENTS = (0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65)


def run(command, directory):
    result = subprocess.run(list(map(str, command)), cwd=directory,
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{command}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def functions(disassembly):
    result, current = {}, None
    for line in disassembly.splitlines():
        label = re.fullmatch(r"[0-9a-f]+ <_?(case_\d+)>:", line.strip())
        if label:
            current = result.setdefault(int(label[1][5:]), [])
        elif re.fullmatch(r"[0-9a-f]+ <.*>:", line.strip()):
            current = None
        elif current is not None:
            instruction = re.match(r"\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )*[0-9a-f]{2})(?:\s|$)", line)
            if instruction:
                address, raw = int(instruction[1], 16), bytes.fromhex(instruction[2])
                # llvm-objdump can print a leading LOCK as its own line.
                if current and current[-1][1] == b"\xf0" and native(raw) is not None:
                    address, prefix = current.pop()
                    raw = prefix + raw
                current.append((address, raw))
                # Alignment NOP choices depend on the assembler's tuning CPU.
                # These wrappers have one native return and no trailing code.
                if raw in (b"\xc3", b"\xf2\xc3"):
                    current = None
    return result


def native(raw):
    position = 0
    while position < len(raw) and (raw[position] in (*SEGMENTS, 0x66, 0x67, 0xf0, 0xf2, 0xf3)
                                  or 0x40 <= raw[position] <= 0x4f):
        position += 1
    if raw[position:position + 2] not in (b"\x0f\x1a", b"\x0f\x1b"):
        return None
    return position


def verify_case(instructions, operation, bound, bits, segment, windows, mode):
    start = instructions[0][0]
    size = instructions[-1][0] + len(instructions[-1][1]) - start
    code = bytearray(size)
    checkpoints = []
    for address, raw in instructions:
        code[address - start:address - start + len(raw)] = raw
        cursor = native(raw)
        if cursor is not None:
            checkpoints.append((address - start, raw, cursor))
    args = (0xfedcba9876543210, 0x8877665544332211,
            0x1234567800001000, 0xaabbccdd00002000)
    mask = (1 << mode) - 1
    args = tuple(value & mask for value in args)
    expected = []
    if operation not in ("bndmk", "bndmov_load", "bndldx"):
        initial = (bound + 1) % 4 if operation == "bndmov_reg" else bound
        expected.append(("bndmov_load", initial, mode, 0, args[2], None))
    expected += [(operation, bound, bits, segment, args[0], args[1]),
                 ("bndmov_store", bound, mode, 0, args[3], None)]
    assert len(checkpoints) == len(expected), "native effects lost or duplicated"
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64 if mode == 64 else unicorn.UC_MODE_32)
    machine.mem_map(CODE, PAGE)
    machine.mem_map(STACK, PAGE)
    machine.mem_map(STOP, PAGE)
    machine.mem_write(CODE, bytes(code))
    number = lambda name: getattr(regs, "UC_X86_REG_" + (name if mode == 64 else "E" + name[1:]))
    preserved = ("RBX", "RBP") + (("R12", "R13", "R14", "R15") if mode == 64 else ())
    preserved += ("RSI", "RDI") if windows or mode == 32 else ()
    saved = {name: (0x1122334455660000 + i) & mask for i, name in enumerate(preserved)}
    for name, value in saved.items():
        machine.reg_write(number(name), value)
    sp = STACK + PAGE - 0x108
    machine.reg_write(number("RSP"), sp)
    pack = lambda value: struct.pack("<Q" if mode == 64 else "<I", value)
    machine.mem_write(sp, pack(STOP))
    if mode == 64:
        for name, value in zip(("RCX", "RDX", "R8", "R9") if windows else
                               ("RDI", "RSI", "RDX", "RCX"), args):
            machine.reg_write(number(name), value)
    else:
        machine.mem_write(sp + 4, b"".join(pack(value) for value in args))
    position = CODE
    for (offset, raw, cursor), (name, bank, width, prefix, value, second) in zip(checkpoints, expected):
        pc = CODE + offset
        if position != pc:
            machine.emu_start(position, pc, count=1000)
            assert machine.reg_read(number("RIP")) == pc
        prefixes, modrm = raw[:cursor], raw[cursor + 2]
        assert raw[cursor + 1] == OPCODE[name], "wrong native opcode"
        assert [p for p in prefixes if p in (0x66, 0xf2, 0xf3)] == ([NATIVE_PREFIX[name]] if NATIVE_PREFIX[name] else [])
        assert [p for p in prefixes if p in SEGMENTS] == ([prefix] if prefix else [])
        assert prefixes.count(0x67) == int(width != mode and name not in MPX.REGISTER)
        assert prefixes.count(0xf0) == int(name == "bndmov_store_lock")
        rex = next((p for p in reversed(prefixes) if 0x40 <= p <= 0x4f), 0)
        assert ((modrm >> 3) & 7) | ((rex & 4) << 1) == bank
        if name in MPX.REGISTER:
            assert modrm >> 6 == 3
            if name == "bndmov_reg":
                assert modrm & 7 == (bank + 1) % 4 and not rex & 1
            else:
                register = (modrm & 7) | ((rex & 1) << 3)
                assert machine.reg_read(number(GPRS[register])) == value, "check input changed"
        else:
            # BX supplies default DS independently of the allocated input GPR.
            assert modrm >> 6 == 0 and not rex & 1
            assert machine.reg_read(number("RBX")) == value, "base was truncated or changed"
            if name in MPX.PAIR:
                assert modrm & 7 == 4 and raw[cursor + 3] == 0x33 and not rex & 2
                expected_index = (second - value) & mask if name == "bndmk" else second
                assert machine.reg_read(number("RSI")) == expected_index, "index role changed"
            else:
                assert modrm & 7 == 3
        # The native boundary is deliberately not executed by Unicorn.
        position = pc + len(raw)
    machine.emu_start(position, STOP, count=1000)
    assert machine.reg_read(number("RIP")) == STOP
    assert machine.reg_read(number("RSP")) == sp + mode // 8
    for name, value in saved.items():
        assert machine.reg_read(number(name)) == value, "callee-saved register changed: " + name


def verify(args, directory):
    directory.mkdir(parents=True, exist_ok=True)
    ir, source, header, count = MPX.generate(args.mode)
    (directory / "cases.ll").write_text(ir)
    (directory / "cases.c").write_text(source)
    cases = re.findall(r"\{ case_(\d+), (\w+), (\d+), (\d+), (\d+) \}", header)
    names = {enum: name for name, enum in MPX.OPERATIONS}
    backend = args.llc or args.backend_llc
    for triple in TRIPLES:
        if args.mode == 32:
            triple = triple.replace("x86_64", "i386")
        for opt in ("-O0", "-O2"):
            stem = triple + opt
            output = stem + ".o"
            input_ir = "cases.ll"
            if args.compiler:
                command = [args.compiler, "--target=" + triple, "-std=c23", opt,
                           "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc",
                           "-fno-stack-protector", "-mno-red-zone", "-mllvm", "-verify-machineinstrs"]
                input_ir = stem + ".ll"
                run([*command, "-S", "-emit-llvm", "cases.c", "-o", input_ir], directory)
                text = (directory / input_ir).read_text()
                assert " asm " not in text and "@llvm.x86.bndmk(" in text
            elif opt == "-O2":
                input_ir = stem + ".ll"
                run([Path(backend).with_name("opt"), "-S", "-passes=default<O2>", "cases.ll", "-o", input_ir], directory)
            if backend:
                command = [backend, "-mtriple=" + triple, opt, "-verify-machineinstrs", input_ir]
                run([*command, "-filetype=obj", "-o", output], directory)
            else:
                run([*command, "-c", "cases.c", "-o", output], directory)
            decoded = functions(run([args.objdump, "-d", output], directory))
            assert len(decoded) == count
            for index, enum, bank, width, segment in cases:
                try:
                    verify_case(decoded[int(index)], names[enum], int(bank), int(width), int(segment), "windows" in triple, args.mode)
                except Exception as error:
                    raise RuntimeError(f"{stem} case_{index} {enum} bank={bank} width={width} segment={segment}") from error
            for dialect in ("att", "intel"):
                assembly, roundtrip = stem + "-" + dialect + ".s", stem + "-" + dialect + ".o"
                if backend:
                    run([*command, "-filetype=asm", "-x86-asm-syntax=" + dialect, "-o", assembly], directory)
                    run([Path(args.objdump).with_name("llvm-mc"), "-triple=" + triple, "-filetype=obj", assembly, "-o", roundtrip], directory)
                else:
                    run([*command, "-S", "-masm=" + dialect, "cases.c", "-o", assembly], directory)
                    run([args.compiler, "--target=" + triple, "-c", assembly, "-o", roundtrip], directory)
                assert functions(run([args.objdump, "-d", roundtrip], directory)) == decoded, "assembly round trip changed bytes"
            print(f"{stem}: {count} native MPX encodings, operand transport and ABI; both assembly dialects", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--llc", type=lambda s: str(Path(s).resolve()))
    modes.add_argument("--compiler", type=lambda s: str(Path(s).resolve()))
    parser.add_argument("--backend-llc", type=lambda s: str(Path(s).resolve()))
    parser.add_argument("--objdump", required=True, type=lambda s: str(Path(s).resolve()))
    parser.add_argument("--keep", type=lambda s: Path(s).resolve())
    parser.add_argument("--mode", choices=(32, 64), default=64, type=int)
    args = parser.parse_args()
    if args.backend_llc and not args.compiler:
        parser.error("--backend-llc requires --compiler")
    if args.mode == 32 and not args.llc:
        parser.error("NeverC supports x86-64 code; --mode=32 requires --llc")
    if args.keep:
        verify(args, args.keep)
    else:
        with tempfile.TemporaryDirectory(prefix="mpx-encoding-") as temporary:
            verify(args, Path(temporary))


if __name__ == "__main__":
    main()
