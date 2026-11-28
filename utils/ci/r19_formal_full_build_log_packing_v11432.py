"""Retain the exact build log within the existing bounded formal archive."""

import argparse
import base64
from contextlib import contextmanager
from copy import copy
from hashlib import sha256
import json
import os
from pathlib import Path
from tempfile import TemporaryDirectory
import zlib

from r19_formal_sdk_bundle_v10902 import FORMAL, CONTEXT, INDEX, stage


LOG = f"{FORMAL}/runtime-build/fresh-c23-stage1.log"
ARCHIVE_ROOT = f"{FORMAL}/compressed-build-log/"
MANIFEST = ARCHIVE_ROOT + "index.json"
CONTENTS = ARCHIVE_ROOT + "tail.txt"
MEMBER_LIMIT = 32 * 1024**2
EXTRA_LIMIT = 64 * 1024**2
TAIL_LIMIT = MEMBER_LIMIT
INVENTORY_LIMIT = 640 * 1024**2


def digest(data):
    return sha256(data).hexdigest()


def parts(raw, available):
    assert 0 < len(raw) <= MEMBER_LIMIT and available >= 0
    cutoff = len(raw) - min(len(raw), TAIL_LIMIT, available)
    while cutoff < len(raw) and (raw[cutoff] & 0xC0) == 0x80:
        cutoff += 1
    head, tail = raw[:cutoff], raw[cutoff:]
    return head, tail


def encode_build_log(raw, available):
    head, tail = parts(raw, available)
    body = base64.b64encode(zlib.compress(tail, level=9))
    manifest = dict(log_bytes=len(raw), prefix_bytes=len(head), tail_bytes=len(tail),
                    log_sha256=digest(raw), tail_sha256=digest(tail))
    assert len(body) <= MEMBER_LIMIT
    return head, body, (json.dumps(manifest, sort_keys=True) + "\n").encode()


def verified_log(head, encoded, index, available):
    assert len(index) <= 4096
    manifest = json.loads(index)
    assert manifest.keys() == {"log_bytes", "prefix_bytes", "tail_bytes", "log_sha256", "tail_sha256"}
    total, prefix, missing = (manifest[key] for key in ("log_bytes", "prefix_bytes", "tail_bytes"))
    assert all(type(value) is int for value in (total, prefix, missing))
    assert prefix == len(head) and 0 <= missing <= min(TAIL_LIMIT, available)
    assert 0 < total == prefix + missing <= MEMBER_LIMIT and 0 < len(encoded) <= MEMBER_LIMIT
    decoder = zlib.decompressobj()
    tail = decoder.decompress(base64.b64decode(encoded, validate=True), missing + 1)
    assert len(tail) == missing and decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail
    assert digest(tail) == manifest["tail_sha256"]
    raw = head + tail
    assert digest(raw) == manifest["log_sha256"]
    raw.decode()
    return raw


def response_extra_bytes(index):
    return sum(index["blocks"][key] for _, key in index["responses"].values())


def pack(formal, bootstrap, output, count):
    stage(formal, bootstrap, output, count)
    index = json.loads((output / FORMAL / CONTEXT / INDEX).read_bytes())
    remaining = EXTRA_LIMIT - response_extra_bytes(index)
    log = output / LOG
    original = log.read_bytes()
    head, encoded, manifest = encode_build_log(original, remaining)
    assert verified_log(head, encoded, manifest, remaining) == original
    (output / ARCHIVE_ROOT).mkdir()
    (output / MANIFEST).write_bytes(manifest)
    (output / CONTENTS).write_bytes(encoded)
    with TemporaryDirectory(prefix=".formal-build-log-", dir=log.parent) as folder:
        prefix = Path(folder) / log.name
        prefix.write_bytes(head)
        # The original packer hardlinks unchanged evidence from the runner.
        os.replace(prefix, log)
    assert (formal.parent / LOG).read_bytes() == original
    assert sum(path.stat().st_size for path in output.rglob("*") if path.is_file()) <= INVENTORY_LIMIT


class BuildLogArchive:
    def __init__(self, package, packed):
        self.logical = {entry.filename: entry for entry in packed.infolist()}
        controls = {path for path in self.logical if path.startswith(ARCHIVE_ROOT) and path != ARCHIVE_ROOT}
        assert controls == {CONTENTS, MANIFEST}
        remaining = EXTRA_LIMIT - response_extra_bytes(json.loads(package.read(f"{FORMAL}/{CONTEXT}/{INDEX}")))
        self.log = verified_log(packed.read(LOG), packed.read(CONTENTS), packed.read(MANIFEST), remaining)
        self.packed = packed
        self.logical = {path: entry for path, entry in self.logical.items() if not path.startswith(ARCHIVE_ROOT)}
        entry = copy(self.logical[LOG])
        entry.file_size = len(self.log)
        self.logical[LOG] = entry

    def infolist(self):
        return list(self.logical.values())

    def read(self, member):
        name = member if isinstance(member, str) else member.filename
        assert name in self.logical
        return self.log if name == LOG else self.packed.read(member)


def install(net, count):
    from r19_formal_sdk_bundle_workflow_v10904 import FormalArchive

    original = net["checked_zip"]

    @contextmanager
    def checked(archive):
        with original(archive) as package:
            packed = FormalArchive(package, count)
            yield BuildLogArchive(package, packed)

    net["checked_zip"] = checked


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("formal", type=Path)
    parser.add_argument("bootstrap", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("count", type=int)
    args = parser.parse_args()
    pack(args.formal, args.bootstrap, args.output, args.count)
