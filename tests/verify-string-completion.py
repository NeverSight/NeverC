#!/usr/bin/env python3
"""Check native REP string intrinsics, memory execution and operand transport.

MOVS/STOS execute in Unicorn against an independent sequential byte oracle.
Port I/O and huge/unmapped operands use controlled instruction completions.
DF is selected at the instruction boundary and restored before C code resumes;
this does not relax the platform ABI's direction-flag requirements.
Requires capstone, lief and unicorn. No host port I/O is performed.
"""

import argparse
import re
import struct
import subprocess
import tempfile
from pathlib import Path

import capstone
import lief
import unicorn
import unicorn.x86_const as regs


CODE, STACK, DATA, PAGE = 0x10000, 0x80000, 0xA0000, 0x1000
STOP = CODE + PAGE - 1
OPERATIONS = {
    "movsb": ("move", 1), "movsw": ("move", 2), "movsd_rep": ("move", 4), "movsq": ("move", 8),
    "stosb": ("store", 1), "stosw": ("store", 2), "stosd": ("store", 4), "stosq": ("store", 8),
    "inbytestring": ("in", 1), "inwordstring": ("in", 2), "indwordstring": ("in", 4),
    "outbytestring": ("out", 1), "outwordstring": ("out", 2), "outdwordstring": ("out", 4),
}


