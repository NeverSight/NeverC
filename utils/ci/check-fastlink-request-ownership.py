"""Build actual FastLink sources, then test serial, nested and concurrent calls."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import re
import struct
import subprocess
import time
import traceback


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root, output = args.repo.resolve(), args.output.resolve()
    assert platform.system() == 'Linux' and platform.machine() == 'x86_64'
    assert not output.exists()
    output.mkdir(parents=True)
    report = {'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
              'passed': False, 'completed': False, 'commands': [], 'variants': [],
              'neverc_compiler_rebuilt': False, 'full_ci_passed': False, 'goal_complete': False,
              'process_teardown_allocation_policy_changed': False}

    def sha(path):
        return hashlib.sha256(Path(path).read_bytes()).hexdigest()

    def save():
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(command, label, expected=0):
        log = output / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        before = time.monotonic()
        with log.open('w') as stream:
            try:
                process = subprocess.run(list(map(str, command)), cwd=root, stdout=stream,
                                         stderr=subprocess.STDOUT, timeout=60)
                code = process.returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = 124
        report['commands'].append({'command': list(map(str, command)), 'returncode': code,
                                   'expected_returncode': expected, 'timeout_seconds': 60,
                                   'elapsed_seconds': time.monotonic() - before,
                                   'log': str(log.relative_to(output)), 'log_sha256': sha(log)})
        save()
        assert code == expected, (label, code, expected)
        return log.read_text()

    def elf(path, kind):
        data = path.read_bytes()
        assert data[:6] == b'\x7fELF\x02\x01' and int.from_bytes(data[18:20], 'little') == 62
        assert int.from_bytes(data[16:18], 'little') in kind
        return {'path': str(path.relative_to(output)), 'size': len(data), 'sha256': sha(path)}

    def section_bytes(path, wanted):
        data = path.read_bytes()
        offset = struct.unpack_from('<Q', data, 40)[0]
        size, count, names = struct.unpack_from('<HHH', data, 58)
        rows = [struct.unpack_from('<IIQQQQIIQQ', data, offset + index * size)
                for index in range(count)]
        strings = rows[names]
        strings = data[strings[4]:strings[4] + strings[5]]
        for row in rows:
            end = strings.index(b'\0', row[0])
            if strings[row[0]:end].decode() == wanted:
                return data[row[4]:row[4] + row[5]], row[2]
        raise AssertionError('missing actual output section ' + wanted)

    save()
    try:
        run(['python3', '--version'], 'environment/python')
        run(['c++', '--version'], 'environment/cxx')
        run(['cc', '--version'], 'environment/cc')
        run(['nm', '--version'], 'environment/nm')
        source = 'neverc/lib/Linker/Backends/ELF/FastPath/FastLink.cpp'
        include = root / Path(source).parent
        harness = 'tests/neverc/Inputs/Linker/FastLinkRequestIsolation.cpp'
        helper = 'utils/ci/check-fastlink-request-ownership.py'
        report['source_sha256'] = {name: sha(root / name) for name in [
            source, str(Path(source).with_name('FastLink.h')),
            str(Path(source).with_name('FastLinkSupport.h')), harness, helper,
            '.github/workflows/diagnose-cpp-template-source.yml']}
        parent = '4440fea67bfe10661ec522feeb8128dd8d35193c'
        subprocess.run(['git', 'merge-base', '--is-ancestor', parent, report['revision']], cwd=root, check=True)
        report['baseline_revision'] = parent
        original = subprocess.check_output(['git', 'show', parent + ':' + source], cwd=root)
        baseline = output / 'original/FastLink.cpp'
        baseline.parent.mkdir()
        baseline.write_bytes(original)
        report['baseline_source'] = {'path': str(baseline.relative_to(output)), 'sha256': sha(baseline)}
        for name in ['FastLink.h', 'FastLinkSupport.h']:
            assert subprocess.check_output(['git', 'show', parent + ':' + str(Path(source).with_name(name))], cwd=root) == (include / name).read_bytes()
        inputs = output / 'inputs'
        inputs.mkdir()
        (inputs / 'entry.s').write_text(''' .text
 .globl _start
 .type _start,@function
_start:
 call result
 mov %eax, %edi
 mov $60, %eax
 syscall
 .section .note.GNU-stack,"",@progbits
''')
        run(['cc', '-c', inputs / 'entry.s', '-o', inputs / 'entry.o'], 'fixtures/entry')
        report['input_objects'] = [elf(inputs / 'entry.o', {1})]
        for index, value in enumerate([13, 29, 47, 61]):
            unit = inputs / ('value-' + str(index) + '.c')
            unit.write_text('''extern const char text_a[], text_b[];
__attribute__((noinline)) int duplicate_one(void) { return 2; }
__attribute__((noinline)) int duplicate_two(void) { return 2; }
int result(void) { return @VALUE@ + duplicate_one() - duplicate_two() + text_a[0] - text_b[0]; }
__asm__(".pushsection .rodata.str1.1,\\\"aMS\\\",@progbits,1\\n"
        ".globl text_a\\ntext_a: .asciz \\\"same-merge-piece\\\"\\n"
        ".globl text_b\\ntext_b: .asciz \\\"same-merge-piece\\\"\\n.popsection\\n"
        ".pushsection .debug_str,\\\"MS\\\",@progbits,1\\n"
        ".asciz \\\"scope-debug-piece\\\"\\n"
        ".asciz \\\"scope-debug-piece\\\"\\n.popsection\\n");
'''.replace('@VALUE@', str(value)))
            obj = unit.with_suffix('.o')
            run(['cc', '-std=c11', '-O0', '-g', '-ffunction-sections', '-fdata-sections',
                 '-fno-pie', '-fno-stack-protector', '-c', unit, '-o', obj], 'fixtures/value-' + str(index))
            report['input_objects'].append(elf(obj, {1}))
        wrong = bytearray((inputs / 'value-0.o').read_bytes())
        # AArch64's machine identifier is enough for the actual early reject;
        # it is never executed, interpreted as AArch64 code or reported valid.
        wrong[18:20] = (183).to_bytes(2, 'little')
        (inputs / 'wrong.o').write_bytes(wrong)
        report['wrong_machine_fixture'] = {'path': 'inputs/wrong.o', 'sha256': sha(inputs / 'wrong.o'),
                                            'mutated_bytes': [18, 19], 'expected': 'declined before linking'}
        baseline_products = {}
        for variant, mode, fastlink, scope in [
                ('original', '-O2', baseline, 'serial'),
                ('owned', '-O0', root / source, 'all'),
                ('owned', '-O2', root / source, 'all')]:
            tag = variant + '-' + mode[1:]
            program = output / (tag + '.probe')
            run(['c++', '-std=c++17', mode, '-g0', '-pthread', '-fno-fast-math', '-ffp-contract=off',
                 '-I' + str(include), fastlink, root / harness, '-o', program], 'build/' + tag)
            binary = elf(program, {2, 3})
            symbol_text = run(['nm', '-C', '--defined-only', program], 'payload-symbols/' + tag)
            writable = [match.group(1) for line in symbol_text.splitlines()
                        if (match := re.match(r'^[0-9a-fA-F]+\s+[bBdD]\s+(.+)$', line))]
            former_payloads = ['(anonymous namespace)::' + name for name in [
                'activeOptions', 'ctx', 'needsTlsLd', 'needsGotBase', 'markTime',
                'relKinds', 'L', 'mergeCandidates', 'mergeGroups', 'mergeTable',
                'mergeRank', 'mergeOut']]
            former_payloads += ['(anonymous namespace)::icf::' + name for name in [
                'targetPlaced', 'targetBase', 'outputFoldable']]
            payloads = sorted(name for name in writable if name in former_payloads)
            assert payloads == (sorted(former_payloads) if variant == 'original' else [])
            text = run([program, inputs, output / tag, scope], 'harness/' + tag)
            assert text.splitlines()[-1] == 'ERRORS\t0'
            rows = [line.split('\t') for line in text.splitlines() if line.startswith('RESULT\t')]
            assert len(rows) == (5 if variant == 'original' else 24)
            assert len({row[1] for row in rows}) == len(rows)
            case = {'variant': variant, 'optimization': mode, 'scope': scope,
                    'binary': binary, 'results': [], 'linked': 0, 'declined': 0,
                    'actual_writable_former_payload_symbols': payloads}
            report['variants'].append(case)
            for row in sorted(rows, key=lambda entry: entry[1]):
                assert len(row) == 7 and row[3] == 'pass'
                name, status, expected, path = row[1], row[2], int(row[4]), Path(row[5])
                assert path == output / tag / (name + '.elf')
                if status == 'declined':
                    assert expected == 0 and 'not an x86-64 ELF object' in row[6]
                    assert not path.exists()
                    case['declined'] += 1
                    case['results'].append({'name': name, 'status': status, 'reason': row[6]})
                    continue
                assert status == 'linked' and expected in [13, 29, 47, 61] and not row[6]
                product = elf(path, {2})
                run([path], 'native/' + tag + '-' + name, expected)
                symbols = run(['nm', '--defined-only', path], 'symbols/' + tag + '-' + name)
                addresses = {}
                for line in symbols.splitlines():
                    parts = line.split()
                    if len(parts) == 3 and parts[2] in ['duplicate_one', 'duplicate_two', 'text_a', 'text_b']:
                        addresses[parts[2]] = int(parts[0], 16)
                assert len(addresses) == 4
                assert addresses['text_a'] != addresses['text_b']
                allocated, allocated_flags = section_bytes(path, '.rodata')
                debug, debug_flags = section_bytes(path, '.debug_str')
                assert allocated_flags & 2 and allocated.count(b'same-merge-piece\0') == 2
                assert not debug_flags & 2 and debug.count(b'scope-debug-piece\0') == 1
                folded = addresses['duplicate_one'] == addresses['duplicate_two']
                assert folded == (name not in ['serial-0', 'serial-3'])
                if variant == 'original':
                    baseline_products[name] = product['sha256']
                elif name.startswith('serial-'):
                    assert product['sha256'] == baseline_products[name]
                case['linked'] += 1
                case['results'].append({'name': name, 'status': status, 'expected_exit': expected,
                                        'product': product, 'addresses': addresses, 'icf_folded': folded,
                                        'allocated_text_pieces': 2, 'merged_debug_pieces': 1})
            assert (case['linked'], case['declined']) == ((4, 1) if variant == 'original' else (18, 6))
        report['serial_products_identical_to_original'] = True
        report['native_product_executions'] = 40
        report['real_fastlink_builds'] = 3
        report['sdk_bytes_modified'] = False
        report['global_state_allowlist_modified'] = False
        report['completed'] = report['passed'] = True
    except Exception:
        report['error'] = traceback.format_exc()
        save()
        raise
    finally:
        save()
    print('Actual FastLink serial/nested/concurrent evidence passed; full CI remains separate.')


if __name__ == '__main__':
    main()
