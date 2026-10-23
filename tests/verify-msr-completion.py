#!/usr/bin/env python3
"""Check native RDMSR/WRMSR/WRMSRNS operand and result transport at O0/O2.

Requires capstone, lief and unicorn. The real instruction's register inputs are
checked, then a controlled read completion is supplied to the remaining code.
This tests compiler transport, not privileged access to an actual MSR.
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


CODE, STACK, PAGE = 0x10000, 0x80000, 0x1000
STOP = CODE + PAGE - 1
VALUES = ((0, 0), (0x80000123, 0x9172536485A6B7C8),
          (0xFFFFFFFF, 0xFEDCBA9876543210), (0x10, 0x100000000))


def compile_case(args, directory, kind, opt, bits):
    name = f"{kind}_{opt}_{bits}"
    writing = kind in ("write", "write_ns")
    intrinsic = "wrmsrns" if kind == "write_ns" else "wrmsr" if writing else "rdmsr"
    ir = directory / (name + ".ll")
    output = directory / (name + ".obj")
    if args.llc:
        declaration = (f"declare void @llvm.x86.{intrinsic}(i32, i64)" if writing
                       else "declare i64 @llvm.x86.rdmsr(i32)")
        call = (f"call void @llvm.x86.{intrinsic}(i32 %index, i64 %value)" if writing
                else "%result = call i64 @llvm.x86.rdmsr(i32 %index)")
        result = "i64" if kind == "read" else "void"
        terminator = "ret i64 %result" if kind == "read" else "ret void"
        ir.write_text(f"{declaration}\ndefine {result} @probe(i32 %index, i64 %value) {{\n"
                      f" {call}\n {terminator}\n}}\n")
    else:
        source = directory / (name + ".c")
        result = "unsigned __int64" if kind == "read" else "void"
        statement = ("__builtin_ia32_wrmsrns(index, value);" if kind == "write_ns" else
                     "__writemsr(index, value);" if writing else
                     ("return " if kind == "read" else "") + "__readmsr(index);")
        source.write_text('#include <intrin.h>\n'
                          f'{result} probe(unsigned int index, unsigned __int64 value) {{\n'
                          f' {statement}\n}}\n')
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
        if not re.search(r"\bcall\b[^@\n]*@llvm\.x86\." + intrinsic + r"\(", text) or " asm " in text:
            raise AssertionError(f"{name}: expected a native LLVM intrinsic")
    backend = args.llc or args.backend_llc
    if backend:
        arch = "x86_64" if bits == 64 else "i686"
        run = subprocess.run(
            [str(backend), "-mtriple=" + arch + "-pc-windows-msvc", "-" + opt,
             "-verify-machineinstrs", "-filetype=obj", str(ir), "-o", str(output)],
            capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(f"{name}: backend failed:\n{run.stderr}")
    image = lief.parse(str(output))
    if image is None:
        raise AssertionError(f"{name}: expected a COFF object")
    return name, bytes(next(s for s in image.sections
                            if s.name == ".text" and len(s.content)).content)


def execute(code, kind, index, value, bits):
    mode = unicorn.UC_MODE_64 if bits == 64 else unicorn.UC_MODE_32
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, mode)
    ip = regs.UC_X86_REG_RIP if bits == 64 else regs.UC_X86_REG_EIP
    sp = regs.UC_X86_REG_RSP if bits == 64 else regs.UC_X86_REG_ESP
    machine.mem_map(CODE, PAGE)
    machine.mem_write(CODE, code)
    machine.mem_map(STACK, PAGE)
    stack_pointer = STACK + PAGE - 64 - bits // 8
    machine.reg_write(sp, stack_pointer)
    machine.mem_write(stack_pointer, struct.pack("<Q" if bits == 64 else "<I", STOP))
    if bits == 64:
        machine.reg_write(regs.UC_X86_REG_RCX, index | 0xA1B2C3D400000000)
        machine.reg_write(regs.UC_X86_REG_RDX, value)
    else:
        machine.mem_write(stack_pointer + 4, struct.pack("<IQ", index, value))
    decoder = capstone.Cs(capstone.CS_ARCH_X86,
                          capstone.CS_MODE_64 if bits == 64 else capstone.CS_MODE_32)
    if kind == "write_ns":
        # Capstone 5 predates this encoding. Prove the byte match is at an
        # instruction boundary by decoding all preceding instructions.
        encoding = bytes.fromhex("0f 01 c6")
        offsets = [i for i in range(len(code)) if code.startswith(encoding, i)]
        if len(offsets) != 1 or sum(i.size for i in decoder.disasm(code[:offsets[0]], CODE)) != offsets[0]:
            raise AssertionError("expected one native WRMSRNS at an instruction boundary")
        instruction_address, instruction_size = CODE + offsets[0], len(encoding)
    else:
        mnemonic = "wrmsr" if kind == "write" else "rdmsr"
        accesses = [i for i in decoder.disasm(code, CODE) if i.mnemonic == mnemonic]
        if len(accesses) != 1:
            raise AssertionError(f"expected one {mnemonic}, found {len(accesses)}")
        instruction_address, instruction_size = accesses[0].address, accesses[0].size
    machine.emu_start(CODE, instruction_address, count=1000)
    if machine.reg_read(ip) != instruction_address:
        raise AssertionError("did not reach the MSR instruction")
    if machine.reg_read(regs.UC_X86_REG_ECX) != index:
        raise AssertionError("MSR index was changed")
    if kind in ("write", "write_ns"):
        low = machine.reg_read(regs.UC_X86_REG_EAX)
        high = machine.reg_read(regs.UC_X86_REG_EDX)
        if low | high << 32 != value:
            raise AssertionError("MSR write did not receive the full EDX:EAX value")
    else:
        # RDMSR zeroes the upper halves of both destination registers in 64-bit mode.
        machine.reg_write(regs.UC_X86_REG_RAX if bits == 64 else regs.UC_X86_REG_EAX,
                          value & 0xFFFFFFFF)
        machine.reg_write(regs.UC_X86_REG_RDX if bits == 64 else regs.UC_X86_REG_EDX,
                          value >> 32)
    machine.emu_start(instruction_address + instruction_size, STOP, count=1000)
    if machine.reg_read(ip) != STOP:
        raise AssertionError("MSR completion did not return")
    if kind == "read":
        actual = (machine.reg_read(regs.UC_X86_REG_RAX) if bits == 64 else
                  machine.reg_read(regs.UC_X86_REG_EAX) |
                  machine.reg_read(regs.UC_X86_REG_EDX) << 32)
        if actual != value:
            raise AssertionError(f"RDMSR result is {actual:#x}, expected {value:#x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--compiler", type=Path,
                      default=Path(__file__).resolve().parents[1] / "build-neverc/bin/neverc")
    mode.add_argument("--llc", type=Path, help="test the LLVM fork directly in both word sizes")
    parser.add_argument("--backend-llc", type=Path, help="compile NeverC's IR using the LLVM fork")
    args = parser.parse_args()
    if args.llc and args.backend_llc:
        parser.error("--backend-llc cannot be combined with --llc")
    count = 0
    with tempfile.TemporaryDirectory(prefix="neverc-msr-") as temp:
        for bits in ((32, 64) if args.llc else (64,)):
            for opt in ("O0", "O2"):
                for kind in ("read", "read_dead", "write", "write_ns"):
                    name, code = compile_case(args, Path(temp), kind, opt, bits)
                    for index, value in VALUES:
                        try:
                            execute(code, kind, index, value, bits)
                        except (AssertionError, unicorn.UcError) as error:
                            raise AssertionError(f"{name}, index={index:#x}, value={value:#x}: {error}") from error
                        count += 1
    print(f"PASS: {count} MSR transport cases, native LLVM IR and machine code")


if __name__ == "__main__":
    main()
