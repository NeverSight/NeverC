#!/usr/bin/env python3
"""Capture exact ARM64 network compilation failure without claiming a fix."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time
import traceback


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root, output = args.repo.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {'completed': False, 'evidence_captured': False, 'compiler_crash_fixed': False,
              'full_ci_passed': False, 'goal_complete': False, 'compiler_rebuilt': False,
              'commands': [], 'experiments': []}

    def save():
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(command, name):
        command = list(map(str, command))
        log = output / (name + '.log')
        started = time.time_ns()
        with log.open('w') as stream:
            try:
                status = subprocess.run(command, cwd=root, stdout=stream,
                                        stderr=subprocess.STDOUT, timeout=60).returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                status = -1000
        report['commands'].append({'command': command, 'returncode': status,
                                   'log': log.name, 'log_sha256': sha(log),
                                   'timeout_seconds': 60, 'elapsed_ns': time.time_ns() - started})
        save()
        print(name, status, flush=True)
        return status

    try:
        assert platform.system() == 'Windows' and platform.machine().lower() in ('arm64', 'aarch64')
        report['host'] = {'platform': platform.platform(), 'machine': platform.machine(),
                          'processors': os.cpu_count(), 'python': platform.python_version()}
        assert report['host']['python'] == '3.12.10'
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        provenance = json.loads(args.baseline.read_text())
        assert provenance['revision'] == '89574033312b3bf5946cecf4a3d59903792c99d5'
        assert provenance['run_id'] == 39304339088 and provenance['artifact_id'] == 12224282690
        assert provenance['artifact_sha256'] == '0d28178df7b5731e38f726db279d2367ef2669e83b32e7978253b0219bcdbb3e'
        compiler = Path(provenance['compiler'])
        assert sha(compiler) == provenance['compiler_sha256']
        report['baseline_provenance'] = provenance
        files = ['utils/ci/check-windows-arm-network-crash.py',
                 'utils/ci/download-windows-arm-network-baseline.py',
                 '.github/workflows/diagnose-cpp-template-source.yml',
                 'utils/ci/build-network-product-examples.sh',
                 'examples/network-authoritative-server/main.c',
                 'examples/network-anticheat-collector/main.c',
                 'llvm/lib/CodeGen/SelectionDAG/LegalizeIntegerTypes.cpp',
                 'neverc/lib/Emit/Backend/ParallelCodeGenMerge.cpp']
        report['source_sha256'] = {name: sha(root / name) for name in files}
        report['strict_pcg_environment'] = os.environ.get('NEVERC_PCG_STRICT')
        assert report['strict_pcg_environment'] == '1'
        assert run([compiler, '--version'], 'compiler-version') == 0
        common = ['--target=aarch64-pc-windows-msvc', '-I' + str(root / 'std/include'),
                  '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                  '-Wl,--nodefaultlib=runtimeobject.lib']
        report['original_common_flags'] = common
        variants = [('server-exact', 'network-authoritative-server', []),
                    ('collector-exact-0', 'network-anticheat-collector', []),
                    ('collector-exact-1', 'network-anticheat-collector', []),
                    ('collector-exact-2', 'network-anticheat-collector', []),
                    ('collector-serial', 'network-anticheat-collector', ['-fno-parallel-codegen']),
                    ('collector-single-worker', 'network-anticheat-collector', ['-fparallel-codegen=1']),
                    ('collector-no-lto', 'network-anticheat-collector', ['-fno-lto']),
                    ('collector-no-heap', 'network-anticheat-collector', ['-fno-builtin-mimalloc'])]
        for name, example, extra in variants:
            target = output / (name + '.exe')
            source = root / 'examples' / example / 'main.c'
            status = run([compiler, *common, *extra, '-o', target, source], name)
            log = (output / (name + '.log')).read_text(errors='replace')
            record = {'name': name, 'source': str(source.relative_to(root)),
                      'extra_flags': extra, 'returncode': status,
                      'matching_legalizer_stack': 'ExpandIntRes_ADDSUB' in log,
                      'has_stack_frames': '#0 ' in log,
                      'artifact_created': target.is_file()}
            if target.is_file():
                data = target.read_bytes()
                assert data[:2] == b'MZ'
                offset = int.from_bytes(data[60:64], 'little')
                assert data[offset:offset+6] == b'PE\0\0\x64\xaa'
                record['artifact_sha256'] = sha(target)
            report['experiments'].append(record)
            save()
        # Preserve optimized IR independently. The linker-only flag is omitted
        # for this compile-only diagnostic; it is retained for every replay.
        ir = output / 'collector-optimized.ll'
        status = run([compiler, *[f for f in common if not f.startswith('-Wl,')],
                      '-S', '-emit-llvm', '-o', ir,
                      root / 'examples/network-anticheat-collector/main.c'], 'collector-ir')
        report['ir'] = {'returncode': status, 'created': ir.is_file(),
                        'sha256': sha(ir) if ir.is_file() else None}
        candidates = [Path(base) / 'Windows Kits/10/Debuggers/arm64/cdb.exe'
                      for base in [os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)'),
                                   os.environ.get('ProgramFiles', 'C:/Program Files')]]
        debugger = next((p for p in candidates if p.is_file()), None)
        report['debugger'] = str(debugger) if debugger else None
        report['pdb_available'] = list(map(str, compiler.parent.glob('*.pdb')))
        dumps = list((Path(os.environ['RUNNER_TEMP']) / 'windows-arm-network-dumps').glob('*.dmp'))
        report['dumps'] = [{'name': path.name, 'sha256': sha(path), 'size': path.stat().st_size} for path in dumps]
        if debugger:
            for index, dump in enumerate(dumps):
                run([debugger, '-z', dump, '-y', compiler.parent,
                     '-c', '.reload /f; .exr -1; .ecxr; kP 80; ~*kb 40; r; lmvm neverc; q'],
                    'dump-' + str(index))
        report['matching_failure_reproduced'] = any(
            row['name'].startswith('collector-exact-') and row['returncode'] != 0
            and row['matching_legalizer_stack'] for row in report['experiments'])
        report['completed'] = report['evidence_captured'] = True
    except Exception:
        report['error'] = traceback.format_exc()
        save()
        raise
    finally:
        save()
    print('Captured exact ARM64 codegen diagnosis; reproduced:', report['matching_failure_reproduced'],
          '; compiler_crash_fixed:false.', flush=True)


if __name__ == '__main__':
    main()