def compile_case(args, directory, operation, bits, opt, memory):
    kind, width = OPERATIONS[operation]
    port = kind in ("in", "out")
    name = f"{operation}_{bits}_{opt}_{memory}_{args.spelling}"
    ir, output = directory / (name + ".ll"), directory / (name + ".obj")
    word, c_word = f"i{bits}", "unsigned long long"
    types = ("i16", "ptr", "i32") if port else ("ptr", "ptr" if kind == "move" else f"i{width * 8}", word)
    c_value = {1: "unsigned char", 2: "unsigned short", 4: "unsigned int", 8: c_word}[width]
    c_types = ("unsigned short", "void *", "unsigned int") if port else \
              ("void *", "const void *" if kind == "move" else c_value, c_word)
    names = ("first", "second", "count")
    api = operation + ("32" if bits == 32 and not port else "")
    ir_api = api.replace("_", ".")
    pointer = "second" if port else "first"
    if args.llc:
        params = ", ".join(f"{ty} %{name}" for ty, name in zip(types, names))
        body = [f"store i8 26, ptr %{pointer}"] if memory else []
        body.append(f"call void @llvm.x86.{ir_api}({params})")
        values = []
        for ty, name in zip(types, names):
            if ty == word:
                values.append("%" + name)
            else:
                body.append(f"%wide_{name} = {'ptrtoint' if ty == 'ptr' else 'zext'} {ty} %{name} to {word}")
                values.append("%wide_" + name)
        body.extend((f"%one = xor {word} {values[0]}, {values[1]}",
                     f"%two = xor {word} %one, {values[2]}", f"%three = xor {word} %two, %seed"))
        if memory:
            body.extend((f"%byte = load i8, ptr %{pointer}", f"%byte_wide = zext i8 %byte to {word}",
                         f"%result = xor {word} %three, %byte_wide"))
        body.append(f"ret {word} %{'result' if memory else 'three'}")
        ir.write_text(f"declare void @llvm.x86.{ir_api}({', '.join(types)})\n"
                      f"define {word} @probe({params}, {word} %seed) {{\n " + "\n ".join(body) + "\n}\n")
    else:
        source = directory / (name + ".c")
        builtin = "__builtin_ia32_" + operation if args.spelling == "builtin" else \
                  "__" + ("movsd" if operation == "movsd_rep" else operation)
        header = "#include <intrin.h>\n" if args.spelling == "microsoft" else ""
        parameters = ", ".join(ty + " " + name for ty, name in zip(c_types, names))
        before = f"*(unsigned char *){pointer} = 26;" if memory else ""
        after = f" ^ *(unsigned char *){pointer}" if memory else ""
        source.write_text(header + f"{c_word} probe({parameters}, {c_word} seed) {{\n"
                          f" {before}\n {builtin}(first, second, count);\n"
                          f" return ({c_word})first ^ ({c_word})second ^ count ^ seed{after};\n}}\n")
        common = [str(args.compiler), "--target=x86_64-pc-windows-msvc", "-" + opt, "-Werror",
                  "-ffreestanding", "-fno-lto", "-fno-stack-protector", "-mllvm", "-verify-machineinstrs", str(source)]
        commands = [common + ["-S", "-emit-llvm", "-o", str(ir)]]
        if not args.backend_llc:
            commands.append(common + ["-c", "-o", str(output)])
        for command in commands:
            run = subprocess.run(command, capture_output=True, text=True)
            if run.returncode:
                raise AssertionError(f"{name}: frontend failed:\n{run.stderr}")
        text = ir.read_text()
        if " asm " in text or not re.search(r"\bcall\b[^@\n]*@llvm\.x86\." + re.escape(ir_api) + r"\(", text):
            raise AssertionError(f"{name}: expected a native REP intrinsic")
    backend = args.llc or args.backend_llc
    if backend:
        arch = "x86_64" if bits == 64 else "i686"
        run = subprocess.run([str(backend), "-mtriple=" + arch + "-pc-windows-msvc", "-" + opt,
                              "-verify-machineinstrs", "-filetype=obj", str(ir), "-o", str(output)],
                             capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(f"{name}: backend failed:\n{run.stderr}")
    image = lief.parse(str(output))
    return name, bytes(next(s for s in image.sections if s.name == ".text" and len(s.content)).content)


def execute(code, operation, bits, memory, count, backward, overlap, transport, unmapped):
    kind, width = OPERATIONS[operation]
    port = kind in ("in", "out")
    mask = (1 << bits) - 1
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64 if bits == 64 else unicorn.UC_MODE_32)
    reg = lambda name: getattr(regs, "UC_X86_REG_" + ("R" if bits == 64 else "E") + name)
    pack = lambda value: struct.pack("<Q" if bits == 64 else "<I", value & mask)
    for address in (CODE, STACK):
        machine.mem_map(address, PAGE)
    machine.mem_write(CODE, code)
    initial = bytes((index * 37 + 19) & 255 for index in range(2 * PAGE))
    expected = bytearray(initial)
    if not unmapped:
        machine.mem_map(DATA, 2 * PAGE)
        machine.mem_write(DATA, initial)
    source, destination = DATA + 128, DATA + PAGE + 128
    if overlap:
        destination = source + 1
    if transport:
        source = (0x8123456789ABCDEF if bits == 64 else 0x89ABCDEF) & mask
        destination = (0xFEDCBA9876543210 if bits == 64 else 0x76543210) & mask
    value = 0xD7E6F5A493827160 & ((1 << (width * 8)) - 1)
    seed = 0x8ABCDEF012345678 & mask
    first, second = (0xF123, destination) if port else (destination, source if kind == "move" else value)
    stack_pointer = STACK + PAGE - 64 - bits // 8
    machine.reg_write(reg("SP"), stack_pointer)
    machine.mem_write(stack_pointer, pack(STOP))
    args = [first, second, count, seed]
    if bits == 64:
        for register, argument in zip((regs.UC_X86_REG_RCX, regs.UC_X86_REG_RDX, regs.UC_X86_REG_R8, regs.UC_X86_REG_R9), args):
            machine.reg_write(register, argument)
        # The ABI does not guarantee zero upper bits for the i32 port count.
        if port:
            machine.reg_write(regs.UC_X86_REG_R8, count | 0xDEADBEEF00000000)
    else:
        machine.mem_write(stack_pointer + 4, b"".join(pack(argument) for argument in args))
    saved = {reg(name): (0xA1B2C3D4E5F60710 + index) & mask for index, name in enumerate(("BX", "BP", "SI", "DI"))}
    for register, value_saved in saved.items():
        machine.reg_write(register, value_saved)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if bits == 64 else capstone.CS_MODE_32)
    decoder.detail = True
    instructions = list(decoder.disasm(code, CODE))
    accesses = [i for i in instructions if i.mnemonic.startswith("rep ")]
    if len(accesses) != 1:
        raise AssertionError("expected exactly one REP string instruction")
    instruction = accesses[0]
    family = {"move": "movs", "store": "stos", "in": "ins", "out": "outs"}[kind]
    if not instruction.mnemonic.startswith("rep " + family) or instruction.addr_size != bits // 8:
        raise AssertionError("wrong REP instruction or address width")
    if any(o.size != width for o in instruction.operands if o.type == capstone.x86.X86_OP_MEM):
        raise AssertionError("wrong REP element width")
    machine.emu_start(CODE, instruction.address, count=1000)
    if machine.reg_read(reg("IP")) != instruction.address:
        raise AssertionError("did not reach the REP instruction")
    if machine.reg_read(reg("CX")) != count:
        raise AssertionError("REP element count was truncated or not zero extended")
    if kind != "out" and machine.reg_read(reg("DI")) != destination:
        raise AssertionError("REP destination pointer was changed")
    if kind in ("move", "out") and machine.reg_read(reg("SI")) != (source if kind == "move" else destination):
        raise AssertionError("REP source pointer was changed")
    if port and machine.reg_read(regs.UC_X86_REG_DX) != first:
        raise AssertionError("REP port selector was changed")
    if kind == "store" and machine.reg_read(reg("AX")) & ((1 << (width * 8)) - 1) != second:
        raise AssertionError("REP fill value was changed")
    if memory:
        expected[destination - DATA] = 26
    if not unmapped and bytes(machine.mem_read(DATA, 2 * PAGE)) != expected:
        raise AssertionError("ordinary stores crossed the REP operation")
    flags = (machine.reg_read(regs.UC_X86_REG_EFLAGS) & ~0xCD5) | 0x895 | (0x400 if backward else 0)
    machine.reg_write(regs.UC_X86_REG_EFLAGS, flags)
    step = -width if backward else width
    if not transport:
        for index in range(count):
            target = destination - DATA + step * index
            if kind == "move":
                origin = source - DATA + step * index
                expected[target:target + width] = bytes(expected[origin:origin + width])
            elif kind == "store":
                expected[target:target + width] = second.to_bytes(width, "little")
            elif kind == "in":
                item = (0xD4C3B2A1 + index * 17) & ((1 << (width * 8)) - 1)
                expected[target:target + width] = item.to_bytes(width, "little")
    if port or transport:
        if not unmapped:
            machine.mem_write(DATA, bytes(expected))
        machine.reg_write(reg("CX"), 0)
        if kind != "out":
            machine.reg_write(reg("DI"), (destination + step * count) & mask)
        if kind in ("move", "out"):
            machine.reg_write(reg("SI"), ((source if kind == "move" else destination) + step * count) & mask)
    else:
        machine.emu_start(instruction.address, instruction.address + instruction.size, count=1000)
        if machine.reg_read(reg("CX")) != 0:
            raise AssertionError("REP execution did not consume its count")
    if kind != "out" and machine.reg_read(reg("DI")) != (destination + step * count) & mask:
        raise AssertionError("REP destination progression differs from the oracle")
    if kind in ("move", "out"):
        origin = source if kind == "move" else destination
        if machine.reg_read(reg("SI")) != (origin + step * count) & mask:
            raise AssertionError("REP source progression differs from the oracle")
    if machine.reg_read(regs.UC_X86_REG_EFLAGS) & 0xCD5 != flags & 0xCD5:
        raise AssertionError("REP modified arithmetic flags or DF")
    machine.reg_write(regs.UC_X86_REG_EFLAGS, flags & ~0x400)
    machine.emu_start(instruction.address + instruction.size, STOP, count=1000)
    result = first ^ second ^ count ^ seed
    if memory:
        result ^= expected[destination - DATA]
    if machine.reg_read(reg("IP")) != STOP or machine.reg_read(reg("AX")) != result:
        raise AssertionError("REP lost a live input or forwarded a stale memory value")
    if not unmapped and bytes(machine.mem_read(DATA, 2 * PAGE)) != expected:
        raise AssertionError("REP memory differs from the sequential oracle")
    if any(machine.reg_read(register) != value for register, value in saved.items()):
        raise AssertionError("REP did not restore callee-saved registers")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--compiler", type=Path, default=Path(__file__).resolve().parents[1] / "build-neverc/bin/neverc")
    mode.add_argument("--llc", type=Path)
    parser.add_argument("--backend-llc", type=Path)
    parser.add_argument("--spelling", choices=("builtin", "microsoft"), default="builtin")
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    if args.llc and args.backend_llc:
        parser.error("--backend-llc applies only to NeverC IR")
    with tempfile.TemporaryDirectory(prefix="string-completion-") as temporary:
        directory = args.keep or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        total = 0
        for bits in ((32, 64) if args.llc else (64,)):
            for operation, (kind, width) in OPERATIONS.items():
                if bits == 32 and width == 8:
                    continue
                for memory in (False, True):
                    for opt in ("O0", "O2"):
                        name, code = compile_case(args, directory, operation, bits, opt, memory)
                        cases = [(count, backward, overlap, False, False)
                                 for count in (0, 1, 7) for backward in (False, True)
                                 for overlap in ((False, True) if kind == "move" else (False,))]
                        if not memory:
                            cases += [(0, backward, False, False, True) for backward in (False, True)]
                            high_count = 0x80000001 if kind in ("in", "out") else (1 << (bits - 1)) | 7
                            cases += [(high_count, backward, False, True, True) for backward in (False, True)]
                        for case in cases:
                            try:
                                execute(code, operation, bits, memory, *case)
                            except Exception as error:
                                raise AssertionError(f"{name}, {case}: {error}") from error
                            total += 1
        print(f"PASS: {total} native/controlled REP string completions")


if __name__ == "__main__":
    main()
