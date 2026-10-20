"""Reuse an authenticated real C-entry caller on Actions for eight SDK closures."""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import time
import traceback
import urllib.error
import urllib.request
import zipfile

PREFIX = 'neverc/lib/Translate/Cpp/Frontend/'
TARGETS = ['x86_64-apple-macosx15.0.0', 'arm64-apple-macosx15.0.0',
           'x86_64-unknown-linux-gnu', 'aarch64-unknown-linux-gnu',
           'i686-unknown-linux-gnu', 'x86_64-pc-windows-msvc',
           'aarch64-pc-windows-msvc', 'i686-pc-windows-msvc']


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, stream, code, message, headers, url):
        return None


def api(endpoint, output=None):
    query = urllib.request.Request('https://api.github.com/repos/NeverSight/NeverC/' + endpoint,
        headers={'Authorization': 'Bearer ' + os.environ['GITHUB_TOKEN'],
                 'Accept': 'application/vnd.github+json', 'Accept-Encoding': 'gzip',
                 'Cache-Control': 'no-cache', 'User-Agent': 'NeverC-actual-SDK-ABI-closure'})
    try:
        try:
            response = urllib.request.build_opener(NoRedirect).open(query, timeout=60)
        except urllib.error.HTTPError as error:
            if error.code != 302:
                raise
            location = error.headers['Location']
            assert location.startswith('https://')
            # Repository credentials never follow the signed download URL.
            response = urllib.request.urlopen(urllib.request.Request(location,
                headers={'User-Agent': 'NeverC-actual-SDK-ABI-closure'}), timeout=180)
        with response:
            if output is not None:
                with Path(output).open('wb') as stream:
                    while chunk := response.read(1024 * 1024):
                        stream.write(chunk)
                return
            data = response.read()
            if response.headers.get('Content-Encoding') == 'gzip':
                data = gzip.decompress(data)
            return json.loads(data)
    except (urllib.error.URLError, TimeoutError) as error:
        # No signed URLs or tokens are written to evidence or exception text.
        raise RuntimeError('Authenticated Actions artifact request failed: ' +
                           type(error).__name__ + ' ' + str(getattr(error, 'code', ''))) from None


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
    report = {'fixture_group': 'algorithm-sdk-abi-manifest', 'passed': False, 'completed': False,
              'commands': [], 'requests': [], 'private_component_recompiled': False,
              'host_neverc_rebuilt': False, 'c_entry_caller_relinked': False,
              'existing_authenticated_c_entry_caller_reused': True,
              'frontend_execution_timeout_seconds': 60, 'original_test_modified': False,
              'formal_test_executed': False, 'full_ci_passed': False,
              'published_to_dev': False, 'goal_complete': False}

    def save():
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    environment = dict(os.environ, NEVERC_CPP_PROTOCOL_EXECUTION_DIR=str(out / 'invocations'))

    def run(command, label):
        command = list(map(str, command))
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        with log.open('w') as stream:
            try:
                code = subprocess.run(command, cwd=root, env=environment,
                    stdout=stream, stderr=subprocess.STDOUT, timeout=60).returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1000
        item = {'command': command, 'returncode': code, 'timeout_seconds': 60,
                'cwd': str(root), 'log': str(log.relative_to(out)), 'log_sha256': sha(log),
                'elapsed_seconds': time.monotonic() - started}
        report['commands'].append(item)
        save()
        print(label, code, flush=True)
        return code

    def download(record, destination):
        actual = api('actions/artifacts/' + str(record['id']))
        assert actual['name'] == record['name'] and actual['digest'] == record['digest']
        assert actual['workflow_run']['head_sha'] == plan['producer_commit']
        assert actual['workflow_run']['id'] == plan['producer_run_id'] and not actual['expired']
        archive = build / (str(record['id']) + '.zip')
        api('actions/artifacts/' + str(record['id']) + '/zip', archive)
        assert actual['digest'] == 'sha256:' + sha(archive)
        destination.mkdir()
        with zipfile.ZipFile(archive) as z:
            for entry in z.infolist():
                path = Path(entry.filename)
                assert not path.is_absolute() and '..' not in path.parts
                assert not stat.S_ISLNK(entry.external_attr >> 16)
                assert (destination / path).resolve().is_relative_to(destination.resolve())
            z.extractall(destination)
        return {'id': record['id'], 'name': record['name'], 'digest': actual['digest'],
                'artifact_bytes': archive.stat().st_size, 'downloaded_on_actions_only': True}

    try:
        plan = json.loads(args.plan.read_text())
        assert plan['producer_commit'] == 'a55ea81deaea21d36b0576d506f2ec8fe20273f8'
        assert plan['producer_run_id'] == 39360350577
        assert plan['target_abis'] == TARGETS
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        report['plan'] = plan
        paths = ['utils/ci/check-algorithm-sdk-abi-manifest.py',
                 'utils/ci/cpp-algorithm-sdk-abi-plan.json', 'utils/ci/FrontendProtocolDriver.cpp',
                 'utils/ci/algorithm-standard-linux-sdk-reference.json',
                 'tests/neverc/TranslateTests.cpp', '.github/workflows/diagnose-cpp-template-source.yml']
        report['source_sha256'] = {name: sha(root / name) for name in paths}
        assert sha(root / 'utils/ci/FrontendProtocolDriver.cpp') == plan['producer_c_entry_source_sha256']
        assert sha(root / PREFIX / 'FrontendEntry.h') == plan['producer_c_entry_header_sha256']
        identities = plan['exact_private_input_sha256']
        assert len(identities) == 19
        for name, identity in identities.items():
            assert sha(root / PREFIX / name) == identity
        report['exact_private_input_sha256'] = identities
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == plan['sdk_catalog_sha256'] == '1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        catalog = json.loads((sdk / 'catalog.json').read_text())
        records = catalog['headers'] + catalog['metadata']
        assert len(records) == 665
        catalog_index = {}
        for entry in records:
            assert sha(sdk / entry['root'] / entry['path']) == entry['sha256']
            catalog_index[(entry['root'], entry['path'])] = entry['sha256']
        report['sdk_files_verified'] = 665
        producer_run = api('actions/runs/' + str(plan['producer_run_id']))
        assert producer_run['head_sha'] == plan['producer_commit']
        assert producer_run['name'] == 'cpp-template-source-diagnostics'
        report['producer_downloads'] = [download(plan['producer_small_artifact'], build / 'producer-evidence')]
        small = build / 'producer-evidence'
        reports = list(small.rglob('results.json'))
        assert len(reports) == 1
        producer_path = reports[0]
        producer = json.loads(producer_path.read_text())
        assert sha(producer_path) == plan['producer_report_sha256']
        assert producer['revision'] == plan['producer_commit']
        assert producer['private_frontend_rebuilt'] and not producer['neverc_host_compiler_rebuilt']
        assert producer['exact_private_input_sha256'] == identities and producer['sdk_files_verified'] == 665
        assert producer['sdk_catalog_sha256'] == plan['sdk_catalog_sha256']
        assert producer['source_sha256']['utils/ci/FrontendProtocolDriver.cpp'] == plan['producer_c_entry_source_sha256']
        for label in ['build/actual-private-component.log', 'build/private-tool-version.log',
                      'build/private-system-libraries.log', 'build/real-c-entry-caller.log',
                      'execution/real-version.log']:
            command = next(row for row in producer['commands'] if row['log'] == label)
            assert command['returncode'] == 0
            assert sha(producer_path.parent / label) == command['log_sha256']
        assert producer['actual_frontend_version'] == plan['expected_frontend_version']
        metadata = producer_path.parent / 'private-component-producer.json'
        assert sha(metadata) == plan['producer_manifest_sha256'] == producer['remote_component_producer_manifest_sha256']
        shutil.copyfile(metadata, out / 'authenticated-producer-metadata.json')
        binary_info = producer['actual_private_executable']
        assert binary_info['sha256'] == plan['producer_executable_sha256']
        assert binary_info['bytes'] == plan['producer_executable_bytes']
        shutil.copyfile(producer_path, out / 'authenticated-producer-report.json')
        report['producer_report_sha256'] = sha(producer_path)
        report['producer_downloads'].append(download(plan['producer_binary_artifact'], build / 'producer-binary'))
        binaries = [path for path in (build / 'producer-binary').rglob('*') if path.is_file()]
        assert len(binaries) == 1
        caller = binaries[0]
        assert sha(caller) == binary_info['sha256'] and caller.stat().st_size == binary_info['bytes']
        report['reused_executable_sha256'] = sha(caller)
        report['reused_executable_bytes'] = caller.stat().st_size
        data = caller.read_bytes()[:64]
        assert len(data) == 64 and data[:6] == b'\x7fELF\x02\x01' and data[18:20] == b'\x3e\x00'
        (out / 'reused-c-entry-elf-header.bin').write_bytes(data)
        report['reused_executable_header_sha256'] = sha(out / 'reused-c-entry-elf-header.bin')
        caller.chmod(0o755)
        assert run([caller, '__neverc_cpp_frontend', '--version'], 'reused-c-entry-version') == 0
        assert (out / 'reused-c-entry-version.log').read_text().strip() == plan['expected_frontend_version']
        original = (root / 'tests/neverc/TranslateTests.cpp').read_text()
        start = re.search(r'TEST_F\(\s*TranslateTest,\s*' + plan['original_test_name'] + r'\)\s*\{', original)
        assert start
        body = original[start.start():original.index('\nTEST_F(', start.end())]
        assert hashlib.sha256(body.encode()).hexdigest() == plan['original_test_body_sha256']
        program = re.search(r'writeFile\(Source, R"cpp\((.*?)\)cpp"\);', body, re.S).group(1)
        assert hashlib.sha256(program.encode()).hexdigest() == plan['original_program_sha256']
        reference = root / 'utils/ci/algorithm-standard-linux-sdk-reference.json'
        assert sha(reference) == plan['linux_reference_manifest_sha256']
        reference_rows = json.loads(reference.read_text())
        actual_sets = {}
        for target in TARGETS:
            directory = out / 'targets' / target
            directory.mkdir(parents=True)
            source, request, response = directory / 'input.cpp', directory / 'request.json', directory / 'response.json'
            source.write_text(program)
            request.write_text(json.dumps({'protocol': 1, 'profile': 'cpp-core-v2', 'root': str(directory),
                'source': str(source), 'target': target, 'arguments': ['-std=c++17'],
                'sdk': {'distribution_id': catalog['distribution_id'], 'catalog_sha256': plan['sdk_catalog_sha256']}}) + '\n')
            code = run([caller, '__neverc_cpp_frontend', '--request', request, '--output', response], 'execute-' + target)
            row = {'target': target, 'returncode': code, 'request': str(request.relative_to(out)),
                   'request_sha256': sha(request), 'source_sha256': sha(source),
                   'response': str(response.relative_to(out)) if response.is_file() else None,
                   'response_sha256': sha(response) if response.is_file() else None, 'passed': False}
            try:
                assert code == 0 and response.is_file()
                result = json.loads(response.read_text())
                assert result['diagnostics'] == [] and result['profile'] == 'cpp-core-v2'
                assert result['frontend']['version'] == '20.1.8'
                assert result['frontend']['build'] == plan['expected_frontend_version'].split()[-1]
                target_data = result['target']
                assert target_data['triple'] == target and target_data['int_bits'] == 32 and target_data['little_endian']
                assert target_data['pointer_bits'] == (32 if target.startswith('i686-') else 64)
                assert result['sdk_catalog_sha256'] == plan['sdk_catalog_sha256']
                rows = result['sdk_dependencies']
                keys = [(item['root'], item['path']) for item in rows]
                assert rows and len(keys) == len(set(keys))
                for item in rows:
                    assert item['root'] != 'platform' and catalog_index[(item['root'], item['path'])] == item['sha256']
                ordered = sorted(rows, key=lambda item: (item['root'], item['path']))
                if target == 'x86_64-unknown-linux-gnu':
                    assert ordered == reference_rows
                actual_sets[target] = ordered
                row.update(passed=True, dependency_count=len(rows), target_data=target_data)
            except (AssertionError, KeyError, ValueError):
                row['validation_failure'] = traceback.format_exc()
            report['requests'].append(row)
            save()
        report['invocation_inventory'] = [{'path': str(path.relative_to(out)), 'sha256': sha(path), 'bytes': path.stat().st_size}
            for path in sorted((out / 'invocations').rglob('*')) if path.is_file()]
        golden = out / 'algorithm-standard-sdk-dependencies-by-target.json'
        golden.write_text(json.dumps(actual_sets, indent=2) + '\n')
        report['target_manifest_sha256'] = sha(golden)
        report['successful_target_count'] = len(actual_sets)
        report.update(completed=True, passed=len(actual_sets) == 8 and all(row['passed'] for row in report['requests']))
        save()
        assert report['passed'], 'Actual target request rejected or closure differs; preserve every raw response and diagnose.'
        print('PASS8 actual unchanged-Sema SDK closures; original GTest repair and fullCI remain required.')
    except BaseException:
        report['failure'] = traceback.format_exc()
        save()
        raise


if __name__ == '__main__':
    main()
