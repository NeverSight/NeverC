"""Check portable test oracles and retain the real remaining ARM compiler failures."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import time
import traceback


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser()
    for name in ['repo', 'plan', 'baseline', 'output', 'build']:
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    root, out, build = args.repo.resolve(), args.output.resolve(), args.build.resolve()
    out.mkdir(parents=True, exist_ok=True)
    build.mkdir(parents=True)
    r = {'completed': False, 'passed': False, 'commands': [], 'oracle_observations': [],
        'protocol_observations': [], 'crash_observations': [], 'compiler_rebuilt': False,
        'current_production_sources_runtime_verified': False, 'formal_xml_cases': 0,
        'source_frontend_timeout_seconds': 60, 'full_ci_passed': False,
        'published_to_dev': False, 'goal_complete': False}

    def save():
        (out / 'results.json').write_text(json.dumps(r, indent=2) + '\n')

    def run(argv, label):
        argv = list(map(str, argv))
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        with log.open('w') as stream:
            proc = subprocess.Popen(argv, cwd=root, stdout=stream,
                stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            try:
                code = proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'],
                    stdout=stream, stderr=subprocess.STDOUT, timeout=15)
                proc.wait(timeout=15)
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1000
        row = {'command': argv, 'cwd': str(root), 'returncode': code,
            'log': str(log.relative_to(out)), 'log_sha256': sha(log),
            'timeout_seconds': 60, 'elapsed_seconds': time.monotonic() - started}
        r['commands'].append(row)
        save()
        print(label, code, flush=True)
        return code

    def executable(path):
        assert path.is_file()
        data = path.open('rb').read(512)
        assert data[:2] == b'MZ'
        offset = int.from_bytes(data[60:64], 'little')
        if offset + 6 > len(data):
            with path.open('rb') as stream:
                stream.seek(offset)
                signature = stream.read(6)
        else:
            signature = data[offset:offset + 6]
        assert signature == b'PE\0\0\x64\xaa'
        return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': sha(path),
                'first_64_bytes_hex': data[:64].hex(), 'machine': 'ARM64'}

    save()
    try:
        assert os.environ.get('GITHUB_ACTIONS') == 'true'
        assert platform.system() == 'Windows' and platform.machine().lower() in ['arm64', 'aarch64']
        assert platform.python_version() == '3.12.10'
        assert os.environ.get('VSCMD_ARG_TGT_ARCH', '').lower() == 'arm64'
        plan = json.loads(args.plan.read_text())
        r.update(plan=plan, revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                 host={'platform': platform.platform(), 'machine': platform.machine(),
                       'python': platform.python_version(), 'native_cpp_library': 'MSVC STL'})
        r['source_sha256'] = {name: sha(root / name) for name in plan['source_paths']}
        baseline = json.loads(args.baseline.read_text())
        assert baseline['revision'] == plan['compiler_source_revision'] == '43c7c0c55874c8b3ebfd4c1c39096189741f9616'
        assert baseline['run_id'] == 39341054945 and baseline['artifact_id'] == 12235108529
        assert baseline['artifact_sha256'] == '995c74533ccaf3aa43111bf07fba09b3fffd15faae908833b5d26ef3ecf1c707'
        compiler = Path(baseline['compiler'])
        assert sha(compiler) == baseline['compiler_sha256']
        assert baseline['equality_to_later_test_binary_established'] is False
        assert baseline['preinstall_wide_compiler_sha256'] == plan['preinstall_wide_compiler_sha256']
        r['baseline_provenance'] = baseline
        assert run([compiler, '--version'], 'environment/compiler-version') == 0
        assert run([compiler, '__neverc_cpp_frontend', '--version'], 'environment/private-frontend-version') == 0
        assert (out / 'environment/private-frontend-version.log').read_text().strip() == plan['expected_frontend_version']
        assert run(['cl.exe'], 'environment/native-msvc') in [0, 2]
        native_compiler = shutil.which('clang-cl.exe')
        assert native_compiler, 'Real native ARM64 clang-cl unavailable; no reference compiler stub'
        assert run([native_compiler, '--version'], 'environment/native-clang') == 0
        version = (out / 'environment/native-clang.log').read_text()
        assert 'clang version' in version and ('aarch64' in version or 'arm64' in version)
        r['native_reference_compiler'] = {'path': native_compiler, 'version': version,
            'sha256': sha(native_compiler), 'vendor_cpp_library': 'MSVC STL',
            'frozen_sdk_abi_identity_proved': False}
        assert sha(compiler) == plan['compiler_sha256']
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == plan['sdk_catalog_sha256'] == '1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        catalog = json.loads((sdk / 'catalog.json').read_text())
        sdk_files = catalog['headers'] + catalog['metadata']
        assert len(sdk_files) == 665
        for row in sdk_files:
            assert sha(sdk / row['root'] / row['path']) == row['sha256']
        r['sdk_files_verified'] = 665
        for case in plan['oracle_cases']:
            for variant in ['original', 'proposed']:
                folder = out / 'sources' / case['name'] / variant
                folder.mkdir(parents=True)
                source = folder / 'input.cpp'
                source.write_bytes(case[variant + '_source'].encode())
                assert sha(source) == case[variant + '_source_sha256']
                native_source = source
                if case.get('c_harness'):
                    harness = folder / 'harness.c'
                    harness.write_bytes(case['c_harness'].encode())
                    assert sha(harness) == case['c_harness_sha256']
                nc = build / (case['name'] + '-' + variant + '.nc')
                translated = run([compiler, 'translate', '--from', 'cpp', source,
                    '--profile', 'cpp-core-v2', '-o', nc, '--', '-std=c++17'],
                    'translation/' + case['name'] + '-' + variant)
                row = {'name': case['name'], 'variant': variant, 'source_sha256': sha(source),
                    'translation_returncode': translated, 'native': [], 'translated': []}
                if nc.is_file():
                    saved = out / 'generated' / nc.name
                    saved.parent.mkdir(exist_ok=True)
                    shutil.copyfile(nc, saved)
                    row['nc_sha256'] = sha(saved)
                for mode, native_flag in [('O0', '/Od'), ('O2', '/O2')]:
                    stem = case['name'] + '-' + variant + '-' + mode
                    native = build / (stem + '-native.exe')
                    obj = build / (stem + '-native.obj')
                    if case.get('c_harness'):
                        code = run([native_compiler, '/nologo', '/std:c++17', '/EHsc', '/MT', native_flag,
                            '/c', source, '/Fo' + str(obj)], 'native/' + stem + '-compile')
                        if code == 0:
                            code = run([native_compiler, '/nologo', '/MT', native_flag, harness, obj,
                                '/Fo' + str(build / (stem + '-harness.obj')), '/Fe' + str(native)],
                                'native/' + stem + '-link')
                    else:
                        code = run([native_compiler, '/nologo', '/std:c++17', '/EHsc', '/MT', native_flag,
                            source, '/Fo' + str(obj), '/Fe' + str(native)], 'native/' + stem + '-compile')
                    n = {'mode': mode, 'compile_returncode': code}
                    if code == 0:
                        n['image'] = executable(native)
                        n['returncode'] = run([native], 'native/' + stem + '-execute')
                    row['native'].append(n)
                    t = {'mode': mode, 'compile_returncode': None}
                    if translated == 0:
                        binary = build / (stem + '-translated.exe')
                        if case.get('c_harness'):
                            obj = build / (stem + '-translated.obj')
                            code = run([compiler, nc, '-' + mode, '-fno-lto', '-fno-builtin-mimalloc',
                                '-o', obj, '-c', '-fstrict-aliasing'], 'backend/' + stem + '-compile')
                            if code == 0:
                                code = run([compiler, harness, '-' + mode, '-fno-lto', '-fno-builtin-mimalloc',
                                        '-o', binary, obj],
                                           'backend/' + stem + '-link')
                        else:
                            code = run([compiler, nc, '-' + mode, '-fno-lto', '-fno-builtin-mimalloc',
                                        '-o', binary, '-fno-inline'], 'backend/' + stem + '-compile')
                        t['compile_returncode'] = code
                        if code == 0:
                            t['image'] = executable(binary)
                            t['returncode'] = run([binary], 'backend/' + stem + '-execute')
                    row['translated'].append(t)
                r['oracle_observations'].append(row)
                save()
        for case in plan['protocol_cases']:
            for target in plan['target_abis']:
                folder = out / 'protocol' / case['name'] / target
                folder.mkdir(parents=True)
                source, request, response = folder / 'input.cpp', folder / 'request.json', folder / 'response.json'
                source.write_bytes(case['source'].encode())
                assert sha(source) == case['source_sha256']
                request.write_text(json.dumps({'protocol': 1, 'profile': 'cpp-core-v2', 'root': str(folder),
                    'source': str(source), 'target': target, 'arguments': ['-std=c++17'],
                    'sdk': {'distribution_id': catalog['distribution_id'], 'catalog_sha256': plan['sdk_catalog_sha256']}}) + '\n')
                status = run([compiler, '__neverc_cpp_frontend', '--request', request, '--output', response],
                    'protocol-execute/' + case['name'] + '-' + target)
                result = json.loads(response.read_text()) if response.exists() else None
                r['protocol_observations'].append({'name': case['name'], 'target': target,
                    'source_sha256': sha(source), 'request_sha256': sha(request), 'returncode': status,
                    'response_sha256': sha(response) if response.exists() else None,
                    'diagnostics': result.get('diagnostics') if result else None,
                    'frontend': result.get('frontend') if result else None,
                    'actual_target': result.get('target') if result else None})
                save()
        source = out / 'sources/string-concat-rvalue.cpp'
        source.write_bytes(plan['crash_source'].encode())
        assert sha(source) == plan['crash_source_sha256']
        for i in range(3):
            target = build / ('string-concat-' + str(i) + '.nc')
            status = run([compiler, 'translate', '--from', 'cpp', source, '--profile', 'cpp-core-v2',
                '-o', target, '--', '-std=c++17'], 'crash/string-concat-' + str(i))
            r['crash_observations'].append({'attempt': i, 'returncode': status,
                'output_exists': target.exists(), 'nc_sha256': sha(target) if target.exists() else None})
            save()
        originals = [row for row in r['oracle_observations'] if row['variant'] == 'original']
        proposed = [row for row in r['oracle_observations'] if row['variant'] == 'proposed']
        assert len(originals) == len(proposed) == 3
        native_ok = all(len(row['native']) == 2 and all(x.get('returncode') == 0 for x in row['native'])
                        for row in r['oracle_observations'])
        proposed_ok = all(row['translation_returncode'] == 0 and len(row['translated']) == 2
                         and all(x.get('returncode') == 0 for x in row['translated']) for row in proposed)
        r.update(completed=True, native_reference_passed=native_ok,
            proposed_oracle_runtime_passed=proposed_ok,
            existing_other_compiler_failures_remain=any(row['returncode'] != 0 for row in r['protocol_observations'])
                or any(row['returncode'] != 0 for row in r['crash_observations']))
        # Preserve actual remaining source failures as failures in the job.
        r['passed'] = native_ok and proposed_ok and not r['existing_other_compiler_failures_remain']
        assert sha(compiler) == baseline['compiler_sha256']
        save()
        return 0 if r['passed'] else 1
    except BaseException:
        r['failure'] = traceback.format_exc()
        save()
        raise


if __name__ == '__main__':
    raise SystemExit(main())
