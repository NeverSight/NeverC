#!/usr/bin/env python3
"""Compare native i386 REP pointer builtins with independent original bytes.

Both portable and Microsoft spellings retain element counts, aliasing, DF and
realigned dynamic frames. Small counts execute in Unicorn; large counts stop
at the actual REP boundary. DF=1 is a raw lowering probe, not a C ABI entry
claim. No privileged instruction or target hardware restart is exercised.
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

CODE, STOP, STACK, DATA, PAGE = 0x10000, 0x1ffff, 0x80000, 0xa0000, 0x1000
CANARY = 0x12345678 ^ 0xa5
OPS = {kind + suffix: (kind == 'movs', width)
       for kind in ('movs', 'stos') for suffix, width in (('b', 1), ('w', 2), ('d', 4))}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def original(operation):
    move, width = OPS[operation]
    code = bytes.fromhex('56 57 8b 7c 24 0c')
    code += bytes.fromhex('8b 74 24 10' if move else '8b 44 24 10')
    code += bytes.fromhex('8b 4c 24 14 0f b6 17 52 f3')
    code += b'\x66' if width == 2 else b''
    code += bytes([0xa4 if move else 0xaa]) if width == 1 else bytes([0xa5 if move else 0xab])
    return code + bytes.fromhex('58 8b 54 24 0c 0f b6 12 31 d0 35') + \
        struct.pack('<I', CANARY) + bytes.fromhex('5f 5e c3')


def execute(code, operation, sample, corrupt=None):
    count, backward, allocation, overlap, value = sample
    move, width = OPS[operation]
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoded = list(decoder.disasm(code, CODE))
    assert sum(i.size for i in decoded) == len(code), 'incomplete decode'
    repeats = [i for i in decoded if i.mnemonic.startswith('rep ')]
    assert len(repeats) == 1, 'REP multiplicity'
    rep = repeats[0]
    assert rep.bytes[-1] == (0xa4 if move else 0xaa) + (width != 1), 'REP operation'
    assert (0x66 in rep.bytes) == (width == 2) and 0x67 not in rep.bytes, 'REP widths'
    m = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    for base in (CODE, STACK, DATA):
        m.mem_map(base, 8 * PAGE)
    m.mem_write(CODE, code)
    src = DATA + 256
    dst = src + overlap if overlap else DATA + 128
    initial = bytes((i * 13 + 71) & 255 for i in range(PAGE))
    m.mem_write(DATA, initial)
    sp = STACK + 7 * PAGE - 32
    m.mem_write(sp, struct.pack('<5I', STOP, dst, src if move else value, count, allocation))
    m.reg_write(regs.UC_X86_REG_ESP, sp)
    m.reg_write(regs.UC_X86_REG_EFLAGS, 2 | (backward << 10))
    saved = {r: 0x19283740 + i for i, r in enumerate((regs.UC_X86_REG_EBX,
             regs.UC_X86_REG_ESI, regs.UC_X86_REG_EDI, regs.UC_X86_REG_EBP))}
    for r, v in saved.items():
        m.reg_write(r, v)
    m.emu_start(CODE, rep.address, count=10000)
    assert m.reg_read(regs.UC_X86_REG_EIP) == rep.address, 'REP boundary'
    mutations = {'destination': regs.UC_X86_REG_EDI, 'source': regs.UC_X86_REG_ESI,
                 'count': regs.UC_X86_REG_ECX, 'value': regs.UC_X86_REG_EAX}
    if corrupt in mutations:
        r = mutations[corrupt]
        m.reg_write(r, m.reg_read(r) ^ (0x80000000 if corrupt == 'count' else 1))
    if corrupt == 'direction':
        m.reg_write(regs.UC_X86_REG_EFLAGS, m.reg_read(regs.UC_X86_REG_EFLAGS) ^ 0x400)
    assert m.reg_read(regs.UC_X86_REG_EDI) == dst, 'destination'
    assert m.reg_read(regs.UC_X86_REG_ECX) == count, 'count'
    if move:
        assert m.reg_read(regs.UC_X86_REG_ESI) == src, 'source'
    else:
        mask = (1 << (width * 8)) - 1
        assert m.reg_read(regs.UC_X86_REG_EAX) & mask == value & mask, 'value'
    assert bool(m.reg_read(regs.UC_X86_REG_EFLAGS) & 0x400) == bool(backward), 'direction'
    assert bytes(m.mem_read(DATA, PAGE)) == initial, 'early memory'
    if count > 7:
        return {'kind': 'boundary', 'count': count}
    expected = bytearray(initial)
    step = -width if backward else width
    for i in range(count):
        at = dst - DATA + i * step
        if move:
            start = src - DATA + i * step
            element = bytes(expected[start:start + width])
        else:
            element = (value & ((1 << (width * 8)) - 1)).to_bytes(width, 'little')
        expected[at:at + width] = element
    m.emu_start(rep.address, STOP, count=10000)
    assert m.reg_read(regs.UC_X86_REG_EIP) == STOP, 'return'
    if corrupt == 'stack':
        m.reg_write(regs.UC_X86_REG_ESP, m.reg_read(regs.UC_X86_REG_ESP) ^ 4)
    if corrupt == 'callee saved':
        m.reg_write(regs.UC_X86_REG_EBX, m.reg_read(regs.UC_X86_REG_EBX) ^ 1)
    if corrupt == 'result':
        m.reg_write(regs.UC_X86_REG_EAX, m.reg_read(regs.UC_X86_REG_EAX) ^ 1)
    if corrupt == 'memory':
        m.mem_write(DATA, bytes([expected[0] ^ 1]))
    assert m.reg_read(regs.UC_X86_REG_ESP) == sp + 4, 'stack'
    assert all(m.reg_read(r) == v for r, v in saved.items()), 'callee saved'
    assert bytes(m.mem_read(DATA, PAGE)) == expected, 'memory'
    result = initial[dst - DATA] ^ expected[dst - DATA] ^ CANARY
    assert m.reg_read(regs.UC_X86_REG_EAX) == result, 'result'
    assert bool(m.reg_read(regs.UC_X86_REG_EFLAGS) & 0x400) == bool(backward), 'raw DF'
    return {'kind': 'completion', 'memory_sha256': hashlib.sha256(expected).hexdigest(), 'result': result}


def source(operation, spelling, frame):
    move, _ = OPS[operation]
    api = '__' + operation if spelling == 'microsoft' else \
        '__builtin_ia32_' + ('movsd_rep' if operation == 'movsd' else operation) + '32'
    prefix = ('_Alignas(64) volatile unsigned fixed[16]; fixed[0]=0x12345678;\n'
              'volatile unsigned char *dynamic=__builtin_alloca(allocation); dynamic[0]=0xa5;\n') if frame else ''
    canary = 'fixed[0] ^ dynamic[0]' if frame else '0x12345678U ^ 0xa5U'
    argument = 'const void *s' if move else 'unsigned value'
    return ('#include <intrin.h>\n' if spelling == 'microsoft' else '') + \
        f'unsigned probe(unsigned char *d, {argument}, unsigned count, unsigned allocation) {{\n' + \
        prefix + 'unsigned first=d[0];\n' + api + '(d,' + ('s' if move else 'value') + ',count);\n' + \
        'return first ^ d[0] ^ ' + canary + ';\n}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--backend-llc', type=Path, required=True)
    parser.add_argument('--keep', type=Path, required=True)
    args = parser.parse_args()
    args.keep.mkdir(parents=True)
    records = []
    samples = list(itertools.product((0, 1, 7, 0x80000003, 0xffffffff), (0, 1),
                                     (1, 129), (0, 1, -1), (0, 0xfedcba98)))
    for operation in OPS:
        raw = original(operation)
        reference = args.keep / (operation + '.bin')
        reference.write_bytes(raw)
        baselines = [execute(raw, operation, sample) for sample in samples]
        for spelling, frame, opt in itertools.product(('portable', 'microsoft'), (False, True), ('O0', 'O2')):
            name = f'{operation}-{spelling}-{frame}-{opt}'
            src, ir, obj = (args.keep / (name + ext) for ext in ('.c', '.ll', '.o'))
            src.write_text(source(operation, spelling, frame))
            common = [str(args.compiler), '--target=i386-unknown-linux-gnu', '-'+opt, '-std=gnu23',
                      '-fms-extensions', '-ffreestanding', '-fno-lto', '-fno-builtin-mimalloc',
                      '-fno-pic', '-fno-pie', '-fno-stack-protector', str(src)]
            commands = [common + ['-S', '-emit-llvm', '-o', str(ir)],
                        common + ['-fllvm-backend=' + str(args.backend_llc), '-c', '-o', str(obj)]]
            for i, command in enumerate(commands):
                r = subprocess.run(command, capture_output=True, text=True, timeout=120)
                (args.keep / (name + f'-{i}.log')).write_text(r.stdout + r.stderr)
                assert r.returncode == 0, r.stderr
            text = ir.read_text()
            intrinsic = 'movsd.rep' if operation == 'movsd' else operation
            assert f'@llvm.x86.{intrinsic}32(' in text and ' asm ' not in text
            image = lief.parse(str(obj))
            assert isinstance(image, lief.ELF.Binary)
            assert not any(r.has_section and r.section.name == '.text' for r in image.relocations)
            assert image.get_symbol('probe').value == 0
            code = bytes(image.get_section('.text').content)
            for sample, expected in zip(samples, baselines):
                assert execute(code, operation, sample) == expected
            fields = ('destination', 'source' if OPS[operation][0] else 'value', 'count',
                      'direction', 'result', 'memory', 'stack', 'callee saved')
            for field in fields:
                try:
                    execute(code, operation, (7, 0, 129, 0, 0xfedcba98), field)
                except AssertionError as error:
                    assert str(error) == field, error
                else:
                    raise AssertionError(field + ' corruption escaped oracle')
            records.append({'operation':operation,'spelling':spelling,'dynamic_frame':frame,'optimization':opt,
                            'commands':commands,'source_sha256':digest(src),'original_sha256':digest(reference),
                            'object_sha256':digest(obj),'completions':sum(r['kind']=='completion' for r in baselines),
                            'boundaries':sum(r['kind']=='boundary' for r in baselines),'negative_controls':fields})
    report = {'profiles':records,'compiler_sha256':digest(args.compiler),'llc_sha256':digest(args.backend_llc),
              'completions':sum(r['completions'] for r in records),'boundaries':sum(r['boundaries'] for r in records),
              'negative_controls':sum(len(r['negative_controls']) for r in records),'hardware_execution':False}
    (args.keep/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS: {len(records)} objects; {report["completions"]} completions; '
          f'{report["boundaries"]} boundaries; {report["negative_controls"]} rejected corruptions')


if __name__ == '__main__':
    main()
