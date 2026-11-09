#!/usr/bin/env python3
"""Check native register VMREAD and memory VMWRITE through controlled completions.

This qualifies the emitted instruction operands, full values, failure result,
status, memory barriers and ABI preservation. It does not execute a host VMCS.
Requires capstone, lief and unicorn.
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
SOURCE, ORDINARY = DATA + 13, DATA + 128
SENTINEL = 0xB1C2D3E4F5A68798
RESULTS = (0x81234567FEDCBA98, 0xF0E1D2C3B4A59687)
VALUES = ((0, 0, 0), (0xD012345689ABCDEF, 0xAABBCCDD12345678, 0x91726354AABBCCDD),
          (0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0x8000000080000000))


def compile_case(args, directory, operation, variant, bits, opt):
    name = f"{operation}_{variant}_{bits}_{opt}"
    ir, obj = directory / (name + ".ll"), directory / (name + ".obj")
    word = f"i{bits}"
    read = operation == "vmread.reg"
    second = word if read else "ptr"
    api = "llvm.x86." + operation + ("32" if bits == 32 else "")
    ty = "{ " + word + ", i8 }" if read else "i8"
    count = 2 if variant == "twice" else 1
    keep_value = variant not in ("dead", "status")
    keep_status = variant not in ("dead", "value")
    if args.llc:
        body = [f"store {word} %seed, ptr %ordinary"] if variant == "memory" else []
        terms = ["%field", "%prior" if read else "%address", "%seed"]
        if not read:
            body.append(f"%address = ptrtoint ptr %prior to {word}")
        for index in range(count):
            body.append(f"%call{index} = call {ty} @{api}({word} %field, {second} %prior)")
            if read:
                body += [f"%value{index} = extractvalue {ty} %call{index}, 0",
                         f"%status{index} = extractvalue {ty} %call{index}, 1"]
                if keep_value:
                    terms.append(f"%value{index}")
            if keep_status:
                status = f"%status{index}" if read else f"%call{index}"
                body += [f"%wide{index} = zext i8 {status} to {word}",
                         f"%flags{index} = shl {word} %wide{index}, {8 + index * 8}"]
                terms.append(f"%flags{index}")
        if variant == "memory":
            body.append(f"%memory = load {word}, ptr %ordinary")
            terms.append("%memory")
        result = terms[0]
        for index, term in enumerate(terms[1:]):
            body.append(f"%xor{index} = xor {word} {result}, {term}")
            result = f"%xor{index}"
        body.append(f"ret {word} {result}")
        ir.write_text(f"declare {ty} @{api}({word}, {second})\n"
                      f"define {word} @probe({word} %field, {second} %prior, ptr %ordinary, {word} %seed) {{\n"
                      + "\n".join(body) + "\n}\n")
    else:
        source = directory / (name + ".c")
        c_word = "unsigned long long"
        body = ["*ordinary = seed;"] if variant == "memory" else []
        terms = ["field", "prior" if read else f"({c_word})prior", "seed"]
        for index in range(count):
            if read:
                body += [f"unsigned char status{index};",
                         f"{c_word} value{index} = __builtin_ia32_vmread_reg(field, prior, &status{index});"]
                if keep_value:
                    terms.append(f"value{index}")
            else:
                body.append(f"unsigned char status{index} = __builtin_ia32_vmwrite_mem(field, prior);")
            if keep_status:
                terms.append(f"(({c_word})status{index} << {8 + index * 8})")
        if variant == "memory":
            terms.append("*ordinary")
        body.append("return " + " ^ ".join(terms) + ";")
        source.write_text(f"{c_word} probe({c_word} field, {c_word if read else 'const void *'} prior, "
                          f"{c_word} *ordinary, {c_word} seed) {{\n" + "\n".join(body) + "\n}\n")
        common = [str(args.compiler), "--target=x86_64-pc-windows-msvc", "-" + opt,
                  "-Werror", "-ffreestanding", "-fno-lto", "-fno-stack-protector",
                  "-mllvm", "-verify-machineinstrs", str(source)]
        commands = [common + ["-S", "-emit-llvm", "-o", str(ir)]]
        if not args.backend_llc:
            commands.append(common + ["-c", "-o", str(obj)])
        for command in commands:
            run = subprocess.run(command, capture_output=True, text=True)
            if run.returncode:
                raise AssertionError(f"{name}: frontend failed:\n{run.stderr}")
        text = ir.read_text()
        if " asm " in text or not re.search(r"\bcall\b[^@\n]*@" + re.escape(api) + r"\(", text):
            raise AssertionError(f"{name}: expected a native intrinsic")
    backend = args.llc or args.backend_llc
    if backend:
        arch = "x86_64" if bits == 64 else "i686"
        run = subprocess.run([str(backend), "-mtriple=" + arch + "-pc-windows-msvc", "-" + opt,
                              "-verify-machineinstrs", "-filetype=obj", str(ir), "-o", str(obj)],
                             capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(f"{name}: backend failed:\n{run.stderr}")
    image = lief.parse(str(obj))
    if image is None:
        raise AssertionError(f"{name}: missing COFF object")
    return name, bytes(next(s for s in image.sections if s.name == ".text" and len(s.content)).content)


def execute(code, operation, variant, bits, values, statuses, mapped):
    read = operation == "vmread.reg"
    mask = (1 << bits) - 1
    field, prior, seed = (value & mask for value in values)
    if not read:
        prior = SOURCE
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64 if bits == 64 else unicorn.UC_MODE_32)
    reg = lambda name: getattr(regs, "UC_X86_REG_" + ("R" if bits == 64 else "E") + name)
    pack = lambda value: struct.pack("<Q" if bits == 64 else "<I", value & mask)
    for base in (CODE, STACK, DATA):
        machine.mem_map(base, PAGE)
    # Put an unmapped source on a separate page while ordinary memory stays valid.
    if not mapped:
        prior = seed | (1 << (bits - 1)) | 13
    machine.mem_write(CODE, code)
    machine.mem_write(SOURCE, pack(SENTINEL))
    machine.mem_write(ORDINARY, pack(SENTINEL))
    stack_pointer = STACK + PAGE - 64 - bits // 8
    machine.reg_write(reg("SP"), stack_pointer)
    machine.mem_write(stack_pointer, pack(STOP))
    arguments = (field, prior, ORDINARY, seed)
    if bits == 64:
        for register, value in zip((regs.UC_X86_REG_RCX, regs.UC_X86_REG_RDX,
                                    regs.UC_X86_REG_R8, regs.UC_X86_REG_R9), arguments):
            machine.reg_write(register, value)
    else:
        machine.mem_write(stack_pointer + 4, b"".join(pack(value) for value in arguments))
    saved = {reg(name): (0xA1B2C3D4E5F60710 + index) & mask
             for index, name in enumerate(("BX", "BP", "SI", "DI"))}
    for register, value in saved.items():
        machine.reg_write(register, value)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if bits == 64 else capstone.CS_MODE_32)
    decoder.detail = True
    instructions = [i for i in decoder.disasm(code, CODE) if i.mnemonic == operation.split(".")[0]]
    if len(instructions) != len(statuses):
        raise AssertionError("VMCS accesses were removed or merged")
    start = CODE
    result = field ^ prior ^ seed
    for index, (instruction, status) in enumerate(zip(instructions, statuses)):
        machine.emu_start(start, instruction.address, count=1000)
        if machine.reg_read(reg("IP")) != instruction.address:
            raise AssertionError("did not reach the VMCS instruction")
        operands = instruction.operands
        field_operand = operands[1] if read else operands[0]
        register = lambda operand: getattr(regs, "UC_X86_REG_" + instruction.reg_name(operand.reg).upper())
        if field_operand.type != capstone.x86.X86_OP_REG or machine.reg_read(register(field_operand)) != field:
            raise AssertionError("VMCS selector was changed")
        if read:
            if operands[0].type != capstone.x86.X86_OP_REG or operands[0].size != bits // 8:
                raise AssertionError("VMREAD must use the native-width register form")
            if status == 0:
                machine.reg_write(register(operands[0]), RESULTS[index] & mask)
            if variant not in ("dead", "status"):
                result ^= (RESULTS[index] if status == 0 else prior) & mask
        else:
            operand = operands[1]
            if operand.type != capstone.x86.X86_OP_MEM or operand.size != bits // 8:
                raise AssertionError("VMWRITE source must remain native-width memory")
            mem = operand.mem
            reg_value = lambda number: machine.reg_read(getattr(regs, "UC_X86_REG_" + instruction.reg_name(number).upper())) if number else 0
            address = (reg_value(mem.base) + reg_value(mem.index) * mem.scale + mem.disp) & mask
            if mem.segment or address != prior:
                raise AssertionError("VMWRITE source address changed")
            if not mapped:
                return  # No eager compiler load; do not invent a fault completion.
            if bytes(machine.mem_read(SOURCE, bits // 8)) != pack(SENTINEL):
                raise AssertionError("VMWRITE modified its source")
        if variant == "memory":
            if bytes(machine.mem_read(ORDINARY, bits // 8)) != pack(seed):
                raise AssertionError("ordinary store crossed the VMCS instruction")
            # Probe the conservative memory effect with an external completion.
            machine.mem_write(ORDINARY, pack(SENTINEL))
            result ^= SENTINEL & mask
        if variant not in ("dead", "value"):
            result ^= status << (8 + index * 8)
        flags = machine.reg_read(regs.UC_X86_REG_EFLAGS) & ~0x8D5
        machine.reg_write(regs.UC_X86_REG_EFLAGS, flags | (0x40 if status == 1 else 1 if status == 2 else 0))
        start = instruction.address + instruction.size
    machine.emu_start(start, STOP, count=1000)
    if machine.reg_read(reg("IP")) != STOP or machine.reg_read(reg("AX")) != result:
        raise AssertionError("VMCS lost a result, failure value, status or live input")
    if any(machine.reg_read(register) != value for register, value in saved.items()):
        raise AssertionError("VMCS did not restore callee-saved registers")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--compiler", type=Path, default=Path(__file__).resolve().parents[1] / "build-neverc/bin/neverc")
    mode.add_argument("--llc", type=Path)
    parser.add_argument("--backend-llc", type=Path)
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    if args.llc and args.backend_llc:
        parser.error("--backend-llc applies only to NeverC IR")
    total = 0
    with tempfile.TemporaryDirectory(prefix="vmcs-forms-") as temporary:
        directory = args.keep or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        for bits in ((32, 64) if args.llc else (64,)):
            for operation in ("vmread.reg", "vmwrite.mem"):
                variants = ("value", "status", "both", "dead", "twice", "memory") if operation == "vmread.reg" else ("status", "dead", "twice", "memory")
                for variant in variants:
                    for opt in ("O0", "O2"):
                        name, code = compile_case(args, directory, operation, variant, bits, opt)
                        statuses = [(a, b) for a in (0, 1, 2) for b in (0, 1, 2)] if variant == "twice" else [(a,) for a in (0, 1, 2)]
                        cases = [(values, status, True) for values in VALUES for status in statuses]
                        if operation == "vmwrite.mem":
                            cases.append((VALUES[1], statuses[0], False))
                        for values, status, mapped in cases:
                            try:
                                execute(code, operation, variant, bits, values, status, mapped)
                            except Exception as error:
                                raise AssertionError(f"{name}, {values}, {status}, mapped={mapped}: {error}") from error
                            total += 1
    print(f"PASS: {total} controlled VMCS register/memory completions")


if __name__ == "__main__":
    main()
