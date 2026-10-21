#!/usr/bin/env python3
"""Diagnose exact Linux baseline failures using an authenticated existing build."""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import subprocess
import time
import traceback
from pathlib import Path


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--clangxx', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.repo.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {'passed': False, 'completed': False, 'full_ci_passed': False,
              'host_bug_fixed': False, 'compiler_rebuilt': False,
              'formal_test_binary_executed': False, 'goal_complete': False,
              'commands': [], 'math_cases': [], 'missing_math_links': [],
              'host_experiments': []}

    def save():
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(command, log, timeout=60):
        command = list(map(str, command))
        log.parent.mkdir(parents=True, exist_ok=True)
        started = time.time_ns()
        with log.open('w') as stream:
            try:
                code = subprocess.run(command, cwd=root, stdout=stream,
                                      stderr=subprocess.STDOUT,
                                      timeout=timeout).returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1000
        entry = {'command': command, 'returncode': code,
                 'log': str(log.relative_to(output)), 'log_sha256': sha(log),
                 'timeout_seconds': timeout, 'elapsed_ns': time.time_ns() - started}
        report['commands'].append(entry)
        save()
        return code

    def executable(path):
        data = path.read_bytes()
        assert data[:6] == b'\x7fELF\x02\x01' and data[18:20] == b'\x3e\x00'
        return {'path': str(path.relative_to(output)), 'sha256': sha(path),
                'size': len(data), 'format': 'ELF64-x86_64'}

    try:
        assert platform.system() == 'Linux' and platform.machine() == 'x86_64'
        report['revision'] = subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        report['runner_os_release'] = Path('/etc/os-release').read_text()
        assert 'VERSION_ID="22.04"' in report['runner_os_release']
        assert run(['uname', '-a'], output / 'environment/uname.log') == 0
        assert run(['getconf', 'GNU_LIBC_VERSION'], output / 'environment/libc.log') == 0
        provenance = json.loads(args.baseline.read_text())
        assert provenance['revision'] == '1617f18ead74a8fa719e3d1bd02f9c84a10de551'
        assert provenance['run_id'] == 39244936536
        assert provenance['artifact_id'] == 12195306550
        assert provenance['artifact_sha256'] == '9332fad0d0226257f109ae74c1655a4e35510783d1029dd2bc8bdc15bfaf5206'
        compiler = Path(provenance['compiler'])
        assert sha(compiler) == provenance['compiler_sha256']
        report['baseline_provenance'] = provenance
        clangxx = args.clangxx.absolute()
        assert run([clangxx, '--version'], output / 'environment/clang-version.log') == 0
        assert 'clang version 20.1.8' in (output / 'environment/clang-version.log').read_text()
        report['native_compiler_sha256'] = sha(clangxx)
        inputs = ['utils/ci/check-cpp-linux-baseline.py',
                  'utils/ci/download-cpp-linux-baseline.py',
                  'utils/ci/cpp-linux-baseline-cases.json',
                  '.github/workflows/diagnose-cpp-template-source.yml',
                  'tests/neverc/TranslateTests.cpp']
        report['source_sha256'] = {name: sha(root / name) for name in inputs}
        plan = json.loads((root / inputs[2]).read_text())
        assert plan['baseline_revision'] == provenance['revision']
        assert plan['corrected_tests_sha256'] == report['source_sha256'][inputs[-1]]
        report['boundary_source_sha256'] = plan['boundary_source_sha256']
        assert all(sha(root / name) == value for name, value in report['boundary_source_sha256'].items())
        suites = plan['suites']
        assert len(suites) == 16
        cases = [case for suite in suites for case in suite['cases']]
        assert len(cases) == len({case['name'] for case in cases}) == 63
        report['fixture_sha256'] = {case['source']: case['source_sha256'] for case in cases}
        assert all(sha(root / name) == value for name, value in report['fixture_sha256'].items())
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        report['sdk_catalog_sha256'] = sha(sdk / 'catalog.json')
        assert report['sdk_catalog_sha256'] == '1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        catalog = json.loads((sdk / 'catalog.json').read_text())
        assert len(catalog['headers']) == 664 and len(catalog['metadata']) == 1
        for entry in catalog['headers'] + catalog['metadata']:
            assert sha(sdk / entry['root'] / entry['path']) == entry['sha256']
        report['sdk_files_verified'] = 665
        strict = ['-fno-fast-math', '-ffp-contract=off']
        native_flags = [clangxx, '--no-default-config', '-std=c++17', '-stdlib=libc++',
                        '-nostdinc++', '-resource-dir', sdk / 'resource',
                        '-isystem', sdk / 'libcxx', *strict,
                        '-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES', '-D_NEVERC_CPP_CORE_V2_SDK',
                        '-D_LIBCPP___ITERATOR_ISTREAM_ITERATOR_H',
                        '-D_LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H',
                        '-D_LIBCPP___ITERATOR_OSTREAM_ITERATOR_H',
                        '-D_LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H',
                        '-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS']

        # Preserve the originally failing program and flags before experimenting.
        host = root / 'tests/neverc/standards/test_c_stdlib_patterns.c'
        original = output / 'host/exact'
        original.parent.mkdir()
        assert run([compiler, '-std=c11', '-fno-lto', '-c', host, '-o',
                    original.with_suffix('.o')], output / 'host/exact-object.log') == 0
        variants = [('exact', []), ('heap-opt-out', ['-fno-builtin-mimalloc']),
                    ('debug-O0', ['-g', '-O0']), ('debug-O2', ['-g', '-O2'])]
        for name, extra in variants:
            path = output / 'host' / name
            build = run([compiler, '-std=c11', '-fno-lto', *extra, host, '-o', path],
                        output / 'host' / (name + '-build.log'))
            row = {'name': name, 'extra_flags': extra, 'build_returncode': build,
                   'executions': []}
            if build == 0:
                row['executable'] = executable(path)
                for index in range(5 if name in ('exact', 'heap-opt-out') else 1):
                    log = output / 'host' / (name + '-run-' + str(index) + '.log')
                    code = run([path], log, 30)
                    row['executions'].append({'returncode': code,
                        'all_passed_message': 'test_c_stdlib_patterns: ALL PASSED' in log.read_text()})
                debugger = output / 'host' / (name + '-gdb.log')
                row['debugger_returncode'] = run([
                    'gdb', '-batch', '-ex', 'set pagination off',
                    '-ex', 'set disable-randomization off', '-ex', 'run',
                    '-ex', 'thread apply all bt full', '-ex', 'info registers',
                    '-ex', 'x/16i $pc-16', '-ex', 'info sharedlibrary', str(path)], debugger)
                row['debugger_observed_sigsegv'] = 'SIGSEGV' in debugger.read_text()
                row['debugger_log_sha256'] = sha(debugger)
            report['host_experiments'].append(row)
            save()
        native_host = output / 'host/native-clang'
        native_build = run([clangxx, '--no-default-config', '-x', 'c', '-std=c11',
                            '-fno-lto', host, '-o', native_host], output / 'host/native-build.log')
        native_run = run([native_host], output / 'host/native-run.log', 30) if native_build == 0 else None
        report['native_host'] = {'build_returncode': native_build, 'run_returncode': native_run}

        for suite in suites:
            for index, case in enumerate(suite['cases']):
                directory = output / 'math' / case['name']
                directory.mkdir(parents=True)
                source = root / case['source']
                generated = directory / (case['name'] + '.nc')
                row = {'name': case['name'], 'suite': suite['test'],
                       'source': case['source'], 'source_sha256': sha(source), 'modes': []}
                code = run([compiler, 'translate', '--from', 'cpp', source,
                            '--profile', 'cpp-core-v2', '--target', 'x86_64-unknown-linux-gnu',
                            '-o', generated, '--', '-std=c++17'], directory / 'translate.log')
                row['translate_returncode'] = code
                if code == 0:
                    row['generated_sha256'] = {p.name: sha(p) for p in directory.glob(generated.name + '*')}
                    # One exact original link attempt per failing suite, at O0.
                    if index == 0:
                        log = directory / 'former-missing-libm.log'
                        before = run([compiler, generated, '-O0', '-fno-lto',
                                      '-fno-builtin-mimalloc', '-o', directory / 'former'], log)
                        symbols = sorted(set(re.findall(r'undefined symbol: ([^\s]+)', log.read_text())))
                        matches = set(suite['actual_baseline_undefined_symbols']).issubset(symbols)
                        report['missing_math_links'].append({'suite': suite['test'],
                            'returncode': before, 'symbols': symbols,
                            'actual_failure_reproduced': before > 0 and matches,
                            'log_sha256': sha(log)})
                    for mode in ['-O0', '-O2']:
                        native = directory / ('native' + mode)
                        translated = directory / ('translated' + mode)
                        native_build = run(native_flags + [mode, source, '-o', native],
                                           directory / (mode + '-native-build.log'))
                        native_run = run([native], directory / (mode + '-native-run.log'), 30) if native_build == 0 else None
                        # This exactly mirrors compileGeneratedWithMath on Linux.
                        build = run([compiler, generated, mode, '-fno-lto',
                                     '-fno-builtin-mimalloc', '-o', translated, *strict, '-lm'],
                                    directory / (mode + '-translated-build.log'))
                        execution = run([translated], directory / (mode + '-translated-run.log'), 30) if build == 0 else None
                        result = {'mode': mode, 'native_build_returncode': native_build,
                                  'native_run_returncode': native_run, 'build_returncode': build,
                                  'run_returncode': execution,
                                  'passed': native_build == native_run == build == execution == 0}
                        if native_build == 0:
                            result['native_executable'] = executable(native)
                        if build == 0:
                            result['translated_executable'] = executable(translated)
                        row['modes'].append(result)
                row['passed'] = code == 0 and len(row['modes']) == 2 and all(m['passed'] for m in row['modes'])
                report['math_cases'].append(row)
                save()
                print(('PASS ' if row['passed'] else 'FAIL ') + case['name'], flush=True)

        assert sha(compiler) == provenance['compiler_sha256']
        assert sha(clangxx) == report['native_compiler_sha256']
        for mapping in ['source_sha256', 'fixture_sha256', 'boundary_source_sha256']:
            assert all(sha(root / name) == value for name, value in report[mapping].items())
        report['host_crash_observed'] = any(
            c['returncode'] in (-11, 139) for row in report['host_experiments']
            for c in row['executions'])
        report['host_diagnosis_completed'] = (len(report['host_experiments']) == 4 and
            all(row['build_returncode'] == 0 and 'debugger_log_sha256' in row and
                row['debugger_returncode'] in (0, 1) and
                len(row['executions']) == (5 if row['name'] in ('exact', 'heap-opt-out') else 1) and
                all(e['returncode'] != -1000 for e in row['executions'])
                for row in report['host_experiments'])
            and report['native_host']['build_returncode'] == report['native_host']['run_returncode'] == 0)
        report['math_harness_verified'] = (len(report['math_cases']) == 63 and
            all(c['passed'] for c in report['math_cases']) and len(report['missing_math_links']) == 16
            and all(c['actual_failure_reproduced'] for c in report['missing_math_links']))
        report['native_math_executions'] = sum(m['native_run_returncode'] == 0 for c in report['math_cases'] for m in c['modes'])
        report['translated_math_executions'] = sum(m['run_returncode'] == 0 for c in report['math_cases'] for m in c['modes'])
        report['completed'] = True
        report['passed'] = report['math_harness_verified'] and report['host_diagnosis_completed']
        save()
        assert report['passed'], 'Inspect the complete retained baseline experiment logs.'
        print('PASS focused math harness proof and crash evidence collection; complete CI and host crash fix remain unverified.', flush=True)
    except BaseException:
        report['failure'] = traceback.format_exc()
        save()
        raise


if __name__ == '__main__':
    main()
