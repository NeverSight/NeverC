#!/usr/bin/env python3
"""Qualify native INT 2C continuation, memory ordering and ABI transport.

Controlled completions preserve registers and change ordinary memory between
two interrupts. Linux x64 children separately compare native faults and resume
from the actual instruction through sigreturn. This does not model an arbitrary
interrupt handler's register ABI or prove a privileged interrupt-gate transfer.
Requires capstone, lief and unicorn. --llc also covers i386; --backend-llc
checks NeverC's native bitcode through the full LLVM fork.
"""

import argparse
import importlib.util
import itertools
import platform
import shutil
import signal
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
CODE, STACK, DATA, PAGE, STOP = COMMON.CODE, COMMON.STACK, COMMON.DATA, COMMON.PAGE, COMMON.STOP
VALUES = (0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0xdeadbeef)
SOURCE = """unsigned probe(unsigned *memory, unsigned input) {
  unsigned less = input < 0x80000000U;
  *memory = input;
  __builtin_ia32_int(44);
  unsigned first = *memory;
  __builtin_ia32_int(44);
  return first ^ *memory ^ input ^ less;
}
"""
IR = """declare void @llvm.x86.int(i8 immarg)
define i32 @probe(ptr %memory, i32 %input) {
  %less = icmp ult i32 %input, 2147483648
  %condition = zext i1 %less to i32
  store i32 %input, ptr %memory
  call void @llvm.x86.int(i8 44)
  %first = load i32, ptr %memory
  call void @llvm.x86.int(i8 44)
  %last = load i32, ptr %memory
  %r0 = xor i32 %first, %last
  %r1 = xor i32 %r0, %input
  %r2 = xor i32 %r1, %condition
  ret i32 %r2
}
"""


def compile_case(args, directory, mode, target, opt):
    triple = ("x86_64" if mode == 64 else "i686") + "-" + target
    base = directory / f"int2c-{mode}-{target}-{opt}"
    source, ir, bitcode, obj = (base.with_suffix(s) for s in (".c", ".ll", ".bc", ".o"))
    if args.llc:
        ir.write_text(f'target triple = "{triple}"\n' + IR)
        if opt == "O2":
            run([args.llc.parent / "opt", "-passes=default<O2>", "-S", ir, "-o", ir])
    else:
        source.write_text(SOURCE)
        compiler = [args.compiler, "--target=" + triple, "-" + opt, "-std=c23",
                    "-ffreestanding", "-fno-builtin-mimalloc", "-fno-lto", "-Werror",
                    "-mllvm", "-verify-machineinstrs", source]
        run(compiler + ["-S", "-emit-llvm", "-o", ir])
        text = ir.read_text()
        assert text.count("@llvm.x86.int(i8 44)") == 2, "INT calls lost or not using the shared native API"
        assert all(s not in text for s in ("noreturn", "unreachable", "llvm.x86.int2c", " asm "))
        if args.backend_llc:
            run(compiler + ["-c", "-emit-llvm", "-o", bitcode])
        else:
            run(compiler + ["-c", "-o", obj])
    backend = args.llc or args.backend_llc
    if backend:
        run([backend, "-mtriple=" + triple, "-" + opt, "-verify-machineinstrs", "-relocation-model=pic",
             "-filetype=obj", bitcode if args.backend_llc else ir, "-o", obj])
    return obj


def execute(code, mode, target, value):
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64 if mode == 64 else unicorn.UC_MODE_32)
    reg = lambda name: getattr(regs, "UC_X86_REG_" + ("R" if mode == 64 else "E") + name)
    mask = (1 << mode) - 1
    pack = lambda x: struct.pack("<Q" if mode == 64 else "<I", x & mask)
    for address in (CODE, STACK, DATA):
        machine.mem_map(address, PAGE)
    machine.mem_write(CODE, code)
    sp = STACK + PAGE - 128 - mode // 8
    machine.reg_write(reg("SP"), sp)
    machine.mem_write(sp, pack(STOP))
    windows = target == "pc-windows-msvc"
    names = ("BX", "BP", *(("SI", "DI") if mode == 32 or windows else ()),
             *(("12", "13", "14", "15") if mode == 64 else ()))
    saved = {reg(name): (0xa1b2c3d4e5f60710 + i) & mask for i, name in enumerate(names)}
    for register, initial in saved.items():
        machine.reg_write(register, initial)
    if mode == 32:
        machine.mem_write(sp + 4, pack(DATA) + pack(value))
    else:
        for register, argument in zip(("CX", "DX") if windows else ("DI", "SI"), (DATA, value)):
            machine.reg_write(reg(register), argument)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if mode == 64 else capstone.CS_MODE_32)
    interrupts = [i for i in decoder.disasm(code, CODE) if i.mnemonic == "int"]
    assert len(interrupts) == 2 and all(bytes(i.bytes) == b"\xcd\x2c" for i in interrupts)
    start, current, results = CODE, value, []
    for instruction, delta in zip(interrupts, (13, 29)):
        machine.emu_start(start, instruction.address, count=1000)
        assert machine.reg_read(reg("IP")) == instruction.address
        assert struct.unpack("<I", machine.mem_read(DATA, 4))[0] == current, "store moved across interrupt"
        current = (current + delta) & 0xffffffff
        machine.mem_write(DATA, struct.pack("<I", current))
        results.append(current)
        start = instruction.address + instruction.size
    machine.emu_start(start, STOP, count=1000)
    expected = results[0] ^ results[1] ^ value ^ int(value < 0x80000000)
    assert machine.reg_read(reg("IP")) == STOP and machine.reg_read(regs.UC_X86_REG_EAX) == expected
    assert machine.reg_read(reg("SP")) == sp + mode // 8, "stack pointer changed"
    assert all(machine.reg_read(r) == initial for r, initial in saved.items()), "nonvolatile register changed"


