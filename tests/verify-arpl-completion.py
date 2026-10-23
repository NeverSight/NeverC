#!/usr/bin/env python3
"""Qualify full LLVM's i386 ARPL intrinsics, including real Linux IA32 execution.

NeverC targets x86-64/AArch64; ARPL has no long-mode encoding. This harness
tests the full fork's 32-bit API. Python's RPL oracle checks actual Unicorn
execution, all C object formats and assembly round trips. A freestanding IA32
process independently checks hardware results and compares memory faults with
raw ARPL, including read-only and inaccessible pages. Test assembly is only
the independent oracle and Linux syscall boundary, never the intrinsic body.
"""

import argparse
import importlib.util
import itertools
import platform
import struct
import subprocess
import tempfile
from pathlib import Path

import capstone
import unicorn
import unicorn.x86_const as regs


SPEC = importlib.util.spec_from_file_location(
    "descriptor", Path(__file__).with_name("verify-descriptor-query-completion.py"))
COMMON = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMMON)
run, text_section = COMMON.run, COMMON.text_section
CODE, STACK, DATA, PAGE = COMMON.CODE, COMMON.STACK, COMMON.DATA, COMMON.PAGE
STOP, CELL = COMMON.STOP, DATA + 13
PAIRS = tuple((0x8123aaf0 | a, 0xfedcb5d0 | b) for a in range(4) for b in range(4))


def module(memory, variant):
    ty = "i8" if memory else "{ i16, i8 }"
    params = "ptr, i16" if memory else "i16, i16"
    api = "llvm.x86.arpl." + ("mem" if memory else "reg")
    body = ["%d = trunc i32 %a to i16", "%s = trunc i32 %b to i16",
            "%address = ptrtoint ptr %cell to i32"]
    terms = ["%a", "%b", "%address"]
    for i in range(2 if variant == "twice" else 1):
        inputs = "ptr %cell, i16 %s" if memory else "i16 %d, i16 %s"
        body.append(f"%call{i} = call {ty} @{api}({inputs})")
        flag = f"%call{i}"
        if memory:
            body.append(f"%value{i} = load i16, ptr %cell, align 1")
        else:
            body += [f"%value{i} = extractvalue {ty} %call{i}, 0",
                     f"%flag{i} = extractvalue {ty} %call{i}, 1"]
            flag = f"%flag{i}"
        if variant not in ("dead", "status"):
            body.append(f"%wide_value{i} = zext i16 %value{i} to i32")
            terms.append(f"%wide_value{i}")
        if variant not in ("dead", "value"):
            body += [f"%wide_flag{i} = zext i8 {flag} to i32",
                     f"%status{i} = shl i32 %wide_flag{i}, {16 + i}"]
            terms.append(f"%status{i}")
    result = terms[0]
    for i, term in enumerate(terms[1:]):
        body.append(f"%xor{i} = xor i32 {result}, {term}")
        result = f"%xor{i}"
    body.append(f"ret i32 {result}")
    return f"declare {ty} @{api}({params})\ndefine i32 @probe(i32 %a, i32 %b, ptr %cell) {{\n" + "\n".join(body) + "\n}\n"


def compile_case(args, directory, memory, variant, opt, target):
    name = f"arpl-{'mem' if memory else 'reg'}-{variant}-{opt}-{COMMON.TRIPLES.index(target)}"
    ir, obj = directory / (name + ".ll"), directory / (name + ".o")
    ir.write_text(module(memory, variant))
    triple = "i686-" + target
    backend = [args.llc, "-mtriple=" + triple, "-" + opt, "-verify-machineinstrs", ir]
    run(backend + ["-filetype=obj", "-o", obj])
    if variant == "both":
        for dialect in ("att", "intel"):
            asm = directory / (name + "-" + dialect + ".s")
            assembled = asm.with_suffix(".o")
            run(backend + ["-filetype=asm", "-x86-asm-syntax=" + dialect, "-o", asm])
            run([args.llc.parent / "llvm-mc", "-triple=" + triple, "-filetype=obj", asm, "-o", assembled])
            assert text_section(obj) == text_section(assembled), "ARPL assembly round trip changed bytes"
    return text_section(obj)


