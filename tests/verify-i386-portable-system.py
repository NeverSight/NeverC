#!/usr/bin/env python3
"""Qualify portable i386 CPUID, INT, WRMSRNS and REP port source interfaces.

Compile actual i386 source through NeverC's full-backend driver at O0/O2.
Independent original bytes and controlled completions check operands, memory,
continuation and callee-saved registers. Port I/O and privileged instructions
never execute. INT assumes a register-preserving returning handler; raw REP
DF=1 probes qualify lowering only, not an ABI-compliant C caller. Large counts
stop at the instruction boundary. No interrupt-gate, device or REP restart
semantics are claimed. Requires capstone, lief and unicorn.
"""

import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import struct
import subprocess

import capstone
import lief
import unicorn
import unicorn.x86_const as regs


ROOT = Path(__file__).resolve().parent
CODE, STACK, DATA, PAGE, STOP = 0x10000, 0x80000, 0xA0000, 0x1000, 0x1FFFF
FRAME = 0x12345678
PORTS = {direction + width + 'string': (direction == 'in', size)
         for direction in ('in', 'out')
         for width, size in (('byte', 1), ('word', 2), ('dword', 4))}


def witness(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), ROOT / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


MSR = witness('verify-msr-completion')
SYSTEM = witness('verify-system-completion')
INTERRUPT = witness('verify-interrupt-completion')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rep_bytes(operation):
    reading, width = PORTS[operation]
    # Save ESI/EDI, load cdecl inputs, and retain the first byte for comparison
    # with a fresh read after completion. Neither reference uses compiler IR.
    code = bytes.fromhex('56 57 8b 54 24 0c')
    code += bytes.fromhex('8b 7c 24 10' if reading else '8b 74 24 10')
    code += bytes.fromhex('8b 4c 24 14 0f b6') + (b'\x07' if reading else b'\x06') + b'\x50'
    code += b'\xf3' + (b'\x66' if width == 2 else b'')
    code += bytes([0x6C + (not reading) * 2 + (width != 1)])
    return code + bytes.fromhex('58 8b 4c 24 10 0f b6 11 31 d0 35') + \
        struct.pack('<I', FRAME ^ 0xA5) + bytes.fromhex('5f 5e c3')


def rep_execute(code, operation, sample, corrupt=None):
    port, count, direction, allocation = sample
    reading, width = PORTS[operation]
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoded = list(decoder.disasm(code, CODE))
    assert sum(i.size for i in decoded) == len(code), 'incomplete instruction decode'
    accesses = [i for i in decoded if i.mnemonic.startswith('rep ')]
    assert len(accesses) == 1, 'native REP count'
    access = accesses[0]
    expected_opcode = 0x6C + (not reading) * 2 + (width != 1)
    assert access.bytes[-1] == expected_opcode and (0x66 in access.bytes) == (width == 2)
    assert 0x67 not in access.bytes, 'wrong address size'
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    for base in (CODE, STACK, DATA):
        machine.mem_map(base, 8 * PAGE)
    machine.mem_write(CODE, code)
    pointer, sp = DATA + 129, STACK + 7 * PAGE - 32
    initial = bytes((i * 13 + 71) & 255 for i in range(PAGE))
    machine.mem_write(DATA, initial)
    machine.mem_write(sp, struct.pack('<5I', STOP, port, pointer, count, allocation))
    machine.reg_write(regs.UC_X86_REG_ESP, sp)
    machine.reg_write(regs.UC_X86_REG_EFLAGS, 2 | (direction << 10))
    saved = {r: 0x19283740 + i for i, r in enumerate(
        (regs.UC_X86_REG_EBX, regs.UC_X86_REG_ESI, regs.UC_X86_REG_EDI, regs.UC_X86_REG_EBP))}
    for register, value in saved.items():
        machine.reg_write(register, value)
    machine.emu_start(CODE, access.address, count=10000)
    assert machine.reg_read(regs.UC_X86_REG_EIP) == access.address, 'REP was not reached'
    address_reg = regs.UC_X86_REG_EDI if reading else regs.UC_X86_REG_ESI
    mutations = {'port': regs.UC_X86_REG_EDX, 'pointer': address_reg, 'count': regs.UC_X86_REG_ECX}
    if corrupt in mutations:
        register = mutations[corrupt]
        machine.reg_write(register, machine.reg_read(register) ^ 1)
    assert machine.reg_read(regs.UC_X86_REG_DX) == port, 'port transport'
    assert machine.reg_read(address_reg) == pointer, 'pointer transport'
    assert machine.reg_read(regs.UC_X86_REG_ECX) == count, 'count transport'
    assert (machine.reg_read(regs.UC_X86_REG_EFLAGS) >> 10) & 1 == direction, 'DF transport'
    assert bytes(machine.mem_read(DATA, PAGE)) == initial, 'early memory side effect'
    if count > 7:
        return 'boundary'
    expected = bytearray(initial)
    for i in range(count):
        offset = 129 + (-1 if direction else 1) * i * width
        if reading:
            expected[offset:offset + width] = bytes((0xE1 + i + j) & 255 for j in range(width))
        else:
            assert bytes(machine.mem_read(DATA + offset, width)) == initial[offset:offset + width]
    machine.mem_write(DATA, bytes(expected))
    machine.reg_write(address_reg, pointer + (-1 if direction else 1) * count * width)
    machine.reg_write(regs.UC_X86_REG_ECX, 0)
    machine.emu_start(access.address + access.size, STOP, count=10000)
    assert machine.reg_read(regs.UC_X86_REG_EIP) == STOP, 'REP did not return'
    assert machine.reg_read(regs.UC_X86_REG_ESP) == sp + 4, 'cdecl stack'
    assert all(machine.reg_read(r) == v for r, v in saved.items()), 'callee-saved register'
    assert bytes(machine.mem_read(DATA, PAGE)) == expected, 'unrelated memory modified'
    if corrupt == 'result':
        machine.reg_write(regs.UC_X86_REG_EAX, machine.reg_read(regs.UC_X86_REG_EAX) ^ 1)
    assert machine.reg_read(regs.UC_X86_REG_EAX) == initial[129] ^ expected[129] ^ FRAME ^ 0xA5, 'result transport'
    assert (machine.reg_read(regs.UC_X86_REG_EFLAGS) >> 10) & 1 == direction, 'raw DF changed'
    return 'completion'


