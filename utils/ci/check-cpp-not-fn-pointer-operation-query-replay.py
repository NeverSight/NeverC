"""Rebuild five bridge units against an authenticated unchanged private ABI."""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import os
import re
import shlex
import shutil
import signal
import stat
import subprocess
import time
import traceback
import urllib.error
import urllib.request
import zipfile

PREFIX = 'neverc/lib/Translate/Cpp/Frontend/'
BRIDGE = ['Frontend.cpp', 'Lowering.cpp', 'Project.cpp', 'MathSDK.cpp', 'RandomSDK.cpp']


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, stream, code, message, headers, url):
        return None


def api(endpoint, destination=None):
    request = urllib.request.Request('https://api.github.com/repos/NeverSight/NeverC/' + endpoint,
        headers={'Authorization': 'Bearer ' + os.environ['GITHUB_TOKEN'],
                 'Accept': 'application/vnd.github+json', 'Accept-Encoding': 'gzip',
                 'Cache-Control': 'no-cache', 'User-Agent': 'NeverC-private-bridge-source-replay'})
    try:
        try:
            response = urllib.request.build_opener(NoRedirect).open(request, timeout=45)
        except urllib.error.HTTPError as error:
            if error.code != 302:
                raise
            location = error.headers['Location']
            assert location.startswith('https://')
            response = urllib.request.urlopen(urllib.request.Request(location,
                headers={'User-Agent': 'NeverC-private-bridge-source-replay'}), timeout=180)
        with response:
            if destination:
                with destination.open('wb') as out:
                    while data := response.read(1024 * 1024):
                        out.write(data)
                return
            data = response.read()
            if response.headers.get('Content-Encoding') == 'gzip':
                data = gzip.decompress(data)
            return json.loads(data)
    except (urllib.error.URLError, TimeoutError) as error:
        raise RuntimeError('Authenticated producer artifact GET failed: ' + type(error).__name__ +
                           ' ' + str(getattr(error, 'code', ''))) from None


