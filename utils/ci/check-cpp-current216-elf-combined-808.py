"""Actions-only original combined regression using an authenticated unchanged ELF."""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import os
import platform
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
    assert os.environ.get('GITHUB_ACTIONS') == 'true'
    assert (platform.system(), platform.machine(), platform.python_version()) == ('Linux', 'x86_64', '3.12.10')
    out.mkdir(parents=True, exist_ok=True)
    build.mkdir(parents=True, exist_ok=True)
    plan = json.loads(args.plan.read_text())
    result = dict(completed=False, passed=False, commands=[], requests=[], invocation_inventory=[],
                  source_component_rebuilt=False, host_neverc_rebuilt=False, debugger_used=False,
                  actual_frontend_timeout_seconds=60, native_generated_programs_executed=False,
                  formal_xml=0, full_ci_passed=False, not_fn_private_support_complete=False,
                  published_to_dev=False, goal_complete=False, all_binaries_remote_only=True)

    def save():
        temporary = out / 'results.tmp'
        temporary.write_text(json.dumps(result, indent=2) + '\n')
        temporary.replace(out / 'results.json')

    def api(endpoint, destination=None):
        request = urllib.request.Request('https://api.github.com/repos/NeverSight/NeverC/' + endpoint,
            headers={'Authorization': 'Bearer ' + os.environ['GITHUB_TOKEN'],
                     'Accept': 'application/vnd.github+json', 'Accept-Encoding': 'gzip',
                     'Cache-Control': 'no-cache', 'User-Agent': 'NeverC-authenticated-ELF-regression'})

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
                            total = 0
                            while chunk := response.read(1048576):
                                total += len(chunk)
                                assert total <= 256 * 1024 ** 2
                                stream.write(chunk)
                        return
                    raw = response.read(16 * 1024 ** 2 + 1)
                    assert len(raw) <= 16 * 1024 ** 2
                    if response.headers.get('Content-Encoding') == 'gzip':
                        raw = gzip.decompress(raw)
                    return json.loads(raw)
            except (urllib.error.URLError, TimeoutError) as error:
                result.setdefault('read_only_get_failures', []).append(
                    dict(attempt=attempt, kind=type(error).__name__, status=getattr(error, 'code', None)))
                save()
        raise RuntimeError('Bounded authenticated producer GET failed; no test rerun or dispatch retry.') from None

    def run(argv, label):
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        environment = dict(os.environ, NEVERC_CPP_PROTOCOL_EXECUTION_DIR=str(out / 'invocations'))
        environment.pop('GITHUB_TOKEN', None)
        environment.pop('GH_TOKEN', None)
        started = time.monotonic()
        with log.open('wb') as stream:
            process = subprocess.Popen(list(map(str, argv)), cwd=root, env=environment,
                                       stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=10)
                stream.write(b'\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1000
        row = dict(command=list(map(str, argv)), pid=process.pid, cwd=str(root),
                   returncode=code, timeout_seconds=60, elapsed_seconds=time.monotonic() - started,
                   log=log.relative_to(out).as_posix(), log_sha256=sha(log))
        result['commands'].append(row)
        save()
        print(label, code, flush=True)
        return row

    save()
    try:
        result.update(revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                      plan=plan, source_sha256={name: sha(root / name) for name in plan['source_paths']})
        prefix = 'neverc/lib/Translate/Cpp/Frontend/'
        assert len(plan['exact_private_input_sha256']) == 19
        for name, identity in plan['exact_private_input_sha256'].items():
            assert sha(root / prefix / name) == identity
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == plan['sdk_catalog_sha256']
        catalog = json.loads((sdk / 'catalog.json').read_text())
        entries = catalog['headers'] + catalog['metadata']
        assert len(entries) == 665
        for entry in entries:
            assert sha(sdk / entry['root'] / entry['path']) == entry['sha256']
        dependencies = {(e['root'], e['path']): e['sha256'] for e in entries}
        result['sdk_files_verified'] = 665
        producer = api('actions/runs/' + str(plan['producer_run_id']))
        job = api('actions/jobs/' + str(plan['producer_job_id']))
        assert producer['head_sha'] == job['head_sha'] == plan['producer_commit']
        assert producer['status'] == job['status'] == 'completed'
        assert producer['conclusion'] == job['conclusion'] == 'success'
        assert producer['run_attempt'] == job['run_attempt'] == plan['producer_attempt']
        assert job['run_id'] == producer['id']
        for name in ['Observe actual residual not_fn defaults and operator source proofs',
                     'Keep new actual source binary remotely for further diagnosis']:
            assert any(s['name'] == name and s['conclusion'] == 'success' for s in job['steps'])
        provenance = out / 'producer'
        provenance.mkdir()
        (provenance / 'run.json').write_text(json.dumps(producer, indent=2) + '\n')
        (provenance / 'job.json').write_text(json.dumps(job, indent=2) + '\n')
        folders = {}
        for kind in ['small', 'binary']:
            expected = plan['producer_' + kind + '_artifact']
            metadata = api('actions/artifacts/' + str(expected['id']))
            assert all(metadata[key] == expected[key] for key in ['id', 'name', 'digest'])
            assert metadata['workflow_run']['id'] == producer['id']
            assert metadata['workflow_run']['head_sha'] == plan['producer_commit'] and not metadata['expired']
            (provenance / (kind + '-artifact.json')).write_text(json.dumps(metadata, indent=2) + '\n')
            archive = build / (kind + '.zip')
            api('actions/artifacts/' + str(expected['id']) + '/zip', archive)
            assert 'sha256:' + sha(archive) == expected['digest']
            destination = build / kind
            destination.mkdir()
            with zipfile.ZipFile(archive) as package:
                infos = package.infolist()
                assert len(infos) <= 20000 and sum(i.file_size for i in infos) < 256 * 1024 ** 2
                assert len({i.filename for i in infos}) == len(infos)
                for info in infos:
                    path = Path(info.filename)
                    assert not path.is_absolute() and '..' not in path.parts
                    assert not stat.S_ISLNK(info.external_attr >> 16)
                    assert (destination / path).resolve().is_relative_to(destination.resolve())
                    if kind == 'binary':
                        assert info.filename == 'private-frontend-source-replay'
                package.extractall(destination)
            folders[kind] = destination
            result[kind + '_zip_sha256'] = sha(archive)
        reports = list(folders['small'].rglob('results.json'))
        assert len(reports) == 1 and sha(reports[0]) == plan['producer_report_sha256']
        previous = json.loads(reports[0].read_text())
        assert previous['revision'] == plan['producer_commit'] and previous['completed'] and previous['passed']
        assert previous['focused_request_contract_passed'] and len(previous['requests']) == 216
        assert all(r['expectation_satisfied'] for r in previous['requests'])
        assert previous['exact_private_input_sha256'] == plan['exact_private_input_sha256']
        assert previous['new_build_id'] == plan['frontend_build_id']
        assert previous['sdk_files_verified'] == 665 and previous['frontend_execution_timeout_seconds'] == 60
        assert previous['all_five_bridge_units_recompiled'] and not previous['upstream_dependency_rebuilt']
        (provenance / 'authenticated-report.json').write_bytes(reports[0].read_bytes())
        binary = folders['binary'] / 'private-frontend-source-replay'
        assert sha(binary) == previous['new_executable_sha256'] == plan['executable_sha256']
        assert binary.stat().st_size == previous['new_executable_bytes'] == plan['executable_bytes']
        binary.chmod(0o755)
        with binary.open('rb') as stream:
            header = stream.read(64)
        assert header[:6] == b'\x7fELF\x02\x01' and header[18:20] == b'\x3e\x00'
        (out / 'actual-reused-entry-header.bin').write_bytes(header)
        result.update(executable_sha256=sha(binary), executable_bytes=binary.stat().st_size,
                      reused_producer_five_compile_and_archive_abi_lineage=True)
        version = run([binary, '__neverc_cpp_frontend', '--version'], 'environment/version')
        assert version['returncode'] == 0
        assert (out / version['log']).read_text().strip() == 'neverc-cpp-frontend 20.1.8 protocol 1 ' + plan['frontend_build_id']
        assert len(plan['cases']) == 101 and len(plan['target_abis']) == 8
        assert len({c['name'] for c in plan['cases']}) == 101
        for target in plan['target_abis']:
            for case in plan['cases']:
                directory = out / 'requests' / target / case['name']
                directory.mkdir(parents=True)
                source = directory / 'input.cpp'
                source.write_bytes(case['source'].encode())
                assert sha(source) == case['source_sha256']
                request, response = directory / 'request.json', directory / 'response.json'
                request.write_text(json.dumps(dict(protocol=1, profile='cpp-core-v2', root=str(directory),
                    source=str(source), target=target, arguments=['-std=c++17'],
                    sdk=dict(distribution_id=catalog['distribution_id'], catalog_sha256=plan['sdk_catalog_sha256']))) + '\n')
                command = run([binary, '__neverc_cpp_frontend', '--request', request, '--response', response],
                              'execute/' + target + '/' + case['name'])
                actual = json.loads(response.read_text()) if response.exists() else None
                valid = False
                if actual is not None:
                    assert actual['protocol'] == 1 and actual['profile'] == 'cpp-core-v2'
                    assert actual['frontend'] == dict(build=plan['frontend_build_id'], name='neverc-cpp-frontend', version='20.1.8')
                    if case['expected'] == 'accept':
                        valid = command['returncode'] == 0 and actual['diagnostics'] == []
                        if valid:
                            assert actual['target']['triple'] == target
                            assert actual['target']['pointer_bits'] == (32 if target.startswith('i686-') else 64)
                            assert actual['target']['int_bits'] == 32 and actual['target']['little_endian'] is True
                            assert actual['sdk_catalog_sha256'] == plan['sdk_catalog_sha256']
                            assert actual['sdk_dependencies'] or case.get('sdk_dependencies_may_be_empty') is True
                            for entry in actual['sdk_dependencies']:
                                assert dependencies[(entry['root'], entry['path'])] == entry['sha256']
                    elif case['expected'] == 'reject':
                        valid = command['returncode'] == 1 and bool(actual['diagnostics']) and all(
                            v['code'] in ['TR0201', 'TR0203'] for v in actual['diagnostics']) and not any(
                            k in actual for k in ['functions', 'records', 'globals'])
                    else:
                        assert case['expected'] == 'invalid'
                        valid = command['returncode'] == 1 and any(v['code'] == 'TR0202' for v in actual['diagnostics']) and not any(
                            k in actual for k in ['functions', 'records', 'globals'])
                result['requests'].append(dict(target=target, name=case['name'], expected=case['expected'],
                    returncode=command['returncode'], source_sha256=sha(source), request_sha256=sha(request),
                    response_sha256=sha(response) if response.exists() else None,
                    diagnostics=actual['diagnostics'] if actual else None, expectation_satisfied=bool(valid),
                    execution_log=command['log'], execution_log_sha256=command['log_sha256'], pid=command['pid']))
                save()
        invocation_root = out / 'invocations'
        processes = list(invocation_root.iterdir())
        assert len(processes) == 808 and all(p.is_dir() and p.name.isdigit() for p in processes)
        commands = {row['pid']: row for row in result['commands'][1:]}
        assert len(commands) == 808
        for process in processes:
            command = commands[int(process.name)]
            argv = (process / 'argv.txt').read_text().splitlines()
            assert argv == command['command']
            assert (process / 'request.json').read_bytes() == Path(argv[3]).read_bytes()
            assert (process / 'response.json').read_bytes() == Path(argv[5]).read_bytes()
            assert int((process / 'returncode.txt').read_text()) == command['returncode']
            assert len(list(process.iterdir())) == 5 and (process / 'environment-before.tsv').is_file()
            for path in sorted(process.iterdir()):
                result['invocation_inventory'].append(dict(path=path.relative_to(out).as_posix(), bytes=path.stat().st_size, sha256=sha(path)))
        assert len(result['requests']) == 808 and len(result['invocation_inventory']) == 4040
        result.update(completed=True, passed=all(row['expectation_satisfied'] for row in result['requests']))
    except Exception:
        result['failure'] = traceback.format_exc()
    save()
    print(json.dumps(dict(completed=result['completed'], passed=result['passed'], requests=len(result['requests']))), flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