def source(operation, spelling, frame):
    api = '__builtin_ia32_' + operation if spelling == 'portable' else '__' + operation
    if operation == 'wrmsrns':
        return f'void probe(unsigned index, unsigned long long value) {{ {api}(index, value); }}\n'
    if operation == 'cpuidex':
        return f'void probe(int *out, unsigned leaf, unsigned subleaf) {{ {api}(out, leaf, subleaf); }}\n'
    if operation == 'int':
        return INTERRUPT.SOURCE
    prefix = ('_Alignas(64) volatile unsigned fixed[16]; fixed[0]=0x12345678;\n'
              'volatile unsigned char *dynamic=__builtin_alloca(allocation); dynamic[0]=0xa5;\n') if frame else ''
    canary = 'fixed[0] ^ dynamic[0]' if frame else '0x12345678U ^ 0xa5U'
    return ('unsigned probe(unsigned short port, unsigned char *memory, unsigned count, unsigned allocation) {\n' +
            prefix + 'unsigned first=memory[0];\n' + api + '(port,memory,count);\n' +
            'return first ^ memory[0] ^ ' + canary + ';\n}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--backend-llc', type=Path, required=True)
    parser.add_argument('--keep', type=Path, required=True)
    args = parser.parse_args()
    args.keep.mkdir(parents=True, exist_ok=True)
    records = []
    samples = list(itertools.product((0, 0xFEDC), (0, 1, 7, 0x80000003, 0xFFFFFFFF), (0, 1), (1, 129)))
    for operation in ('wrmsrns', 'cpuidex', 'int', *PORTS):
        if operation in PORTS:
            baseline = rep_bytes(operation)
            (args.keep / (operation + '.bin')).write_bytes(baseline)
            for sample in samples:
                rep_execute(baseline, operation, sample)
        for spelling in (('portable', 'microsoft') if operation == 'cpuidex' or operation in PORTS else ('portable',)):
            for frame in ((False, True) if operation in PORTS else (False,)):
                for level in ('O0', 'O2'):
                    name = f'{operation}-{spelling}-{frame}-{level}'
                    src, ir, obj = (args.keep / (name + suffix) for suffix in ('.c', '.ll', '.o'))
                    src.write_text(('#include <intrin.h>\n' if spelling == 'microsoft' else '') + source(operation, spelling, frame))
                    common = [str(args.compiler), '--target=i386-unknown-linux-gnu', '-' + level,
                              '-std=c23', '-fms-extensions', '-D_MSC_VER=1930', '-ffreestanding',
                              '-fno-lto', '-fno-builtin-mimalloc', '-fno-pic', '-fno-pie',
                              '-fno-stack-protector', str(src)]
                    commands = [common + ['-S', '-emit-llvm', '-o', str(ir)],
                                common + ['-fllvm-backend=' + str(args.backend_llc), '-c', '-o', str(obj)]]
                    for step, command in enumerate(commands):
                        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
                        (args.keep / (name + f'-{step}.log')).write_text(result.stdout + result.stderr)
                        assert result.returncode == 0, result.stderr
                    text = ir.read_text()
                    assert '@llvm.x86.' + operation + '(' in text and ' asm ' not in text
                    image = lief.parse(str(obj))
                    assert isinstance(image, lief.ELF.Binary)
                    assert not any(r.has_section and r.section.name == '.text' for r in image.relocations)
                    assert image.get_symbol('probe').value == 0
                    code = bytes(image.get_section('.text').content)
                    controls, boundary, completion = [], 0, 0
                    if operation in PORTS:
                        for sample in samples:
                            if rep_execute(code, operation, sample) == 'boundary':
                                boundary += 1
                            else:
                                completion += 1
                        for corruption in ('port', 'pointer', 'count', 'result'):
                            try:
                                rep_execute(code, operation, (0xFEDC, 7, 0, 129), corruption)
                            except AssertionError as error:
                                assert str(error) == corruption + ' transport', error
                            else:
                                raise AssertionError('operand corruption escaped REP oracle')
                            controls.append(corruption)
                    elif operation == 'wrmsrns':
                        for sample in MSR.VALUES:
                            MSR.execute(code, 'write_ns', *sample, 32)
                            completion += 1
                        offset = code.index(bytes.fromhex('0f 01 c6'))
                        for corruption, mutation, message in (
                            ('selector', b'\x31\xc9', 'MSR index was changed'),
                            ('value', b'\x31\xd2', 'MSR write did not receive')):
                            try:
                                MSR.execute(code[:offset] + mutation + code[offset:], 'write_ns', *MSR.VALUES[1], 32)
                            except AssertionError as error:
                                assert str(error).startswith(message), error
                            else:
                                raise AssertionError('corruption escaped MSR oracle')
                            controls.append(corruption)
                    elif operation == 'cpuidex':
                        for sample in SYSTEM.VALUES:
                            SYSTEM.execute(code, 'cpuidex', False, 32, sample, True)
                            completion += 1
                        offset = code.index(b'\x0f\xa2')
                        for corruption, position, mutation, message in (
                            ('leaf', offset, b'\x31\xc0', 'CPUID leaf/subleaf'),
                            ('result', offset + 2, b'\x31\xd2', 'CPUID outputs')):
                            try:
                                SYSTEM.execute(code[:position] + mutation + code[position:], 'cpuidex', False, 32, SYSTEM.VALUES[1], True)
                            except AssertionError as error:
                                assert str(error).startswith(message), error
                            else:
                                raise AssertionError('corruption escaped CPUID oracle')
                            controls.append(corruption)
                    else:
                        for value in INTERRUPT.VALUES:
                            INTERRUPT.execute(code, 32, 'unknown-linux-gnu', value)
                            completion += 1
                        offset = code.index(b'\xcd\x2c')
                        try:
                            INTERRUPT.execute(code[:offset] + b'\x90\x90' + code[offset + 2:], 32, 'unknown-linux-gnu', 1)
                        except AssertionError:
                            controls.append('missing interrupt')
                        else:
                            raise AssertionError('missing interrupt escaped oracle')
                    records.append({'operation': operation, 'spelling': spelling, 'dynamic_frame': frame,
                                    'optimization': level, 'source_sha256': digest(src), 'object_sha256': digest(obj),
                                    'commands': commands, 'boundaries': boundary, 'completions': completion,
                                    'negative_controls': controls})
    report = {'profiles': records, 'compiler_sha256': digest(args.compiler),
              'llc_sha256': digest(args.backend_llc),
              'boundaries': sum(r['boundaries'] for r in records),
              'completions': sum(r['completions'] for r in records),
              'negative_controls': sum(len(r['negative_controls']) for r in records),
              'privileged_hardware_execution': False}
    (args.keep / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(records)} objects; {report["completions"]} completions; '
          f'{report["boundaries"]} large-count boundaries; {report["negative_controls"]} negative controls')


if __name__ == '__main__':
    main()
