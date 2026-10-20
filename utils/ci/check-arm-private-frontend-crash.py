"""Retain the actual ARM64 frontend AV with its exact package and matching PDB."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import platform
import shutil
import struct
import subprocess
import time
import traceback
import uuid
import zipfile


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def codeview(path):
    with Path(path).open('rb') as stream:
        data = stream.read()
    pe = struct.unpack_from('<I', data, 60)[0]
    assert data[:2] == b'MZ' and data[pe:pe + 6] == b'PE\0\0\x64\xaa'
    sections, optional_bytes = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    assert struct.unpack_from('<H', data, optional)[0] == 0x20B
    debug_rva, debug_bytes = struct.unpack_from('<II', data, optional + 112 + 6 * 8)
    entries = optional + optional_bytes
    offset = None
    for index in range(sections):
        section = entries + index * 40
        virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', data, section + 8)
        if rva <= debug_rva < rva + max(virtual_size, raw_size):
            offset = raw + debug_rva - rva
    assert offset is not None and debug_bytes % 28 == 0
    records = []
    for at in range(offset, offset + debug_bytes, 28):
        kind = struct.unpack_from('<I', data, at + 12)[0]
        length, _, raw = struct.unpack_from('<III', data, at + 16)
        if kind == 2:
            cv = data[raw:raw + length]
            assert cv[:4] == b'RSDS'
            records.append({'guid': str(uuid.UUID(bytes_le=cv[4:20])),
                'age': struct.unpack_from('<I', cv, 20)[0],
                'pdb_name': cv[24:].split(b'\0', 1)[0].decode(errors='replace')})
    assert len(records) == 1
    return records[0]


def main():
    parser = argparse.ArgumentParser()
    for name in ['repo', 'plan', 'baseline', 'output', 'build']:
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    root, out, build = args.repo.resolve(), args.output.resolve(), args.build.resolve()
    out.mkdir(parents=True)
    build.mkdir(parents=True)
    report = {'completed': False, 'passed': False, 'commands': [],
        'original_cli_replays': [], 'compiler_rebuilt': False, 'compiler_repaired': False,
        'frontend_execution_timeout_seconds': 60, 'full_ci_passed': False,
        'published_to_dev': False, 'goal_complete': False, 'debugger_changes_memory_or_options': False}

    def save():
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

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
                subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], stdout=stream,
                    stderr=subprocess.STDOUT, timeout=15)
                proc.wait(timeout=15)
                stream.write('\nDIAGNOSTIC PROCESS GROUP TIMED OUT\n')
                status = -1000
        report['commands'].append({'command': argv, 'cwd': str(root), 'returncode': status,
            'timeout_seconds': 60, 'log': log.relative_to(out).as_posix(),
            'log_sha256': sha(log), 'elapsed_seconds': time.monotonic() - started})
        save()
        print(label, status, flush=True)
        return status, log

    save()
    try:
        assert os.environ.get('GITHUB_ACTIONS') == 'true' and platform.system() == 'Windows'
        assert platform.machine().lower() in ['arm64', 'aarch64'] and platform.python_version() == '3.12.10'
        plan = json.loads(args.plan.read_text())
        report.update(revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(), plan=plan,
            source_sha256={name: sha(root / name) for name in plan['source_paths']})
        baseline = json.loads(args.baseline.read_text())
        assert baseline['revision'] == plan['compiler_source_revision'] == '43c7c0c55874c8b3ebfd4c1c39096189741f9616'
        assert baseline['run_id'] == 39341054945 and baseline['artifact_id'] == 12235108529
        assert baseline['artifact_sha256'] == '995c74533ccaf3aa43111bf07fba09b3fffd15faae908833b5d26ef3ecf1c707'
        compiler = Path(baseline['compiler'])
        assert sha(compiler) == baseline['compiler_sha256'] == plan['compiler_sha256'] == 'adf3b0421e9ccf5120e121393595b85e351b90dfaccc16d4a707cd3c374e0176'
        report['baseline_provenance'] = baseline
        report['actual_pe_codeview'] = codeview(compiler)
        assert run([compiler, '__neverc_cpp_frontend', '--version'], 'environment/frontend-version')[0] == 0
        assert (out / 'environment/frontend-version.log').read_text().strip() == plan['expected_frontend_version']
        source = out / 'original-string-concat-rvalue.cpp'
        source.write_bytes(plan['source'].encode())
        assert sha(source) == plan['source_sha256']
        for i in range(3):
            target = build / ('original-' + str(i) + '.nc')
            status, log = run([compiler, 'translate', '--from', 'cpp', source,
                '--profile', 'cpp-core-v2', '-o', target, '--', '-std=c++17'], 'original-cli/' + str(i))
            report['original_cli_replays'].append({'attempt': i, 'returncode': status,
                'frontend_av_reported': '-1073741819' in log.read_text(), 'output_exists': target.exists()})
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == plan['sdk_catalog_sha256']
        catalog = json.loads((sdk / 'catalog.json').read_text())
        request, response = out / 'direct-request.json', out / 'direct-response.json'
        request.write_text(json.dumps({'protocol': 1, 'profile': 'cpp-core-v2', 'root': str(out),
            'source': str(source), 'target': 'aarch64-pc-windows-msvc', 'arguments': ['-std=c++17'],
            'sdk': {'distribution_id': catalog['distribution_id'], 'catalog_sha256': plan['sdk_catalog_sha256']}}) + '\n')
        report['request_sha256'] = sha(request)
        argv = [compiler, '__neverc_cpp_frontend', '--request', request, '--output', response]
        status, log = run(argv, 'direct/unmodified-private-entry')
        report['direct_entry_returncode'] = status
        report['direct_response_sha256'] = sha(response) if response.exists() else None
        helper = root / 'utils/ci/download-repaired-windows-arm.py'
        spec = importlib.util.spec_from_file_location('auth_artifact_download', helper)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        token = os.environ['GITHUB_TOKEN']
        record = plan['pdb_artifact']
        actual = module.request('actions/artifacts/' + str(record['id']), token)
        assert actual['id'] == 12235763065 and actual['name'] == record['name']
        assert actual['digest'] == record['digest'] == 'sha256:a8ae24fbde3907580152347c2c35f52dbc1e2faee66f572d81ed7b5aadded9b0'
        assert actual['workflow_run']['id'] == 39341054945 and actual['workflow_run']['head_sha'] == baseline['revision']
        assert not actual['expired']
        archive = build / 'pdb-artifact.zip'
        module.request('actions/artifacts/' + str(record['id']) + '/zip', token, archive)
        assert actual['digest'] == 'sha256:' + sha(archive)
        package = build / 'windows-arm64-neverc-pdb.zip'
        with zipfile.ZipFile(archive) as z:
            assert z.namelist() == ['windows-arm64-neverc-pdb.zip']
            with z.open(z.namelist()[0]) as stream, package.open('wb') as output:
                shutil.copyfileobj(stream, output)
        pdb_folder = build / 'pdbs'
        pdb_folder.mkdir()
        module.check_zip(package, pdb_folder)
        with zipfile.ZipFile(package) as z:
            names = [name for name in z.namelist() if Path(name).name.lower() == 'neverc.pdb']
            assert len(names) == 1
            z.extract(names[0], pdb_folder)
        pdb = pdb_folder / names[0]
        with pdb.open('rb') as stream:
            assert stream.read(32).startswith(b'Microsoft C/C++ MSF 7.00')
        report['pdb_identity'] = {'artifact': record, 'artifact_sha256': sha(archive),
            'package_sha256': sha(package), 'pdb_sha256': sha(pdb), 'bytes': pdb.stat().st_size,
            'downloaded_and_retained_on_actions_only': True}
        cdb_candidates = [Path(os.environ[key]) / 'Windows Kits/10/Debuggers/arm64/cdb.exe'
                          for key in ['ProgramFiles', 'ProgramFiles(x86)'] if key in os.environ]
        debuggers = [path for path in cdb_candidates if path.is_file()]
        assert debuggers, 'Actual ARM64 CDB unavailable; no stack or symbol stub permitted'
        cdb = debuggers[0]
        script = out / 'actual-av.cdb'
        dump = build / 'actual-private-frontend.dmp'
        script.write_text('.symopt-0x40; sxd ibp; sxe av; g; .echo NEVERC_ACTUAL_PRIVATE_AV; '
            'lmv m neverc; .ecxr; kv 30; r; u @pc L20; dv /t; .dump /m "' + str(dump) + '"; q\n')
        report['debugger_script_sha256'] = sha(script)
        report['debugger_sha256'] = sha(cdb)
        status, log = run([cdb, '-o', '-G', '-y', pdb.parent, '-cf', script, *argv], 'debugger/actual-av')
        report['debugger_returncode'] = status
        report['dump'] = {'created': dump.is_file(), 'bytes': dump.stat().st_size if dump.exists() else 0,
                          'sha256': sha(dump) if dump.exists() else None, 'remote_only': True}
        report['symbolic_stack_observed'] = 'neverc!' in log.read_text(errors='replace')
        report['actual_av_context_observed'] = 'NEVERC_ACTUAL_PRIVATE_AV' in log.read_text(errors='replace')
        assert sha(compiler) == plan['compiler_sha256'] and sha(pdb) == report['pdb_identity']['pdb_sha256']
        report.update(completed=True, reproduced_av=status != -1000 and report['direct_entry_returncode'] in [-1073741819, 3221225477]
            and all(x['frontend_av_reported'] for x in report['original_cli_replays']))
        save()
        # Observing an unfixed crash is a failing compiler result.
        return 1
    except BaseException:
        report['failure'] = traceback.format_exc()
        save()
        raise


if __name__ == '__main__':
    raise SystemExit(main())
