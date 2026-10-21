#!/usr/bin/env python3
"""Verify preserved stored-operator positives and native-valid source boundaries."""
from pathlib import Path
import argparse
import hashlib
import json
import platform
import subprocess
import traceback


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
              'test_binary_executed': False, 'private_frontend_changed': False,
              'new_cpp_support_implemented': False, 'goal_complete': False,
              'commands': [], 'cases': []}

    def save():
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(command, log, timeout=60):
        argv = list(map(str, command))
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open('w') as stream:
            try:
                code = subprocess.run(argv, cwd=root, stdout=stream,
                                      stderr=subprocess.STDOUT, timeout=timeout).returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1000
        report['commands'].append({'command': argv, 'returncode': code,
            'log': str(log.relative_to(output)), 'log_sha256': sha(log),
            'timeout_seconds': timeout})
        save()
        return code

    def binary(path):
        data = path.read_bytes()
        assert data[:6] == b'\x7fELF\x02\x01' and data[18:20] == b'\x3e\x00'
        return {'path': str(path.relative_to(output)), 'sha256': sha(path), 'size': len(data)}

    try:
        assert platform.system() == 'Linux' and platform.machine() == 'x86_64'
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        report['runner_os_release'] = Path('/etc/os-release').read_text()
        assert 'VERSION_ID="22.04"' in report['runner_os_release']
        names = ['utils/ci/check-cpp-stored-wrapper-controls.py',
                 'utils/ci/download-cpp-stored-wrapper-controls.py',
                 'utils/ci/cpp-stored-wrapper-controls-cases.json',
                 '.github/workflows/diagnose-cpp-template-source.yml',
                 'tests/neverc/TranslateTests.cpp']
        report['source_sha256'] = {name: sha(root / name) for name in names}
        plan = json.loads((root / names[2]).read_text())
        assert report['source_sha256'][names[-1]] == plan['corrected_tests_sha256']
        assert len(plan['promoted']) == 30 and len(plan['guards']) == 15
        cases = plan['promoted'] + plan['guards']
        assert len(cases) == len({c['name'] for c in cases}) == 45
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        report['sdk_catalog_sha256'] = sha(sdk / 'catalog.json')
        assert report['sdk_catalog_sha256'] == plan['sdk_catalog_sha256'] == '1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        catalog = json.loads((sdk / 'catalog.json').read_text())
        assert len(catalog['headers']) == 664 and len(catalog['metadata']) == 1
        for entry in catalog['headers'] + catalog['metadata']:
            assert sha(sdk / entry['root'] / entry['path']) == entry['sha256']
        report['sdk_files_verified'] = 665
        provenance = json.loads(args.baseline.read_text())
        assert provenance['revision'] == plan['baseline_revision'] == '1617f18ead74a8fa719e3d1bd02f9c84a10de551'
        assert provenance['run_id'] == 39244936536 and provenance['artifact_id'] == 12195306550
        assert provenance['artifact_sha256'] == '9332fad0d0226257f109ae74c1655a4e35510783d1029dd2bc8bdc15bfaf5206'
        compiler = Path(provenance['compiler'])
        assert sha(compiler) == provenance['compiler_sha256']
        report['baseline_provenance'] = provenance
        clangxx = args.clangxx.absolute()
        assert run([clangxx, '--version'], output / 'clang-version.log') == 0
        assert 'clang version 20.1.8' in (output / 'clang-version.log').read_text()
        report['native_compiler_sha256'] = sha(clangxx)
        strict = ['-fno-fast-math', '-ffp-contract=off']
        flags = [clangxx, '--no-default-config', '-std=c++17', '-stdlib=libc++',
                 '-nostdinc++', '-resource-dir', sdk / 'resource', '-isystem', sdk / 'libcxx',
                 *strict, '-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES', '-D_NEVERC_CPP_CORE_V2_SDK',
                 '-D_LIBCPP___ITERATOR_ISTREAM_ITERATOR_H', '-D_LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H',
                 '-D_LIBCPP___ITERATOR_OSTREAM_ITERATOR_H', '-D_LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H',
                 '-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS']
        for case in cases:
            directory = output / case['category'] / case['name']
            directory.mkdir(parents=True)
            source = directory / (case['name'] + '.cpp')
            source.write_text(case['source'])
            assert sha(source) == case['source_sha256']
            row = {'name': case['name'], 'category': case['category'], 'test': case['test'],
                   'source': str(source.relative_to(output)), 'source_sha256': sha(source),
                   'native': [], 'translated': []}
            # A guard must first compile and execute with the real pinned SDK.
            for mode in ['-O0', '-O2']:
                path = directory / ('native' + mode)
                built = run(flags + [mode, source, '-o', path], directory / (mode + '-native-build.log'))
                executed = run([path], directory / (mode + '-native-run.log'), 30) if built == 0 else None
                entry = {'mode': mode, 'build_returncode': built, 'run_returncode': executed,
                         'passed': built == executed == 0}
                if built == 0:
                    entry['executable'] = binary(path)
                row['native'].append(entry)
            generated = directory / (case['name'] + '.nc')
            log = directory / 'translate.log'
            code = run([compiler, 'translate', '--from', 'cpp', source, '--profile', 'cpp-core-v2',
                        '--target', 'x86_64-unknown-linux-gnu', '-o', generated, '--', '-std=c++17'], log)
            row['translate_returncode'] = code
            artifacts = sorted(directory.glob(generated.name + '*'))
            if case['category'] == 'guard':
                text = log.read_text()
                row['bounded_refusal'] = (code > 0 and ('TR0201' in text or 'TR0203' in text)
                    and not any(marker in text for marker in ['TR0101', 'TR0102', 'TR0202', 'DIAGNOSTIC COMMAND TIMED OUT'])
                    and not artifacts)
                row['artifacts_absent'] = not artifacts
                row['passed'] = all(n['passed'] for n in row['native']) and row['bounded_refusal']
            else:
                if code == 0:
                    row['generated_sha256'] = {a.name: sha(a) for a in artifacts}
                    for mode in ['-O0', '-O2']:
                        path = directory / ('translated' + mode)
                        built = run([compiler, generated, mode, '-fno-lto', '-fno-builtin-mimalloc',
                                     '-o', path, *strict], directory / (mode + '-translated-build.log'))
                        executed = run([path], directory / (mode + '-translated-run.log'), 30) if built == 0 else None
                        entry = {'mode': mode, 'build_returncode': built, 'run_returncode': executed,
                                 'passed': built == executed == 0}
                        if built == 0:
                            entry['executable'] = binary(path)
                        row['translated'].append(entry)
                row['passed'] = (all(n['passed'] for n in row['native']) and code == 0
                    and len(row['translated']) == 2 and all(t['passed'] for t in row['translated']))
            report['cases'].append(row)
            save()
            print(('PASS ' if row['passed'] else 'FAIL ') + case['category'] + ' ' + case['name'], flush=True)
        assert sha(compiler) == provenance['compiler_sha256']
        assert sha(clangxx) == report['native_compiler_sha256']
        assert all(sha(root / name) == digest for name, digest in report['source_sha256'].items())
        report['native_executions'] = sum(n['passed'] for c in report['cases'] for n in c['native'])
        report['translated_executions'] = sum(t['passed'] for c in report['cases'] for t in c['translated'])
        report['source_guard_refusals'] = sum(c.get('bounded_refusal', False) for c in report['cases'])
        report['completed'] = True
        report['passed'] = (len(report['cases']) == 45 and all(c['passed'] for c in report['cases'])
            and report['native_executions'] == 90 and report['translated_executions'] == 60
            and report['source_guard_refusals'] == 15)
        save()
        assert report['passed'], 'Inspect exact native/managed source and logs before integrating test corrections.'
        print('PASS 30 preserved/enhanced positives and 15 native-valid source guards; full test binary and full CI remain unverified.', flush=True)
    except BaseException:
        report['failure'] = traceback.format_exc()
        save()
        raise


if __name__ == '__main__':
    main()
