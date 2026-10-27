#!/usr/bin/env python3
"""Qualify native FEMMS transport and live MMX values across its clobbers.

FEMMS boundaries are independently decoded and completed with adversarial
MMX/x87 payloads. This proves surrounding code and ABI behavior, not physical
3DNow! execution, pending-exception delivery or a complete x87 state model.
Requires capstone, lief and unicorn. The full LLVM fork must implement FEMMS.
"""
import argparse
import itertools
import json
import struct
import subprocess
from pathlib import Path

import capstone
import lief
import unicorn
import unicorn.x86_const as regs

TRIPLES = ("x86_64-unknown-linux-gnu", "x86_64-pc-windows-msvc", "x86_64-apple-darwin")
GPRS = ("RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI",
        "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15")
MASK = (1 << 64) - 1
SAMPLES = ((0, 0), (MASK, 1), (1, MASK), (0x7fffffffffffffff, 0x8000000000000000),
           (0x3141592653589793, 0x0123456789abcdef), (0x55aa55aaaa55aa55, 0xa5a55a5aa5a55a5a))
SOURCE = r"""
#include <stdint.h>
#include <mmintrin.h>
#define FRAME \
  volatile uint64_t frame[16] __attribute__((aligned(64))); \
  volatile unsigned char *dynamic = __builtin_alloca((a & 255) + 65); \
  frame[0] = UINT64_C(0x123456789abcdef0); \
  frame[15] = UINT64_C(0xfedcba9876543210); \
  *dynamic = 0xa5;
#define CANARIES ((frame[0] ^ UINT64_C(0x123456789abcdef0)) ^ \
                  (frame[15] ^ UINT64_C(0xfedcba9876543210)) ^ (*dynamic ^ 0xa5))
__attribute__((target("3dnow"), noinline))
uint64_t mmx_reset(uint64_t a, uint64_t b, uint64_t *memory) {
  FRAME
  *memory = a;
  unsigned less = a < b;
  __builtin_ia32_femms();
  __builtin_ia32_femms();
  return *memory ^ less ^ CANARIES;
}
__attribute__((target("3dnow"), noinline))
uint64_t mmx_live(uint64_t a, uint64_t b, uint64_t *memory) {
  FRAME
  *memory = a;
  union { uint64_t bits; __m64 value; } left = {a}, right = {b}, result;
  __m64 sum = _mm_add_pi8(left.value, right.value);
  __builtin_ia32_femms();
  result.value = _mm_xor_si64(sum, left.value);
  _mm_empty();
  return result.bits ^ CANARIES;
}
"""


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True)
    assert result.returncode == 0, (command, result.stderr)
    assert "not a recognized feature" not in result.stderr, result.stderr


