"""Materialize verified inputs for Python documentation/registration checks."""
from __future__ import annotations

import ast
import hashlib
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import subprocess
from urllib.parse import unquote, urlsplit


CHECKS = ('utils/plugin-api/check-docs-links.py',
          'utils/plugin-api/check-docs-facts.py',
          'tests/neverc/NotFnRegisteredGTestsTests.py')


class GitInputs:
    def __init__(self, repo, commit, *, fetch_missing=False):
        self.repo = Path(repo).resolve()
        self.fetch_missing = fetch_missing
        self.env = dict(os.environ, GIT_NO_REPLACE_OBJECTS='1', GIT_NO_LAZY_FETCH='1', GIT_OPTIONAL_LOCKS='0')
        self.commit = self.git('rev-parse', '--verify', commit + '^{commit}').decode().strip()
        if not re.fullmatch('[0-9a-f]{40}', self.commit):
            raise ValueError('expected a SHA-1 Git commit')
        self.tree = self.git('rev-parse', self.commit + '^{tree}').decode().strip()
        self.entries = {}
        for row in self.git('ls-tree', '-r', '-z', self.commit).split(b'\0'):
            if not row:
                continue
            metadata, name = row.split(b'\t', 1)
            mode, kind, oid = metadata.decode().split()
            if kind == 'blob':
                self.entries[os.fsdecode(name)] = mode, oid

    def git(self, *args, data=None):
        return subprocess.check_output(['git', *args], cwd=self.repo, env=self.env, input=data)

    def blobs(self, names):
        names = sorted(names)
        wanted = sorted({self.entries[n][1] for n in names})
        availability = self.git('cat-file', '--batch-check', data=''.join(n + '\n' for n in wanted).encode())
        missing = [row.split()[0].decode() for row in availability.splitlines() if row.endswith(b' missing')]
        if missing:
            if not self.fetch_missing:
                raise ValueError('required Git blobs are missing; use --fetch-missing to fetch only selected inputs')
            self.git('fetch', '--quiet', '--no-tags', '--no-write-fetch-head', '--no-recurse-submodules',
                     '--filter=blob:none', '--refmap=', '--stdin', 'origin',
                     data=''.join(n + '\n' for n in missing).encode())
        for start in range(0, len(names), 64):
            batch = names[start:start + 64]
            raw = self.git('cat-file', '--batch', data=''.join(self.entries[n][1] + '\n' for n in batch).encode())
            offset = 0
            for name in batch:
                end = raw.index(b'\n', offset)
                header = raw[offset:end].split()
                mode, oid = self.entries[name]
                if len(header) != 3 or header[:2] != [oid.encode(), b'blob']:
                    raise ValueError('missing or substituted Git blob: ' + name)
                size = int(header[2])
                payload = raw[end + 1:end + 1 + size]
                offset = end + size + 2
                actual = hashlib.sha1(b'blob ' + str(size).encode() + b'\0' + payload).hexdigest()
                if len(payload) != size or raw[offset - 1:offset] != b'\n' or actual != oid:
                    raise ValueError('Git blob identity mismatch: ' + name)
                yield name, mode, oid, payload
            if offset != len(raw):
                raise ValueError('unexpected cat-file output')

    def docs_profile(self):
        prefixes = ('docs/', 'examples/', 'pluginsdk/', 'utils/plugin-api/',
                    'utils/translate-frontends/docs/', 'neverc/include/neverc/Plugin/')
        selected = {n for n in self.entries if n.startswith(prefixes) or '/' not in n and n.endswith('.md')}
        selected.update(CHECKS)
        selected.update(n for n in self.entries if n.startswith('utils/ci/not-fn-pointer-') and n.endswith('-formal-additions.cpp'))
        selected.update(('tests/neverc/TranslateTests.cpp', 'utils/ci/not_fn_registered_gtests.py',
                         'utils/ci/extract_cpp_gtests.py', 'utils/ci/cpp-pair-piecewise-formal-tests.json',
                         'utils/translate-frontends/cpp/README.md', 'utils/translate-prototype/README.md',
                         'neverc/lib/Translate/Cpp/SDK/README.md'))
        # Read the consumers' own source declarations rather than duplicating
        # their release-source/companion lists in this profile.
        for name, _, _, payload in self.blobs(CHECKS):
            for node in ast.walk(ast.parse(payload, filename=name)):
                if isinstance(node, ast.Constant) and isinstance(node.value, str) and node.value in self.entries:
                    selected.add(node.value)
        if not selected <= self.entries.keys():
            raise ValueError('missing declared checker inputs: ' + repr(sorted(selected - self.entries.keys())))
        return selected

    def materialize(self, destination, selected):
        destination = Path(destination).resolve()
        pending, written, manifest, directories = set(selected), set(), [], set()
        while pending:
            next_inputs = set()
            for name, mode, oid, payload in self.blobs(pending):
                relative = PurePosixPath(name)
                if relative.is_absolute() or '..' in relative.parts or mode not in ('100644', '100755', '120000'):
                    raise ValueError('unsafe Git fixture path: ' + name)
                target = destination / name
                if not target.resolve().is_relative_to(destination):
                    raise ValueError('fixture path escapes destination')
                target.parent.mkdir(parents=True, exist_ok=True)
                if mode == '120000':
                    link = os.fsdecode(payload)
                    if Path(link).is_absolute() or not (target.parent / link).resolve().is_relative_to(destination):
                        raise ValueError('unsafe Git fixture symlink')
                    target.symlink_to(link)
                    referred = posixpath.normpath(str(relative.parent / link))
                    if referred in self.entries:
                        next_inputs.add(referred)
                else:
                    target.write_bytes(payload)
                    target.chmod(0o755 if mode == '100755' else 0o644)
                manifest.append(dict(path=name, mode=mode, git_blob=oid, bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest()))
                written.add(name)
                if name.endswith('.md'):
                    text = payload.decode('utf-8')
                    links = re.findall(r'\]\(<?([^\s)>]+)|^\[[^\]]+\]:\s*<?([^\s>]+)', text, re.M)
                    for first, second in links:
                        url = urlsplit(first or second)
                        if url.scheme or url.netloc or not url.path or url.path.startswith('/'):
                            continue
                        linked = posixpath.normpath(str(relative.parent / unquote(url.path)))
                        if linked in self.entries:
                            next_inputs.add(linked)
                        elif not linked.startswith('../') and any(n.startswith(linked.rstrip('/') + '/') for n in self.entries):
                            directories.add(linked)
                    for cited in re.findall(r'[\w./-]+\.(?:c|h|cpp|py|json|inc|td|cmake|txt|nc|def|sh)\b', text):
                        if cited in self.entries:
                            next_inputs.add(cited)
            pending = next_inputs - written
        for directory in directories:
            target = destination / directory
            if not target.resolve().is_relative_to(destination):
                raise ValueError('linked directory escapes fixture')
            target.mkdir(parents=True, exist_ok=True)
        encoded = '\n'.join(row['path'] + '\0' + row['git_blob'] for row in sorted(manifest, key=lambda r: r['path'])).encode()
        return dict(commit=self.commit, tree=self.tree, git_blobs_verified=len(manifest),
                    bytes=sum(row['bytes'] for row in manifest), manifest_sha256=hashlib.sha256(encoded).hexdigest(),
                    git_backed_directories=sorted(directories), files=manifest)
