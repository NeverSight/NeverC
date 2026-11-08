"""Owned local scratch space and bounded, content-addressed diagnostic receipts.

Only this store's directories are collected. A shared kernel lock represents
each live reader; explicit pins represent pending readers between processes.
There are no PID-age heuristics or lists of protected task directory names.
"""
from __future__ import annotations

from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import time
import uuid


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def atomic_json(path, value):
    path = Path(path)
    fd, temporary = tempfile.mkstemp(prefix='.' + path.name + '-', dir=path.parent)
    try:
        with os.fdopen(fd, 'w') as stream:
            json.dump(value, stream, sort_keys=True, indent=2)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def read_json(path):
    return json.loads(Path(path).read_bytes())


class ArtifactStore:
    def __init__(self, root, *, max_receipts=16, max_bytes=256 * 1024**2):
        if max_receipts < 1 or max_bytes < 1:
            raise ValueError('retention limits must be positive')
        self.root = Path(root).absolute()
        if self.root.is_symlink():
            raise ValueError('artifact root must not be a symlink')
        marker = self.root / '.neverc-local-artifacts.json'
        if self.root.is_dir() and not marker.exists() and set(self.root.iterdir()) - {self.root / '.lock'}:
            raise ValueError('refusing to adopt a nonempty directory')
        self.root.mkdir(parents=True, exist_ok=True)
        self.max_receipts, self.max_bytes = max_receipts, max_bytes
        with self.lock():
            marker = self.root / '.neverc-local-artifacts.json'
            if marker.exists():
                if read_json(marker) != {'format': 1}:
                    raise ValueError('unrecognized artifact store')
            else:
                if set(self.root.iterdir()) - {self.root / '.lock'}:
                    raise ValueError('refusing to adopt a nonempty directory')
                atomic_json(marker, {'format': 1})
            for name in ('runs', 'pins', 'objects', 'receipts'):
                path = self.root / name
                if path.is_symlink():
                    raise ValueError('store namespace must not be a symlink')
                path.mkdir(exist_ok=True)
        self.collect()

    @contextmanager
    def lock(self):
        with (self.root / '.lock').open('a+b') as stream:
            fcntl.flock(stream, fcntl.LOCK_EX)
            yield

    def _pin_name(self, owner):
        if not owner:
            raise ValueError('a pending reader needs a stable owner name')
        return self.root / 'pins' / (hashlib.sha256(owner.encode()).hexdigest() + '.json')

    def pin(self, owner, run_id=None, receipt=None):
        """Replace one pending reader's references; unpin when it consumes them."""
        if run_id is None and receipt is None:
            raise ValueError('a pending reader must reference a run or receipt')
        with self.lock():
            if run_id is not None and not re.fullmatch('[0-9a-f]{32}', run_id):
                raise ValueError('invalid run identity')
            if receipt is not None and not re.fullmatch('[0-9a-f]{64}\.json', receipt):
                raise ValueError('invalid receipt identity')
            if run_id is not None and not (self.root / 'runs' / run_id / 'owner.json').is_file():
                raise ValueError('run does not exist')
            if receipt is not None and not (self.root / 'receipts' / receipt).is_file():
                raise ValueError('receipt does not exist')
            atomic_json(self._pin_name(owner), dict(owner=owner, run=run_id, receipt=receipt))

    def unpin(self, owner):
        with self.lock():
            self._pin_name(owner).unlink(missing_ok=True)
        return self.collect()

    @contextmanager
    def reader(self, run_id):
        """Acquire before releasing a producer or a pending-reader pin."""
        if not re.fullmatch('[0-9a-f]{32}', run_id):
            raise ValueError('invalid run identity')
        with self.lock():
            path = self.root / 'runs' / run_id
            stream = (path / '.lease').open('rb')
            fcntl.flock(stream, fcntl.LOCK_SH)
        try:
            yield path
        finally:
            stream.close()
            self.collect()

    @contextmanager
    def attempt(self, purpose):
        self.collect()
        run_id = uuid.uuid4().hex
        with self.lock():
            path = self.root / 'runs' / run_id
            path.mkdir()
            stream = (path / '.lease').open('w+b')
            fcntl.flock(stream, fcntl.LOCK_SH)
            atomic_json(path / 'owner.json', dict(format=1, id=run_id, purpose=purpose))
        try:
            yield Attempt(self, run_id, path, stream.fileno())
        finally:
            try:
                atomic_json(path / 'owner.json', dict(format=1, id=run_id, purpose=purpose, closed=True))
            finally:
                stream.close()
                self.collect()

    def publish(self, key, result, files=(), *, pending_owner=None):
        """Keep a verified receipt, deduplicating bytes; evict closed old receipts.

        Pin a receipt for an asynchronous consumer. Pins and live leases are
        never evicted to satisfy the cache budget. Historical immutable Git
        inputs should be recorded by commit/object ID, not copied here.
        """
        with self.lock():
            entries = []
            for name, source in files:
                source = Path(source)
                sha = digest(source)
                destination = self.root / 'objects' / sha
                if not destination.exists():
                    temporary = self.root / 'objects' / ('.' + uuid.uuid4().hex)
                    try:
                        shutil.copyfile(source, temporary)
                        if digest(temporary) != sha:
                            raise ValueError('diagnostic changed while being retained')
                        os.replace(temporary, destination)
                    finally:
                        temporary.unlink(missing_ok=True)
                if digest(destination) != sha:
                    raise ValueError('corrupt diagnostic object')
                entries.append(dict(name=name, sha256=sha, bytes=destination.stat().st_size))
            value = dict(format=1, key=key, updated_ns=time.time_ns(), result=result, files=entries)
            encoded = (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()
            if pending_owner is None and len(encoded) + sum(r['bytes'] for r in {r['sha256']: r for r in entries}.values()) > self.max_bytes:
                raise ValueError('receipt exceeds retention budget; specify a real pending reader to retain it')
            identity = hashlib.sha256(encoded).hexdigest() + '.json'
            atomic_json(self.root / 'receipts' / identity, value)
            if pending_owner is not None:
                atomic_json(self._pin_name(pending_owner), dict(owner=pending_owner, run=None, receipt=identity))
        self.collect()
        return identity

    def receipt(self, identity):
        """Authenticate the complete receipt and each diagnostic on consumption."""
        if not re.fullmatch('[0-9a-f]{64}\.json', identity):
            raise ValueError('invalid receipt identity')
        with self.lock():
            path = self.root / 'receipts' / identity
            if digest(path) + '.json' != identity:
                raise ValueError('corrupt receipt')
            value = read_json(path)
            for row in value['files']:
                source = self.root / 'objects' / row['sha256']
                if source.stat().st_size != row['bytes'] or digest(source) != row['sha256']:
                    raise ValueError('corrupt diagnostic object')
            return value

    def collect(self):
        removed, recovered = [], []
        with self.lock():
            pins = [read_json(p) for p in (self.root / 'pins').glob('*.json')]
            held_runs = {p.get('run') for p in pins}
            held_receipts = {p.get('receipt') for p in pins}
            for path in (self.root / 'runs').iterdir():
                if path.is_symlink() or not path.is_dir() or not re.fullmatch('[0-9a-f]{32}', path.name):
                    continue
                owner = path / 'owner.json'
                if not owner.is_file() or read_json(owner).get('id') != path.name or path.name in held_runs:
                    continue
                with (path / '.lease').open('rb') as stream:
                    try:
                        fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    except BlockingIOError:
                        continue
                    state = read_json(owner)
                    if not state.get('closed'):
                        # Uncatchable interruption: retain a compact, explicitly
                        # incomplete diagnostic before reclaiming its scratch.
                        files, source_log = [], path / 'console.log'
                        original = None
                        if source_log.is_file() and not source_log.is_symlink():
                            size, original_sha = source_log.stat().st_size, digest(source_log)
                            with source_log.open('rb') as log:
                                log.seek(max(0, size - 65536))
                                payload = log.read()
                            sha = hashlib.sha256(payload).hexdigest()
                            obj = self.root / 'objects' / sha
                            obj.write_bytes(payload)
                            if digest(obj) != sha:
                                raise ValueError('orphan diagnostic retention failed')
                            files.append(dict(name='interrupted-console.log', sha256=sha, bytes=len(payload)))
                            original = dict(sha256=original_sha, bytes=size, retained_tail_only=size > len(payload))
                        value = dict(format=1, key=state['purpose'] + ':interrupted', updated_ns=time.time_ns(),
                                     result=dict(run=path.name, completed=False, orphan_recovered=True, original_log=original), files=files)
                        identity = hashlib.sha256((json.dumps(value, sort_keys=True, indent=2) + '\n').encode()).hexdigest() + '.json'
                        atomic_json(self.root / 'receipts' / identity, value)
                        recovered.append(identity)
                    # shutil.rmtree does not traverse directory symlinks.
                    shutil.rmtree(path)
                    removed.append(path.name)
            receipts = sorted((p for p in (self.root / 'receipts').glob('*.json')),
                              key=lambda p: read_json(p)['updated_ns'], reverse=True)
            count, used, closed_count, closed_used, live_objects = 0, 0, 0, 0, set()
            # Pending readers win over the retention budget, regardless of age.
            receipts.sort(key=lambda p: p.name not in held_receipts)
            for path in receipts:
                value = read_json(path)
                objects = {row['sha256']: row['bytes'] for row in value['files']}
                extra = path.stat().st_size + sum(size for sha, size in objects.items() if sha not in live_objects)
                pinned = path.name in held_receipts
                if pinned or (closed_count < self.max_receipts and closed_used + extra <= self.max_bytes):
                    count += 1
                    used += extra
                    if not pinned:
                        closed_count += 1
                        closed_used += extra
                    live_objects.update(objects)
                else:
                    path.unlink()
            for path in (self.root / 'objects').iterdir():
                if path.is_file() and not path.is_symlink() and (
                    path.name.startswith('.') or re.fullmatch('[0-9a-f]{64}', path.name) and path.name not in live_objects
                ):
                    path.unlink()
        return dict(retired_runs=removed, retained_receipts=count, retained_bytes=used,
                    pending_readers=len(pins), recovered_orphan_receipts=recovered)


class Attempt:
    def __init__(self, store, identity, path, lease_fd):
        self.store, self.id, self.path, self.lease_fd = store, identity, path, lease_fd

    def directory(self, name):
        if not re.fullmatch('[a-zA-Z0-9_.-]+', name) or name in ('.', '..'):
            raise ValueError('scratch directory needs one safe path component')
        path = self.path / name
        path.mkdir()
        return path