def qualify(path, windows):
    binary = lief.parse(str(path))
    sections = [s for s in binary.sections if s.name in (".text", "__text") and s.content]
    assert len(sections) == 1
    section, = sections
    if isinstance(binary, lief.ELF.Binary):
        assert all(not r.has_section or r.section.name != section.name for r in binary.relocations)
    else:
        assert not list(section.relocations), "controlled code has unresolved relocations"
    code = bytes(section.content)
    symbols = {s.name.lstrip("_"): s.value - getattr(section, "virtual_address", 0)
               for s in binary.symbols if s.name.lstrip("_") in ("mmx_reset", "mmx_live")}
    assert set(symbols) == {"mmx_reset", "mmx_live"}
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    boundaries = set()
    starts = sorted(symbols.values())
    for name, start in symbols.items():
        end = next((p for p in starts if p > start), len(code))
        count, returned = 0, False
        for insn in cs.disasm(code[start:end], start):
            if insn.mnemonic == "femms":
                assert bytes(insn.bytes) == b"\x0f\x0e"
                boundaries.add(insn.address)
                count += 1
            if insn.mnemonic == "ret":
                returned = True
                break
        assert returned and count == (2 if name == "mmx_reset" else 1), (path, name, count)
    patched = bytearray(code)
    for at in boundaries:
        patched[at:at + 2] = b"\x90\x90"
    uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
    base, stack, data, page = 0x1000000, 0x80000, 0xa0000, 4096
    span = (len(code) + 2 * page - 1) // page * page
    stop = base + span - 1
    uc.mem_map(base, span)
    uc.mem_write(base, bytes(patched))
    uc.mem_map(stack, 4 * page)
    uc.mem_map(data, page)
    reg = lambda name: getattr(regs, "UC_X86_REG_" + name)
    def boundary(machine, address, size, user):
        if address - base in boundaries or address == stop:
            machine.emu_stop()
    uc.hook_add(unicorn.UC_HOOK_CODE, boundary)
    saved_names = ("RBX", "RBP", "R12", "R13", "R14", "R15")
    if windows:
        saved_names += ("RSI", "RDI") + tuple("XMM" + str(i) for i in range(6, 16))
    checks = 0
    for name, (a, b), clobber in itertools.product(symbols, SAMPLES, (0, MASK, 0x0123456789abcdef)):
        for i, r in enumerate(GPRS + tuple("XMM" + str(i) for i in range(16))):
            uc.reg_write(reg(r), 0x2345678900000000 + i)
        saved = {r: uc.reg_read(reg(r)) for r in saved_names}
        for i in range(8):
            uc.reg_write(reg("FP" + str(i)), (0, 0))
        uc.reg_write(reg("FPCW"), 0x37f)
        uc.reg_write(reg("FPSW"), 0)
        uc.reg_write(reg("FPTAG"), 0xffff)
        for r, value in zip(("RCX", "RDX", "R8") if windows else ("RDI", "RSI", "RDX"), (a, b, data)):
            uc.reg_write(reg(r), value)
        sp = stack + 2 * page - 8
        uc.reg_write(reg("RSP"), sp)
        uc.reg_write(reg("EFLAGS"), 0x202)
        uc.mem_write(sp, struct.pack("<Q", stop))
        uc.mem_write(data, bytes(8))
        pc, seen = base + symbols[name], 0
        while pc != stop:
            uc.emu_start(pc, stop, count=10000)
            pc = uc.reg_read(reg("RIP"))
            if pc == stop:
                break
            assert pc - base in boundaries, (path, name, "missing FEMMS boundary", hex(pc))
            assert int.from_bytes(uc.mem_read(data, 8), "little") == a
            # MMX register accessors are no-ops in Unicorn 2.1. Use the
            # physical x87 aliases, whose low 64 bits are the MMX payloads.
            for i in range(8):
                uc.reg_write(reg("FP" + str(i)), ((clobber + i) & MASK, 0xffff))
            uc.reg_write(reg("FPTAG"), 0xffff)
            seen += 1
            pc += 2
        assert seen == (2 if name == "mmx_reset" else 1)
        byte_sum = sum(((((a >> shift) & 255) + ((b >> shift) & 255)) & 255) << shift
                       for shift in range(0, 64, 8))
        expected = (a ^ int(a < b)) if name == "mmx_reset" else byte_sum ^ a
        assert uc.reg_read(reg("RAX")) == expected, (path, name, a, b, clobber)
        assert uc.reg_read(reg("RSP")) == sp + 8, (path, "return stack")
        assert all(uc.reg_read(reg(r)) == value for r, value in saved.items()), (path, "C ABI")
        assert uc.reg_read(reg("FPTAG")) == 0xffff, (path, name, "MMX operation crossed final reset")
        checks += 1
    return {"functions": 2, "femms_boundaries": len(boundaries), "controlled_completions": checks,
            "x87_tags_empty_on_return": True}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--compiler", type=Path, required=True)
    p.add_argument("--llc", type=Path, required=True)
    p.add_argument("--keep", type=Path, required=True)
    args = p.parse_args()
    args.keep.mkdir(parents=True, exist_ok=True)
    source = args.keep / "mmx-state.c"
    source.write_text(SOURCE)
    reports = []
    for opt, (index, triple) in itertools.product(("O0", "O2"), enumerate(TRIPLES)):
        stem = f"{opt}-{index}"
        obj, ir = args.keep / (stem + ".o"), args.keep / (stem + ".ll")
        command = [args.compiler, "--target=" + triple, "-" + opt, "-ffreestanding",
                   "-fno-lto", "-fno-builtin-mimalloc", "-fno-stack-protector",
                   "-fno-function-sections", "-mno-stack-arg-probe",
                   "-mllvm", "-verify-machineinstrs", source]
        run(command + ["-c", "-o", obj])
        reports.append({"route": "NeverC", "optimization": opt, "triple": triple,
                        **qualify(obj, index == 1)})
        run(command + ["-S", "-emit-llvm", "-o", ir])
        assert " asm " not in ir.read_text()
        bridge = args.keep / (stem + ".bridge.o")
        run([args.llc, "-mtriple=" + triple, "-" + opt, "-verify-machineinstrs",
             "-filetype=obj", ir, "-o", bridge])
        reports.append({"route": "NeverC IR to LLVM", "optimization": opt, "triple": triple,
                        **qualify(bridge, index == 1)})
        print({"optimization": opt, "triple": triple, "qualified_objects": len(reports)}, flush=True)
    (args.keep / "qualification.json").write_text(json.dumps({"programs": reports,
        "physical_femms_execution_qualified": False, "x87_exception_delivery_qualified": False}, indent=2) + "\n")


if __name__ == "__main__":
    main()
