"""Replay preserved MSVC archives with collision-safe diagnostic records only."""
from pathlib import Path
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
import platform
import re
import shutil
import stat
import struct
import subprocess
import sys
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
    r = {'completed': False, 'passed': False, 'commands': [], 'requests': [], 'stages': {},
         'neverc_host_compiler_rebuilt': False, 'private_component_rebuilt': False,
         'frontend_timeout_seconds': 60, 'actual_translate_verifier_executed': False,
         'native_products_executed': 0, 'formal_xml': 0, 'all_binaries_remote_only': True,
         'full_ci_passed': False, 'published_to_dev': False, 'goal_complete': False}
    env = dict(os.environ, NEVERC_CPP_PROTOCOL_EXECUTION_DIR=str(out / 'invocations'))

    def save():
        (out / 'results.json').write_text(json.dumps(r, indent=2) + '\n')

    def run(argv, label, timeout=60):
        argv = list(map(str, argv))
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        with log.open('wb') as stream:
            process = subprocess.Popen(argv, cwd=root, env=env, stdout=stream,
                                       stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                               stdout=stream, stderr=subprocess.STDOUT, timeout=15)
                process.wait(timeout=15)
                stream.write(b'\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1000
        row = {'command': argv, 'cwd': str(root), 'pid': process.pid, 'returncode': code,
               'timeout_seconds': timeout, 'log': log.relative_to(out).as_posix(),
               'log_sha256': sha(log), 'elapsed_seconds': time.monotonic() - started}
        r['commands'].append(row)
        save()
        print(label, code, flush=True)
        return row

    def api(endpoint, destination=None):
        token = os.environ['GITHUB_TOKEN']
        request = urllib.request.Request('https://api.github.com/repos/NeverSight/NeverC/' + endpoint,
            headers={'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json',
                     'Accept-Encoding': 'gzip', 'Cache-Control': 'no-cache',
                     'User-Agent': 'NeverC-remote-only-ARM-component-replay'})

        class NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, request, stream, code, message, headers, url):
                return None

        for attempt in range(3):
            try:
                try:
                    response = urllib.request.build_opener(NoRedirect).open(request, timeout=45)
                except urllib.error.HTTPError as error:
                    if error.code != 302:
                        raise
                    location = error.headers['Location']
                    assert location.startswith('https://')
                    response = urllib.request.urlopen(urllib.request.Request(location), timeout=300)
                with response:
                    if destination:
                        with destination.open('wb') as stream:
                            while block := response.read(1048576):
                                stream.write(block)
                        return
                    data = response.read()
                    if response.headers.get('Content-Encoding') == 'gzip':
                        data = gzip.decompress(data)
                    return json.loads(data)
            except (urllib.error.URLError, TimeoutError) as error:
                # Keep only the exception kind/status; redirect URLs remain in memory.
                r.setdefault('read_only_download_attempts', []).append(
                    {'attempt': attempt, 'type': type(error).__name__, 'status': getattr(error, 'code', None)})
                save()
        raise RuntimeError('Read-only producer evidence unavailable; no test or POST retried.')

    def pe(path, stage):
        with path.open('rb') as stream:
            header = stream.read(4096)
        offset = struct.unpack_from('<I', header, 60)[0]
        assert header[:2] == b'MZ' and header[offset:offset + 6] == b'PE\0\0\x64\xaa'
        saved = out / stage / 'caller-pe-header.bin'
        saved.parent.mkdir(parents=True, exist_ok=True)
        saved.write_bytes(header[:min(4096, offset + 256)])
        return {'path': str(path), 'sha256': sha(path), 'bytes': path.stat().st_size,
                'header': saved.relative_to(out).as_posix(), 'header_sha256': sha(saved)}

    def request(binary, stage, target, case, iteration=0):
        folder = out / stage / 'requests' / target / (case['name'] + '-' + str(iteration))
        folder.mkdir(parents=True)
        source, req, response = folder / 'input.cpp', folder / 'request.json', folder / 'response.json'
        source.write_bytes(case['source'].encode())
        assert sha(source) == case['source_sha256']
        req.write_text(json.dumps({'protocol': 1, 'profile': 'cpp-core-v2',
            'root': str(folder), 'source': str(source), 'target': target,
            'arguments': ['-std=c++17'], 'sdk': {'distribution_id': catalog['distribution_id'],
                                             'catalog_sha256': plan['sdk_catalog_sha256']}}) + '\n')
        cmd = run([binary, '__neverc_cpp_frontend', '--request', req, '--output', response],
                  stage + '/execution/' + target + '-' + case['name'] + '-' + str(iteration))
        actual = json.loads(response.read_text()) if response.exists() else None
        codes = [item['code'] for item in actual['diagnostics']] if actual else []
        if stage == 'baseline':
            passed = cmd['returncode'] in [-1073741819, 3221225477] and actual is None
        elif case['expected'] == 'accept':
            passed = cmd['returncode'] == 0 and actual is not None and not codes
        else:
            passed = cmd['returncode'] != 0 and actual is not None and bool(codes) and set(codes) <= {'TR0201', 'TR0203'}
        matching = [path for path in (out / 'invocations').glob(str(cmd['pid']) + '-*')
                    if (path / 'request.json').is_file() and sha(path / 'request.json') == sha(req)]
        assert len(matching) == 1
        invocation = matching[0]
        (invocation / 'parent-observed-returncode.txt').write_text(str(cmd['returncode']) + '\n')
        row = {'stage': stage, 'target': target, 'name': case['name'], 'iteration': iteration,
               'expected': 'access-violation' if stage == 'baseline' else case['expected'],
               'source_sha256': sha(source), 'request': req.relative_to(out).as_posix(),
               'request_sha256': sha(req), 'response': response.relative_to(out).as_posix() if actual else None,
               'response_sha256': sha(response) if actual else None, 'returncode': cmd['returncode'],
               'pid': cmd['pid'], 'invocation': invocation.relative_to(out).as_posix(),
               'diagnostic_codes': codes, 'passed': passed}
        if stage == 'current' and case['name'] == plan['optional_case']['name'] and passed:
            row['actual_record_layout'] = helper.optional_layout(actual)
            row['passed'] = row['actual_record_layout']['matches'] and (
                row['actual_record_layout']['tail_carrier'] == target.endswith('-pc-windows-msvc'))
        r['requests'].append(row)
        save()

    save()
    try:
        assert os.environ.get('GITHUB_ACTIONS') == 'true' and platform.system() == 'Windows'
        assert platform.machine().lower() in ['arm64', 'aarch64'] and platform.python_version() == '3.12.10'
        assert os.environ['VSCMD_ARG_TGT_ARCH'].lower() == 'arm64'
        r.update(revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                 plan=plan, source_sha256={name: sha(root / name) for name in plan['source_paths']})
        frontend = root / 'neverc/lib/Translate/Cpp/Frontend'
        assert {name: sha(frontend / name) for name in plan['current_private_input_sha256']} == plan['current_private_input_sha256']
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == plan['sdk_catalog_sha256']
        catalog = json.loads((sdk / 'catalog.json').read_text())
        entries = catalog['headers'] + catalog['metadata']
        assert len(entries) == 665
        for entry in entries:
            assert sha(sdk / entry['root'] / entry['path']) == entry['sha256']
        r['sdk_files_verified'] = 665
        version = run(['cl'], 'environment/msvc-version')
        assert version['returncode'] in [0, 2]
        assert re.search(r'Version 19\.51\.', (out / version['log']).read_text(errors='replace'))
        producer = api('actions/runs/' + str(plan['producer_run_id']))
        assert producer['head_sha'] == plan['producer_commit'] and producer['status'] == 'completed'
        assert producer['conclusion'] == 'failure' and producer['run_attempt'] == plan['producer_run_attempt']
        producer_job = api('actions/jobs/' + str(plan['producer_job_id']))
        assert producer_job['head_sha'] == plan['producer_commit'] and producer_job['status'] == 'completed'
        assert producer_job['run_id'] == plan['producer_run_id'] and producer_job['conclusion'] == 'failure'
        for artifact in [plan['producer_small_artifact'], plan['producer_remote_artifact']]:
            metadata = api('actions/artifacts/' + str(artifact['id']))
            assert metadata['name'] == artifact['name'] and metadata['digest'] == artifact['digest']
            assert metadata['workflow_run']['id'] == plan['producer_run_id']
            assert metadata['workflow_run']['head_sha'] == plan['producer_commit'] and not metadata['expired']
        archive = build / 'certified-producer.zip'
        api('actions/artifacts/' + str(plan['producer_remote_artifact']['id']) + '/zip', archive)
        assert 'sha256:' + sha(archive) == plan['producer_remote_artifact']['digest']
        retained = build / 'producer'
        retained.mkdir()
        with zipfile.ZipFile(archive) as package:
            assert sum(info.file_size for info in package.infolist()) < 3 * 1024 ** 3
            for info in package.infolist():
                path = Path(info.filename)
                assert not path.is_absolute() and '..' not in path.parts
                assert path.parts[0] in ['baseline', 'current'] and not stat.S_ISLNK(info.external_attr >> 16)
                assert (retained / path).resolve().is_relative_to(retained.resolve())
            package.extractall(retained)
        r['producer_archive_sha256'] = sha(archive)
        callers = {}
        for stage in ['baseline', 'current']:
            saved = retained / stage
            info = json.loads((saved / 'producer.json').read_text())
            assert info == plan['producer_stages'][stage]
            library = saved / 'nevercCppFrontend.lib'
            assert sha(library) == info['archive_sha256']
            assert sha(saved / 'actual-private-frontend.exe') == info['actual_executable']['sha256']
            for name, digest in info['generated_header_sha256'].items():
                assert sha(saved / name) == digest
            folder = build / 'retained' / stage
            folder.mkdir(parents=True)
            caller = folder / 'actual-private-frontend.exe'
            command = run(['cl', '/nologo', '/std:c++17', '/EHsc', '/MT', '/O2',
                           '/I' + str(frontend), root / 'utils/ci/ArmCurrentFrontendDriver.cpp',
                           '/Fo' + str(folder / 'caller.obj'), '/Fe' + str(caller), library,
                           '/link', 'psapi.lib', 'shell32.lib', 'ole32.lib', 'uuid.lib',
                           'advapi32.lib', 'ws2_32.lib', 'ntdll.lib', 'version.lib'], stage + '/link-real-C-entry')
            assert command['returncode'] == 0 and sha(library) == info['archive_sha256']
            observed_version = run([caller, '__neverc_cpp_frontend', '--version'], stage + '/version')
            assert observed_version['returncode'] == 0
            assert (out / observed_version['log']).read_text().strip() == 'neverc-cpp-frontend 20.1.8 protocol 1 ' + info['build_id']
            r['stages'][stage] = {'producer': info, 'archive_sha256': sha(library),
                                  'actual_executable': pe(caller, stage)}
            callers[stage] = caller
            save()
        helper_path = root / 'utils/ci/check-arm-current-component.py'
        assert sha(helper_path) == plan['original_component_helper_sha256']
        spec = importlib.util.spec_from_file_location('pinned_optional_layout', helper_path)
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        for stage in ['baseline', 'current']:
            for iteration in range(3):
                request(callers[stage], stage, 'aarch64-pc-windows-msvc', plan['string_case'], iteration)
        for target in plan['target_abis']:
            for case in plan['pair_cases'] + [plan['optional_case']]:
                request(callers['current'], 'current', target, case)
        project = run([sys.executable, root / 'utils/translate-frontends/cpp/tests/project.py',
                       '--neverc', callers['current'], '--target', 'aarch64-pc-windows-msvc',
                       '--output-dir', out / 'project'], 'current/original-project-suite', 1800)
        r['original_project_suite_passed'] = project['returncode'] == 0
        assert sha(root / 'utils/translate-frontends/cpp/tests/project.py') == plan['original_project_script_sha256']
        r['actual_current_requests'] = sum(row['stage'] == 'current' for row in r['requests'])
        assert len(r['requests']) == 118 and r['actual_current_requests'] == 115
        r['passed'] = r['original_project_suite_passed'] and all(row['passed'] for row in r['requests'])
        r['completed'] = True
        save()
        return 0 if r['passed'] else 1
    except BaseException:
        r['failure'] = traceback.format_exc()
        save()
        print(r['failure'], flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
