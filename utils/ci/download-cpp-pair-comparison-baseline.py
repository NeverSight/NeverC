#!/usr/bin/env python3
"""Download an exact prior CI compiler for comparison regression baselines."""

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import urllib.error
import urllib.request
import zipfile


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, stream, code, message, headers, url):
        return None


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def request(endpoint, token, output=None):
    url = 'https://api.github.com/repos/NeverSight/NeverC/' + endpoint
    query = urllib.request.Request(url, headers={
        'Authorization': 'Bearer ' + token,
        'Accept': 'application/vnd.github+json',
        'Accept-Encoding': 'gzip',
        'User-Agent': 'NeverC-comparison-baseline',
    })
    try:
        response = urllib.request.build_opener(NoRedirect).open(query, timeout=60)
    except urllib.error.HTTPError as error:
        if error.code != 302:
            raise
        location = error.headers['Location']
        assert location.startswith('https://')
        # Do not forward repository credentials to the signed blob URL.
        response = urllib.request.urlopen(urllib.request.Request(location,
            headers={'User-Agent': 'NeverC-comparison-baseline'}), timeout=180)
    with response:
        if output is not None:
            with output.open('wb') as stream:
                while chunk := response.read(1024 * 1024):
                    stream.write(chunk)
            return
        body = response.read()
        if response.headers.get('Content-Encoding') == 'gzip':
            body = gzip.decompress(body)
        return json.loads(body)


def check_zip(archive, destination):
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            relative = Path(entry.filename)
            assert not relative.is_absolute() and '..' not in relative.parts
            target = (destination / relative).resolve()
            assert target.is_relative_to(destination.resolve())
            if stat.S_ISLNK(entry.external_attr >> 16):
                link = Path(source.read(entry).decode())
                assert not link.is_absolute()
                assert (target.parent / link).resolve().is_relative_to(destination.resolve())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-id', type=int, required=True)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert re.fullmatch('[0-9a-f]{40}', args.revision)
    token = os.environ['GITHUB_TOKEN']
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    run = request(f'actions/runs/{args.run_id}', token)
    assert run['head_sha'] == args.revision
    assert run['name'] == 'macos-arm64-neverc-build' and run['status'] in ('in_progress', 'completed')
    artifacts = request(f'actions/runs/{args.run_id}/artifacts?per_page=100', token)['artifacts']
    matches = [a for a in artifacts if a['name'] == 'macos-arm64-neverc-compiler']
    assert len(matches) == 1 and not matches[0]['expired']
    artifact = matches[0]
    outer = root / 'compiler-artifact.zip'
    request(f"actions/artifacts/{artifact['id']}/zip", token, outer)
    outer_sha = sha(outer)
    assert artifact['digest'] == 'sha256:' + outer_sha
    with zipfile.ZipFile(outer) as source:
        assert source.namelist() == ['macos-arm64-neverc-compiler.zip']
        package = root / 'macos-arm64-neverc-compiler.zip'
        with source.open(source.namelist()[0]) as stream, package.open('wb') as output:
            while chunk := stream.read(1024 * 1024):
                output.write(chunk)
    destination = root / 'unpacked'
    destination.mkdir()
    check_zip(package, destination)
    subprocess.run(['/usr/bin/ditto', '-x', '-k', str(package), str(destination)], check=True)
    compiler = destination / 'install/bin/neverc'
    assert compiler.is_file() and os.access(compiler, os.X_OK)
    evidence = {'revision': args.revision, 'run_id': args.run_id,
                'run_url': run['html_url'], 'artifact_id': artifact['id'],
                'artifact_sha256': outer_sha, 'package_sha256': sha(package),
                'compiler': str(compiler), 'compiler_sha256': sha(compiler),
                'baseline_executed': False}
    (root / 'provenance.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('Downloaded exact-revision compiler baseline:', args.revision, flush=True)


if __name__ == '__main__':
    main()
