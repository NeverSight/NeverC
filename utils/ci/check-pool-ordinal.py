#!/usr/bin/env python3
"""Compare the exact old Pool header with scoped worker ordinals on Actions."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import traceback


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root, output = args.repo.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {'passed': False, 'completed': False, 'compiler_rebuilt': False,
              'full_ci_passed': False, 'host_crash_fixed': False,
              'host_crash_causal_link_established': False, 'goal_complete': False,
              'commands': [], 'cases': []}

    def save():
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(command, name):
        command = list(map(str, command))
        log = output / (name + '.log')
        started = time.time_ns()
        with log.open('w') as stream:
            try:
                code = subprocess.run(command, cwd=root, stdout=stream,
                                      stderr=subprocess.STDOUT, timeout=60).returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1000
        report['commands'].append({'command': command, 'returncode': code,
                                   'timeout_seconds': 60, 'log': log.name,
                                   'log_sha256': sha(log), 'elapsed_ns': time.time_ns() - started})
        save()
        return code

    try:
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        files = ['neverc/lib/Linker/Backends/ELF/FastPath/FastLinkSupport.h',
                 'utils/ci/pool-ordinal-original/FastLinkSupport.h',
                 'utils/ci/pool-ordinal-probe.cpp', 'utils/ci/check-pool-ordinal.py',
                 '.github/workflows/diagnose-cpp-template-source.yml']
        report['source_sha256'] = {name: sha(root / name) for name in files}
        assert report['source_sha256'][files[1]] == 'ee5f70bb7df32e01de2aeb4c7a987e9cf8ce94e304816476887014e2dce45135'
        assert run(['/usr/bin/clang++-22', '--version'], 'compiler-version') == 0
        assert 'clang version 22.' in (output / 'compiler-version.log').read_text()
        expected = ['serial-run', 'serial-for-each', 'small-for-each', 'parallel-run',
                    'parallel-for-each', 'serial-throw', 'small-throw', 'parallel-throw', 'zero-for-each']
        for variant, include in [('original', root / files[1]), ('scoped', root / files[0])]:
            for mode in ['-O0', '-O2']:
                tag = variant + mode
                binary = output / tag
                assert run(['/usr/bin/clang++-22', '-std=c++17', '-pthread', mode,
                            '-I' + str(include.parent), root / files[2], '-o', binary], tag + '-build') == 0
                data = binary.read_bytes()
                assert data[:6] == b'\x7fELF\x02\x01' and data[18:20] == b'\x3e\x00'
                row = {'variant': variant, 'mode': mode, 'executable': binary.name,
                       'executable_sha256': sha(binary), 'runs': []}
                for index in range(3):
                    name = tag + '-run-' + str(index)
                    command = [binary] + (['--expect-original'] if variant == 'original' else [])
                    status = run(command, name)
                    rows = []
                    for line in (output / (name + '.log')).read_text().splitlines():
                        parts = line.split()
                        assert len(parts) == 6
                        rows.append({'name': parts[0], **dict(zip(
                            ['visits', 'wrong', 'bounds', 'restores', 'errors'], map(int, parts[1:])))})
                    assert [r['name'] for r in rows] == expected
                    assert all(r['restores'] == r['errors'] == 0 for r in rows)
                    if variant == 'original':
                        assert all(r['wrong'] >= 8 and r['bounds'] >= 8
                                   for r in rows if r['name'] not in ['parallel-for-each', 'zero-for-each'])
                    else:
                        assert all(r['wrong'] == r['bounds'] == 0 for r in rows)
                    assert status == 0
                    row['runs'].append({'log': name + '.log', 'observations': rows})
                report['cases'].append(row)
                save()
        report['completed'] = report['passed'] = True
    except Exception:
        report['error'] = traceback.format_exc()
        save()
        raise
    finally:
        save()
    print('PASS actual Pool old-versus-scoped ordinals: four native builds, twelve runs; host crash remains unexplained.')


if __name__ == '__main__':
    main()
