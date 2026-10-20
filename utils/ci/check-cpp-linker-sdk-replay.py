#!/usr/bin/env python3
"""Execute preserved linker GoogleTests and actual SDK consistency checks."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
from cpp_diagnostic_gtest import build_gtest
import platform
import re
import shlex
import subprocess
import sys
import time
import traceback
import xml.etree.ElementTree as ET


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--clangxx', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--host-ubuntu-version', choices=['22.04', '24.04'], default='22.04')
    args = parser.parse_args()
    root, output = args.repo.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {'passed': False, 'completed': False, 'full_ci_passed': False,
              'compiler_rebuilt': False, 'private_frontend_changed': False,
              'goal_complete': False, 'commands': [], 'xml': []}

    def save():
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(command, name, timeout=60):
        command = list(map(str, command))
        log = output / (name + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        started = time.time_ns()
        with log.open('w') as stream:
            try:
                code = subprocess.run(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT,
                                      timeout=timeout).returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1000
        report['commands'].append({'command': command, 'returncode': code,
                                   'timeout_seconds': timeout, 'log': str(log.relative_to(output)),
                                   'log_sha256': sha(log), 'elapsed_ns': time.time_ns() - started})
        save()
        print(name, code, flush=True)
        return code

    def binary(path):
        data = path.read_bytes()
        assert data[:6] == b'\x7fELF\x02\x01' and data[18:20] == b'\x3e\x00'
        return {'path': str(path.relative_to(output)), 'sha256': sha(path), 'size': len(data)}

    try:
        assert platform.system() == 'Linux' and platform.machine() == 'x86_64'
        release = Path('/etc/os-release').read_text()
        assert 'VERSION_ID="' + args.host_ubuntu_version + '"' in release
        report['actual_host_ubuntu_version'] = args.host_ubuntu_version
        report['actual_host_os_release'] = release
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        plan = json.loads((root / 'utils/ci/cpp-linker-sdk-replay-plan.json').read_text())
        report['plan'] = plan
        assert len(plan['test_names']) == 62
        for name, value in plan['source_sha256'].items():
            assert sha(root / name) == value, name
        inputs = [*plan['source_sha256'], 'utils/ci/check-cpp-linker-sdk-replay.py',
                  'utils/ci/cpp-linker-sdk-replay-plan.json',
                  '.github/workflows/diagnose-cpp-template-source.yml',
                  'utils/ci/cpp_diagnostic_gtest.py']
        report['source_sha256'] = {name: sha(root / name) for name in inputs}
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

        # Run the original registered command, including BOTH generators and
        # real C/C++ compilation. A separate real CTest emits the actual XML.
        ctest = output / 'sdk-ctest'
        ctest.mkdir()
        registration = '''cmake_minimum_required(VERSION 3.20)
project(NeverCOriginalSDKConsistency NONE)
enable_testing()
add_test(NAME PluginSDKGenerationConsistency COMMAND "@PYTHON@" "@ROOT@/utils/plugin-api/check-single-header.py")
'''.replace('@PYTHON@', sys.executable).replace('@ROOT@', str(root))
        (ctest / 'CMakeLists.txt').write_text(registration)
        assert run(['cmake', '-S', ctest, '-B', ctest / 'build', '-G', 'Ninja'], 'sdk/configure') == 0
        sdk_status = run(['ctest', '--test-dir', ctest / 'build', '--output-on-failure',
                          '--output-junit', str(output / 'sdk-consistency.xml')], 'sdk/original-consistency', 180)
        report['sdk_ctest_returncode'] = sdk_status
        # Independently retain actual object files rather than relying solely
        # on the original script's disposable compile outputs.
        probe = output / 'sdk-probe.c'
        probe.write_text('#include "neverc/Plugin/NevercPluginAPI.h"\nint probe(void){return (int)sizeof(NevercABITableHeader);}\n')
        report['sdk_smoke_objects'] = []
        for language, standard in [('c', 'c11'), ('c', 'c23'), ('c++', 'c++17')]:
            target = output / ('sdk-' + standard + '.o')
            assert run([clangxx, '-x', language, '-std=' + standard,
                        '-I' + str(root / 'pluginsdk/include'), '-c', probe, '-o', target],
                       'sdk/compile-' + standard) == 0
            report['sdk_smoke_objects'].append(binary(target))

        sources = [root / 'tests/neverc' / name for name in [
            'NeverCTestFixture.cpp', 'LinkerTests.cpp', 'PluginSDKGenerationTests.cpp',
            'PluginSDKGenerationSingleHeader.cpp']]
        tests, owner = build_gtest(
            root, output, clangxx, compiler, sources,
            ['LLVMObject', 'LLVMSupport'], 'linker-sdk-tests', run,
            'build/configure-owning-libraries', 1800,
            definitions={'NEVERC_PLUGINSDK_SINGLE_HEADER':
                         root / 'pluginsdk/include/neverc/Plugin/NevercPluginAPI.h'})
        report['cmake_gtest_owner'] = owner
        report['gtest_binary'] = binary(tests)
        assert run([tests, '--gtest_list_tests'], 'gtest/list') == 0
        listed, suite = [], None
        for line in (output / 'gtest/list.log').read_text().splitlines():
            if line and not line.startswith(' ') and line.endswith('.'):
                suite = line.strip()
            elif line.startswith('  ') and suite:
                listed.append(suite + line.strip())
        expected = ['LinkerTest.' + name for name in plan['test_names']]
        expected.append('PluginSDKGenerationTest.SingleHeaderMatchesModularHeaders')
        assert sorted(listed) == sorted(expected)
        xml = output / 'gtest/linker-sdk.xml'
        status = run([tests, '--gtest_output=xml:' + str(xml)], 'gtest/execute', 1200)
        cases = ET.parse(xml).getroot().findall('.//testcase')
        assert sorted(c.attrib['classname'] + '.' + c.attrib['name'] for c in cases) == sorted(expected)
        failures = [c.attrib['classname'] + '.' + c.attrib['name'] for c in cases if c.find('failure') is not None]
        skipped = [c.attrib['classname'] + '.' + c.attrib['name'] for c in cases if c.find('skipped') is not None]
        executed = [c.attrib['classname'] + '.' + c.attrib['name'] for c in cases if c.find('skipped') is None]
        report['xml'] = [{'path': str(xml.relative_to(output)), 'sha256': sha(xml),
                          'registered': expected, 'executed': executed,
                          'skipped': skipped, 'failed': failures}]
        report['gtest_process_returncode'] = status
        sdk_xml = output / 'sdk-consistency.xml'
        sdk_cases = ET.parse(sdk_xml).getroot().findall('.//testcase')
        assert len(sdk_cases) == 1 and sdk_cases[0].attrib['name'] == 'PluginSDKGenerationConsistency'
        report['sdk_ctest_xml'] = {'path': sdk_xml.name, 'sha256': sha(sdk_xml)}
        report['completed'] = True
        report['passed'] = sdk_status == 0 and status == 0 and not failures
    except Exception:
        report['error'] = traceback.format_exc()
        save()
        raise
    finally:
        save()
    print('Real linker/SDK replay passed:', report['passed'], '; full CI remains separate.', flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
