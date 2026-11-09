#!/usr/bin/env python3
"""Check i386 cpuid.h availability, ID restoration, signature and ABI.

Use the actual NeverC header in native and forced-fallback configurations, plus
stock Clang. Controlled CPUs permit or refuse EFLAGS.ID changes; CPUID receives
independent leaf results. No native CPUID or interrupt executes. Requires
capstone, lief and unicorn.
"""

import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess

import capstone
import lief
import unicorn
import unicorn.x86_const as regs

CODE, STACK, DATA, STOP, PAGE = 0x10000, 0x80000, 0xA0000, 0x1FFFF, 0x1000
ID = 1 << 21
SIGNATURE = 0x756E6547
SOURCE = """
#ifdef TEST_FALLBACK
#undef __has_builtin
#define __has_builtin(name) 0
#endif
#include <cpuid.h>
unsigned probe(unsigned leaf, unsigned *signature) {
  return __get_cpuid_max(leaf, signature);
}
"""


def execute(code, entry, segments, available, initial_id, leaf, signature, corruption=None):
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoded = {i.address: i for i in decoder.disasm(code, CODE)}
    assert sum(i.size for i in decoded.values()) == len(code)
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    machine.mem_map(CODE - PAGE, 17 * PAGE)
    for base in (STACK, DATA):
        machine.mem_map(base, 16 * PAGE)
    for address, content in segments:
        assert CODE - PAGE <= address and address + len(content) <= CODE + 16 * PAGE
        machine.mem_write(address, content)
    machine.mem_write(DATA, b'\xA5' * PAGE)
    sp = STACK + 15 * PAGE - 16
    machine.mem_write(sp, struct.pack('<3I', STOP, leaf, DATA + 128 if signature else 0))
    machine.reg_write(regs.UC_X86_REG_ESP, sp)
    machine.reg_write(regs.UC_X86_REG_EFLAGS, 2 | (ID if initial_id else 0))
    saved = {r: 0x12340000 + i for i, r in enumerate(
        (regs.UC_X86_REG_EBX, regs.UC_X86_REG_EBP, regs.UC_X86_REG_ESI, regs.UC_X86_REG_EDI))}
    for register, value in saved.items():
        machine.reg_write(register, value)
    calls, writes = [], []

    def hook(cpu, address, size, _):
        instruction = decoded[address]
        if instruction.mnemonic == 'popfd':
            word = struct.unpack('<I', cpu.mem_read(cpu.reg_read(regs.UC_X86_REG_ESP), 4))[0]
            writes.append(word & ID)
            # Model the non-writable ID bit at the architectural write.
            # Rewriting Unicorn EFLAGS at every hook would invalidate its
            # lazy arithmetic flags at a conditional branch.
            if not available:
                word = (word & ~ID) | (ID if initial_id else 0)
            if corruption == 'restore' and len(writes) == 2:
                word ^= ID
            cpu.mem_write(cpu.reg_read(regs.UC_X86_REG_ESP), struct.pack('<I', word))
        if instruction.mnemonic == 'cpuid':
            assert available, 'CPUID executed on unavailable CPU'
            assert (cpu.reg_read(regs.UC_X86_REG_EFLAGS) & ID) == (ID if initial_id else 0), 'ID restoration'
            assert cpu.reg_read(regs.UC_X86_REG_EAX) == leaf, 'CPUID leaf'
            calls.append(address)
            cpu.emu_stop()

    machine.hook_add(unicorn.UC_HOOK_CODE, hook)
    machine.emu_start(CODE + entry, STOP, count=10000)
    if calls:
        assert len(calls) == 1
        for register, value in zip((regs.UC_X86_REG_EAX, regs.UC_X86_REG_EBX,
                                     regs.UC_X86_REG_ECX, regs.UC_X86_REG_EDX),
                                    (leaf + 17, SIGNATURE, 0x12345678, 0xFEDCBA98)):
            machine.reg_write(register, value)
        instruction = decoded[calls[0]]
        machine.emu_start(instruction.address + instruction.size, STOP, count=10000)
    assert len(calls) == int(available), 'CPUID admission'
    assert len(writes) == 2, 'missing ID toggle or restore'
    assert machine.reg_read(regs.UC_X86_REG_EIP) == STOP, 'did not return'
    assert machine.reg_read(regs.UC_X86_REG_EAX) == (leaf + 17 if available else 0), 'leaf result'
    assert (machine.reg_read(regs.UC_X86_REG_EFLAGS) & ID) == (ID if initial_id else 0), 'ID restoration'
    assert machine.reg_read(regs.UC_X86_REG_ESP) == sp + 4, 'cdecl stack'
    assert all(machine.reg_read(r) == value for r, value in saved.items()), 'callee-saved registers'
    expected = bytearray(b'\xA5' * PAGE)
    if available and signature:
        expected[128:132] = struct.pack('<I', SIGNATURE)
    assert bytes(machine.mem_read(DATA, PAGE)) == expected, 'signature or unrelated memory'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--backend-llc', type=Path, required=True)
    parser.add_argument('--clang', type=Path, required=True)
    parser.add_argument('--linker', type=Path, required=True)
    parser.add_argument('--keep', type=Path, required=True)
    args = parser.parse_args()
    args.keep.mkdir(parents=True, exist_ok=True)
    resource = Path(subprocess.check_output([str(args.compiler), '-print-resource-dir'], text=True).strip())
    header = resource / 'include/cpuid.h'
    source = args.keep / 'availability.c'
    source.write_text(SOURCE)
    records = []
    for compiler, fallback, level, pic, dialect in itertools.product(
            ('neverc', 'clang'), (False, True), ('O0', 'O2'), (False, True), ('att', 'intel')):
        name = f'{compiler}-{fallback}-{level}-{pic}-{dialect}'
        ir, obj, exe = (args.keep / (name + suffix) for suffix in ('.ll', '.o', '.elf'))
        common = [str(args.compiler if compiler == 'neverc' else args.clang), '--target=i386-unknown-linux-gnu',
                  '-' + level, '-std=c23', '-ffreestanding', '-fno-lto', '-fno-stack-protector',
                  '-fPIC' if pic else '-fno-pic', '-masm=' + dialect, '-I', str(header.parent), str(source)]
        if compiler == 'neverc':
            common += ['-fno-builtin-mimalloc']
        if fallback:
            common += ['-DTEST_FALLBACK']
        commands = [common + ['-S', '-emit-llvm', '-o', str(ir)],
                    common + ['-c', '-o', str(obj)] +
                    (['-fllvm-backend=' + str(args.backend_llc)] if compiler == 'neverc' else []),
                    [str(args.linker), '-m', 'elf_i386', '-e', 'probe', '--image-base=0x10000',
                     '-Ttext=0x10000', str(obj), '-o', str(exe)]]
        for step, command in enumerate(commands):
            result = subprocess.run(command, capture_output=True, text=True, timeout=120)
            (args.keep / (name + f'-{step}.log')).write_text(result.stdout + result.stderr)
            assert result.returncode == 0, result.stderr
        if compiler == 'neverc' and not fallback:
            text = ir.read_text()
            assert ' asm ' not in text
            for operation in ('cpuidex', 'flags.read.u32', 'flags.write.u32'):
                assert '@llvm.x86.' + operation + '(' in text
        image = lief.parse(str(exe))
        assert not image.relocations
        assert image.get_section('.text').virtual_address == CODE
        code, entry = bytes(image.get_section('.text').content), image.get_symbol('probe').value - CODE
        segments = [(s.virtual_address, bytes(s.content)) for s in image.segments
                    if s.type == lief.ELF.Segment.TYPE.LOAD and s.physical_size]
        cases = list(itertools.product((False, True), (False, True), (0, 0x80000000), (False, True)))
        for case in cases:
            execute(code, entry, segments, *case)
        try:
            execute(code, entry, segments, True, False, 0, True, 'restore')
        except AssertionError as error:
            assert str(error) == 'ID restoration', error
        else:
            raise AssertionError('ID corruption escaped availability oracle')
        records.append({'name': name, 'comparisons': len(cases), 'negative_controls': 1,
                        'commands': commands, 'object_sha256': hashlib.sha256(obj.read_bytes()).hexdigest(),
                        'image_sha256': hashlib.sha256(exe.read_bytes()).hexdigest()})
    report = {'profiles': records, 'comparisons': sum(r['comparisons'] for r in records),
              'negative_controls': sum(r['negative_controls'] for r in records),
              'header_sha256': hashlib.sha256(header.read_bytes()).hexdigest()}
    (args.keep / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(records)} objects; {report["comparisons"]} availability/ABI comparisons; '
          f'{report["negative_controls"]} ID restoration controls')


if __name__ == '__main__':
    main()
