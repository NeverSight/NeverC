#!/usr/bin/env python3
"""Check native port I/O, descriptor tables, interrupt control and CPUID.

Requires capstone, lief and unicorn. Privileged instructions are intercepted at
an instruction boundary and given controlled completions. This checks compiler
transport and instruction-owned accesses, not privileged hardware execution.
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
PORTS = {"inbyte": 8, "inword": 16, "indword": 32,
         "outbyte": 8, "outword": 16, "outdword": 32}
MEMORY = ("invlpg", "sgdt", "sidt", "lgdt", "lidt")
BUILTINS = {"cli": "_disable", "sti": "_enable", "invlpg": "__invlpg",
            "sgdt": "_sgdt", "sidt": "__sidt", "lgdt": "_lgdt", "lidt": "__lidt",
            "cpuidex": "__cpuidex"}
VALUES = ((0, 0), (0xFFFF, 0xFEDCBA98), (0x8ABC, 0x12345678))
CPUID_VALUES = (0x87654321, 0xFEDCBA98, 0x81234567, 0x9ABCDEF0)


def compile_case(args, directory, operation, dead, opt, bits):
    name = f"{operation}_{dead}_{opt}_{bits}"
    ir, output = directory / (name + ".ll"), directory / (name + ".obj")
    reads = operation in PORTS and operation.startswith("in")
    result = f"i{PORTS[operation]}" if reads else "void"
    c_result = {"void": "void", "i8": "unsigned char", "i16": "unsigned short",
                "i32": "unsigned int"}[result]
    parameters, c_parameters, arguments = "", "void", ""
    if operation in PORTS:
        parameters, c_parameters, arguments = "i16 %port", "unsigned short port", "port"
        if not reads:
            width = PORTS[operation]
            c_value = {8: "unsigned char", 16: "unsigned short", 32: "unsigned int"}[width]
            parameters += f", i{width} %value"
            c_parameters += f", {c_value} value"
            arguments += ", value"
    elif operation in MEMORY:
        parameters, c_parameters, arguments = "ptr %address", "void *address", "address"
    elif operation == "cpuidex":
        parameters = "ptr %address, i32 %leaf, i32 %subleaf"
        c_parameters = "int *address, int leaf, int subleaf"
        arguments = "address, leaf, subleaf"
    if args.llc:
        types = re.sub(r" %\w+", "", parameters)
        returned = "void" if dead else result
        assign = "%value = " if reads else ""
        ret = f"ret {result} %value" if reads and not dead else "ret void"
        ir.write_text(
            f"declare {result} @llvm.x86.{operation}({types})\n"
            f"define {returned} @probe({parameters}) {{\n"
            f" {assign}call {result} @llvm.x86.{operation}({parameters})\n {ret}\n}}\n")
    else:
        source = directory / (name + ".c")
        builtin = "__" + operation if operation in PORTS else BUILTINS[operation]
        prefix = "return " if reads and not dead else ""
        source.write_text('#include <intrin.h>\n'
                          f'{"void" if dead else c_result} probe({c_parameters}) {{\n'
                          f' {prefix}{builtin}({arguments});\n}}\n')
        common = [str(args.compiler), "--target=x86_64-pc-windows-msvc", "-" + opt,
                  "-ffreestanding", "-fno-lto", "-fno-stack-protector",
                  "-mllvm", "-verify-machineinstrs", str(source)]
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
        arch = "x86_64" if bits == 64 else "i686"
        run = subprocess.run([str(backend), "-mtriple=" + arch + "-pc-windows-msvc",
                              "-" + opt, "-verify-machineinstrs", "-filetype=obj",
                              str(ir), "-o", str(output)], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(f"{name}: backend failed:\n{run.stderr}")
    image = lief.parse(str(output))
    if image is None:
        raise AssertionError(f"{name}: expected a COFF object")
    return name, bytes(next(s for s in image.sections
                            if s.name == ".text" and len(s.content)).content)


def execute(code, operation, dead, bits, sample, mapped):
    port, value = sample
    mode = unicorn.UC_MODE_64 if bits == 64 else unicorn.UC_MODE_32
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, mode)
    ip = regs.UC_X86_REG_RIP if bits == 64 else regs.UC_X86_REG_EIP
    sp = regs.UC_X86_REG_RSP if bits == 64 else regs.UC_X86_REG_ESP
    ax = regs.UC_X86_REG_RAX if bits == 64 else regs.UC_X86_REG_EAX
    bx = regs.UC_X86_REG_RBX if bits == 64 else regs.UC_X86_REG_EBX
    machine.mem_map(CODE, PAGE)
    machine.mem_write(CODE, code)
    machine.mem_map(STACK, PAGE)
    stack_pointer = STACK + PAGE - 64 - bits // 8
    machine.reg_write(sp, stack_pointer)
    machine.mem_write(stack_pointer, struct.pack("<Q" if bits == 64 else "<I", STOP))
    destination = DATA + 13
    if mapped:
        machine.mem_map(DATA, PAGE)
        machine.mem_write(DATA, b"\xA5" * PAGE)
    arguments = [port, value] if operation in PORTS else [destination, 0x80000001, value]
    if bits == 64:
        for register, argument in zip((regs.UC_X86_REG_RCX, regs.UC_X86_REG_RDX,
                                       regs.UC_X86_REG_R8), arguments):
            machine.reg_write(register, argument)
    else:
        machine.mem_write(stack_pointer + 4, struct.pack("<" + "I" * len(arguments), *arguments))
    preserved = 0xA1B2C3D4 if bits == 32 else 0xA1B2C3D4E5F60718
    machine.reg_write(bx, preserved)
    decoder = capstone.Cs(capstone.CS_ARCH_X86,
                          capstone.CS_MODE_64 if bits == 64 else capstone.CS_MODE_32)
    decoder.detail = True
    mnemonic = ("in" if operation.startswith("in") else "out") if operation in PORTS else \
               "cpuid" if operation == "cpuidex" else operation
    accesses = [i for i in decoder.disasm(code, CODE) if i.mnemonic == mnemonic]
    if len(accesses) != 1:
        raise AssertionError(f"expected one {mnemonic}, got {len(accesses)}")
    instruction = accesses[0]
    machine.emu_start(CODE, instruction.address, count=1000)
    if machine.reg_read(ip) != instruction.address:
        raise AssertionError("did not reach the native instruction")
    if operation in PORTS:
        if machine.reg_read(regs.UC_X86_REG_DX) != port:
            raise AssertionError("port selector was changed")
        width = PORTS[operation]
        mask = (1 << width) - 1
        data = next(o for o in instruction.operands if o.type == capstone.x86.X86_OP_REG
                    and instruction.reg_name(o.reg) != "dx")
        if data.size * 8 != width:
            raise AssertionError("wrong I/O value width")
        if operation.startswith("in"):
            before = machine.reg_read(ax)
            machine.reg_write(ax, value & mask if width == 32 else (before & ~mask) | (value & mask))
        elif machine.reg_read(ax) & mask != value & mask:
            raise AssertionError("port output was truncated or swapped")
    elif operation in MEMORY:
        operand = instruction.operands[0]
        if operand.type != capstone.x86.X86_OP_MEM:
            raise AssertionError("descriptor/invalidation operand must remain memory-form")
        address = operand.mem.disp
        for register, scale in ((operand.mem.base, 1), (operand.mem.index, operand.mem.scale)):
            if register:
                reg = getattr(regs, "UC_X86_REG_" + instruction.reg_name(register).upper())
                address += machine.reg_read(reg) * scale
        if address & ((1 << bits) - 1) != destination:
            raise AssertionError("native memory address was changed")
        if operation != "invlpg" and operand.size != (10 if bits == 64 else 6):
            raise AssertionError("wrong descriptor-table operand width")
        if not mapped:
            return  # No compiler-generated access before the fault boundary.
        if bytes(machine.mem_read(DATA, PAGE)) != b"\xA5" * PAGE:
            raise AssertionError("memory was accessed before the descriptor instruction")
        if operation in ("sgdt", "sidt"):
            descriptor = bytes(range(10 if bits == 64 else 6))
            machine.mem_write(destination, descriptor)
    elif operation == "cpuidex":
        if machine.reg_read(regs.UC_X86_REG_EAX) != 0x80000001 or \
                machine.reg_read(regs.UC_X86_REG_ECX) != value:
            raise AssertionError("CPUID leaf/subleaf was changed")
        for register, output in zip((regs.UC_X86_REG_EAX, regs.UC_X86_REG_EBX,
                                     regs.UC_X86_REG_ECX, regs.UC_X86_REG_EDX), CPUID_VALUES):
            machine.reg_write(register, output)
    machine.emu_start(instruction.address + instruction.size, STOP, count=1000)
    if machine.reg_read(ip) != STOP:
        raise AssertionError("native completion did not return")
    if operation in PORTS and operation.startswith("in") and not dead:
        if machine.reg_read(ax) & mask != value & mask:
            raise AssertionError("port input result was lost")
    if operation in MEMORY and mapped:
        expected = bytearray(b"\xA5" * PAGE)
        if operation in ("sgdt", "sidt"):
            expected[13:13 + len(descriptor)] = descriptor
        if bytes(machine.mem_read(DATA, PAGE)) != expected:
            raise AssertionError("descriptor completion modified unrelated memory")
    if operation == "cpuidex":
        expected = bytearray(b"\xA5" * PAGE)
        expected[13:29] = struct.pack("<4I", *CPUID_VALUES)
        if bytes(machine.mem_read(DATA, PAGE)) != expected:
            raise AssertionError("CPUID outputs were reordered, truncated or stored at the wrong address")
    if machine.reg_read(bx) != preserved:
        raise AssertionError("callee-saved RBX/EBX was not restored")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--compiler", type=Path,
                      default=Path(__file__).resolve().parents[1] / "build-neverc/bin/neverc")
    mode.add_argument("--llc", type=Path, help="test the LLVM fork in both code modes")
    parser.add_argument("--backend-llc", type=Path, help="compile NeverC's IR using the LLVM fork")
    args = parser.parse_args()
    if args.llc and args.backend_llc:
        parser.error("--backend-llc cannot be combined with --llc")
    count = 0
    with tempfile.TemporaryDirectory(prefix="neverc-system-") as temp:
        for bits in ((32, 64) if args.llc else (64,)):
            for opt in ("O0", "O2"):
                for operation in (*PORTS, *MEMORY, "cli", "sti", "cpuidex"):
                    for dead in ((False, True) if operation in PORTS and operation.startswith("in") else (False,)):
                        name, code = compile_case(args, Path(temp), operation, dead, opt, bits)
                        for sample in VALUES:
                            for mapped in ((True, False) if operation in MEMORY else (True,)):
                                try:
                                    execute(code, operation, dead, bits, sample, mapped)
                                except (AssertionError, unicorn.UcError) as error:
                                    raise AssertionError(f"{name}, sample={sample}, mapped={mapped}: {error}") from error
                                count += 1
    print(f"PASS: {count} system instruction transport/completion cases, native LLVM IR and machine code")


if __name__ == "__main__":
    main()
