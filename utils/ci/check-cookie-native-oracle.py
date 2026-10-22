"""Retain exact native cookie oracle, emitted IR and separate-object observations."""
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
    for name in ['repo', 'plan', 'output', 'build']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    root, out, build = args.repo.resolve(), args.output.resolve(), args.build.resolve()
    out.mkdir(parents=True, exist_ok=True)
    build.mkdir(parents=True, exist_ok=True)
    result = {'completed': False, 'passed': False, 'commands': [], 'observations': [],
              'native_oracle_correct': False, 'compiler_rebuilt': False,
              'private_frontend_executed': False, 'formal_xml_cases': 0,
              'compiler_fixed': False, 'full_ci_passed': False,
              'published_to_dev': False, 'goal_complete': False}

    def save():
        (out / 'results.json').write_text(json.dumps(result, indent=2) + '\n')

    def run(argv, label):
        argv = list(map(str, argv))
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        with log.open('w') as stream:
            proc = subprocess.Popen(argv, cwd=root, stdout=stream,
                                    stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            try:
                code = proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'],
                               stdout=stream, stderr=subprocess.STDOUT, timeout=15)
                proc.wait(timeout=15)
                code = -1000
        result['commands'].append({'command': argv, 'cwd': str(root), 'pid': proc.pid,
                                   'returncode': code, 'log': log.relative_to(out).as_posix(),
                                   'log_sha256': sha(log), 'timeout_seconds': 60,
                                   'elapsed_seconds': time.monotonic() - started})
        save()
        print(label, code, flush=True)
        return code

    def image(path):
        with path.open('rb') as stream:
            header = stream.read(64)
            assert header[:2] == b'MZ'
            stream.seek(int.from_bytes(header[60:64], 'little'))
            assert stream.read(6) == b'PE\0\0\x64\xaa'
        return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': sha(path),
                'machine': 'ARM64', 'first_64_bytes_hex': header.hex()}

    save()
    try:
        assert os.environ.get('GITHUB_ACTIONS') == 'true'
        assert platform.system() == 'Windows' and platform.machine().lower() in ['arm64', 'aarch64']
        assert platform.python_version() == '3.12.10'
        assert os.environ.get('VSCMD_ARG_TGT_ARCH', '').lower() == 'arm64'
        plan = json.loads(args.plan.read_text())
        compiler = shutil.which('clang-cl.exe')
        assert compiler
        result.update(plan=plan, revision=subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
            host={'platform': platform.platform(), 'machine': platform.machine(),
                  'python': platform.python_version()},
            source_sha256={name: sha(root / name) for name in plan['source_paths']})
        assert run([compiler, '--version'], 'environment/native-clang') == 0
        version = (out / 'environment/native-clang.log').read_text()
        assert version == plan['native_reference_version']
        result['native_compiler'] = {'path': compiler, 'sha256': sha(compiler), 'version': version}
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == plan['sdk_catalog_sha256']
        catalog = json.loads((sdk / 'catalog.json').read_text())
        entries = catalog['headers'] + catalog['metadata']
        assert len(entries) == 665
        for entry in entries:
            assert sha(sdk / entry['root'] / entry['path']) == entry['sha256']
        result['sdk_files_verified'] = 665
        source_root = out / 'sources'
        source_root.mkdir()
        observer = source_root / 'observer.cpp'
        observer.write_bytes(plan['observer_source'].encode())
        assert sha(observer) == plan['observer_source_sha256']
        for variant in ['original', 'proposed']:
            case = plan['cases'][variant]
            source = source_root / (variant + '.cpp')
            source.write_bytes(case['source'].encode())
            assert sha(source) == case['source_sha256']
            harness = source_root / (variant + '-harness.c')
            harness.write_bytes(plan['c_harness'].encode())
            assert sha(harness) == plan['c_harness_sha256']
            for mode, flag in [('O0', '/Od'), ('O2', '/O2')]:
                stem = variant + '-' + mode
                obj = build / (stem + '.obj')
                harness_obj = build / (stem + '-harness.obj')
                executable = build / (stem + '.exe')
                flags = ['/nologo', '/std:c++17', '/EHsc', '/MT', flag]
                assert run([compiler, *flags, '/c', source, '/Fo' + str(obj)], stem + '/compile') == 0
                assert run([compiler, '/nologo', '/MT', flag, '/c', harness,
                            '/Fo' + str(harness_obj)], stem + '/harness') == 0
                assert run([compiler, '/nologo', obj, harness_obj,
                            '/Fe' + str(executable)], stem + '/link') == 0
                row = {'variant': variant, 'mode': mode, 'source_sha256': sha(source),
                       'object_sha256': sha(obj), 'image': image(executable),
                       'returncode': run([executable], stem + '/execute')}
                ir = out / (stem + '.ll')
                assert run([compiler, *flags, '/clang:-S', '/clang:-emit-llvm',
                            '/clang:-o', '/clang:' + str(ir), source], stem + '/emit-ir') == 0
                assert ir.is_file() and ir.stat().st_size > 1000
                text = ir.read_text()
                assert 'define' in text and '@default_sized_delete_check' in text
                row.update(ir_file=ir.name, ir_sha256=sha(ir))
                if variant == 'original':
                    observer_obj = build / (stem + '-observer.obj')
                    observed = build / (stem + '-observed.exe')
                    assert run([compiler, *flags, '/c', observer,
                                '/Fo' + str(observer_obj)], stem + '/observer-compile') == 0
                    assert sha(obj) == row['object_sha256']
                    assert run([compiler, '/nologo', obj, observer_obj,
                                '/Fe' + str(observed)], stem + '/observer-link') == 0
                    row['observer_image'] = image(observed)
                    row['observer_returncode'] = run([observed], stem + '/observer-execute')
                    row['same_original_object_in_observer'] = sha(obj) == row['object_sha256']
                result['observations'].append(row)
                save()
        result.update(completed=True, original_source_and_flags_preserved=True,
                      actual_native_runs=6, actual_emitted_ir_files=4)
        # This authenticates a failing oracle's evidence; it does not repair it.
        result['observed_original_failure_reproduced'] = all(
            row['returncode'] == (0 if row['mode'] == 'O0' else 13)
            for row in result['observations'])
    except BaseException:
        result['error'] = traceback.format_exc()
        save()
        raise
    save()
    raise SystemExit(1)


if __name__ == '__main__':
    main()
