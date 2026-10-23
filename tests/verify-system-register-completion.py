#!/usr/bin/env python3
"""Verify native CR/DR operand transport, ordering and result retention.

Requires capstone, lief and unicorn. Intercept CR/DR MOVs and supply controlled
completions; never execute privileged accesses on the host. This qualifies
compiler behavior, not page-table changes, debug traps or hardware privileges.
DR4/DR5 must retain their encodings and their environment-dependent behavior.
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
VARIANTS = ("read", "dead", "twice", "write", "writes", "same_writes",
            "write_read", "memory_read", "memory_write", "flags_read", "flags_write")
SAMPLES = ((0, 0xFFFFFFFFFFFFFFFF), (0xFEDCBA9876543210, 0x8123456789ABCDEF),
           (0xFFFFFFFFFFFFFFFF, 1))


def compile_case(args, directory, bank, number, variant, opt, bits):
    name = f"{bank}{number}_{variant}_{opt}_{bits}_{args.spelling}"
    ir, output = directory / (name + ".ll"), directory / (name + ".obj")
    suffix = "" if bits == 64 else "32"
    read, write = f"llvm.x86.read{bank}{suffix}", f"llvm.x86.write{bank}{suffix}"
    word = f"i{bits}"
    if args.llc:
        read_call = lambda value: f"%{value} = call {word} @{read}(i32 {number})"
        write_call = lambda value: f"call void @{write}(i32 {number}, {word} %{value})"
        body = []
        if variant.startswith("memory_"):
            body.append(f"store {word} %a, ptr %pointer")
        if variant.startswith("flags_"):
            body.extend((f"%sum = add {word} %a, %b", f"%carry = icmp ult {word} %sum, %a"))
        if variant in ("write", "writes", "same_writes", "write_read"):
            body.append(write_call("a"))
        if variant in ("writes", "memory_write", "flags_write"):
            body.append(write_call("b"))
        if variant == "same_writes":
            body.append(write_call("a"))
        if variant in ("read", "dead", "twice", "write_read", "memory_read", "flags_read"):
            body.append(read_call("first"))
        if variant == "twice":
            body.extend((read_call("second"), f"%result = xor {word} %first, %second"))
        elif variant.startswith("memory_"):
            body.append(f"%result = load {word}, ptr %pointer")
        elif variant.startswith("flags_"):
            body.append(f"%result = zext i1 %carry to {word}")
        elif variant not in ("read", "write_read"):
            body.append(f"%result = xor {word} %a, %b")
        result = "first" if variant in ("read", "write_read") else "result"
        ir.write_text(f"declare {word} @{read}(i32 immarg)\n"
                      f"declare void @{write}(i32 immarg, {word})\n"
                      f"define {word} @probe({word} %a, {word} %b, ptr %pointer) {{\n " +
                      "\n ".join(body) + f"\n ret {word} %{result}\n}}\n")
    else:
        source = directory / (name + ".c")
        if args.spelling == "builtin":
            read_c = f"__builtin_ia32_read{bank}({number})"
            write_c = lambda value: f"__builtin_ia32_write{bank}({number}, {value});"
            header = ""
        elif bank == "cr":
            read_c = f"__readcr{number}()"
            write_c = lambda value: f"__writecr{number}({value});"
            header = "#include <intrin.h>\n"
        else:
            read_c = f"__readdr({number})"
            write_c = lambda value: f"__writedr({number}, {value});"
            header = "#include <intrin.h>\n"
        body = []
        if variant.startswith("memory_"):
            body.append("*pointer = a;")
        if variant.startswith("flags_"):
            body.append("unsigned long long carry = (a + b) < a;")
        if variant in ("write", "writes", "same_writes", "write_read"):
            body.append(write_c("a"))
        if variant in ("writes", "memory_write", "flags_write"):
            body.append(write_c("b"))
        if variant == "same_writes":
            body.append(write_c("a"))
        if variant in ("read", "dead", "twice", "write_read", "memory_read", "flags_read"):
            body.append(f"unsigned long long first = {read_c};")
        if variant == "twice":
            result = f"first ^ {read_c}"
        elif variant.startswith("memory_"):
            result = "*pointer"
        elif variant.startswith("flags_"):
            result = "carry"
        elif variant in ("read", "write_read"):
            result = "first"
        else:
            result = "a ^ b"
        source.write_text(header + "unsigned long long probe(unsigned long long a, "
                          "unsigned long long b, unsigned long long *pointer) {\n " +
                          "\n ".join(body) + f"\n return {result};\n}}\n")
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
        if " asm " in text or not re.search(r"\bcall\b[^@\n]*@llvm\.x86\.(read|write)" + bank, text):
            raise AssertionError(f"{name}: expected native LLVM register intrinsics")
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
    return name, bytes(next(s for s in image.sections if s.name == ".text" and len(s.content)).content)


def execute(code, bank, number, variant, bits, sample):
    mask = (1 << bits) - 1
    a, b = (value & mask for value in sample)
    completion = (0xBADC0FFEE1234567 & mask, 0x7654321089ABCDEF & mask)
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64 if bits == 64 else unicorn.UC_MODE_32)
    ip = regs.UC_X86_REG_RIP if bits == 64 else regs.UC_X86_REG_EIP
    sp = regs.UC_X86_REG_RSP if bits == 64 else regs.UC_X86_REG_ESP
    ax = regs.UC_X86_REG_RAX if bits == 64 else regs.UC_X86_REG_EAX
    pack = lambda value: struct.pack("<Q" if bits == 64 else "<I", value)
    for address in (CODE, STACK, DATA):
        machine.mem_map(address, PAGE)
    machine.mem_write(CODE, code)
    machine.mem_write(DATA, b"\xA5" * PAGE)
    pointer = DATA + 16
    stack_pointer = STACK + PAGE - 64 - bits // 8
    machine.reg_write(sp, stack_pointer)
    machine.mem_write(stack_pointer, pack(STOP))
    if bits == 64:
        for register, value in zip((regs.UC_X86_REG_RCX, regs.UC_X86_REG_RDX, regs.UC_X86_REG_R8), (a, b, pointer)):
            machine.reg_write(register, value)
    else:
        machine.mem_write(stack_pointer + 4, pack(a) + pack(b) + pack(pointer))
    saved = {}
    for index, name in enumerate(("BX", "SI", "DI", "BP")):
        register = getattr(regs, "UC_X86_REG_" + ("R" if bits == 64 else "E") + name)
        saved[register] = (0xA123456789ABCDE0 + index) & mask
        machine.reg_write(register, saved[register])

    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if bits == 64 else capstone.CS_MODE_32)
    decoder.detail = True
    accesses = []
    for instruction in decoder.disasm(code, CODE):
        if instruction.mnemonic != "mov":
            continue
        names = [instruction.reg_name(o.reg) if o.type == capstone.x86.X86_OP_REG else ""
                 for o in instruction.operands]
        if any(re.fullmatch(r"[cd]r\d+", name) for name in names):
            if bank + str(number) not in names:
                raise AssertionError(f"wrong system register encoding: {names}")
            accesses.append((instruction, names[0] == bank + str(number)))
    expected_writes = []
    if variant in ("write", "writes", "same_writes", "write_read"):
        expected_writes.append(a)
    if variant in ("writes", "memory_write", "flags_write"):
        expected_writes.append(b)
    if variant == "same_writes":
        expected_writes.append(a)
    expected_reads = 2 if variant == "twice" else int(variant in
        ("read", "dead", "write_read", "memory_read", "flags_read"))
    if [write for _, write in accesses] != [True] * len(expected_writes) + [False] * expected_reads:
        raise AssertionError("system register accesses were removed, merged or reordered")
    cursor, reads, writes = CODE, 0, 0
    for instruction, write in accesses:
        machine.emu_start(cursor, instruction.address, count=1000)
        if machine.reg_read(ip) != instruction.address:
            raise AssertionError("did not reach the native register access")
        operand = instruction.operands[1 if write else 0]
        if operand.type != capstone.x86.X86_OP_REG or operand.size * 8 != bits:
            raise AssertionError("wrong general-register operand width")
        register = getattr(regs, "UC_X86_REG_" + instruction.reg_name(operand.reg).upper())
        if write:
            if machine.reg_read(register) != expected_writes[writes]:
                raise AssertionError("system register input was truncated or changed")
            writes += 1
        else:
            machine.reg_write(register, completion[reads])
            reads += 1
        # CR/DR MOVs leave arithmetic flags undefined. Pick the opposite carry
        # value to expose any compiler dependency carried across the access.
        flags = machine.reg_read(regs.UC_X86_REG_EFLAGS) & ~0x8D5
        if (a + b) <= mask:
            flags |= 0x8D5
        machine.reg_write(regs.UC_X86_REG_EFLAGS, flags | 2)
        if variant.startswith("memory_"):
            if bytes(machine.mem_read(pointer, bits // 8)) != pack(a):
                raise AssertionError("ordinary store crossed the register access")
            machine.mem_write(pointer, pack(completion[1]))
        cursor = instruction.address + instruction.size
    machine.emu_start(cursor, STOP, count=1000)
    if machine.reg_read(ip) != STOP:
        raise AssertionError("native register completion did not return")
    expected = a ^ b
    if variant in ("read", "write_read"):
        expected = completion[0]
    elif variant == "twice":
        expected = completion[0] ^ completion[1]
    elif variant.startswith("memory_"):
        expected = completion[1]
    elif variant.startswith("flags_"):
        expected = int(a + b > mask)
    if machine.reg_read(ax) != expected:
        raise AssertionError(f"wrong completion result: {machine.reg_read(ax):#x} != {expected:#x}")
    if any(machine.reg_read(register) != value for register, value in saved.items()):
        raise AssertionError("nonvolatile register was not restored")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--compiler", type=Path,
                      default=Path(__file__).resolve().parents[1] / "build-neverc/bin/neverc")
    mode.add_argument("--llc", type=Path)
    parser.add_argument("--backend-llc", type=Path)
    parser.add_argument("--spelling", choices=("builtin", "microsoft"), default="builtin")
    parser.add_argument("--variant", choices=VARIANTS)
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    if args.llc and args.backend_llc:
        parser.error("--backend-llc applies only to NeverC IR")
    with tempfile.TemporaryDirectory(prefix="system-register-completion-") as temporary:
        directory = args.keep or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        count = 0
        for bits in ((32, 64) if args.llc else (64,)):
            for bank, numbers in (("cr", (0, 2, 3, 4, 8) if bits == 64 else (0, 2, 3, 4)), ("dr", range(8))):
                for number in numbers:
                    for variant in ((args.variant,) if args.variant else VARIANTS):
                        for opt in ("O0", "O2"):
                            name, code = compile_case(args, directory, bank, number, variant, opt, bits)
                            for sample in SAMPLES:
                                try:
                                    execute(code, bank, number, variant, bits, sample)
                                except Exception as error:
                                    raise AssertionError(f"{name}, {sample}: {error}") from error
                                count += 1
        print(f"PASS: {count} controlled CR/DR completions")


if __name__ == "__main__":
    main()