def execute(code, memory, variant, a, b):
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    accesses = [i for i in decoder.disasm(code, CODE) if i.mnemonic == "arpl"]
    assert len(accesses) == (2 if variant == "twice" else 1), "ARPL was lost or merged"
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    for base in (CODE, STACK, DATA):
        machine.mem_map(base, PAGE)
    machine.mem_write(CODE, code)
    machine.mem_write(CELL, struct.pack("<H", a & 0xffff))
    sp = STACK + PAGE - 32
    machine.reg_write(regs.UC_X86_REG_ESP, sp)
    machine.mem_write(sp, struct.pack("<IIII", STOP, a, b, CELL))
    saved = {getattr(regs, "UC_X86_REG_" + r): 0xa1b2c300 + i
             for i, r in enumerate(("EBX", "EBP", "ESI", "EDI"))}
    for r, value in saved.items():
        machine.reg_write(r, value)
    start, result, prior = CODE, a ^ b ^ CELL, a & 0xffff
    reg = lambda r: machine.reg_read(getattr(regs, "UC_X86_REG_" + decoder.reg_name(r).upper())) if r else 0
    for i, instruction in enumerate(accesses):
        machine.emu_start(start, instruction.address, count=1000)
        assert machine.reg_read(regs.UC_X86_REG_EIP) == instruction.address
        dst, src = instruction.operands
        assert src.type == capstone.x86.X86_OP_REG and src.size == 2 and reg(src.reg) == b & 0xffff
        if memory:
            assert dst.type == capstone.x86.X86_OP_MEM and dst.size == 2, "ARPL memory was decomposed"
            m = dst.mem
            assert not m.segment and (reg(m.base) + reg(m.index) * m.scale + m.disp) & 0xffffffff == CELL
        else:
            prior = a & 0xffff
            assert dst.type == capstone.x86.X86_OP_REG and dst.size == 2 and reg(dst.reg) == prior
        changed = int((prior & 3) < (b & 3))
        expected = (prior & ~3) | (b & 3) if changed else prior
        # Isolate the actual instruction from compiler arithmetic on either
        # side; only ZF may change in this boundary's arithmetic flags.
        flags = machine.reg_read(regs.UC_X86_REG_EFLAGS)
        start = instruction.address + instruction.size
        machine.emu_start(instruction.address, start, count=1)
        after = machine.reg_read(regs.UC_X86_REG_EFLAGS)
        assert (after >> 6) & 1 == changed and (flags ^ after) & 0x895 == 0
        actual = struct.unpack("<H", machine.mem_read(CELL, 2))[0] if memory else reg(dst.reg)
        assert actual == expected, "destination's non-RPL bits or low RPL changed"
        if variant not in ("dead", "status"):
            result ^= expected
        if variant not in ("dead", "value"):
            result ^= changed << (16 + i)
        prior = expected
    machine.emu_start(start, STOP, count=1000)
    assert machine.reg_read(regs.UC_X86_REG_EIP) == STOP
    assert machine.reg_read(regs.UC_X86_REG_EAX) == result, "live inputs or completion result changed"
    assert all(machine.reg_read(r) == value for r, value in saved.items()), "callee-saved register corrupted"


NATIVE_IR = '''
declare {i16, i8} @llvm.x86.arpl.reg(i16, i16)
declare i8 @llvm.x86.arpl.mem(ptr, i16)
define i32 @adjust(i16 %d, i16 %s) {
  %r = call {i16, i8} @llvm.x86.arpl.reg(i16 %d, i16 %s)
  %v = extractvalue {i16, i8} %r, 0
  %f = extractvalue {i16, i8} %r, 1
  %value = zext i16 %v to i32
  %flag = zext i8 %f to i32
  %shift = shl i32 %flag, 16
  %result = or i32 %value, %shift
  ret i32 %result
}
define i32 @adjust_memory(ptr %d, i16 %s) {
  %f = call i8 @llvm.x86.arpl.mem(ptr %d, i16 %s)
  %result = zext i8 %f to i32
  ret i32 %result
}
'''

