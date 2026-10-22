#!/usr/bin/env python3
"""Download the exact existing ARM64 compiler for native SDK boundary validation."""

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
        'User-Agent': 'NeverC-piecewise-baseline',
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
            headers={'User-Agent': 'NeverC-piecewise-baseline'}), timeout=180)
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
    assert run['name'] == 'windows-arm64-neverc-build' and run['status'] in ('in_progress', 'completed')
    artifacts = request(f'actions/runs/{args.run_id}/artifacts?per_page=100', token)['artifacts']
    matches = [a for a in artifacts if a['name'] == 'windows-arm64-neverc']
    assert len(matches) == 1 and not matches[0]['expired']
    artifact = matches[0]
    assert artifact['id'] == 12224282690
    assert artifact['digest'] == 'sha256:0d28178df7b5731e38f726db279d2367ef2669e83b32e7978253b0219bcdbb3e'
    outer = root / 'compiler-artifact.zip'
    request(f"actions/artifacts/{artifact['id']}/zip", token, outer)
    outer_sha = sha(outer)
    assert artifact['digest'] == 'sha256:' + outer_sha
    with zipfile.ZipFile(outer) as source:
        assert source.namelist() == ['windows-arm64-neverc.zip']
        package = root / 'windows-arm64-neverc.zip'
        with source.open(source.namelist()[0]) as stream, package.open('wb') as output:
            while chunk := stream.read(1024 * 1024):
                output.write(chunk)
    destination = root / 'unpacked'
    destination.mkdir()
    check_zip(package, destination)
    # Write regular files first. Create links only after all archive file writes,
    # and require each resolved link and parent to remain in this fresh folder.
    links = []
    with zipfile.ZipFile(package) as source:
        for entry in source.infolist():
            target = destination / entry.filename
            mode = entry.external_attr >> 16
            if stat.S_ISLNK(mode):
                links.append((target, source.read(entry).decode()))
                continue
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.open(entry) as data, target.open('xb') as output:
                while chunk := data.read(1024 * 1024):
                    output.write(chunk)
            target.chmod((mode & 0o777) or 0o644)
    for target, link in links:
        assert target.parent.resolve().is_relative_to(destination.resolve())
        target.parent.mkdir(parents=True, exist_ok=True)
        target.symlink_to(link)
    for target, _ in links:
        assert target.resolve().is_relative_to(destination.resolve())
    compiler = destination / 'install/bin/neverc.exe'
    assert compiler.is_file()
    data = compiler.read_bytes()
    assert data[:2] == b'MZ'
    offset = int.from_bytes(data[60:64], 'little')
    assert data[offset:offset+6] == b'PE\0\0\x64\xaa'
    evidence = {'revision': args.revision, 'run_id': args.run_id,
                'run_url': run['html_url'], 'artifact_id': artifact['id'],
                'artifact_sha256': outer_sha, 'package_sha256': sha(package),
                'compiler': str(compiler), 'compiler_sha256': sha(compiler),
                'baseline_executed': False}
    (root / 'provenance.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('Downloaded exact-revision compiler baseline:', args.revision, flush=True)


if __name__ == '__main__':
    main()
