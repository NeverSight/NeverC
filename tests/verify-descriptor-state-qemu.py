#!/usr/bin/env python3
"""Run descriptor-state intrinsics at CPL0 in disposable QEMU TCG guests.

Tests real generated instructions against guest GDT/LDT/TSS and CR0 state in
32/64-bit modes. INVD is checked for encoding and successful fallthrough only:
TCG does not qualify physical cache discard behavior. No host privileged state
is accessed. The NeverC-to-fork route uses bitcode so LLVM's reader can upgrade
legacy constant expressions. Requires QEMU, clang, ld.lld and GNU objcopy.
"""

import argparse
import importlib.util
import shutil
import subprocess
import tempfile
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "state", Path(__file__).with_name("verify-descriptor-state-completion.py"))
STATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STATE)
run = STATE.run


def source(mode, llvm_route, address_bits=None):
    declarations = []
    if llvm_route:
        # Clang supplies only ordinary C lowering here. Assembly labels name
        # the new IR intrinsics, which are selected by the fork's llc.
        for case in STATE.cases(mode):
            operation, form, bits = case
            api = STATE.api_name(*case)
            argument = "void *" if form == "mem" else ("unsigned short" if form and operation not in STATE.READS else "void")
            declarations.append(f'extern {STATE.CTYPE[bits] if bits else "void"} __builtin_ia32_{api.replace(".", "_")}({argument}) __asm__("llvm.x86.{api}");')
    if address_bits and llvm_route:
        for operation in (*STATE.READS, *STATE.VERIFY, "lldt", "ltr", "lmsw",
                          "sgdt", "sidt", "lgdt", "lidt", "invlpg"):
            memory = operation not in ("sgdt", "sidt", "lgdt", "lidt", "invlpg")
            api = operation + (".mem" if memory else "") + ".address"
            c_name = "__builtin_ia32_" + api.replace(".", "_")
            result = "unsigned char" if operation in STATE.VERIFY else "void"
            suffix = "32" if mode == 32 else ""
            declarations.append(f'extern {result} {c_name}(__UINTPTR_TYPE__, unsigned, unsigned) __asm__("llvm.x86.{api}{suffix}");')
    if address_bits and llvm_route and mode == 32:
        for operation in ("sgdt", "sidt", "lgdt", "lidt"):
            declarations.append(f'extern void __builtin_ia32_{operation}16_address(unsigned, unsigned, unsigned) __asm__("llvm.x86.{operation}16.address32");')
    wrappers = []
    if address_bits:
        for operation in (*STATE.READS, *STATE.VERIFY, "lldt", "ltr", "lmsw"):
            wrappers.append(f"#define __builtin_ia32_{operation}_mem(p) __builtin_ia32_{operation}_mem_address((word)(p), {address_bits}, 0)")
    body = [*declarations, "typedef __UINTPTR_TYPE__ word;", *wrappers,
            "extern unsigned long long guest_gdt[10];",
            "static unsigned long long ldt[2] = {0, 0x00cf93000000ffffULL};",
            "static unsigned char tss[2][104];",
            "static void descriptor(unsigned index, const void *base, unsigned limit, unsigned access) {",
            "  unsigned long long address = (word)base;",
            "  guest_gdt[index] = (limit & 65535) | ((address & 65535) << 16) | (((address >> 16) & 255) << 32) |",
            "      ((unsigned long long)access << 40) | ((unsigned long long)(limit >> 16) << 48) | ((address >> 24 & 255) << 56);",
            "  guest_gdt[index + 1] = address >> 32;", "}",
            "unsigned probe(void) {", "  unsigned short selector;",
            "  struct { unsigned char before; unsigned short value; unsigned char after; } __attribute__((packed)) cell = {0xa5, 0, 0x5a};"]
    checks = 0

    def check(expression):
        nonlocal checks
        checks += 1
        body.append(f"  if (!({expression})) return {32 + checks};")

    def read_selector(operation, expected):
        for bits in ((16, 32, 64) if mode == 64 else (16, 32)):
            check(f"__builtin_ia32_{operation}_reg{bits}() == {expected}")
        body.append(f"  __builtin_ia32_{operation}_mem((void *)((char *)&cell + 1));")
        check(f"cell.value == {expected} && cell.before == 0xa5 && cell.after == 0x5a")

    body += ["  descriptor(4, ldt, sizeof(ldt) - 1, 0x82);",
             "  descriptor(6, tss[0], sizeof(tss[0]) - 1, 0x89);",
             "  descriptor(8, tss[1], sizeof(tss[1]) - 1, 0x89);",
             "  __builtin_ia32_lldt_reg(0x20);"]
    read_selector("sldt", "0x20")
    for selector, readable, writable in ((0, 0, 0), (8, 1, 0), (0x10, 1, 1), (0x0c, 1, 1), (0xffff, 0, 0)):
        body.append(f"  selector = {selector};")
        for operation, expected in (("verr", readable), ("verw", writable)):
            check(f"__builtin_ia32_{operation}_reg(selector) == {expected}")
            check(f"__builtin_ia32_{operation}_mem(&selector) == {expected}")
    body += ["  selector = 0;", "  __builtin_ia32_lldt_mem(&selector);"]
    read_selector("sldt", "0")
    check("__builtin_ia32_verr_reg(0x0c) == 0")
    body.append("  __builtin_ia32_ltr_reg(0x30);")
    read_selector("str", "0x30")
    check("(guest_gdt[6] >> 40 & 15) == 11")
    body += ["  selector = 0x40;", "  __builtin_ia32_ltr_mem(&selector);"]
    read_selector("str", "0x40")
    check("(guest_gdt[8] >> 40 & 15) == 11")
    body += ['  word before, after; __asm__ volatile("mov %%cr0, %0" : "=r"(before) : : "memory");']
    for bits in ((16, 32, 64) if mode == 64 else (16, 32)):
        # Non-64-bit SMSW r32 has undefined upper bits; only its low word is compared.
        mask = ((1 << bits) - 1) if mode == 64 else 65535
        check(f"(__builtin_ia32_smsw_reg{bits}() & {mask}ULL) == (before & {mask}ULL)")
    body.append("  __builtin_ia32_smsw_mem((void *)((char *)&cell + 1));")
    check("cell.value == (unsigned short)before && cell.before == 0xa5 && cell.after == 0x5a")
    body += ["  __builtin_ia32_lmsw_reg((unsigned short)(before ^ 8));",
             '  __asm__ volatile("mov %%cr0, %0" : "=r"(after) : : "memory");']
    check("after == (before ^ 8)")
    body += ["  selector = (unsigned short)(before & ~1U);", "  __builtin_ia32_lmsw_mem(&selector);",
             '  __asm__ volatile("mov %%cr0, %0" : "=r"(after) : : "memory");']
    check("after == before")  # LMSW cannot clear PE in protected mode.
    if address_bits:
        body += ["  struct table { unsigned short limit; word base; } __attribute__((packed));",
                 "  struct { unsigned char before; struct table value; unsigned char after; } __attribute__((packed)) table = {0xa5, {0, 0}, 0x5a};"]
        for store, load in (("sgdt", "lgdt"), ("sidt", "lidt")):
            body.append(f"  __builtin_ia32_{store}_address((word)&table.value, {address_bits}, 0);")
            check("table.before == 0xa5 && table.after == 0x5a")
            if store == "sgdt":
                check("table.value.base == (word)guest_gdt && table.value.limit == sizeof(guest_gdt) - 1")
            body += ["  struct table " + store + "_prior = table.value;",
                     f"  __builtin_ia32_{load}_address((word)&table.value, {address_bits}, 0);",
                     "  table.value.limit = 0; table.value.base = 0;",
                     f"  __builtin_ia32_{store}_address((word)&table.value, {address_bits}, 0);"]
            check(f"table.value.limit == {store}_prior.limit && table.value.base == {store}_prior.base")
        if mode == 32:
            for store, load in (("sgdt", "lgdt"), ("sidt", "lidt")):
                check(f"({store}_prior.base >> 24) == 0")
                body += [f"  table.value = {store}_prior;",
                         "  table.value.base |= 0xff000000U;",
                         f"  __builtin_ia32_{load}16_address((word)&table.value, 32, 0);",
                         f"  __builtin_ia32_{store}_address((word)&table.value, 32, 0);"]
                check(f"table.value.base == {store}_prior.base && table.value.limit == {store}_prior.limit")
                body += [f"  struct table {store}_expected;",
                         f'  __asm__ volatile(".byte 0x66,0x0f,0x01,{0 if store == "sgdt" else 8}" : : "a"(&{store}_expected) : "memory");',
                         f"  __builtin_ia32_{store}16_address((word)&table.value, 32, 0);"]
                check(f"table.value.base == {store}_expected.base && table.value.limit == {store}_expected.limit")
                check("table.before == 0xa5 && table.after == 0x5a")
        body.append(f"  __builtin_ia32_invlpg_address((word)tss, {address_bits}, 0);")
    body += ["  __builtin_ia32_invd();", "  return 16;", "}"]
    return "\n".join(body) + "\n", checks + 1