NATIVE_DRIVER = r'''
typedef unsigned short u16;
extern unsigned adjust(u16, u16);
extern unsigned adjust_memory(u16 *, u16);
static __attribute__((noinline)) unsigned char original(u16 *d, u16 s) {
  unsigned char zf;
  __asm__ volatile("arpl %w2, %0; setz %1" : "+m"(*d), "=q"(zf) : "r"(s) : "cc", "memory");
  return zf;
}
static int sys(unsigned n, unsigned a, unsigned b, unsigned c) {
  int result;
  __asm__ volatile("int $0x80" : "=a"(result) : "0"(n), "b"(a), "c"(b), "d"(c) : "memory");
  return result;
}
static __attribute__((noreturn)) void finish(unsigned result) {
  sys(1, result, 0, 0);
  __builtin_unreachable();
}
static u16 page[2048] __attribute__((aligned(4096)));
static int observe(unsigned which, unsigned protection, u16 d, u16 s) {
  page[0] = d;
  int pid = sys(2, 0, 0, 0);
  if (pid < 0) return -1;
  if (!pid) {
    if (sys(125, (unsigned)page, 4096, protection)) finish(90);
    unsigned zf = which ? adjust_memory(page, s) : original(page, s);
    finish(zf);
  }
  int status;
  if (sys(7, pid, (unsigned)&status, 0) != pid) return -2;
  return status;
}
void _start(void) {
  // This static IA32 executable cannot preload the host's no-core helper.
  // Disable dumping before expected fault children can invoke a core pipe.
  if (sys(172, 4, 0, 0)) finish(4); // prctl(PR_SET_DUMPABLE, 0)
  for (unsigned d = 0; d < 256; ++d)
    for (unsigned s = 0; s < 16; ++s) {
      u16 a = 0xaa00 | d, b = 0xb5e0 | s;
      unsigned change = (a & 3) < (b & 3);
      u16 expected = change ? (a & ~3) | (b & 3) : a;
      if (adjust(a, b) != (expected | (change << 16))) finish(1);
      u16 first = a, second = a;
      if (adjust_memory(&first, b) != original(&second, b) ||
          first != second || first != expected) finish(2);
    }
  for (unsigned d = 0; d < 4; ++d)
    for (unsigned s = 0; s < 4; ++s)
      for (unsigned protection = 0; protection <= 3; ++protection) {
        if (protection == 2) continue;
        int a = observe(0, protection, 0xaa00 | d, 0xb5e0 | s);
        int b = observe(1, protection, 0xaa00 | d, 0xb5e0 | s);
        if (a < 0 || a != b || a == (90 << 8)) finish(3);
      }
  finish(0);
}
'''


def native_oracle(args, directory):
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        print("SKIP: real ARPL execution needs Linux with IA32 execution support")
        return 0
    ir, src = directory / "native.ll", directory / "native.c"
    ir.write_text(NATIVE_IR)
    src.write_text(NATIVE_DRIVER)
    for opt in ("O0", "O2"):
        obj, driver, exe = (directory / ("native-" + opt + suffix) for suffix in (".o", "-driver.o", ".exe"))
        run([args.llc, "-mtriple=i386-unknown-linux-gnu", "-" + opt, "-filetype=obj", "-verify-machineinstrs", ir, "-o", obj])
        run([args.clang, "--target=i386-unknown-linux-gnu", "-" + opt, "-ffreestanding", "-fno-stack-protector",
             "-fno-pic", "-fno-pie", "-c", src, "-o", driver])
        run([args.linker, "-m", "elf_i386", "-e", "_start", "-o", exe, obj, driver])
        child = subprocess.run([str(exe.resolve())], capture_output=True, timeout=30)
        assert child.returncode == 0, f"native ARPL {opt} failed: {child.returncode}"
    return 2 * (4096 * 2 + 48)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llc", type=Path, required=True)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--linker", default="ld.lld")
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    args.llc = args.llc.resolve()
    with tempfile.TemporaryDirectory(prefix="arpl-") as temp:
        directory = args.keep or Path(temp)
        directory.mkdir(parents=True, exist_ok=True)
        count = 0
        for memory, variant, opt, target in itertools.product(
                (False, True), ("both", "value", "status", "dead", "twice"), ("O0", "O2"), COMMON.TRIPLES):
            code = compile_case(args, directory, memory, variant, opt, target)
            for a, b in PAIRS:
                try:
                    execute(code, memory, variant, a, b)
                except Exception as error:
                    raise AssertionError(f"{memory}/{variant}/{opt}/{target}/{a}/{b}: {error}") from error
                count += 1
        native = native_oracle(args, directory)
    print(f"PASS: {count} ARPL compiler/ISA cases; {native} real IA32 result/fault comparisons")


if __name__ == "__main__":
    main()
