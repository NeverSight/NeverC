#!/usr/bin/env python3
"""Exercise native SVM and supervisor state in disposable QEMU TCG guests.

Qualifies VMSAVE/VMLOAD MSR round trips, INVLPGA fallthrough, AC and GS-base
changes, GIF interrupt masking and HLT resumption. VMFUNC needs VMX non-root
setup and is covered only by the separate transport/CPL3 fault harness.
Requires QEMU, clang, ld.lld and GNU objcopy. No host privileged state changes.
"""

import argparse
import importlib.util
import shutil
import tempfile
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "guest", Path(__file__).with_name("verify-descriptor-state-qemu.py"))
GUEST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GUEST)


def source(mode, llvm_route):
    declarations = []
    if llvm_route:
        for operation in ("vmload", "vmsave", "invlpga", "stgi", "clgi", "clac", "stac", "hlt", "swapgs"):
            if operation == "swapgs" and mode == 32:
                continue
            api = operation + ("32" if mode == 32 and operation in ("vmload", "vmsave", "invlpga") else "")
            arguments = "__UINTPTR_TYPE__, unsigned" if operation == "invlpga" else "__UINTPTR_TYPE__" if operation in ("vmload", "vmsave") else "void"
            declarations.append(f'extern void __builtin_ia32_{operation}({arguments}) __asm__("llvm.x86.{api}");')
    body = declarations + [r'''
typedef __UINTPTR_TYPE__ word;
typedef unsigned long long u64;
extern void timer_interrupt(void);
volatile unsigned timer_ticks;
static unsigned char vmcb[4096] __attribute__((aligned(4096)));
static u64 idt[512] __attribute__((aligned(16)));
static u64 read_msr(unsigned index) {
  unsigned a, d;
  __asm__ volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(index) : "memory");
  return ((u64)d << 32) | a;
}
static void write_msr(unsigned index, u64 value) {
  __asm__ volatile("wrmsr" : : "c"(index), "a"((unsigned)value), "d"((unsigned)(value >> 32)) : "memory");
}
static word flags(void) {
  word value;
  __asm__ volatile("pushf; pop %0" : "=r"(value) : : "memory");
  return value;
}
static void out(unsigned short port, unsigned char value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "d"(port) : "memory");
}
static unsigned char in(unsigned short port) {
  unsigned char value;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "d"(port) : "memory");
  return value;
}
unsigned probe(void) {
  unsigned a = 0x80000001, b, c = 0, d;
  __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
''']
    checks = 0

    def check(expression):
        nonlocal checks
        checks += 1
        body.append(f"  if (!({expression})) return {32 + checks};")

    check("c & 4")  # SVM must actually be available in this guest.
    body += ["  write_msr(0xc0000080, read_msr(0xc0000080) | 4096);",
             "  u64 saved_star = read_msr(0xc0000081);",
             "  u64 saved_kernel = read_msr(0xc0000102);",
             "  u64 saved_sysenter = read_msr(0x174);",
             "  __builtin_ia32_vmsave((word)vmcb);",
             "  write_msr(0xc0000081, 0x0020001000000040ULL);",
             "  write_msr(0xc0000102, 0x123450);",
             "  write_msr(0x174, 0x20);"]
    check("read_msr(0xc0000081) == 0x0020001000000040ULL")
    check("read_msr(0xc0000102) == 0x123450")
    check("read_msr(0x174) == 0x20")
    body.append("  __builtin_ia32_vmload((word)vmcb);")
    check("read_msr(0xc0000081) == saved_star")
    check("read_msr(0xc0000102) == saved_kernel")
    check("read_msr(0x174) == saved_sysenter")
    body += ["  __builtin_ia32_invlpga((word)vmcb, 1);",
             "  __builtin_ia32_stac();"]
    check("flags() & (1U << 18)")
    body.append("  __builtin_ia32_clac();")
    check("!(flags() & (1U << 18))")
    if mode == 64:
        body += ["  u64 saved_gs = read_msr(0xc0000101);",
                 "  write_msr(0xc0000101, 0x12345678);",
                 "  write_msr(0xc0000102, 0x87654321);",
                 "  __builtin_ia32_swapgs();"]
        check("read_msr(0xc0000101) == 0x87654321 && read_msr(0xc0000102) == 0x12345678")
        body.append("  __builtin_ia32_swapgs();")
        check("read_msr(0xc0000101) == 0x12345678 && read_msr(0xc0000102) == 0x87654321")
        body += ["  write_msr(0xc0000101, saved_gs);",
                 "  write_msr(0xc0000102, saved_kernel);"]
    slot = 64 if mode == 64 else 32
    selector = 8 if mode == 64 else 24
    body += ["  u64 handler = (word)timer_interrupt;",
             f"  idt[{slot}] = (handler & 65535) | ({selector}ULL << 16) | (0x8eULL << 40) | ((handler >> 16 & 65535) << 48);"]
    if mode == 64:
        body.append(f"  idt[{slot + 1}] = handler >> 32;")
    body += ["  struct { unsigned short limit; word base; } __attribute__((packed)) idtr = {sizeof(idt)-1, (word)idt};",
             '  __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");',
             "  out(0x20, 0x11); out(0xa0, 0x11); out(0x21, 0x20); out(0xa1, 0x28);",
             "  out(0x21, 4); out(0xa1, 2); out(0x21, 1); out(0xa1, 1);",
             "  out(0x21, 0xfe); out(0xa1, 0xff);",
             "  out(0x43, 0x36); out(0x40, 0x9c); out(0x40, 0x2e);",  # 100 Hz.
             "  __builtin_ia32_clgi();",
             '  __asm__ volatile("sti" : : : "memory");',
             "  unsigned pending = 0;",
             "  for (unsigned i = 0; i < 5000000; ++i) {",
             "    out(0x20, 0x0a); if (in(0x20) & 1) { pending = 1; break; }",
             "  }"]
    check("pending && timer_ticks == 0")
    body += ["  __builtin_ia32_stgi();",
             "  unsigned before = timer_ticks;",
             "  __builtin_ia32_hlt();",
             '  __asm__ volatile("cli" : : : "memory");']
    check("timer_ticks > before")
    body += ["  return 16;", "}"]
    return "\n".join(body) + "\n", checks + 1