def check_microsoft_assertion(args, directory, opt):
    source, ir, bitcode, obj = (directory / ("assertion-" + opt + suffix)
                               for suffix in (".c", ".ll", ".bc", ".o"))
    source.write_text("#include <intrin.h>\nint assertion(void) { __int2c(); return 7; }\n")
    compiler = [args.compiler, "--target=x86_64-pc-windows-msvc", "-" + opt,
                "-ffreestanding", "-fno-builtin-mimalloc", "-fno-lto", source]
    run(compiler + ["-S", "-emit-llvm", "-o", ir])
    text = ir.read_text()
    assert "@llvm.x86.int(i8 44)" in text and "noreturn" in text
    if opt == "O2":
        assert "unreachable" in text
    assert "llvm.x86.int2c" not in text and " asm " not in text
    if args.backend_llc:
        run(compiler + ["-c", "-emit-llvm", "-o", bitcode])
        run([args.backend_llc, "-" + opt, "-verify-machineinstrs", "-filetype=obj", bitcode, "-o", obj])
    else:
        run(compiler + ["-c", "-o", obj])
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    instructions = list(decoder.disasm(text_section(obj), CODE))
    assert sum(bytes(i.bytes) == b"\xcd\x2c" for i in instructions) == 1
    assert not any(i.mnemonic == "call" for i in instructions), "private intrinsic became an external call"


DRIVER = r"""#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <ucontext.h>
#include <unistd.h>
extern unsigned probe(unsigned *, unsigned);
static volatile sig_atomic_t seen;
static void resume(int sig, siginfo_t *info, void *context) {
  (void)info;
  ucontext_t *state = context;
  const unsigned char *pc = (const unsigned char *)state->uc_mcontext.gregs[REG_RIP];
  if (sig != SIGSEGV || seen >= 2 || pc[0] != 0xcd || pc[1] != 0x2c)
    _exit(71);
  ++seen;
  state->uc_mcontext.gregs[REG_RIP] += 2;
}
__attribute__((noinline)) static unsigned original(unsigned *memory, unsigned input) {
  unsigned less = input < 0x80000000U;
  *memory = input;
  __asm__ volatile(".byte 0xcd, 0x2c" ::: "memory", "cc");
  unsigned first = *memory;
  __asm__ volatile(".byte 0xcd, 0x2c" ::: "memory", "cc");
  return first ^ *memory ^ input ^ less;
}
int main(int argc, char **argv) {
  prctl(PR_SET_DUMPABLE, 0);
  unsigned memory = 0;
  if (argc > 1) {
    if (argv[1][0] == 'o') return (int)original(&memory, 7);
    return (int)probe(&memory, 7);
  }
  struct sigaction action = {0};
  action.sa_sigaction = resume;
  action.sa_flags = SA_SIGINFO;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGSEGV, &action, 0)) return 72;
  const unsigned edges[] = {0, 1, 0x7fffffffU, 0x80000000U, 0xffffffffU, 0xdeadbeefU};
  unsigned random = 0x31415926;
  for (unsigned i = 0; i < 262; ++i) {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    unsigned input = i < 6 ? edges[i] : random;
    unsigned results[2];
    for (unsigned route = 0; route < 2; ++route) {
      seen = 0; memory = 0;
      results[route] = route ? probe(&memory, input) : original(&memory, input);
      if (seen != 2 || memory != input) return 73;
    }
    if (results[0] != results[1] || results[1] != (input ^ (input < 0x80000000U))) return 74;
  }
  return 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path("build-neverc/bin/neverc"))
    backend = parser.add_mutually_exclusive_group()
    backend.add_argument("--llc", type=Path)
    backend.add_argument("--backend-llc", type=Path)
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    for name in ("compiler", "llc", "backend_llc"):
        if getattr(args, name):
            setattr(args, name, getattr(args, name).resolve())
    cases = native = faults = assertions = 0
    with tempfile.TemporaryDirectory(prefix="interrupt-completion-") as temporary:
        directory = args.keep or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        for mode, target, opt in itertools.product((32, 64) if args.llc else (64,), COMMON.TRIPLES, ("O0", "O2")):
            obj = compile_case(args, directory, mode, target, opt)
            for value in VALUES:
                execute(text_section(obj), mode, target, value)
                cases += 1
            if mode == 64 and target == "unknown-linux-gnu" and platform.system() == "Linux" and platform.machine() == "x86_64":
                driver, executable = directory / "driver.c", directory / ("native-" + opt)
                driver.write_text(DRIVER)
                run([shutil.which("cc"), "-O2", "-fno-lto", driver, obj, "-o", executable])
                run([executable.resolve()])
                native += 262
                outcomes = [subprocess.run([executable.resolve(), route], capture_output=True).returncode
                            for route in ("original", "probe")]
                assert outcomes == [-signal.SIGSEGV] * 2, outcomes
                faults += 2
        if not args.llc:
            for opt in ("O0", "O2"):
                check_microsoft_assertion(args, directory, opt)
                assertions += 1
    print(f"PASS: {cases} interrupt completion checks; {native} native sigreturn comparisons; "
          f"{faults} native faults; {assertions} Microsoft assertion contracts")


if __name__ == "__main__":
    main()
