"""Read-only GDB diagnosis of the exact remotely retained source-probe ELF."""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import os
import platform
import shutil
import signal
import stat
import subprocess
import time
import traceback
import urllib.error
import urllib.request
import zipfile


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
    plan = json.loads(args.plan.read_text())
    r = {'completed': False, 'passed': False, 'commands': [], 'requests': [], 'gdb_observations': [],
         'neverc_host_rebuilt': False, 'private_component_rebuilt': False,
         'debugger_memory_modified': False, 'production_options_changed': False,
         'frontend_timeout_seconds': 60, 'all_binaries_remote_only': True,
         'not_fn_admission_implemented': False, 'not_fn_lowering_implemented': False,
         'formal_xml': 0, 'full_ci_passed': False, 'published_to_dev': False, 'goal_complete': False}

    def save():
        temporary = out / 'results.tmp'
        temporary.write_text(json.dumps(r, indent=2) + '\n')
        temporary.replace(out / 'results.json')

    def api(endpoint, destination=None):
        request = urllib.request.Request('https://api.github.com/repos/NeverSight/NeverC/' + endpoint,
            headers={'Authorization': 'Bearer ' + os.environ['GITHUB_TOKEN'],
                     'Accept': 'application/vnd.github+json', 'Accept-Encoding': 'gzip',
                     'Cache-Control': 'no-cache', 'User-Agent': 'NeverC-remote-only-source-probe-stack'})

        class NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, request, stream, code, message, headers, url):
                return None

        for attempt in range(3):
            try:
                try:
                    response = urllib.request.build_opener(NoRedirect).open(request, timeout=40)
                except urllib.error.HTTPError as error:
                    if error.code != 302:
                        raise
                    location = error.headers['Location']
                    assert location.startswith('https://')
                    response = urllib.request.urlopen(urllib.request.Request(location), timeout=180)
                with response:
                    if destination:
                        with destination.open('wb') as stream:
                            while chunk := response.read(1048576):
                                stream.write(chunk)
                        return
                    raw = response.read()
                    if response.headers.get('Content-Encoding') == 'gzip':
                        raw = gzip.decompress(raw)
                    return json.loads(raw)
            except (urllib.error.URLError, TimeoutError) as error:
                r.setdefault('read_only_get_failures', []).append({'attempt': attempt,
                    'kind': type(error).__name__, 'http_status': getattr(error, 'code', None)})
                save()
        raise RuntimeError('Authenticated producer GET unavailable; no test or dispatch retry.')

    def run(argv, label):
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        env = dict(os.environ, NEVERC_CPP_PROTOCOL_EXECUTION_DIR=str(out / 'invocations' / label))
        env.pop('GITHUB_TOKEN', None)
        env.pop('GH_TOKEN', None)
        started = time.monotonic()
        with log.open('wb') as stream:
            proc = subprocess.Popen(list(map(str, argv)), cwd=root, env=env,
                stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait(timeout=10)
                stream.write(b'\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1000
        row = {'command': list(map(str, argv)), 'pid': proc.pid, 'cwd': str(root),
            'returncode': code, 'timeout_seconds': 60, 'elapsed_seconds': time.monotonic() - started,
            'log': log.relative_to(out).as_posix(), 'log_sha256': sha(log)}
        r['commands'].append(row)
        save()
        print(label, code, flush=True)
        return row

    save()
    try:
        assert os.environ.get('GITHUB_ACTIONS') == 'true' and platform.system() == 'Linux'
        assert platform.machine() == 'x86_64' and platform.python_version() == '3.12.10'
        r.update(revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
            plan=plan, source_sha256={name: sha(root / name) for name in plan['source_paths']})
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == plan['sdk_catalog_sha256']
        catalog = json.loads((sdk / 'catalog.json').read_text())
        entries = catalog['headers'] + catalog['metadata']
        assert len(entries) == 665
        for entry in entries:
            assert sha(sdk / entry['root'] / entry['path']) == entry['sha256']
        r['sdk_files_verified'] = 665
        producer = api('actions/runs/' + str(plan['producer_run_id']))
        job = api('actions/jobs/' + str(plan['producer_job_id']))
        assert producer['head_sha'] == job['head_sha'] == plan['producer_commit']
        assert producer['status'] == job['status'] == 'completed'
        assert producer['conclusion'] == job['conclusion'] == 'failure'
        assert producer['run_attempt'] == plan['producer_attempt'] and job['run_id'] == producer['id']
        assert any(step['name'] == 'Keep new actual source binary remotely for further diagnosis'
            and step['conclusion'] == 'success' for step in job['steps'])
        producer_folder = build / 'producer-evidence'
        producer_folder.mkdir()
        for kind in ['small', 'binary']:
            expected = plan['producer_' + kind + '_artifact']
            metadata = api('actions/artifacts/' + str(expected['id']))
            assert all(metadata[key] == expected[key] for key in ['id', 'name', 'digest'])
            assert metadata['workflow_run']['id'] == producer['id']
            assert metadata['workflow_run']['head_sha'] == plan['producer_commit'] and not metadata['expired']
            archive = build / (kind + '.zip')
            api('actions/artifacts/' + str(expected['id']) + '/zip', archive)
            assert 'sha256:' + sha(archive) == expected['digest']
            destination = producer_folder / kind
            destination.mkdir()
            with zipfile.ZipFile(archive) as package:
                assert sum(x.file_size for x in package.infolist()) < 128 * 1024 ** 2
                for info in package.infolist():
                    name = Path(info.filename)
                    assert not name.is_absolute() and '..' not in name.parts
                    assert not stat.S_ISLNK(info.external_attr >> 16)
                    assert (destination / name).resolve().is_relative_to(destination.resolve())
                    if kind == 'binary':
                        assert info.filename == 'private-frontend-source-replay'
                package.extractall(destination)
            r[kind + '_zip_sha256'] = sha(archive)
        reports = list((producer_folder / 'small').rglob('results.json'))
        assert len(reports) == 1 and sha(reports[0]) == plan['producer_report_sha256']
        old_root = reports[0].parent
        old = json.loads(reports[0].read_text())
        assert old['revision'] == plan['producer_commit'] and not old['completed'] and not old['passed']
        assert old['new_executable_sha256'] == plan['executable_sha256']
        assert old['new_build_id'] == plan['frontend_build_id'] and len(old['requests']) == 23
        binary = producer_folder / 'binary/private-frontend-source-replay'
        assert sha(binary) == old['new_executable_sha256'] and binary.stat().st_size == old['new_executable_bytes']
        binary.chmod(0o755)
        r.update(executable_sha256=sha(binary), executable_bytes=binary.stat().st_size)
        with binary.open('rb') as stream:
            header = stream.read(64)
        assert header[:6] == b'\x7fELF\x02\x01' and header[18:20] == b'\x3e\x00'
        (out / 'actual-reused-entry-header.bin').write_bytes(header)
        version = run([binary, '__neverc_cpp_frontend', '--version'], 'environment/version')
        assert version['returncode'] == 0
        assert (out / version['log']).read_text().strip().endswith(plan['frontend_build_id'])
        run(['gdb', '--version'], 'environment/gdb-version')
        by_name = {row['name']: row for row in old['requests']}
        old_commands = {row['log']: row for row in old['commands']}
        for index, name in enumerate(plan['plain_request_names']):
            previous = by_name[name]
            argv = old_commands['execute/x86_64-unknown-linux-gnu/' + name + '.log']['command']
            request = Path(argv[3])
            assert request.is_relative_to(Path('/home/runner/work/_temp/cpp-not-fn-selected-source/requests'))
            stem = old_root / 'requests/x86_64-unknown-linux-gnu' / name
            request.parent.mkdir(parents=True, exist_ok=True)
            for filename in ['input.cpp', 'request.json']:
                shutil.copyfile(stem / filename, request.parent / filename)
            assert sha(request.parent / 'input.cpp') == previous['source_sha256']
            assert (request.parent / 'request.json').read_bytes() == (stem / 'request.json').read_bytes()
            output = out / 'responses' / (str(index) + '.json')
            output.parent.mkdir(parents=True, exist_ok=True)
            call = [str(binary), *argv[1:5], str(output)]
            row = run(call, 'plain/' + str(index) + '-' + name)
            expected_code = 0 if previous['expected'] == 'accept' else -11
            assert row['returncode'] == expected_code
            response = json.loads(output.read_text()) if output.exists() else None
            if expected_code == 0:
                assert response and not response['diagnostics'] and response['frontend']['build'] == plan['frontend_build_id']
            else:
                assert response is None
            r['requests'].append({'name': name, 'index': index, 'source_sha256': previous['source_sha256'],
                'original_request_sha256': sha(request), 'request': str(request), 'returncode': row['returncode'],
                'response_sha256': sha(output) if output.exists() else None, 'log': row['log']})
            save()
        for index, name in enumerate(plan['gdb_request_names']):
            previous = by_name[name]
            old_command = old_commands['execute/x86_64-unknown-linux-gnu/' + name + '.log']['command']
            response = out / 'gdb-responses' / (str(index) + '.json')
            response.parent.mkdir(parents=True, exist_ok=True)
            cmd = ['gdb', '--batch', '-nx', '-ex', 'set pagination off', '-ex', 'set confirm off',
                '-ex', 'set disable-randomization off', '-ex', 'run', '-ex', 'thread apply all bt 30',
                '-ex', 'info registers', '-ex', 'x/20i $pc-32', '-ex', 'info sharedlibrary',
                '--args', str(binary), *old_command[1:5], str(response)]
            row = run(cmd, 'gdb/' + str(index) + '-' + name)
            text = (out / row['log']).read_text(errors='replace')
            assert row['returncode'] == 0 and 'received signal SIGSEGV' in text
            assert '#0 ' in text and not response.exists()
            r['gdb_observations'].append({'name': name, 'actual_sigsegv': True, 'log': row['log'],
                'source_sha256': previous['source_sha256'], 'log_sha256': row['log_sha256']})
            save()
        assert len(r['requests']) == len(plan['plain_request_names'])
        assert len(r['gdb_observations']) == 2
        r['completed'] = True
        r['diagnostic_evidence_complete'] = True
    except Exception:
        r['failure'] = traceback.format_exc()
    save()
    print(json.dumps({'completed': r['completed'], 'passed': False,
        'plain_requests': len(r['requests']), 'gdb_stacks': len(r['gdb_observations'])}), flush=True)
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
