"""Reuse only an authenticated successful component and its exact audited archive."""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import os
import re
import shutil
import stat
import urllib.error
import urllib.request
import zipfile


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
                 'Cache-Control': 'no-cache', 'User-Agent': 'NeverC-real-private-runtime'})
    try:
        try:
            response = urllib.request.build_opener(NoRedirect).open(request, timeout=60)
        except urllib.error.HTTPError as error:
            if error.code != 302:
                raise
            location = error.headers['Location']
            assert location.startswith('https://')
            response = urllib.request.urlopen(urllib.request.Request(location,
                headers={'User-Agent': 'NeverC-real-private-runtime'}), timeout=180)
        with response:
            if destination is not None:
                with Path(destination).open('wb') as stream:
                    while chunk := response.read(1024 * 1024):
                        stream.write(chunk)
                return
            data = response.read()
            if response.headers.get('Content-Encoding') == 'gzip':
                data = gzip.decompress(data)
            return json.loads(data)
    except (urllib.error.URLError, TimeoutError) as error:
        raise RuntimeError('Authenticated component download failed: ' + type(error).__name__ +
                           ' ' + str(getattr(error, 'code', ''))) from None


def main():
    parser = argparse.ArgumentParser()
    for name in ['repo', 'plan', 'output', 'evidence']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    assert os.environ.get('GITHUB_ACTIONS') == 'true'
    root, out = args.repo.resolve(), args.output.resolve()
    out.mkdir(parents=True)
    args.evidence.mkdir(parents=True, exist_ok=True)
    plan = json.loads(args.plan.read_text())
    run = api('actions/runs/' + str(plan['component_run_id']))
    assert run['head_sha'] == plan['component_commit']
    assert run['status'] == 'completed' and run['conclusion'] == 'success'
    actual_jobs = api('actions/runs/' + str(run['id']) + '/attempts/' + str(run['run_attempt']) + '/jobs?per_page=100')
    selected = [j for j in actual_jobs['jobs'] if j['name'] == 'inspect-not-fn-pointer-clean-200']
    assert len(selected) == 1
    job = api('actions/jobs/' + str(selected[0]['id']))
    assert job['head_sha'] == plan['component_commit'] and job['status'] == 'completed' and job['conclusion'] == 'success'
    assert job['id'] == plan['component_actual_job'] and run['run_attempt'] == plan['component_attempt']
    for name, expected in [('Observe actual residual not_fn defaults and operator source proofs', 'success'),
                           ('Retain actual current private archive remotely for source diagnosis', 'success')]:
        assert any(step['name'] == name and step['conclusion'] == expected for step in job['steps'])
    downloads = []

    def download(record, label):
        actual = api('actions/artifacts/' + str(record['id']))
        assert actual['name'] == record['name'] and actual['digest'] == record['digest']
        assert actual['workflow_run']['id'] == plan['component_run_id']
        assert actual['workflow_run']['head_sha'] == plan['component_commit'] and not actual['expired']
        archive = out / (label + '.zip')
        api('actions/artifacts/' + str(record['id']) + '/zip', archive)
        assert actual['digest'] == 'sha256:' + sha(archive)
        folder = out / label
        folder.mkdir()
        with zipfile.ZipFile(archive) as z:
            for entry in z.infolist():
                path = Path(entry.filename)
                assert not path.is_absolute() and '..' not in path.parts
                assert not stat.S_ISLNK(entry.external_attr >> 16)
                assert (folder / path).resolve().is_relative_to(folder.resolve())
            z.extractall(folder)
        downloads.append({'id': record['id'], 'name': record['name'], 'digest': actual['digest'],
                          'artifact_bytes': archive.stat().st_size, 'actions_only': True})
        return folder

    small = download(plan['component_small_artifact'], 'component-evidence')
    reports = list(small.rglob('results.json'))
    assert len(reports) == 1
    report = reports[0]
    assert sha(report) == plan['component_report_sha256']
    r = json.loads(report.read_text())
    assert r['revision'] == plan['component_commit'] and r['completed'] and r['passed'] is True and r['not_fn_private_support_complete'] is False
    assert r['all_five_bridge_units_recompiled'] and r['focused_request_contract_passed']
    assert len(r['requests']) == 200 and all(row['expectation_satisfied'] for row in r['requests'])
    assert sum(row['expected'] == 'accept' for row in r['requests']) == 48
    assert sum(row['expected'] == 'reject' for row in r['requests']) == 144
    assert sum(row['expected'] == 'invalid' for row in r['requests']) == 8
    assert len(r['exact_private_input_sha256']) == 19
    for name, identity in r['exact_private_input_sha256'].items():
        assert identity == plan['exact_private_input_sha256'][name]
        assert sha(root / 'neverc/lib/Translate/Cpp/Frontend' / name) == identity
    assert r['sdk_files_verified'] == 665 and r['new_build_id'] == plan['frontend_build_id']
    assert r['replayed_archive_sha256'] == plan['private_archive_sha256']
    folder = download(plan['component_archive_artifact'], 'component-archive')
    archives = [path for path in folder.rglob('*') if path.is_file()]
    assert len(archives) == 1 and archives[0].name == 'libnevercCppFrontend-replay.a'
    archive = archives[0]
    assert sha(archive) == plan['private_archive_sha256']
    assert archive.open('rb').read(8) == b'!<arch>\n'
    # The ABI prefix belongs to the original authenticated dependency producer.
    # Extract only that exact header on Actions; neither the large producer
    # archive nor its binaries enter the small evidence artifact.
    prefix_record = plan['dependency_producer_components_artifact']
    actual = api('actions/artifacts/' + str(prefix_record['id']))
    assert actual['name'] == prefix_record['name'] and actual['digest'] == prefix_record['digest']
    assert actual['workflow_run']['id'] == plan['dependency_producer_run_id']
    assert actual['workflow_run']['head_sha'] == plan['dependency_producer_commit'] and not actual['expired']
    prefix_zip = out / 'dependency-producer-prefix.zip'
    api('actions/artifacts/' + str(prefix_record['id']) + '/zip', prefix_zip)
    assert actual['digest'] == 'sha256:' + sha(prefix_zip)
    prefix = out / 'NeverCCppPrivatePrefix.h'
    with zipfile.ZipFile(prefix_zip) as z:
        names = [name for name in z.namelist() if Path(name).name == prefix.name]
        assert len(names) == 1 and z.getinfo(names[0]).file_size < 8 * 1024 * 1024
        prefix.write_bytes(z.read(names[0]))
    assert sha(prefix) == plan['private_prefix_sha256']
    downloads.append({'id': actual['id'], 'name': actual['name'], 'digest': actual['digest'],
                      'artifact_bytes': prefix_zip.stat().st_size, 'actions_only': True,
                      'only_authenticated_prefix_extracted': True})
    shutil.copyfile(prefix, args.evidence / prefix.name)
    shutil.copyfile(report, args.evidence / 'authenticated-component-results.json')
    provenance = {'component_commit': plan['component_commit'], 'component_run_id': run['id'],
        'actual_component_job': job['id'], 'actual_component_attempt': run['run_attempt'],
        'component_report': str(report), 'component_report_sha256': sha(report),
        'private_archive': str(archive), 'private_archive_sha256': sha(archive),
        'private_prefix': str(prefix), 'private_prefix_sha256': sha(prefix),
        'downloads': downloads, 'all_binaries_remote_only': True, 'compiler_rebuilt': False,
        'formal_xml_executed': False, 'full_ci_passed': False, 'goal_complete': False}
    (out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    (args.evidence / 'component-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print('Authenticated actual successful current private archive; no NeverC rebuild.', flush=True)


if __name__ == '__main__':
    main()