def members(path):
    """Read ordinary GNU/BSD ar member bytes without rewriting other members."""
    result = []
    with path.open('rb') as stream:
        assert stream.read(8) == b'!<arch>\n'
        strings = b''
        while header := stream.read(60):
            assert len(header) == 60 and header[-2:] == b'`\n'
            size = int(header[48:58])
            raw_name = header[:16].decode('ascii').strip()
            data = stream.read(size)
            assert len(data) == size
            if size % 2:
                assert stream.read(1) == b'\n'
            if raw_name == '//':
                strings = data
                continue
            if raw_name in ['/', '/SYM64/']:
                continue
            if raw_name.startswith('#1/'):
                length = int(raw_name[3:])
                name = data[:length].decode().rstrip('\0')
                data = data[length:]
            elif raw_name.startswith('/'):
                offset = int(raw_name[1:])
                assert offset < len(strings)
                end = strings.index(b'\n', offset)
                name = strings[offset:end].rstrip(b'/').decode()
            else:
                name = raw_name.rstrip('/')
            assert name.endswith(('.o', '.obj'))
            result.append({'name': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    assert result
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    args = parser.parse_args()
    root, out, build = args.repo.resolve(), args.output.resolve(), args.build.resolve()
    out.mkdir(parents=True)
    build.mkdir(parents=True)
    report = {'fixture_group': 'not-fn-pointer-operation-query-replay', 'passed': False, 'completed': False,
        'commands': [], 'requests': [], 'upstream_dependency_rebuilt': False,
        'host_neverc_rebuilt': False, 'all_five_bridge_units_recompiled': False,
        'frontend_execution_timeout_seconds': 60, 'private_feature_fixed': False,
        'formal_test_executed': False, 'full_ci_passed': False,
        'published_to_dev': False, 'goal_complete': False}

    def save():
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(argv, label, timeout=60, allowed_failure=False):
        argv = list(map(str, argv))
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        with log.open('w') as stream:
            process = subprocess.Popen(argv, cwd=root, env=environment,
                stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT; OWNED PROCESS GROUP STOPPED\n')
                code = -1000
        row = {'command': argv, 'cwd': str(root), 'returncode': code, 'timeout_seconds': timeout,
            'elapsed_seconds': time.monotonic() - started, 'log': str(log.relative_to(out)), 'log_sha256': sha(log)}
        report['commands'].append(row)
        save()
        print(label, code, flush=True)
        assert allowed_failure or code == 0, (label, code)
        return code, log

    def download(record, destination):
        actual = api('actions/artifacts/' + str(record['id']))
        assert actual['digest'] == record['digest'] and actual['name'] == record['name']
        assert actual['workflow_run']['id'] == plan['producer_run_id']
        assert actual['workflow_run']['head_sha'] == plan['producer_commit'] and not actual['expired']
        archive = build / (str(record['id']) + '.zip')
        api('actions/artifacts/' + str(record['id']) + '/zip', archive)
        assert actual['digest'] == 'sha256:' + sha(archive)
        destination.mkdir()
        with zipfile.ZipFile(archive) as source:
            for entry in source.infolist():
                path = Path(entry.filename)
                assert not path.is_absolute() and '..' not in path.parts
                assert not stat.S_ISLNK(entry.external_attr >> 16)
                assert (destination / path).resolve().is_relative_to(destination.resolve())
            source.extractall(destination)
        report.setdefault('producer_downloads', []).append(record)
        save()

    environment = dict(os.environ, NEVERC_CPP_PROTOCOL_EXECUTION_DIR=str(out / 'invocations'))
    environment.pop('NEVERC_CPP_PAIR_SOURCE_TRACE', None)
    save()
    try:
        plan = json.loads(args.plan.read_text())
        report['plan'] = plan
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        report['source_sha256'] = {name: sha(root / name) for name in plan['candidate_source_paths']}
        inputs = plan['producer_private_input_sha256']
        assert len(inputs) == 19
        current = {name: sha(root / PREFIX / name) for name in inputs}
        changed = sorted(name for name in current if current[name] != inputs[name])
        assert changed == plan['allowed_bridge_source_changes'] == ['Frontend.cpp', 'Frontend.h', 'MathSDK.cpp']
        report['exact_private_input_sha256'] = current
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == plan['sdk_catalog_sha256'] == '1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        catalog = json.loads((sdk / 'catalog.json').read_text())
        records = catalog['headers'] + catalog['metadata']
        assert len(records) == 665
        for row in records:
            assert sha(sdk / row['root'] / row['path']) == row['sha256']
        report['sdk_files_verified'] = 665
        producer_run = api('actions/runs/' + str(plan['producer_run_id']))
        producer_job = api('actions/jobs/' + str(plan['producer_actual_job']))
        assert producer_run['head_sha'] == producer_job['head_sha'] == plan['producer_commit']
        assert producer_run['run_attempt'] == producer_job['run_attempt'] == plan['producer_attempt']
        assert producer_job['run_id'] == plan['producer_run_id']
        assert producer_run['status'] == producer_job['status'] == 'completed'
        assert producer_run['conclusion'] == producer_job['conclusion'] == plan['producer_expected_run_conclusion'] == 'failure'
        report['actual_producer_run'] = {key: producer_run[key] for key in ['id','head_sha','run_attempt','status','conclusion']}
        report['actual_producer_job'] = {key: producer_job[key] for key in ['id','run_id','head_sha','run_attempt','status','conclusion','steps']}
        download(plan['producer_small_artifact'], build / 'evidence')
        reports = list((build / 'evidence').rglob('results.json'))
        assert len(reports) == 1
        prior_path = reports[0]
        prior = json.loads(prior_path.read_text())
        assert sha(prior_path) == plan['producer_report_sha256']
        assert prior['revision'] == plan['producer_commit'] and prior['exact_private_input_sha256'] == inputs
        producer = prior_path.parent / 'private-component-producer.json'
        assert sha(producer) == plan['producer_manifest_sha256']
        producer_data = json.loads(producer.read_text())
        shutil.copyfile(producer, out / 'authenticated-producer-metadata.json')
        download(plan['producer_components_artifact'], build / 'component')
        archives = list((build / 'component').rglob('libnevercCppFrontend.a'))
        assert len(archives) == 1 and sha(archives[0]) == producer_data['actual_private_archive_sha256']
        original_archive = archives[0]
        private = original_archive.parents[2]
        original_private = Path(prior['actual_private_bridge_compile_commands'][0]['directory'])
        original_root = Path(prior['actual_private_bridge_compile_commands'][0]['file']).parents[5]
        # Actions uses the same checkout and runner-temp layout. Preserve every
        # producer compile argument exactly; unexpected layouts fail closed.
        assert root == original_root
        required_suffix = Path('_deps/neverc-cpp-build')
        assert str(private).endswith(str(required_suffix))
        assert private.relative_to(build / 'component') == required_suffix
        restored = original_private.parent.parent
        assert restored.is_relative_to(Path(os.environ['RUNNER_TEMP'])) and not restored.exists()
        shutil.move(str(build / 'component'), restored)
        private = original_private
        original_archive = private / 'lib/Release/libnevercCppFrontend.a'
        assert sha(original_archive) == producer_data['actual_private_archive_sha256']
        prefix = private / 'NeverCCppPrivatePrefix.h'
        assert sha(prefix) == producer_data['actual_prefix_sha256']
        generated = restored / 'private-component-reproducer'
        for name, identity in producer_data['generated_header_sha256'].items():
            assert sha(generated / name) == identity
        assert sha(generated / 'producer.json') == plan['producer_manifest_sha256']
        _, version_log = run(['/usr/bin/clang++-22', '--version'], 'environment/clang')
        assert sha(version_log) == producer_data['host_compiler_version_log_sha256']
        cmake = (root / PREFIX / 'CMakeLists.txt').read_text()
        ordered = re.search(r'foreach\(_input (.*?)\)\n  file\(SHA256', cmake, re.S).group(1).split()
        assert set(ordered) == set(inputs)
        identity = 'builtin:llvm20.1.8:' + producer_data['llvm_source_archive_sha256']
        identity += ''.join(':' + current[name] for name in ordered)
        identity += ':' + plan['sdk_catalog_sha256'] + ':' + sha(sdk / 'embed.py')
        build_id = 'cpp-frontend-1-' + hashlib.sha256(identity.encode()).hexdigest()
        assert build_id != prior['actual_frontend_version'].split()[-1]
        include = private / 'tools/neverc_cpp'
        include.mkdir(parents=True)
        header = (root / PREFIX / 'BuildID.h.in').read_bytes().replace(
            b'@NEVERC_CPP_BUILD_ID@', build_id.removeprefix('cpp-frontend-1-').encode())
        (include / 'BuildID.h').write_bytes(header)
        shutil.copyfile(generated / 'BuiltinCppSdkData.h', include / 'BuiltinCppSdkData.h')
        report['new_build_id'] = build_id
        report['new_build_id_header_sha256'] = sha(include / 'BuildID.h')
        report['original_bridge_compile_commands'] = prior['actual_private_bridge_compile_commands']
        old = members(original_archive)
        replacements = []
        for name in BRIDGE:
            entry = next(row for row in prior['actual_private_bridge_compile_commands'] if Path(row['file']).name == name)
            argv = shlex.split(entry['command'])
            output = private / entry['output']
            output.parent.mkdir(parents=True, exist_ok=True)
            assert argv[0] == '/usr/bin/clang++-22' and argv[-2:] == ['-c', str(root / PREFIX / name)]
            assert argv[argv.index('-o') + 1] == entry['output'] and not output.exists()
            # Relative producer outputs are resolved in its original working
            # directory; all flags and include paths are byte-for-byte retained.
            absolute = [str(private / item) if index and argv[index - 1] in ['-o', '-MF'] else item
                        for index, item in enumerate(argv)]
            run(absolute, 'compile/' + name, 900)
            assert output.is_file()
            replacements.append(output)
        report['all_five_bridge_units_recompiled'] = True
        report['bridge_objects'] = [{'name': path.name, 'sha256': sha(path), 'bytes': path.stat().st_size} for path in replacements]
        archive = private / 'lib/Release/libnevercCppFrontend-replay.a'
        shutil.copyfile(original_archive, archive)
        for path in replacements:
            assert len([row for row in old if row['name'] == path.name]) == 1
        run(['/usr/bin/llvm-ar-22', 'r', archive, *replacements], 'archive/replace-five-members', 180)
        new = members(archive)
        assert [row['name'] for row in old] == [row['name'] for row in new]
        object_index = {path.name: sha(path) for path in replacements}
        for before, after in zip(old, new):
            if before['name'] in object_index:
                assert after['sha256'] == object_index[after['name']]
            else:
                assert before == after
        ledger = out / 'archive-member-replacement.json'
        ledger.write_text(json.dumps({'old': old, 'new': new}, indent=2) + '\n')
        report['member_replacement_ledger_sha256'] = sha(ledger)
        report['replayed_archive_sha256'] = sha(archive)
        tests = ['IsolateMathCoffSymbolsTests.py', 'VerifyArchiveMembersTests.py',
                 'AuditArchiveTests.py', 'MsvcRuntimeSymbolsTests.py', 'CoffWeakAliasesTests.py']
        for name in tests:
            mode = '-I' if name == 'VerifyArchiveMembersTests.py' else '-E'
            run(['python3', mode, '-B', root / PREFIX / name], 'audit/' + name, 120)
        run(['python3', '-E', '-B', root / 'tests/neverc/HostCoffSymbolsTests.py'], 'audit/HostCoffSymbolsTests.py', 120)
        run(['python3', '-I', '-B', root / PREFIX / 'VerifyArchiveMembers.py',
             '--archive', archive, '--archiver', '/usr/bin/llvm-ar-22', '--kind', 'ar'], 'audit/actual-members', 120)
        nm = private / 'bin/llvm-nm'
        nm.chmod(0o755)
        run(['python3', '-E', '-B', root / PREFIX / 'AuditArchive.py',
             '--nm', nm, '--archive', archive, '--prefix-header', prefix], 'audit/actual-private-ABI', 600)
        prior_commands = {row['log']: row for row in prior['commands']}
        system = prior_path.parent / 'build/private-system-libraries.log'
        assert sha(system) == prior_commands['build/private-system-libraries.log']['log_sha256']
        assert prior_commands['build/private-system-libraries.log']['command'][1:] == ['--system-libs', '--link-static', 'support']
        report['authenticated_system_libraries'] = system.read_text()
        report['authenticated_system_libraries_sha256'] = sha(system)
        executable = restored / 'private-frontend-source-replay'
        run(['/usr/bin/clang++-22', '-std=c++17', '-O2', '-fno-fast-math', '-ffp-contract=off',
             root / 'utils/ci/FrontendProtocolDriver.cpp', '-I' + str(root / PREFIX),
             '-Wl,--start-group', archive, '-Wl,--end-group', *shlex.split(system.read_text()), '-o', executable], 'link/real-C-entry')
        raw = executable.read_bytes()
        assert raw[:6] == b'\x7fELF\x02\x01' and raw[18:20] == b'\x3e\x00'
        (out / 'actual-replayed-entry-header.bin').write_bytes(raw[:64])
        report['new_executable_sha256'] = sha(executable)
        report['new_executable_bytes'] = len(raw)
        _, version = run([executable, '__neverc_cpp_frontend', '--version'], 'execute/version')
        assert version.read_text().strip() == 'neverc-cpp-frontend 20.1.8 protocol 1 ' + build_id
        catalog_rows = {(row['root'], row['path']): row['sha256'] for row in records}
        for target in plan['target_abis']:
            for case in plan['cases']:
                name = case['name']
                directory = out / 'requests' / target / name
                directory.mkdir(parents=True)
                source, request, response = directory / 'input.cpp', directory / 'request.json', directory / 'response.json'
                source.write_text(case['source'])
                assert sha(source) == case['source_sha256']
                request.write_text(json.dumps({'protocol': 1, 'profile': 'cpp-core-v2', 'root': str(directory),
                    'source': str(source), 'target': target, 'arguments': ['-std=c++17'],
                    'sdk': {'distribution_id': catalog['distribution_id'], 'catalog_sha256': plan['sdk_catalog_sha256']}}) + '\n')
                code, log = run([executable, '__neverc_cpp_frontend', '--request', request, '--output', response],
                    'execute/' + target + '/' + name, allowed_failure=True)
                result = json.loads(response.read_text()) if response.exists() else None
                valid_response = bool(result and result.get('protocol') == 1 and result.get('profile') == 'cpp-core-v2'
                    and result.get('frontend', {}).get('build') == build_id)
                diagnostics = result.get('diagnostics') if result else None
                if case['expected'] == 'accept':
                    satisfied = bool(valid_response and code == 0 and diagnostics == []
                        and result.get('target', {}).get('triple') == target
                        and result.get('sdk_catalog_sha256') == plan['sdk_catalog_sha256'])
                    if satisfied:
                        dependencies = result.get('sdk_dependencies')
                        satisfied = bool(isinstance(dependencies, list) and (dependencies or case.get('sdk_dependencies_may_be_empty') is True))
                        if satisfied:
                            satisfied = all(catalog_rows.get((entry['root'], entry['path'])) == entry['sha256']
                                            for entry in dependencies)
                elif case['expected'] == 'invalid':
                    satisfied = bool(valid_response and code == 1 and diagnostics
                        and any(row.get('code') == 'TR0202' for row in diagnostics)
                        and not any(field in result for field in ['functions', 'globals', 'records']))
                else:
                    assert case['expected'] == 'reject'
                    satisfied = bool(valid_response and code == 1 and diagnostics
                        and all(row.get('code') in ['TR0201', 'TR0203'] for row in diagnostics)
                        and not any(field in result for field in ['functions', 'globals', 'records']))
                report['requests'].append({'name': name, 'target': target, 'expected': case['expected'],
                    'returncode': code, 'source_sha256': sha(source), 'response_sha256': sha(response) if response.exists() else None,
                    'diagnostics': diagnostics, 'expectation_satisfied': satisfied,
                    'execution_log_sha256': sha(log)})
                save()
        report['invocation_inventory'] = [{'path': str(path.relative_to(out)), 'sha256': sha(path), 'bytes': path.stat().st_size}
            for path in sorted((out / 'invocations').rglob('*')) if path.is_file()]
        assert len(report['requests']) == 5 and len(report['invocation_inventory']) == 25
        passed = all(row['expectation_satisfied'] for row in report['requests'])
        report.update(completed=True, evidence_authenticated=True, passed=False, not_fn_private_support_complete=False, scope='readonly_default_and_call_source_diagnosis',
                      focused_request_contract_passed=passed,
                      failure=None if passed else 'Actual request expectations failed; retain every original response for diagnosis.')
        save()
        return 1  # Reproducing source refusals is diagnosis, not implemented support.
    except BaseException:
        report['failure'] = traceback.format_exc()
        save()
        raise


if __name__ == '__main__':
    raise SystemExit(main())