def boot(mode):
    common = """.section .multiboot,"a"
.balign 4
.long 0x1badb002, 0, -0x1badb002
.section .text
.code32
.global _start
_start:
  cli
  movl $0x90000, %esp
  lgdt guest_gdt_pointer
  ljmp $0x18, $protected
protected:
  movw $0x10, %ax
  movw %ax, %ds
  movw %ax, %es
  movw %ax, %ss
"""
    if mode == 64:
        common += """  movl $pml4, %eax
  movl %eax, %cr3
  movl %cr4, %eax
  orl $0x20, %eax
  movl %eax, %cr4
  movl $0xc0000080, %ecx
  rdmsr
  orl $0x100, %eax
  wrmsr
  movl %cr0, %eax
  orl $0x80000000, %eax
  movl %eax, %cr0
  ljmp $8, $long_mode
.code64
long_mode:
  movq $0x90000, %rsp
"""
    common += """  call probe
  movw $0xf4, %dx
  outl %eax, %dx
1: hlt
  jmp 1b
.section .data
.balign 8
.global guest_gdt
guest_gdt:
  .quad 0, 0x00af9a000000ffff, 0x00cf92000000ffff, 0x00cf9a000000ffff
  .zero 48
guest_gdt_pointer:
  .word 79
  .long guest_gdt
"""
    if mode == 64:
        common += """.balign 4096
pml4: .quad pdpt + 3
  .zero 4088
pdpt: .quad pd + 3
  .zero 4088
pd:
  .set page, 0
  .rept 512
  .quad (page << 21) | 0x83
  .set page, page + 1
  .endr
"""
    return common


