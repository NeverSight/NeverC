#!/usr/bin/env python3
"""Qualify actual Linux i386 C machine output for scalar MSR and port I/O.

Uses the independent original-byte transport oracles from the MSR and system
witnesses. Privileged instructions receive controlled completions; no MSR or
port instruction executes on the host. Requires capstone, lief and unicorn.
"""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

import capstone
import lief


ROOT = Path(__file__).resolve().parent


def load_witness(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), ROOT / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


MSR = load_witness('verify-msr-completion')
SYSTEM = load_witness('verify-system-completion')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def original_bytes(operation):
    if operation in ('rdmsr', 'wrmsr'):
        code = bytes.fromhex('8b 4c 24 04')
        if operation == 'wrmsr':
            code += bytes.fromhex('8b 44 24 08 8b 54 24 0c')
        return code + bytes.fromhex('0f 30' if operation == 'wrmsr' else '0f 32') + b'\xc3'
    writing = operation.startswith('out')
    code = bytes.fromhex('66 8b 54 24 04')
    if writing:
        code += bytes.fromhex('8b 44 24 08')
    width = SYSTEM.PORTS[operation]
    opcode = (0xEE if writing else 0xEC) + (width != 8)
    return code + (b'\x66' if width == 16 else b'') + bytes([opcode, 0xC3])


def execute(code, operation, dead, sample):
    if operation in ('rdmsr', 'wrmsr'):
        kind = 'write' if operation == 'wrmsr' else 'dead' if dead else 'read'
        MSR.execute(code, kind, *sample, 32)
    else:
        SYSTEM.execute(code, operation, dead, 32, sample, True)


def reject_corruptions(code, operation, dead, sample):
    mnemonic = operation if operation in ('rdmsr', 'wrmsr') else \
        'in' if operation.startswith('in') else 'out'
    decoded = list(capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32).disasm(code, 0))
    native = [i for i in decoded if i.mnemonic == mnemonic]
    assert len(native) == 1 and sum(i.size for i in decoded) == len(code)
    instruction = native[0]
    before, after = instruction.address, instruction.address + instruction.size
    if operation in ('rdmsr', 'wrmsr'):
        cases = [('selector', before, b'\x31\xc9', 'MSR index was changed')]
        if operation == 'wrmsr':
            cases.append(('value', before, b'\x31\xc0\x31\xd2', 'MSR write did not receive'))
        elif not dead:
            cases.append(('result', after, b'\x31\xc0\x31\xd2', 'RDMSR result is'))
    else:
        cases = [('selector', before, b'\x66\x31\xd2', 'port selector was changed')]
        if operation.startswith('out'):
            cases.append(('value', before, b'\x31\xc0', 'port output was truncated'))
        elif not dead:
            cases.append(('result', after, b'\x31\xc0', 'port input result was lost'))
    rejected = []
    for category, offset, mutation, message in cases:
        try:
            execute(code[:offset] + mutation + code[offset:], operation, dead, sample)
        except AssertionError as error:
            assert str(error).startswith(message), error
        else:
            raise AssertionError(category + ' corruption escaped the operand oracle')
        rejected.append(category)
    return rejected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--backend-llc', type=Path, required=True)
    parser.add_argument('--keep', type=Path, required=True)
    args = parser.parse_args()
    args.keep.mkdir(parents=True, exist_ok=True)
    records = []
    for operation in ('rdmsr', 'wrmsr', *SYSTEM.PORTS):
        reading = operation == 'rdmsr' or operation.startswith('in')
        width = 64 if operation in ('rdmsr', 'wrmsr') else SYSTEM.PORTS[operation]
        value_type = {8: 'unsigned char', 16: 'unsigned short',
                      32: 'unsigned', 64: 'unsigned long long'}[width]
        selector_type = 'unsigned' if width == 64 else 'unsigned short'
        parameters = selector_type + ' selector'
        if not reading:
            parameters += ', ' + value_type + ' value'
        values = MSR.VALUES if width == 64 else SYSTEM.VALUES
        baseline = original_bytes(operation)
        original = args.keep / (operation + '.bin')
        original.write_bytes(baseline)
        for dead in ((False, True) if reading else (False,)):
            for sample in values:
                execute(baseline, operation, dead, sample)
            for spelling in ('portable', 'microsoft'):
                api = '__builtin_ia32_' + operation if spelling == 'portable' else \
                    '__readmsr' if operation == 'rdmsr' else '__writemsr' if operation == 'wrmsr' else '__' + operation
                for level in ('O0', 'O2'):
                    name = f'{operation}-{dead}-{spelling}-{level}'
                    source = args.keep / (name + '.c')
                    prefix = '#include <intrin.h>\n' if spelling == 'microsoft' else ''
                    source.write_text(prefix + (value_type if reading and not dead else 'void') +
                                      f' probe({parameters}) {{ ' + ('return ' if reading and not dead else '') +
                                      api + '(selector' + (', value' if not reading else '') + '); }\n')
                    ir, obj = args.keep / (name + '.ll'), args.keep / (name + '.o')
                    common = [str(args.compiler), '--target=i386-unknown-linux-gnu', '-' + level,
                              '-std=c23', '-fms-extensions', '-D_MSC_VER=1930', '-ffreestanding',
                              '-fno-lto', '-fno-builtin-mimalloc', '-fno-pic', '-fno-pie',
                              '-fno-stack-protector', str(source)]
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
                    for sample in values:
                        execute(code, operation, dead, sample)
                    rejected = reject_corruptions(code, operation, dead, values[1])
                    records.append({'operation': operation, 'dead': dead, 'spelling': spelling,
                                    'optimization': level, 'source_sha256': digest(source),
                                    'object_sha256': digest(obj), 'original_sha256': digest(original),
                                    'commands': commands, 'checks': len(values), 'negative_controls': rejected})
    report = {'profiles': records, 'checks': sum(r['checks'] for r in records),
              'negative_controls': sum(len(r['negative_controls']) for r in records),
              'compiler_sha256': digest(args.compiler), 'llc_sha256': digest(args.backend_llc),
              'oracle_sha256': {name: digest(ROOT / (name + '.py')) for name in
                                ('verify-msr-completion', 'verify-system-completion')},
              'privileged_hardware_execution': False}
    (args.keep / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(records)} objects; {report["checks"]} controlled comparisons; '
          f'{report["negative_controls"]} operand negative controls rejected')


if __name__ == '__main__':
    main()
