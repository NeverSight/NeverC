#!/usr/bin/env python3
"""Run exact repaired GoogleTest bodies and preserve faithful host-crash evidence."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import struct
import subprocess
import time
import traceback
import xml.etree.ElementTree as ET


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--clangxx', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root, output = args.repo.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {'passed': False, 'completed': False, 'full_ci_passed': False,
              'host_bug_fixed': False, 'compiler_rebuilt': False,
              'formal_test_binary_executed': False, 'goal_complete': False,
              'commands': [], 'formal_xml': [], 'host_xml': [], 'fixed_path_runs': [],
              'runtime_package_equivalence_verified': False,
              'frontend_timeout_seconds': 60}

    def save():
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(command, name, timeout=60, env=None):
        command = list(map(str, command))
        log = output / (name + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        started = time.time_ns()
        with log.open('w') as stream:
            try:
                status = subprocess.run(command, cwd=root, env=env,
                                        stdout=stream, stderr=subprocess.STDOUT,
                                        timeout=timeout).returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                status = -1000
        row = {'command': command, 'cwd': str(root), 'returncode': status,
               'log': str(log.relative_to(output)), 'log_sha256': sha(log),
               'timeout_seconds': timeout, 'elapsed_ns': time.time_ns() - started}
        report['commands'].append(row)
        save()
        print(name, status, flush=True)
        return status

    def elf(path):
        data = path.read_bytes()
        assert data[:6] == b'\x7fELF\x02\x01' and data[18:20] == b'\x3e\x00'
        result = {'path': str(path.relative_to(output)), 'sha256': sha(path), 'size': len(data)}
        # Parse real section bytes without a generated-code execution oracle.
        offset = struct.unpack_from('<Q', data, 40)[0]
        stride, count, strings = struct.unpack_from('<HHH', data, 58)
        assert stride == 64 and 0 < count < 65535 and strings < count
        rows = [struct.unpack_from('<IIQQQQIIQQ', data, offset + i * stride) for i in range(count)]
        names = data[rows[strings][4]:rows[strings][4] + rows[strings][5]]
        sections = {}
        for row in rows:
            name = names[row[0]:].split(b'\0', 1)[0].decode()
            if name in ('.text', '.rodata', '.data', '.bss'):
                sections[name] = {'size': row[5], 'type': row[1],
                                  'sha256': digest(data[row[4]:row[4] + row[5]]) if row[1] != 8 else None}
        assert '.text' in sections
        result['sections'] = sections
        return result

    def xml_record(path, expected):
        tree = ET.parse(path)
        tests = tree.getroot().findall('.//testcase')
        actual = [row.attrib['classname'] + '.' + row.attrib['name'] for row in tests]
        assert sorted(actual) == sorted(expected), (actual, expected)
        assert all(row.attrib.get('status') == 'run' for row in tests)
        assert all(row.attrib.get('result') == 'completed' for row in tests)
        assert not any(row.find('skipped') is not None for row in tests)
        return {'path': str(path.relative_to(output)), 'sha256': sha(path),
                'test_names': actual, 'failures': sum(row.find('failure') is not None for row in tests)}

    try:
        assert platform.system() == 'Linux' and platform.machine() == 'x86_64'
        report['os_release'] = Path('/etc/os-release').read_text()
        assert 'VERSION_ID="22.04"' in report['os_release']
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        plan_path = root / 'utils/ci/cpp-verified-formal-plan.json'
        plan = json.loads(plan_path.read_text())
        report['plan'] = plan
        names = list(plan['test_body_sha256'])
        assert len(names) == plan['formal_test_count'] == 38
        assert plan['frontend_timeout_seconds'] == 60
        for name, value in plan['source_sha256'].items():
            assert sha(root / name) == value, name
        source_names = [*plan['source_sha256'], 'utils/ci/check-cpp-verified-formal.py',
                        'utils/ci/cpp-verified-formal-plan.json',
                        'utils/ci/download-cpp-linux-baseline.py',
                        '.github/workflows/diagnose-cpp-template-source.yml']
        report['source_sha256'] = {name: sha(root / name) for name in source_names}
        provenance = json.loads(args.baseline.read_text())
        assert provenance['revision'] == '1617f18ead74a8fa719e3d1bd02f9c84a10de551'
        assert provenance['run_id'] == 39244936536 and provenance['artifact_id'] == 12195306550
        assert provenance['artifact_sha256'] == '9332fad0d0226257f109ae74c1655a4e35510783d1029dd2bc8bdc15bfaf5206'
        compiler = Path(provenance['compiler'])
        assert sha(compiler) == provenance['compiler_sha256'] == plan['compiler_sha256']
        report['baseline_provenance'] = provenance
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == '1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        catalog = json.loads((sdk / 'catalog.json').read_text())
        assert len(catalog['headers']) == 664 and len(catalog['metadata']) == 1
        for entry in catalog['headers'] + catalog['metadata']:
            assert sha(sdk / entry['root'] / entry['path']) == entry['sha256']
        report['sdk_files_verified'] = 665
        clangxx = args.clangxx.absolute()
        assert run([clangxx, '--version'], 'environment/clang-version') == 0
        assert 'clang version 22.' in (output / 'environment/clang-version.log').read_text()
        assert run(['uname', '-a'], 'environment/uname') == 0
        assert run(['getconf', 'GNU_LIBC_VERSION'], 'environment/libc') == 0

        source = (root / 'tests/neverc/TranslateTests.cpp').read_bytes()
        matches = list(re.finditer(rb'(?m)^TEST_F\(\s*TranslateTest\s*,\s*(\w+)\s*\)\s*\{', source))
        assert len(matches) == plan['whole_test_names'] == 7493
        prefix = source[:matches[0].start()]
        assert digest(prefix) == plan['fixture_prefix_sha256']
        bodies = {m.group(1).decode(): source[m.start():matches[i + 1].start()]
                  for i, m in enumerate(matches[:-1]) if m.group(1).decode() in names}
        assert {name: digest(body) for name, body in bodies.items()} == plan['test_body_sha256']
        host = (root / 'tests/neverc/HostTests.cpp').read_bytes()
        match = re.search(rb'(?ms)^TEST_F\(HostTest, CStdlibPatterns_NoLTO\) \{.*?(?=^TEST_F\()', host)
        assert match and digest(match.group()) == plan['host_test_body_sha256']
        # Preserve the exact original program, flags, assertions, and popen-based
        # fixture. Only the diagnostic HostTest cleanup override copies its own
        # outputs AFTER TestBody returns, before calling the original TearDown.
        host_class = b'''#include "NeverCTestFixture.h"
#include <cstdlib>
class HostTest : public NeverCTest {
protected:
  void TearDown() override {
    if (const char *destination = std::getenv("NEVERC_DIAGNOSTIC_RETAIN")) {
      std::error_code error;
      fs::create_directories(destination, error);
      fs::copy(tmp(), fs::path(destination) / tmp().filename(),
               fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
    }
    NeverCTest::TearDown();
  }
};
'''
        generated = root / 'tests/neverc/VerifiedFormalReplay.cpp'
        generated.write_bytes(prefix + b'\n'.join(bodies.values()) + b'\n} // namespace\n' + host_class + match.group())
        retained_source = output / 'VerifiedFormalReplay.cpp'
        shutil.copy2(generated, retained_source)
        report['generated_source'] = {'path': retained_source.name, 'sha256': sha(retained_source),
                                      'prefix_byte_preserved': True, 'all_38_body_bytes_preserved': True,
                                      'host_body_byte_preserved': True,
                                      'host_cleanup_override_after_assertions_only': True}
        support = output / 'llvm-support'
        # Repository LLVM JSON/Error APIs differ from upstream apt LLVM. Build
        # their real Support owner; do not mix incompatible libraries or stubs.
        config = ['cmake', '-S', root / 'llvm', '-B', support, '-G', 'Ninja',
                  '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=/usr/bin/clang-22',
                  '-DCMAKE_CXX_COMPILER=' + str(clangxx), '-DLLVM_ENABLE_PROJECTS=',
                  '-DLLVM_TARGETS_TO_BUILD=', '-DLLVM_INCLUDE_TESTS=OFF',
                  '-DLLVM_INCLUDE_EXAMPLES=OFF', '-DLLVM_INCLUDE_BENCHMARKS=OFF',
                  '-DLLVM_INCLUDE_UTILS=OFF', '-DLLVM_INCLUDE_TOOLS=ON',
                  '-DLLVM_ENABLE_ASSERTIONS=OFF', '-DLLVM_ENABLE_ZLIB=OFF',
                  '-DLLVM_ENABLE_ZSTD=OFF', '-DLLVM_ENABLE_LIBXML2=OFF',
                  '-DLLVM_ENABLE_CURL=OFF', '-DLLVM_ENABLE_TERMINFO=OFF']
        assert run(config, 'build/configure-real-support', 180) == 0
        assert run(['cmake', '--build', support, '--target', 'llvm-config', '--parallel', '4'],
                   'build/real-support', 900) == 0
        llvm_config = support / 'bin/llvm-config'
        assert run([llvm_config, '--ldflags', '--libs', 'support', '--system-libs'], 'build/support-link-flags') == 0
        flags = shlex.split((output / 'build/support-link-flags.log').read_text())
        binary = output / 'verified-formal-tests'
        command = [clangxx, '-std=c++17', '-O0', '-g0', '-pthread',
                   '-I' + str(root / 'neverc/include'), '-I' + str(root / 'llvm/include'),
                   '-I' + str(support / 'include'), '-DNEVERC_BINARY="' + str(compiler) + '"',
                   '-DTEST_SOURCE_DIR="' + str(root / 'tests/neverc') + '"',
                   '-DTEST_C_DIR="' + str(root / 'tests/c') + '"',
                   generated, root / 'tests/neverc/NeverCTestFixture.cpp',
                   '-lgtest_main', '-lgtest', *flags, '-o', binary]
        assert run(command, 'build/real-gtest', 180) == 0
        report['gtest_binary'] = elf(binary)
        report['linked_support_source'] = 'actual repository llvm/lib/Support and Demangle'
        assert run([binary, '--gtest_list_tests'], 'gtest/list') == 0
        listing = (output / 'gtest/list.log').read_text().splitlines()
        actual, suite = [], None
        for line in listing:
            if line and not line.startswith(' ') and line.endswith('.'):
                suite = line.strip()
            elif line.startswith('  ') and suite:
                actual.append(suite + line.strip())
        expected = ['TranslateTest.' + name for name in names]
        assert sorted(actual) == sorted(expected + ['HostTest.CStdlibPatterns_NoLTO'])
        report['listed_test_names'] = actual
        xml = output / 'gtest/translate.xml'
        status = run([binary, '--gtest_filter=TranslateTest.*', '--gtest_output=xml:' + str(xml)], 'gtest/translate', 1200)
        report['formal_test_binary_executed'] = True
        report['formal_process_returncode'] = status
        report['formal_xml'].append(xml_record(xml, expected))
        save()

        for index in range(plan['host_test_repetitions']):
            env = dict(os.environ, NEVERC_DIAGNOSTIC_RETAIN=str(output / 'host' / str(index)))
            xml = output / ('gtest/host-' + str(index) + '.xml')
            status = run([binary, '--gtest_filter=HostTest.CStdlibPatterns_NoLTO',
                          '--gtest_output=xml:' + str(xml)], 'gtest/host-' + str(index), 180, env)
            row = xml_record(xml, ['HostTest.CStdlibPatterns_NoLTO'])
            row['process_returncode'] = status
            programs = list((output / 'host' / str(index)).rglob('test_c_stdlib_patterns_nolto'))
            assert len(programs) == 1
            row['retained_program'] = elf(programs[0])
            report['host_xml'].append(row)
            if status != 0:
                run(['gdb', '-batch', '-ex', 'set pagination off', '-ex', 'run',
                     '-ex', 'thread apply all bt full', '-ex', 'info registers', str(programs[0])],
                    'host/gdb-' + str(index))
            save()

        fixed = output / 'fixed-path'
        fixed.mkdir()
        original = root / 'tests/neverc/standards/test_c_stdlib_patterns.c'
        for index in range(plan['fixed_path_compilations']):
            tag = 'fixed-path/' + str(index)
            obj, program = fixed / 'program.o', fixed / 'program'
            assert run([compiler, '-std=c11', '-fno-lto', '-c', original, '-o', obj], tag + '-object') == 0
            assert run([compiler, '-std=c11', '-fno-lto', original, '-o', program], tag + '-link') == 0
            status = run([program], tag + '-run', 30)
            saved = fixed / ('program-' + str(index))
            shutil.copy2(program, saved)
            row = {'index': index, 'returncode': status, 'executable': elf(saved),
                   'object_sha256': sha(obj),
                   'all_passed_message': 'test_c_stdlib_patterns: ALL PASSED' in (output / (tag + '-run.log')).read_text()}
            report['fixed_path_runs'].append(row)
            if status != 0:
                run(['gdb', '-batch', '-ex', 'run', '-ex', 'thread apply all bt full',
                     '-ex', 'info registers', str(saved)], tag + '-gdb')
            save()
        report['fixed_path_distinct_binary_hashes'] = len({row['executable']['sha256'] for row in report['fixed_path_runs']})
        report['host_crash_observed'] = (any(row['failures'] for row in report['host_xml']) or
                                         any(row['returncode'] != 0 for row in report['fixed_path_runs']))
        report['completed'] = True
        report['passed'] = (report['formal_process_returncode'] == 0 and
                            all(row['failures'] == 0 for row in report['formal_xml']) and
                            all(row['failures'] == 0 and row['process_returncode'] == 0 for row in report['host_xml']) and
                            all(row['returncode'] == 0 and row['all_passed_message'] for row in report['fixed_path_runs']))
    except Exception:
        report['error'] = traceback.format_exc()
        save()
        raise
    finally:
        save()
    print('Real formal replay passed:', report['passed'], '; whole CI and host fix remain unverified.', flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