def boot(mode):
    word = "q" if mode == 64 else "l"
    accumulator = "%rax" if mode == 64 else "%eax"
    ticks = "timer_ticks(%rip)" if mode == 64 else "timer_ticks"
    return GUEST.boot(mode) + f"""
.section .text
.code{mode}
.global timer_interrupt
timer_interrupt:
  push{word} {accumulator}
  incl {ticks}
  movb $0x20, %al
  outb %al, $0x20
  pop{word} {accumulator}
  iret{word}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path("build-neverc/bin/neverc"))
    backend = parser.add_mutually_exclusive_group()
    backend.add_argument("--llc", type=Path)
    backend.add_argument("--backend-llc", type=Path)
    for option, tool in (("clang", "clang"), ("linker", "ld.lld"), ("objcopy", "objcopy"), ("qemu", "qemu-system-x86_64")):
        parser.add_argument("--" + option, default=shutil.which(tool))
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    if not all((args.clang, args.linker, args.objcopy, args.qemu)):
        parser.error("QEMU, clang, ld.lld and GNU objcopy are required")
    for key in ("compiler", "llc", "backend_llc"):
        if getattr(args, key):
            setattr(args, key, getattr(args, key).resolve())
    with tempfile.TemporaryDirectory(prefix="virtualization-cpl0-") as temporary:
        directory = args.keep or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        count = sum(GUEST.check_guest(args, directory, mode, opt, source, boot)
                    for mode in ((32, 64) if args.llc else (64,)) for opt in ("O0", "O2"))
    print(f"PASS: {count} CPL0 bounded virtualization/state checks in QEMU TCG")


if __name__ == "__main__":
    main()
