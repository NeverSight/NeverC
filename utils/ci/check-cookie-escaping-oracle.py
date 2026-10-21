"""Exercise allocation bookkeeping with a separately compiled C pointer escape."""
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
    for key in ['repo', 'plan', 'baseline', 'output', 'build']:
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    root, out, build = args.repo.resolve(), args.output.resolve(), args.build.resolve()
    out.mkdir(parents=True, exist_ok=True)
    build.mkdir(parents=True, exist_ok=True)
    r = {'completed': False, 'passed': False, 'commands': [], 'observations': [],
         'compiler_rebuilt': False, 'current_production_compiler_verified': False,
         'formal_xml': 0, 'full_ci_passed': False, 'published_to_dev': False,
         'goal_complete': False}

    def save():
        (out / 'results.json').write_text(json.dumps(r, indent=2) + '\n')

    def run(argv, label):
        argv = list(map(str, argv))
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        with log.open('w') as stream:
            proc = subprocess.Popen(argv, cwd=root, stdout=stream, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            try:
                status = proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'],
                               stdout=stream, stderr=subprocess.STDOUT, timeout=15)
                proc.wait(timeout=15)
                status = -1000
        r['commands'].append({'command': argv, 'cwd': str(root), 'pid': proc.pid,
            'returncode': status, 'log': log.relative_to(out).as_posix(),
            'log_sha256': sha(log), 'timeout_seconds': 60,
            'elapsed_seconds': time.monotonic() - started})
        save()
        print(label, status, flush=True)
        return status

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
        baseline = json.loads(args.baseline.read_text())
        r.update(plan=plan, baseline_provenance=baseline,
                 revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                 host={'platform': platform.platform(), 'machine': platform.machine(),
                       'python': platform.python_version()},
                 source_sha256={name: sha(root / name) for name in plan['source_paths']})
        assert baseline['revision'] == plan['compiler_source_revision']
        assert baseline['run_id'] == 39341054945 and baseline['artifact_id'] == 12235108529
        assert baseline['artifact_sha256'] == plan['compiler_artifact']['digest'].removeprefix('sha256:')
        compiler = Path(baseline['compiler'])
        assert sha(compiler) == baseline['compiler_sha256'] == plan['compiler_sha256']
        assert not baseline['equality_to_later_test_binary_established']
        native = shutil.which('clang-cl.exe')
        assert native
        assert run([native, '--version'], 'environment/native-version') == 0
        assert (out / 'environment/native-version.log').read_text() == plan['native_reference_version']
        r['native_compiler'] = {'path': native, 'sha256': sha(native),
                                'version': plan['native_reference_version']}
        assert run([compiler, '__neverc_cpp_frontend', '--version'], 'environment/frontend-version') == 0
        assert (out / 'environment/frontend-version.log').read_text().strip() == plan['expected_frontend_version']
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == plan['sdk_catalog_sha256']
        catalog = json.loads((sdk / 'catalog.json').read_text())
        entries = catalog['headers'] + catalog['metadata']
        assert len(entries) == 665
        for entry in entries:
            assert sha(sdk / entry['root'] / entry['path']) == entry['sha256']
        r['sdk_files_verified'] = 665
        source = root / plan['fixture']
        harness = root / plan['harness']
        assert sha(source) == plan['fixture_sha256'] and sha(harness) == plan['harness_sha256']
        saved = out / 'sources'
        saved.mkdir()
        shutil.copyfile(source, saved / source.name)
        shutil.copyfile(harness, saved / harness.name)
        nc = build / 'default-sized-delete.nc'
        translation = run([compiler, 'translate', '--from', 'cpp', source, '--profile',
            'cpp-core-v2', '-o', nc, '--', '-std=c++17'], 'frontend/translate')
        r['translation_returncode'] = translation
        if translation == 0:
            target = out / nc.name
            shutil.copyfile(nc, target)
            r['nc_sha256'] = sha(target)
        for mode, flag in [('O0', '/Od'), ('O2', '/O2')]:
            cpp_object = build / (mode + '-native.obj')
            c_object = build / (mode + '-harness.obj')
            product = build / (mode + '-native.exe')
            flags = ['/nologo', '/std:c++17', '/EHsc', '/MT', flag]
            assert run([native, *flags, '/c', source, '/Fo' + str(cpp_object)], mode + '/native-compile') == 0
            assert run([native, '/nologo', '/MT', flag, '/c', harness, '/Fo' + str(c_object)], mode + '/native-harness') == 0
            assert run([native, '/nologo', cpp_object, c_object, '/Fe' + str(product)], mode + '/native-link') == 0
            row = {'mode': mode, 'native_image': image(product),
                   'native_returncode': run([product], mode + '/native-execute')}
            ir = out / (mode + '.ll')
            assert run([native, *flags, '/clang:-S', '/clang:-emit-llvm', '/clang:-o',
                        '/clang:' + str(ir), source], mode + '/native-ir') == 0
            assert '@default_sized_delete_check' in ir.read_text()
            row.update(ir_file=ir.name, ir_sha256=sha(ir))
            if translation == 0:
                translated_object = build / (mode + '-translated.obj')
                translated_product = build / (mode + '-translated.exe')
                status = run([compiler, nc, '-' + mode, '-fno-lto', '-fno-builtin-mimalloc',
                    '-o', translated_object, '-c', '-fstrict-aliasing'], mode + '/translated-compile')
                if status == 0:
                    status = run([compiler, harness, '-' + mode, '-fno-lto', '-fno-builtin-mimalloc',
                        '-o', translated_product, translated_object], mode + '/translated-link')
                row['translated_build_returncode'] = status
                if status == 0:
                    row.update(translated_image=image(translated_product),
                               translated_returncode=run([translated_product], mode + '/translated-execute'))
            r['observations'].append(row)
            save()
        r['passed'] = translation == 0 and len(r['observations']) == 2 and all(
            row['native_returncode'] == 0 and row.get('translated_returncode') == 0 for row in r['observations'])
        r.update(completed=True, fixture_native_and_translated_passed=r['passed'])
        assert sha(compiler) == plan['compiler_sha256']
    except BaseException:
        r['error'] = traceback.format_exc()
        save()
        raise
    save()
    return 0 if r['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
