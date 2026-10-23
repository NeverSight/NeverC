#!/usr/bin/env python3
"""Check native MSRLIST table, mask and completion transport at O0/O2.

Requires capstone, lief and unicorn. The test stops at the real instruction
before it accesses either table. Injected RCX values check register transport;
nonzero values are diagnostic injections, not normal architectural returns.
Hardware fault/restart and privileged MSR behavior require separate validation.
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
MASKS = (0, 1, 0x8000000000000000, 0xFEDCBA9876543210)
COMPLETIONS = (0, 0x8123456789ABCDEF)
ENCODINGS = {"rdmsrlist": bytes.fromhex("f2 0f 01 c6"),
             "wrmsrlist": bytes.fromhex("f3 0f 01 c6")}
MEMORY_RESULT = 0x9182736455AAEEFF


def compile_case(args, directory, operation, result, opt):
    name = f"{operation}_{result}_{opt}"
    ir, output = directory / (name + ".ll"), directory / (name + ".obj")
    if args.llc:
        returned = "void" if result == "dead" else "i64"
        finish = {"dead": "ret void", "live": "ret i64 %remaining",
                  "preserve": "%both = xor i64 %remaining, %mask\n ret i64 %both",
                  "memory": "%data = load i64, ptr %values, align 8\n ret i64 %data"}[result]
        before = "store i64 16, ptr %indices, align 8\n" if result == "memory" else ""
        ir.write_text(
            f"declare i64 @llvm.x86.{operation}(ptr, ptr, i64)\n"
            f"define {returned} @probe(ptr %indices, ptr %values, i64 %mask) {{\n"
            f" {before}%remaining = call i64 @llvm.x86.{operation}(ptr %indices, ptr %values, i64 %mask)\n"
            f" {finish}\n}}\n")
    else:
        source = directory / (name + ".c")
        returned = "void" if result == "dead" else "unsigned __int64"
        finish = {"dead": "", "live": "return remaining;",
                  "preserve": "return remaining ^ mask;", "memory": "return *values;"}[result]
        before = "*indices = 16;" if result == "memory" else ""
        source.write_text(
            f"{returned} probe(unsigned __int64 *indices, unsigned __int64 *values, unsigned __int64 mask) {{\n"
            f" {before}\n unsigned __int64 remaining = __builtin_ia32_{operation}(indices, values, mask);\n"
            f" {finish}\n}}\n")
        common = [str(args.compiler), "--target=x86_64-pc-windows-msvc", "-" + opt,
                  "-ffreestanding", "-fno-lto", "-fno-stack-protector", str(source)]
        commands = [common + ["-S", "-emit-llvm", "-o", str(ir)]]
        if not args.backend_llc:
            commands.append(common + ["-c", "-o", str(output)])
        for command in commands:
            run = subprocess.run(command, capture_output=True, text=True)
            if run.returncode:
                raise AssertionError(f"{name}: frontend failed:\n{run.stderr}")
        text = ir.read_text()
        if not re.search(r"\bcall\b[^@\n]*@llvm\.x86\." + operation + r"\(", text) or " asm " in text:
            raise AssertionError(f"{name}: expected a native LLVM intrinsic")
    backend = args.llc or args.backend_llc
    if backend:
        run = subprocess.run(
            [str(backend), "-mtriple=x86_64-pc-windows-msvc", "-" + opt,
             "-verify-machineinstrs", "-filetype=obj", str(ir), "-o", str(output)],
            capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(f"{name}: backend failed:\n{run.stderr}")
    image = lief.parse(str(output))
    if image is None:
        raise AssertionError(f"{name}: expected a COFF object")
    return name, bytes(next(s for s in image.sections
                            if s.name == ".text" and len(s.content)).content)


def execute(code, operation, result, mask, completion, mapped):
    encoding = ENCODINGS[operation]
    # Capstone 5 predates MSRLIST. Locate its exact bytes, then disassemble the
    # entire preceding instruction stream to prove this is an instruction boundary.
    positions = [i for i in range(len(code)) if code.startswith(encoding, i)]
    if len(positions) != 1:
        raise AssertionError(f"expected one native {operation} encoding")
    offset = positions[0]
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    if sum(i.size for i in decoder.disasm(code[:offset], CODE)) != offset:
        raise AssertionError("MSRLIST byte match is not an instruction boundary")
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
    machine.mem_map(CODE, PAGE)
    machine.mem_write(CODE, code)
    machine.mem_map(STACK, PAGE)
    stack_pointer = STACK + PAGE - 72
    machine.reg_write(regs.UC_X86_REG_RSP, stack_pointer)
    machine.mem_write(stack_pointer, struct.pack("<Q", STOP))
    indices, values = (DATA, DATA + 512) if mapped else (0x12345678ABC0, 0x23456789DEF0)
    if mapped:
        machine.mem_map(DATA, PAGE)
        machine.mem_write(indices, struct.pack("<Q", 0xC0000080))
        machine.mem_write(values, struct.pack("<Q", 0x1122334455667788))
    machine.reg_write(regs.UC_X86_REG_RCX, indices)
    machine.reg_write(regs.UC_X86_REG_RDX, values)
    machine.reg_write(regs.UC_X86_REG_R8, mask)
    preserved = {regs.UC_X86_REG_RSI: 0x1020304050607080,
                 regs.UC_X86_REG_RDI: 0x8877665544332211}
    for register, value in preserved.items():
        machine.reg_write(register, value)
    instruction = CODE + offset
    machine.emu_start(CODE, instruction, count=1000)
    if machine.reg_read(regs.UC_X86_REG_RIP) != instruction:
        raise AssertionError("did not reach MSRLIST before accessing the tables")
    for register, expected in ((regs.UC_X86_REG_RSI, indices),
                               (regs.UC_X86_REG_RDI, values),
                               (regs.UC_X86_REG_RCX, mask)):
        if machine.reg_read(register) != expected:
            raise AssertionError("MSRLIST table address or mask was truncated, swapped or clobbered")
    if result == "memory":
        if bytes(machine.mem_read(indices, 8)) != struct.pack("<Q", 16):
            raise AssertionError("table store was reordered after MSRLIST")
        machine.mem_write(values, struct.pack("<Q", MEMORY_RESULT))
    machine.reg_write(regs.UC_X86_REG_RCX, completion)
    machine.emu_start(instruction + len(encoding), STOP, count=1000)
    if machine.reg_read(regs.UC_X86_REG_RIP) != STOP:
        raise AssertionError("MSRLIST completion did not return")
    if result != "dead":
        expected = (MEMORY_RESULT if result == "memory" else
                    completion ^ mask if result == "preserve" else completion)
        if machine.reg_read(regs.UC_X86_REG_RAX) != expected:
            raise AssertionError("completion RCX, live input mask or post-instruction memory was lost")
    for register, expected in preserved.items():
        if machine.reg_read(register) != expected:
            raise AssertionError("the caller's nonvolatile table register was not restored")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--compiler", type=Path,
                      default=Path(__file__).resolve().parents[1] / "build-neverc/bin/neverc")
    mode.add_argument("--llc", type=Path, help="test native LLVM IR directly")
    parser.add_argument("--backend-llc", type=Path, help="compile NeverC's IR using the LLVM fork")
    args = parser.parse_args()
    if args.llc and args.backend_llc:
        parser.error("--backend-llc cannot be combined with --llc")
    count = 0
    with tempfile.TemporaryDirectory(prefix="neverc-msr-list-") as temp:
        for opt in ("O0", "O2"):
            for operation in ENCODINGS:
                for result in ("live", "dead", "preserve", "memory"):
                    name, code = compile_case(args, Path(temp), operation, result, opt)
                    for mask in MASKS:
                        for completion in COMPLETIONS:
                            for mapped in ((True,) if result == "memory" else (True, False)):
                                try:
                                    execute(code, operation, result, mask, completion, mapped)
                                except (AssertionError, unicorn.UcError) as error:
                                    raise AssertionError(
                                        f"{name}, mask={mask:#x}, completion={completion:#x}, mapped={mapped}: {error}") from error
                                count += 1
    print(f"PASS: {count} MSRLIST transport/memory-boundary cases, native LLVM IR and machine code")


if __name__ == "__main__":
    main()
