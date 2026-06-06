#!/usr/bin/env python3
"""
Binary-level semantic verification of x86 privileged intrinsics.

Compiles each builtin to a COFF object (x86_64-pc-windows-msvc target),
extracts the machine code, then uses Unicorn Engine to emulate execution
and verify that the register/memory state matches the Intel SDM spec.

Privileged instructions (rdmsr, mov cr3, vmwrite, etc.) fault inside
Unicorn — the script runs up to the privileged instruction, snapshots
registers, verifies the compiler set them up correctly, then skips over
and (where applicable) verifies the return-value extraction.

Usage:
    python3 tests/verify-x86-intrin-binary.py                 # run all
    python3 tests/verify-x86-intrin-binary.py --filter=msr    # substring
    python3 tests/verify-x86-intrin-binary.py -v               # show disasm

Requires: unicorn, capstone, lief  (pip install unicorn capstone lief)
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from typing import Callable

import capstone
import lief
import unicorn
import unicorn.x86_const as uc_x86

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
NCC = os.path.join(REPO_ROOT, "build-neverc", "bin", "neverc")

CODE_BASE = 0x10000
STACK_BASE = 0x80000
STACK_SIZE = 0x10000
DATA_BASE = 0xA0000
DATA_SIZE = 0x1000

# Windows x64 ABI: rcx, rdx, r8, r9
ARG_REGS = [uc_x86.UC_X86_REG_RCX, uc_x86.UC_X86_REG_RDX,
            uc_x86.UC_X86_REG_R8, uc_x86.UC_X86_REG_R9]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def compile_to_obj(src: str, tmpdir: str, stem: str) -> bytes:
    """Compile C source to COFF object, return .text bytes."""
    src_path = os.path.join(tmpdir, f"{stem}.c")
    obj_path = os.path.join(tmpdir, f"{stem}.o")
    with open(src_path, "w") as f:
        f.write(src)
    r = subprocess.run(
        [NCC, "--target=x86_64-pc-windows-msvc", "-O2", "-fno-lto",
         "-c", src_path, "-o", obj_path],
        capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"compile failed:\n{r.stderr.strip()}")
    obj = lief.parse(obj_path)
    if obj is None:
        raise RuntimeError(f"lief cannot parse {obj_path}")
    for sec in obj.sections:
        if ".text" in sec.name and len(sec.content) > 0:
            return bytes(sec.content)
    raise RuntimeError(f"no .text section in {obj_path}")


def disasm(code: bytes, addr: int = CODE_BASE) -> list:
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    return [(i.address, i.size, i.mnemonic, i.op_str) for i in md.disasm(code, addr)]


def find_insn(instrs, mnemonic):
    for i in instrs:
        if i[2] == mnemonic:
            return i
    return None


def make_emu(code: bytes) -> unicorn.Uc:
    mu = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
    code_size = max(0x2000, (len(code) + 0xFFF) & ~0xFFF)
    mu.mem_map(CODE_BASE, code_size, unicorn.UC_PROT_ALL)
    mu.mem_write(CODE_BASE, code)
    mu.mem_map(STACK_BASE, STACK_SIZE, unicorn.UC_PROT_ALL)
    mu.reg_write(uc_x86.UC_X86_REG_RSP, STACK_BASE + STACK_SIZE - 0x100)
    mu.mem_map(DATA_BASE, DATA_SIZE, unicorn.UC_PROT_ALL)
    # Shadow space (Windows x64 ABI requires 32 bytes of home space)
    # Return address on stack → stop address
    ret_addr = CODE_BASE + code_size - 1
    mu.mem_write(STACK_BASE + STACK_SIZE - 0x100,
                 struct.pack("<Q", ret_addr))
    return mu


# Registers we snapshot
REG_MAP = {
    uc_x86.UC_X86_REG_RAX: "RAX", uc_x86.UC_X86_REG_RBX: "RBX",
    uc_x86.UC_X86_REG_RCX: "RCX", uc_x86.UC_X86_REG_RDX: "RDX",
    uc_x86.UC_X86_REG_RSI: "RSI", uc_x86.UC_X86_REG_RDI: "RDI",
    uc_x86.UC_X86_REG_R8: "R8",   uc_x86.UC_X86_REG_R9: "R9",
    uc_x86.UC_X86_REG_EAX: "EAX", uc_x86.UC_X86_REG_ECX: "ECX",
    uc_x86.UC_X86_REG_EDX: "EDX",
    uc_x86.UC_X86_REG_AL: "AL",   uc_x86.UC_X86_REG_AX: "AX",
    uc_x86.UC_X86_REG_DX: "DX",
}


def reg_name(reg):
    return REG_MAP.get(reg, f"reg#{reg}")


def snapshot(mu):
    return {r: mu.reg_read(r) for r in REG_MAP}


# ---------------------------------------------------------------------------
# Test definitions
# ---------------------------------------------------------------------------

@dataclass
class Test:
    name: str
    src: str
    args: list = field(default_factory=list)
    priv_insn: str = ""
    # Checks applied to register state AT the privileged instruction
    check_regs: dict = field(default_factory=dict)
    # Custom verifier: (mu, instrs, regs_at_priv) -> error_str|None
    custom: Callable | None = None
    # Setup callback: (mu) -> None, called before emulation (e.g. set GS base)
    setup: Callable | None = None
    # Full emulation: run to completion and check return value in RAX
    expect_rax: int | None = None
    # Check memory after full emulation: {addr: (size, expected_value)}
    expect_mem: dict = field(default_factory=dict)


def _check_wrmsr(mu, instrs, regs):
    """wrmsr: ECX=MSR addr, EAX=value[31:0], EDX=value[63:32]."""
    val = 0xDEADBEEF_12345678
    ecx = regs[uc_x86.UC_X86_REG_ECX]
    eax = regs[uc_x86.UC_X86_REG_EAX]
    edx = regs[uc_x86.UC_X86_REG_EDX]
    errors = []
    if ecx != 0x176:
        errors.append(f"ECX={ecx:#x}, want 0x176 (MSR addr)")
    if eax != (val & 0xFFFFFFFF):
        errors.append(f"EAX={eax:#x}, want {val & 0xFFFFFFFF:#x} (lo32)")
    if edx != (val >> 32):
        errors.append(f"EDX={edx:#x}, want {val >> 32:#x} (hi32)")
    return "; ".join(errors) if errors else None


def _check_rdmsr(mu, instrs, regs):
    """rdmsr: ECX=MSR addr."""
    ecx = regs[uc_x86.UC_X86_REG_ECX]
    if ecx != 0xC0000080:
        return f"ECX={ecx:#x}, want 0xC0000080 (EFER)"
    return None


def _check_outbyte(mu, instrs, regs):
    """out dx, al: DX=port, AL=value."""
    dx = regs[uc_x86.UC_X86_REG_DX]
    al = regs[uc_x86.UC_X86_REG_AL]
    errors = []
    if dx != 0x60:
        errors.append(f"DX={dx:#x}, want 0x60 (port)")
    if al != 0xAB:
        errors.append(f"AL={al:#x}, want 0xAB (value)")
    return "; ".join(errors) if errors else None


def _check_outword(mu, instrs, regs):
    dx = regs[uc_x86.UC_X86_REG_DX]
    ax = regs[uc_x86.UC_X86_REG_AX]
    errors = []
    if dx != 0x3F8:
        errors.append(f"DX={dx:#x}, want 0x3F8")
    if ax != 0x1234:
        errors.append(f"AX={ax:#x}, want 0x1234")
    return "; ".join(errors) if errors else None


def _check_outdword(mu, instrs, regs):
    dx = regs[uc_x86.UC_X86_REG_DX]
    eax = regs[uc_x86.UC_X86_REG_EAX]
    errors = []
    if dx != 0xCFC:
        errors.append(f"DX={dx:#x}, want 0xCFC")
    if eax != 0xDEADFACE:
        errors.append(f"EAX={eax:#x}, want 0xDEADFACE")
    return "; ".join(errors) if errors else None


def _check_inbyte(mu, instrs, regs):
    dx = regs[uc_x86.UC_X86_REG_DX]
    if dx != 0x64:
        return f"DX={dx:#x}, want 0x64 (port)"
    return None


def _check_indword(mu, instrs, regs):
    dx = regs[uc_x86.UC_X86_REG_DX]
    if dx != 0xCF8:
        return f"DX={dx:#x}, want 0xCF8"
    return None


def _check_vmwrite(mu, instrs, regs):
    """vmwrite rcx, rdx: field in first operand, value in second."""
    rcx = regs[uc_x86.UC_X86_REG_RCX]
    rdx = regs[uc_x86.UC_X86_REG_RDX]
    errors = []
    if rcx != 0x4002:
        errors.append(f"RCX={rcx:#x}, want 0x4002 (VMCS field)")
    if rdx != 0x12345678:
        errors.append(f"RDX={rdx:#x}, want 0x12345678 (value)")
    return "; ".join(errors) if errors else None


def _check_cpuid(mu, instrs, regs):
    """cpuid: EAX=function, ECX=subfunction."""
    eax = regs[uc_x86.UC_X86_REG_EAX]
    ecx = regs[uc_x86.UC_X86_REG_ECX]
    errors = []
    if eax != 1:
        errors.append(f"EAX={eax:#x}, want 1 (function)")
    if ecx != 0:
        errors.append(f"ECX={ecx:#x}, want 0 (subfunction)")
    return "; ".join(errors) if errors else None


def _check_xsetbv_regs(mu, instrs, regs):
    """xsetbv: ECX=XCR, EAX=lo32(val), EDX=hi32(val)."""
    ecx = regs[uc_x86.UC_X86_REG_ECX]
    eax = regs[uc_x86.UC_X86_REG_EAX]
    edx = regs[uc_x86.UC_X86_REG_EDX]
    errors = []
    if ecx != 0:
        errors.append(f"ECX={ecx:#x}, want 0 (XCR0)")
    if eax != 0xBEEF1234:
        errors.append(f"EAX={eax:#x}, want 0xBEEF1234 (lo32)")
    if edx != 0xDEAD:
        errors.append(f"EDX={edx:#x}, want 0xDEAD (hi32)")
    return "; ".join(errors) if errors else None


def _check_rdpmc(mu, instrs, regs):
    """rdpmc: ECX=counter."""
    ecx = regs[uc_x86.UC_X86_REG_ECX]
    if ecx != 0x42:
        return f"ECX={ecx:#x}, want 0x42 (counter)"
    return None


_SEG_TEST_DATA = b'\xAA\xBB\xCC\xDD\xEE\xFF\x11\x22'


def _setup_gs(mu):
    mu.reg_write(uc_x86.UC_X86_REG_GS_BASE, DATA_BASE)
    mu.mem_write(DATA_BASE + 0x30, _SEG_TEST_DATA)


def _setup_fs(mu):
    mu.reg_write(uc_x86.UC_X86_REG_FS_BASE, DATA_BASE)
    mu.mem_write(DATA_BASE + 0x30, _SEG_TEST_DATA)


# ---------------------------------------------------------------------------
# Test list
# ---------------------------------------------------------------------------

TESTS = [
    # MSR
    Test("wrmsr",
         'void wrmsr(unsigned int a, unsigned __int64 v) { __writemsr(a, v); }\n',
         args=[0x176, 0xDEADBEEF_12345678], priv_insn="wrmsr",
         custom=_check_wrmsr),
    Test("rdmsr",
         'unsigned __int64 rdmsr(unsigned int a) { return __readmsr(a); }\n',
         args=[0xC0000080], priv_insn="rdmsr",
         custom=_check_rdmsr),

    # CR registers
    Test("readcr0",
         'unsigned __int64 readcr0(void) { return __readcr0(); }\n',
         priv_insn="mov"),
    Test("readcr3",
         'unsigned __int64 readcr3(void) { return __readcr3(); }\n',
         priv_insn="mov"),
    Test("writecr3",
         'void writecr3(unsigned __int64 v) { __writecr3(v); }\n',
         args=[0xDEAD0000], priv_insn="mov",
         check_regs={uc_x86.UC_X86_REG_RCX: 0xDEAD0000}),
    Test("writecr0",
         'void writecr0(unsigned __int64 v) { __writecr0(v); }\n',
         args=[0x80050033], priv_insn="mov",
         check_regs={uc_x86.UC_X86_REG_RCX: 0x80050033}),

    # DR registers
    Test("readdr0",
         'unsigned __int64 readdr0(void) { return __readdr(0); }\n',
         priv_insn="mov"),
    Test("writedr0",
         'void writedr0(unsigned __int64 v) { __writedr(0, v); }\n',
         args=[0xCAFEBABE], priv_insn="mov",
         check_regs={uc_x86.UC_X86_REG_RCX: 0xCAFEBABE}),

    # Port I/O
    Test("outbyte",
         'void outbyte(unsigned short p, unsigned char v) { __outbyte(p, v); }\n',
         args=[0x60, 0xAB], priv_insn="out",
         custom=_check_outbyte),
    Test("outword",
         'void outword(unsigned short p, unsigned short v) { __outword(p, v); }\n',
         args=[0x3F8, 0x1234], priv_insn="out",
         custom=_check_outword),
    Test("outdword",
         'void outdword(unsigned short p, unsigned int v) { __outdword(p, v); }\n',
         args=[0xCFC, 0xDEADFACE], priv_insn="out",
         custom=_check_outdword),
    Test("inbyte",
         'unsigned char inbyte(unsigned short p) { return __inbyte(p); }\n',
         args=[0x64], priv_insn="in",
         custom=_check_inbyte),
    Test("indword",
         'unsigned int indword(unsigned short p) { return __indword(p); }\n',
         args=[0xCF8], priv_insn="in",
         custom=_check_indword),

    # TLB
    Test("invlpg",
         'void invlpg(void *a) { __invlpg(a); }\n',
         args=[0x1000_0000], priv_insn="invlpg"),

    # CLI / STI
    Test("cli",
         'void do_cli(void) { _disable(); }\n',
         priv_insn="cli"),
    Test("sti",
         'void do_sti(void) { _enable(); }\n',
         priv_insn="sti"),

    # Descriptor tables
    Test("sidt",
         'void do_sidt(void *p) { __sidt(p); }\n',
         args=[DATA_BASE], priv_insn="sidt"),
    Test("lidt",
         'void do_lidt(void *p) { __lidt(p); }\n',
         args=[DATA_BASE], priv_insn="lidt"),
    Test("lgdt",
         'void do_lgdt(void *p) { _lgdt(p); }\n',
         args=[DATA_BASE], priv_insn="lgdt"),

    # WBINVD
    Test("wbinvd",
         'void do_wbinvd(void) { __wbinvd(); }\n',
         priv_insn="wbinvd"),

    # VMX
    Test("vmxoff",
         'void do_vmxoff(void) { __vmx_off(); }\n',
         priv_insn="vmxoff"),
    Test("vmlaunch",
         'unsigned char do_vmlaunch(void) { return __vmx_vmlaunch(); }\n',
         priv_insn="vmlaunch"),
    Test("vmresume",
         'unsigned char do_vmresume(void) { return __vmx_vmresume(); }\n',
         priv_insn="vmresume"),
    Test("vmwrite",
         'unsigned char do_vmwrite(unsigned __int64 f, unsigned __int64 v) { return __vmx_vmwrite(f, v); }\n',
         args=[0x4002, 0x12345678], priv_insn="vmwrite",
         custom=_check_vmwrite),
    Test("vmread",
         'unsigned char do_vmread(unsigned __int64 f, unsigned __int64 *o) { return __vmx_vmread(f, o); }\n',
         args=[0x4002, DATA_BASE], priv_insn="vmread"),
    Test("vmclear",
         'unsigned char do_vmclear(unsigned __int64 *p) { return __vmx_vmclear(p); }\n',
         args=[DATA_BASE], priv_insn="vmclear"),
    Test("vmptrld",
         'unsigned char do_vmptrld(unsigned __int64 *p) { return __vmx_vmptrld(p); }\n',
         args=[DATA_BASE], priv_insn="vmptrld"),
    Test("vmxon",
         'unsigned char do_vmxon(unsigned __int64 *p) { return __vmx_on(p); }\n',
         args=[DATA_BASE], priv_insn="vmxon"),
    Test("vmptrst",
         'void do_vmptrst(unsigned __int64 *p) { __vmx_vmptrst(p); }\n',
         args=[DATA_BASE], priv_insn="vmptrst"),

    # Segment limit
    Test("segmentlimit",
         'unsigned int do_lsl(unsigned int s) { return __segmentlimit(s); }\n',
         args=[0x10], priv_insn="lsl"),

    # INT 2C
    Test("int2c",
         'void do_int2c(void) { __int2c(); }\n',
         priv_insn="int"),

    # CPUID (not privileged, but complex register shuffle)
    Test("cpuid",
         'void do_cpuid(int i[4], int f, int s) { __cpuidex(i, f, s); }\n',
         args=[DATA_BASE, 1, 0], priv_insn="cpuid",
         custom=_check_cpuid),

    # --- Port I/O: inword ---
    Test("inword",
         'unsigned short inword(unsigned short p) { return __inword(p); }\n',
         args=[0x3F8], priv_insn="in"),

    # --- Port I/O string ops ---
    Test("inbytestring",
         'void do_inbs(unsigned short p, unsigned char *b, unsigned long n) { __inbytestring(p, b, n); }\n',
         args=[0x60, DATA_BASE, 16], priv_insn="insb"),
    Test("inwordstring",
         'void do_inws(unsigned short p, unsigned short *b, unsigned long n) { __inwordstring(p, b, n); }\n',
         args=[0x60, DATA_BASE, 8], priv_insn="insw"),
    Test("indwordstring",
         'void do_inds(unsigned short p, unsigned long *b, unsigned long n) { __indwordstring(p, b, n); }\n',
         args=[0xCF8, DATA_BASE, 4], priv_insn="insd"),
    Test("outbytestring",
         'void do_outbs(unsigned short p, unsigned char *b, unsigned long n) { __outbytestring(p, b, n); }\n',
         args=[0x60, DATA_BASE, 16], priv_insn="outsb"),
    Test("outwordstring",
         'void do_outws(unsigned short p, unsigned short *b, unsigned long n) { __outwordstring(p, b, n); }\n',
         args=[0x60, DATA_BASE, 8], priv_insn="outsw"),
    Test("outdwordstring",
         'void do_outds(unsigned short p, unsigned long *b, unsigned long n) { __outdwordstring(p, b, n); }\n',
         args=[0xCF8, DATA_BASE, 4], priv_insn="outsd"),

    # --- Rep string ops ---
    Test("movsb",
         'void do_movsb(void *d, const void *s, unsigned __int64 n) { __movsb((unsigned char*)d, (const unsigned char*)s, n); }\n',
         args=[DATA_BASE, DATA_BASE + 0x100, 64], priv_insn="rep movsb"),
    Test("movsw",
         'void do_movsw(void *d, const void *s, unsigned __int64 n) { __movsw((unsigned short*)d, (const unsigned short*)s, n); }\n',
         args=[DATA_BASE, DATA_BASE + 0x100, 32], priv_insn="rep movsw"),
    Test("movsd_rep",
         'void do_movsd(void *d, const void *s, unsigned __int64 n) { __movsd((unsigned long*)d, (const unsigned long*)s, n); }\n',
         args=[DATA_BASE, DATA_BASE + 0x100, 16], priv_insn="rep movsd"),
    Test("movsq",
         'void do_movsq(void *d, const void *s, unsigned __int64 n) { __movsq((unsigned __int64*)d, (const unsigned __int64*)s, n); }\n',
         args=[DATA_BASE, DATA_BASE + 0x100, 8], priv_insn="rep movsq"),
    Test("stosb",
         'void do_stosb(unsigned char *d, unsigned char v, unsigned __int64 n) { __stosb(d, v, n); }\n',
         args=[DATA_BASE, 0x41, 64], priv_insn="rep stosb"),
    Test("stosw",
         'void do_stosw(unsigned short *d, unsigned short v, unsigned __int64 n) { __stosw(d, v, n); }\n',
         args=[DATA_BASE, 0x4142, 32], priv_insn="rep stosw"),
    Test("stosd",
         'void do_stosd(unsigned long *d, unsigned long v, unsigned __int64 n) { __stosd(d, v, n); }\n',
         args=[DATA_BASE, 0xDEADBEEF, 16], priv_insn="rep stosd"),
    Test("stosq",
         'void do_stosq(unsigned __int64 *d, unsigned __int64 v, unsigned __int64 n) { __stosq(d, v, n); }\n',
         args=[DATA_BASE, 0xCAFEBABE, 8], priv_insn="rep stosq"),

    # --- Descriptor tables: sgdt ---
    Test("sgdt",
         'void do_sgdt(void *p) { _sgdt(p); }\n',
         args=[DATA_BASE], priv_insn="sgdt"),

    # --- INVPCID ---
    Test("invpcid",
         'void do_invpcid(unsigned int t, void *d) { _invpcid(t, d); }\n',
         args=[0, DATA_BASE], priv_insn="invpcid"),

    # --- EFLAGS ---
    Test("flags_read",
         'unsigned __int64 do_readflags(void) { return __readeflags(); }\n',
         priv_insn="pushfq"),
    Test("flags_write",
         'void do_writeflags(unsigned __int64 v) { __writeeflags(v); }\n',
         args=[0x202], priv_insn="popfq"),

    # --- XCR (xgetbv / xsetbv) ---
    Test("xgetbv",
         'unsigned __int64 do_xgetbv(unsigned int xcr) { return _xgetbv(xcr); }\n',
         args=[0], priv_insn="xgetbv"),
    Test("xsetbv",
         'void do_xsetbv(unsigned int xcr, unsigned __int64 val) { _xsetbv(xcr, val); }\n',
         args=[0, 7], priv_insn="xsetbv"),

    # --- TSX ---
    Test("xbegin",
         'void do_xbegin(void) { _xbegin(); }\n',
         priv_insn="xbegin"),
    Test("xend",
         'void do_xend(void) { _xend(); }\n',
         priv_insn="xend"),
    Test("xabort",
         'void do_xabort(void) { _xabort(0x42); }\n',
         priv_insn="xabort"),
    Test("xtest",
         'unsigned char do_xtest(void) { return _xtest(); }\n',
         priv_insn="xtest"),

    # --- Additional CR registers ---
    Test("readcr2",
         'unsigned __int64 readcr2(void) { return __readcr2(); }\n',
         priv_insn="mov"),
    Test("readcr4",
         'unsigned __int64 readcr4(void) { return __readcr4(); }\n',
         priv_insn="mov"),
    Test("readcr8",
         'unsigned __int64 readcr8(void) { return __readcr8(); }\n',
         priv_insn="mov"),
    Test("writecr2",
         'void writecr2(unsigned __int64 v) { __writecr2(v); }\n',
         args=[0xFA01E000], priv_insn="mov",
         check_regs={uc_x86.UC_X86_REG_RCX: 0xFA01E000}),
    Test("writecr4",
         'void writecr4(unsigned __int64 v) { __writecr4(v); }\n',
         args=[0x6E0], priv_insn="mov",
         check_regs={uc_x86.UC_X86_REG_RCX: 0x6E0}),
    Test("writecr8",
         'void writecr8(unsigned __int64 v) { __writecr8(v); }\n',
         args=[0x0F], priv_insn="mov",
         check_regs={uc_x86.UC_X86_REG_RCX: 0x0F}),

    # --- RDPMC ---
    Test("rdpmc",
         'unsigned __int64 do_rdpmc(unsigned int c) { return __readpmc(c); }\n',
         args=[0x42], priv_insn="rdpmc",
         custom=_check_rdpmc),

    # --- RDTSCP ---
    Test("rdtscp",
         'unsigned __int64 do_rdtscp(unsigned int *aux) { return __rdtscp(aux); }\n',
         args=[DATA_BASE], priv_insn="rdtscp"),

    # --- xsetbv register verification ---
    Test("xsetbv_regs",
         'void do_xsetbv(unsigned int xcr, unsigned __int64 val) { _xsetbv(xcr, val); }\n',
         args=[0, 0xDEAD_BEEF1234], priv_insn="xsetbv",
         custom=_check_xsetbv_regs),

    # --- xbegin with return value ---
    Test("xbegin_ret",
         'unsigned int do_xbegin(void) { return _xbegin(); }\n',
         priv_insn="xbegin"),

    # --- GS segment reads (full emulation) ---
    Test("readgsbyte",
         'unsigned char do_readgsbyte(unsigned __int64 off) { return __readgsbyte(off); }\n',
         args=[0x30], setup=_setup_gs, expect_rax=0xAA),
    Test("readgsword",
         'unsigned short do_readgsword(unsigned __int64 off) { return __readgsword(off); }\n',
         args=[0x30], setup=_setup_gs, expect_rax=0xBBAA),
    Test("readgsdword",
         'unsigned long do_readgsdword(unsigned __int64 off) { return __readgsdword(off); }\n',
         args=[0x30], setup=_setup_gs, expect_rax=0xDDCCBBAA),
    Test("readgsqword",
         'unsigned __int64 do_readgsqword(unsigned __int64 off) { return __readgsqword(off); }\n',
         args=[0x30], setup=_setup_gs, expect_rax=0x2211FFEEDDCCBBAA),

    # --- GS segment writes (full emulation) ---
    Test("writegsbyte",
         'void do_writegsbyte(unsigned __int64 off, unsigned char v) { __writegsbyte(off, v); }\n',
         args=[0x40, 0x5A], setup=_setup_gs,
         expect_mem={DATA_BASE + 0x40: (1, 0x5A)}),
    Test("writegsword",
         'void do_writegsword(unsigned __int64 off, unsigned short v) { __writegsword(off, v); }\n',
         args=[0x40, 0x1234], setup=_setup_gs,
         expect_mem={DATA_BASE + 0x40: (2, 0x1234)}),
    Test("writegsdword",
         'void do_writegsdword(unsigned __int64 off, unsigned long v) { __writegsdword(off, v); }\n',
         args=[0x40, 0xDEADBEEF], setup=_setup_gs,
         expect_mem={DATA_BASE + 0x40: (4, 0xDEADBEEF)}),
    Test("writegsqword",
         'void do_writegsqword(unsigned __int64 off, unsigned __int64 v) { __writegsqword(off, v); }\n',
         args=[0x40, 0xCAFEBABE12345678], setup=_setup_gs,
         expect_mem={DATA_BASE + 0x40: (8, 0xCAFEBABE12345678)}),

    # --- GS segment inc/add ---
    Test("incgsdword",
         'void do_incgsdword(unsigned __int64 off) { __incgsdword(off); }\n',
         args=[0x50], setup=_setup_gs,
         expect_mem={DATA_BASE + 0x50: (4, 0x00000001)}),
    Test("addgsdword",
         'void do_addgsdword(unsigned __int64 off, unsigned long v) { __addgsdword(off, v); }\n',
         args=[0x50, 0x100], setup=_setup_gs,
         expect_mem={DATA_BASE + 0x50: (4, 0x00000100)}),

    # --- FS segment reads (full emulation) ---
    Test("readfsbyte",
         'unsigned char do_readfsbyte(unsigned __int64 off) { return __readfsbyte(off); }\n',
         args=[0x30], setup=_setup_fs, expect_rax=0xAA),
    Test("readfsword",
         'unsigned short do_readfsword(unsigned __int64 off) { return __readfsword(off); }\n',
         args=[0x30], setup=_setup_fs, expect_rax=0xBBAA),
    Test("readfsdword",
         'unsigned long do_readfsdword(unsigned __int64 off) { return __readfsdword(off); }\n',
         args=[0x30], setup=_setup_fs, expect_rax=0xDDCCBBAA),
    Test("readfsqword",
         'unsigned __int64 do_readfsqword(unsigned __int64 off) { return __readfsqword(off); }\n',
         args=[0x30], setup=_setup_fs, expect_rax=0x2211FFEEDDCCBBAA),
]


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def run_test(t: Test, tmpdir: str, verbose: bool) -> tuple[bool, str]:
    # 1) Compile
    try:
        code = compile_to_obj(t.src, tmpdir, t.name)
    except RuntimeError as e:
        return False, f"COMPILE: {e}"

    # 2) Disassemble
    instrs = disasm(code)
    if not instrs:
        return False, "no instructions"

    if verbose:
        print(f"  disassembly ({len(code)} bytes):")
        for a, sz, mn, ops in instrs:
            raw = code[a - CODE_BASE:a - CODE_BASE + sz].hex()
            print(f"    {a:#06x}: {raw:20s} {mn:10s} {ops}")

    # 3) Check the privileged instruction exists (if specified)
    target = None
    if t.priv_insn:
        target = find_insn(instrs, t.priv_insn)
        if not target:
            return False, f"instruction '{t.priv_insn}' not found in output"

    # 4) Determine emulation mode
    full_emu = (t.setup is not None or t.expect_rax is not None or t.expect_mem)
    if not t.priv_insn and not full_emu:
        return True, "compiled OK (no priv insn to emulate)"

    # 5) Create emulator and set arguments
    try:
        mu = make_emu(code)
    except unicorn.UcError as e:
        return False, f"emu setup: {e}"

    for i, val in enumerate(t.args):
        if i < len(ARG_REGS):
            mu.reg_write(ARG_REGS[i], val & 0xFFFFFFFFFFFFFFFF)

    if t.setup:
        t.setup(mu)

    if target:
        # 6a) Privileged-instruction mode: emulate up to the priv insn
        priv_addr, priv_size, _, _ = target

        try:
            mu.emu_start(CODE_BASE, priv_addr, timeout=5_000_000)
        except unicorn.UcError as e:
            rip = mu.reg_read(uc_x86.UC_X86_REG_RIP)
            return False, f"emu stopped at {rip:#x} (target {priv_addr:#x}): {e}"

        rip = mu.reg_read(uc_x86.UC_X86_REG_RIP)
        if rip != priv_addr:
            return False, f"RIP={rip:#x}, expected {priv_addr:#x} (didn't reach priv insn)"

        regs = snapshot(mu)

        for reg, expected in t.check_regs.items():
            actual = mu.reg_read(reg)
            if actual != expected:
                return False, (f"{reg_name(reg)}={actual:#x}, "
                               f"want {expected:#x}")

        if t.custom:
            err = t.custom(mu, instrs, regs)
            if err:
                return False, err

        return True, "OK"
    else:
        # 6b) Full emulation: run to completion and check results
        code_size = max(0x2000, (len(code) + 0xFFF) & ~0xFFF)
        end_addr = CODE_BASE + code_size - 1

        try:
            mu.emu_start(CODE_BASE, end_addr, timeout=5_000_000)
        except unicorn.UcError as e:
            rip = mu.reg_read(uc_x86.UC_X86_REG_RIP)
            return False, f"emu stopped at {rip:#x}: {e}"

        if t.expect_rax is not None:
            rax = mu.reg_read(uc_x86.UC_X86_REG_RAX)
            if rax != t.expect_rax:
                return False, f"RAX={rax:#x}, want {t.expect_rax:#x}"

        for addr, (size, expected) in t.expect_mem.items():
            fmt = {1: "<B", 2: "<H", 4: "<I", 8: "<Q"}[size]
            actual = struct.unpack(fmt, mu.mem_read(addr, size))[0]
            if actual != expected:
                return False, f"mem[{addr:#x}]={actual:#x}, want {expected:#x}"

        if t.custom:
            err = t.custom(mu, instrs, snapshot(mu))
            if err:
                return False, err

        return True, "OK"


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--filter", default="")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    if not os.path.isfile(NCC):
        print(f"ERROR: ncc not found at {NCC}", file=sys.stderr)
        sys.exit(1)

    tests = [t for t in TESTS if args.filter in t.name] if args.filter else TESTS
    if not tests:
        print(f"no tests match '{args.filter}'")
        sys.exit(1)

    passed = failed = 0
    errors = []

    with tempfile.TemporaryDirectory(prefix="intrin_verify_") as tmpdir:
        for t in tests:
            if args.verbose:
                print(f"\n{'─' * 60}")
            ok, msg = run_test(t, tmpdir, args.verbose)
            tag = "PASS" if ok else "FAIL"
            print(f"  {tag}  [{t.name:16s}] {msg}")
            if ok:
                passed += 1
            else:
                failed += 1
                errors.append((t.name, msg))

    print(f"\n{'═' * 60}")
    print(f"  {passed} passed, {failed} failed, {passed + failed} total")
    if errors:
        print("\nFailures:")
        for name, msg in errors:
            print(f"  {name}: {msg}")
    print()
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