def check_guest(args, directory, mode, opt, source_builder=source, boot_builder=boot):
    prefix = directory / f"guest-{mode}-{opt}"
    src, ir, obj, asm, boot_obj, script, elf, kernel = (prefix.with_suffix(suffix) for suffix in
                                                       (".c", ".ll", ".o", ".s", ".boot.o", ".ld", ".elf", ".multiboot"))
    code, checks = source_builder(mode, bool(args.llc))
    src.write_text(code)
    asm.write_text(boot_builder(mode))
    triple = ("x86_64" if mode == 64 else "i686") + "-unknown-linux-gnu"
    compiler = [args.clang if args.llc else args.compiler, "--target=" + triple, "-" + opt, "-ffreestanding",
                "-fno-stack-protector", "-fno-pic", "-fno-pie", "-fno-lto", "-mno-red-zone", "-mno-sse",
                "-mno-sse2", "-mno-mmx", "-Werror", src]
    if not args.llc:
        compiler += ["-fno-builtin-mimalloc"]
    run(compiler + ["-S", "-emit-llvm", "-o", ir])
    backend = args.llc or args.backend_llc
    if backend:
        backend_input = ir
        if args.backend_llc:
            # Keep the producer's optimized module intact. The new LLVM reader
            # expands legacy constexpr records that its text parser no longer
            # accepts, including shifts used to pack global descriptor bases.
            backend_input = prefix.with_suffix(".bc")
            run(compiler + ["-c", "-emit-llvm", "-o", backend_input])
        run([backend, "-" + opt, "-verify-machineinstrs", "-relocation-model=static", "-filetype=obj", backend_input, "-o", obj])
    else:
        run(compiler + ["-c", "-o", obj, "-mllvm", "-verify-machineinstrs"])
    run([args.clang, "--target=" + triple, "-c", asm, "-o", boot_obj])
    script.write_text("ENTRY(_start)\nSECTIONS { . = 0x100000; .multiboot : { *(.multiboot) } .text : { *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) } }\n")
    run([args.linker, "-m", "elf_x86_64" if mode == 64 else "elf_i386", "-T", script, boot_obj, obj, "-o", elf])
    # QEMU's Multiboot loader accepts ELF32. All relocated guest addresses fit
    # below 4 GiB; the entry starts in protected mode and enables long mode itself.
    run([args.objcopy, "-O", "elf32-i386", elf, kernel])
    process = subprocess.run([args.qemu, "-accel", "tcg", "-machine", "pc", "-cpu", "max", "-m", "32M",
                              "-nodefaults", "-display", "none", "-serial", "none", "-monitor", "none", "-no-reboot",
                              "-nic", "none", "-device", "isa-debug-exit,iobase=0xf4,iosize=4", "-kernel", kernel],
                             capture_output=True, text=True, timeout=20)
    assert process.returncode == 33, f"CPL0 guest {mode}/{opt} failed ({process.returncode}): {process.stderr}"
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path("build-neverc/bin/neverc"))
    backend = parser.add_mutually_exclusive_group()
    backend.add_argument("--llc", type=Path)
    backend.add_argument("--backend-llc", type=Path)
    for option, tool in (("clang", "clang"), ("linker", "ld.lld"), ("objcopy", "objcopy"), ("qemu", "qemu-system-x86_64")):
        parser.add_argument("--" + option, default=shutil.which(tool))
    parser.add_argument("--address-bits", type=int, choices=(32, 64),
                        help="use explicit-address APIs (i386 requires 32)")
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    if not all((args.clang, args.linker, args.objcopy, args.qemu)):
        parser.error("QEMU, clang, ld.lld and GNU objcopy are required")
    for key in ("compiler", "llc", "backend_llc"):
        if getattr(args, key):
            setattr(args, key, getattr(args, key).resolve())
    with tempfile.TemporaryDirectory(prefix="descriptor-cpl0-") as temporary:
        directory = args.keep or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        modes = (32, 64) if args.llc and args.address_bits != 64 else (64,)
        builder = lambda mode, llvm_route: source(mode, llvm_route, args.address_bits)
        count = sum(check_guest(args, directory, mode, opt, source_builder=builder)
                    for mode in modes for opt in ("O0", "O2"))
    print(f"PASS: {count} CPL0 descriptor/machine-state checks in QEMU TCG")


if __name__ == "__main__":
    main()
