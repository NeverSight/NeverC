#!/usr/bin/env python3
"""Check VMCS builtin code generation and completion at O0/O2.

Requires the same capstone/lief/unicorn packages as verify-x86-intrin-binary.py.
The privileged instruction is intercepted, its actual operands are checked,
and each architectural completion is supplied to the remaining machine code.
This tests compiler register/memory transport, not a hardware VMCS environment.
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


CODE = 0x10000
STACK = 0x80000
DATA = 0xA0000
PAGE = 0x1000
STOP = CODE + PAGE - 1
FIELD = 0xF1E2D3C44002A5B6
VALUE = 0x9172536485A6B7C8
SENTINEL = 0x18273645AABBCCDD


def compile_case(compiler, directory, operation, dead, opt):
    name = operation + ("_dead" if dead else "") + "_" + opt
    source = directory / (name + ".c")
    output = directory / (name + ".obj")
    ir = directory / (name + ".ll")
    second = "unsigned __int64 *out" if operation == "vmread" else "unsigned __int64 value"
    argument = "out" if operation == "vmread" else "value"
    result = "void" if dead else "unsigned char"
    prefix = "" if dead else "return "
    source.write_text(
        '#include <intrin.h>\n'
        f'{result} probe(unsigned __int64 field, {second}) {{\n'
        f'  {prefix}__vmx_{operation}(field, {argument});\n}}\n')
    command = [str(compiler), "-target", "x86_64-pc-windows-msvc", "-" + opt,
               "-ffreestanding", "-fno-lto", "-fno-stack-protector", str(source)]
    for flags, path in [(["-c"], output), (["-S", "-emit-llvm"], ir)]:
        compiled = subprocess.run(command + flags + ["-o", str(path)],
                                  capture_output=True, text=True)
        if compiled.returncode:
            raise AssertionError(f"{name}: compilation failed:\n{compiled.stderr}")
    text = ir.read_text()
    if not re.search(r"\bcall\b[^@\n]*@llvm\.x86\." + operation + r"\(", text) or " asm " in text:
        raise AssertionError(f"{name}: expected a native LLVM intrinsic call")
    image = lief.parse(str(output))
    if image is None:
        raise AssertionError(f"{name}: compiler did not produce a COFF object")
    section = next(s for s in image.sections if s.name == ".text" and len(s.content))
    return name, bytes(section.content)


def compile_ir(llc, directory, operation, dead, opt, bits):
    name = f"{operation}_{bits}_{'dead' if dead else 'live'}_{opt}"
    source = directory / (name + ".ll")
    output = directory / (name + ".obj")
    integer = f"i{bits}"
    second = "ptr" if operation == "vmread" else integer
    intrinsic = "llvm.x86." + operation + ("32" if bits == 32 else "")
    result = "void" if dead else "i8"
    terminator = "ret void" if dead else "ret i8 %status"
    source.write_text(
        f"declare i8 @{intrinsic}({integer}, {second})\n"
        f"define {result} @probe({integer} %field, {second} %second) {{\n"
        f" %status = call i8 @{intrinsic}({integer} %field, {second} %second)\n"
        f" {terminator}\n}}\n")
    arch = "i686" if bits == 32 else "x86_64"
    compiled = subprocess.run(
        [str(llc), "-mtriple=" + arch + "-pc-windows-msvc", "-" + opt,
         "-verify-machineinstrs", "-filetype=obj", str(source), "-o", str(output)],
        capture_output=True, text=True)
    if compiled.returncode:
        raise AssertionError(f"{name}: compilation failed:\n{compiled.stderr}")
    image = lief.parse(str(output))
    if image is None:
        raise AssertionError(f"{name}: llc did not produce a COFF object")
    return name, bytes(next(s for s in image.sections
                            if s.name == ".text" and len(s.content)).content)


def register(machine, instruction, number):
    return machine.reg_read(getattr(regs, "UC_X86_REG_" + instruction.reg_name(number).upper()))


def address(machine, instruction, operand):
    mem = operand.mem
    base = register(machine, instruction, mem.base) if mem.base else 0
    index = register(machine, instruction, mem.index) if mem.index else 0
    if mem.segment:
        raise AssertionError("unexpected segmented VMCS builtin destination")
    return (base + index * mem.scale + mem.disp) & ((1 << 64) - 1)


def execute(code, operation, dead, status, mapped, bits=64):
    machine = unicorn.Uc(unicorn.UC_ARCH_X86,
                         unicorn.UC_MODE_64 if bits == 64 else unicorn.UC_MODE_32)
    word = "<Q" if bits == 64 else "<I"
    word_bytes = bits // 8
    mask = (1 << bits) - 1
    ip = regs.UC_X86_REG_RIP if bits == 64 else regs.UC_X86_REG_EIP
    sp = regs.UC_X86_REG_RSP if bits == 64 else regs.UC_X86_REG_ESP
    machine.mem_map(CODE, PAGE)
    machine.mem_write(CODE, code)
    machine.mem_map(STACK, PAGE)
    stack_pointer = STACK + PAGE - 0x108
    machine.reg_write(sp, stack_pointer)
    machine.mem_write(stack_pointer, struct.pack(word, STOP))
    destination = DATA + 13  # VMREAD has no alignment requirement.
    if mapped:
        machine.mem_map(DATA, PAGE)
        machine.mem_write(destination, struct.pack(word, SENTINEL & mask))
    second = destination if operation == "vmread" else VALUE & mask
    if bits == 64:
        machine.reg_write(regs.UC_X86_REG_RCX, FIELD)
        machine.reg_write(regs.UC_X86_REG_RDX, second)
    else:
        machine.mem_write(stack_pointer + 4, struct.pack("<II", FIELD & mask, second))
    decoder = capstone.Cs(capstone.CS_ARCH_X86,
                          capstone.CS_MODE_64 if bits == 64 else capstone.CS_MODE_32)
    decoder.detail = True
    accesses = [i for i in decoder.disasm(code, CODE) if i.mnemonic == operation]
    if len(accesses) != 1:
        raise AssertionError(f"expected one {operation}, found {len(accesses)}")
    instruction = accesses[0]
    machine.emu_start(CODE, instruction.address, count=1000)
    if machine.reg_read(ip) != instruction.address:
        raise AssertionError("did not reach VMCS access")
    operands = instruction.operands
    field_operand = operands[1] if operation == "vmread" else operands[0]
    if register(machine, instruction, field_operand.reg) != FIELD & mask:
        raise AssertionError("VMCS field was truncated or swapped with its value")
    if operation == "vmwrite":
        if register(machine, instruction, operands[1].reg) != VALUE & mask:
            raise AssertionError("VMWRITE value was truncated or swapped")
    else:
        destination_operand = operands[0]
        if destination_operand.type == capstone.x86.X86_OP_MEM:
            actual_address = address(machine, instruction, destination_operand)
            if actual_address != destination:
                raise AssertionError("VMREAD destination address changed")
            if status == 0:
                machine.mem_write(actual_address, struct.pack(word, VALUE & mask))
        elif destination_operand.type == capstone.x86.X86_OP_REG:
            # Also run old register-form lowering: an unconditional subsequent
            # store must fail the unchanged-memory and unmapped-memory cases.
            if status == 0:
                machine.reg_write(getattr(regs, "UC_X86_REG_" + instruction.reg_name(
                    destination_operand.reg).upper()), VALUE & mask)
        else:
            raise AssertionError("unexpected VMREAD destination operand")
    flags = machine.reg_read(regs.UC_X86_REG_EFLAGS) & ~0x8D5
    machine.reg_write(regs.UC_X86_REG_EFLAGS,
                      flags | (0x40 if status == 1 else 1 if status == 2 else 0))
    machine.emu_start(instruction.address + instruction.size, STOP, count=1000)
    if machine.reg_read(ip) != STOP:
        raise AssertionError("VMCS completion did not return")
    if not dead and machine.reg_read(regs.UC_X86_REG_AL) != status:
        raise AssertionError("VMCS completion status was not preserved")
    if mapped and operation == "vmread":
        actual = struct.unpack(word, machine.mem_read(destination, word_bytes))[0]
        expected = (VALUE if status == 0 else SENTINEL) & mask
        if actual != expected:
            raise AssertionError(f"VMREAD memory is {actual:#x}, expected {expected:#x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--compiler", type=Path,
                      default=Path(__file__).resolve().parents[1] / "build-neverc/bin/neverc")
    mode.add_argument("--llc", type=Path, help="test the LLVM fork directly in both word sizes")
    args = parser.parse_args()
    count = 0
    with tempfile.TemporaryDirectory(prefix="neverc-vmcs-") as temp:
        for bits in ((32, 64) if args.llc else (64,)):
            for opt in ("O0", "O2"):
                for operation in ("vmread", "vmwrite"):
                    for dead in (False, True):
                        if args.llc:
                            name, code = compile_ir(args.llc, Path(temp), operation, dead, opt, bits)
                        else:
                            name, code = compile_case(args.compiler, Path(temp), operation, dead, opt)
                        for status in (0, 1, 2):
                            for mapped in ((True, False) if operation == "vmread" and status else (True,)):
                                try:
                                    execute(code, operation, dead, status, mapped, bits)
                                except (AssertionError, unicorn.UcError) as error:
                                    raise AssertionError(
                                        f"{name}, status={status}, mapped={mapped}: {error}") from error
                                count += 1
    print(f"PASS: {count} VMCS completion cases, native LLVM IR and machine code")


if __name__ == "__main__":
    main()
