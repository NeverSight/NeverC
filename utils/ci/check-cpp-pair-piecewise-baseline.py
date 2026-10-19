#!/usr/bin/env python3
"""Record native-only pair piecewise behavior and actual selected Clang ASTs."""
from __future__ import annotations
import argparse
import hashlib
import json
import platform
import subprocess
import traceback
from pathlib import Path


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--clangxx', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.repo.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {'passed': False, 'completed': False, 'native_only': True,
              'managed_behavior_verified': False, 'goal_complete': False,
              'commands': [], 'cases': []}
    def save():
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    def run(command, log, timeout=120):
        command = list(map(str, command))
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open('w') as stream:
            try:
                result = subprocess.run(command, cwd=root, stdout=stream,
                                        stderr=subprocess.STDOUT, timeout=timeout)
                code = result.returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1
        report['commands'].append({'command': command, 'returncode': code,
                                  'log': str(log), 'log_sha256': digest(log)})
        save()
        return code
    try:
        assert platform.system() == 'Linux' and platform.machine() == 'x86_64'
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        report['sdk_catalog_sha256'] = digest(sdk / 'catalog.json')
        assert report['sdk_catalog_sha256'] == '1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        catalog = json.loads((sdk / 'catalog.json').read_text())
        assert len(catalog['headers']) == 664 and len(catalog['metadata']) == 1
        for entry in catalog['headers'] + catalog['metadata']:
            assert digest(sdk / entry['root'] / entry['path']) == entry['sha256']
        clangxx = args.clangxx.absolute()
        assert run([clangxx, '--version'], output / 'clang-version.log') == 0
        assert 'clang version 20.1.8' in (output / 'clang-version.log').read_text()
        report['compiler_sha256'] = digest(clangxx)
        inputs = ['utils/ci/check-cpp-pair-piecewise-baseline.py',
                  'utils/ci/cpp-pair-piecewise-baseline-cases.json',
                  'utils/translate-frontends/cpp/diagnostics/pair-piecewise.cpp',
                  '.github/workflows/diagnose-cpp-template-source.yml']
        report['source_sha256'] = {name: digest(root / name) for name in inputs}
        flags = [clangxx, '--no-default-config', '-std=c++17', '-stdlib=libc++',
                 '-nostdinc++', '-resource-dir', sdk / 'resource',
                 '-isystem', sdk / 'libcxx', '-fno-fast-math', '-ffp-contract=off',
                 '-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES', '-D_NEVERC_CPP_CORE_V2_SDK',
                 '-D_LIBCPP___ITERATOR_ISTREAM_ITERATOR_H', '-D_LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H',
                 '-D_LIBCPP___ITERATOR_OSTREAM_ITERATOR_H', '-D_LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H',
                 '-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS']
        plan = json.loads((root / 'utils/ci/cpp-pair-piecewise-baseline-cases.json').read_text())
        cases = plan['native_cases']
        assert plan['runtime_cases'] == 14 and plan['guard_cases'] == 10
        assert len(cases) == len({c['name'] for c in cases}) == 24
        for case in cases:
            directory = output / case['category'] / case['name']
            directory.mkdir(parents=True)
            source = directory / (case['name'] + '.cpp')
            source.write_text(case['source'])
            assert digest(source) == case['source_sha256']
            modes = []
            for mode in ['-O0', '-O2']:
                executable = directory / ('native' + mode)
                build = run(flags + [mode, source, '-o', executable], directory / (mode + '-build.log'))
                execution = run([executable], directory / (mode + '-run.log'), 30) if build == 0 else None
                modes.append({'mode': mode, 'build_returncode': build, 'run_returncode': execution,
                              'passed': build == 0 and execution == 0})
            report['cases'].append({'name': case['name'], 'category': case['category'],
                                    'source': str(source), 'source_sha256': digest(source),
                                    'modes': modes, 'passed': all(m['passed'] for m in modes)})
            save()
            print(('PASS' if report['cases'][-1]['passed'] else 'FAIL') + ' native ' + case['name'], flush=True)
        probe = output / 'pair-piecewise-ast'
        probe_source = root / 'utils/translate-frontends/cpp/diagnostics/pair-piecewise.cpp'
        built = run([clangxx, '-std=c++17', '-I/usr/lib/llvm-20/include', probe_source,
                     '-L/usr/lib/llvm-20/lib', '-lclang-cpp', '-lLLVM-20', '-o', probe],
                    output / 'ast-probe-build.log') == 0
        asts = []
        if built:
            for case in report['cases']:
                if case['category'] != 'runtime':
                    continue
                log = Path(case['source']).with_suffix('.ast.log')
                code = run([probe, root, case['source']], log, 60)
                text = log.read_text()
                passed = code == 0 and 'PAIR-PIECEWISE-PUBLIC' in text and 'PAIR-PIECEWISE-PRIVATE' in text
                asts.append({'name': case['name'], 'returncode': code, 'log': str(log),
                             'log_sha256': digest(log), 'passed': passed})
        report['asts'] = asts
        report['native_executions'] = sum(m['passed'] for c in report['cases'] for m in c['modes'])
        report['completed'] = True
        report['passed'] = (all(c['passed'] for c in report['cases']) and len(asts) == 14 and
                            all(a['passed'] for a in asts))
        assert all(digest(root / name) == value for name, value in report['source_sha256'].items())
        assert digest(clangxx) == report['compiler_sha256']
        save()
        assert report['passed'], 'Native fixture or AST diagnostic failed; inspect retained logs.'
        print('PASS native-only pair piecewise baseline: 24 fixtures/48 runs, 14 selected ASTs; managed support unverified.', flush=True)
    except BaseException:
        report['failure'] = traceback.format_exc()
        save()
        raise


if __name__ == '__main__':
    main()
