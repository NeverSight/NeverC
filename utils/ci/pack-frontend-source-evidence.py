"""Pack exact invocation response mirrors while preserving their content and paths."""

import argparse
from collections import Counter
from copy import copy
from hashlib import file_digest, sha256
import json
import os
from pathlib import Path, PurePosixPath
import re
from tempfile import TemporaryDirectory

ROOT = "cpp-bind-call-arguments-clean456"
INDEX = "invocation-response-aliases-v1.json"
MEMBER_LIMIT = 64 * 1024**2
ARCHIVE_LIMIT = 128 * 1024**2


def checked_path(value):
    assert isinstance(value, str) and "\\" not in value
    path = PurePosixPath(value)
    assert not path.is_absolute() and ".." not in path.parts and path.as_posix() == value
    return path


def canonical_path(value):
    path = checked_path(value)
    assert len(path.parts) == 4 and path.parts[0] == "requests" and path.name == "response.json"
    return path


def invocation_path(value):
    path = checked_path(value)
    assert len(path.parts) == 3 and path.parts[0] == "invocations" and path.name == "response.json"
    assert re.fullmatch(r"[1-9][0-9]*", path.parts[1])
    return path


def digest(path):
    assert path.is_file() and not path.is_symlink() and path.stat().st_size <= MEMBER_LIMIT
    with path.open("rb") as stream:
        return file_digest(stream, "sha256").hexdigest()


def request_digests(report):
    data = json.loads(report)
    assert data["passed"] and data["completed"]
    keys = {}
    for row in data["requests"]:
        path = canonical_path(f"requests/{row['target']}/{row['name']}/response.json").as_posix()
        identity = row["response_sha256"]
        assert path not in keys and row["expectation_satisfied"] and re.fullmatch(r"[0-9a-f]{64}", identity)
        keys[path] = identity
    assert keys
    return keys


def aliases(index, report, count):
    assert len(index) <= MEMBER_LIMIT and len(report) <= MEMBER_LIMIT
    data = json.loads(index)
    assert set(data) == {"version", "report_sha256", "requests", "aliases"}
    assert data["version"] == 1 and data["report_sha256"] == sha256(report).hexdigest()
    canonical = request_digests(report)
    assert type(data["requests"]) is int and data["requests"] == len(canonical) == count
    assert isinstance(data["aliases"], list) and len(data["aliases"]) == count
    names = {}
    for row in data["aliases"]:
        assert set(row) == {"invocation", "canonical", "size", "sha256"}
        dest = invocation_path(row["invocation"]).as_posix()
        source = canonical_path(row["canonical"]).as_posix()
        assert dest not in names and source in canonical and row["sha256"] == canonical[source]
        assert type(row["size"]) is int and 0 < row["size"] <= MEMBER_LIMIT
        names[dest] = row
    assert Counter(row["sha256"] for row in names.values()) == Counter(canonical.values())
    return names


def same_file_bytes(left, right):
    with left.open("rb") as a, right.open("rb") as b:
        while True:
            first, second = a.read(1024 * 1024), b.read(1024 * 1024)
            if first != second:
                return False
            if not first:
                return True


def build_index(root, expected):
    report = (root / "results.json").read_bytes()
    canonical = request_digests(report)
    assert len(canonical) == expected
    real = {}
    for name, identity in sorted(canonical.items()):
        source = root / name
        size = source.stat().st_size
        assert digest(source) == identity
        real.setdefault((size, identity), name)
    entries = []
    for path in sorted(root.glob("invocations/*/response.json")):
        position = path.relative_to(root).as_posix()
        invocation_path(position)
        size, identity = path.stat().st_size, digest(path)
        source = real[(size, identity)]
        assert same_file_bytes(path, root / source)
        entries.append(dict(invocation=position, canonical=source, size=size, sha256=identity))
    body = (json.dumps(dict(version=1, report_sha256=sha256(report).hexdigest(),
                           requests=expected, aliases=entries), sort_keys=True) + "\n").encode()
    return body, aliases(body, report, expected)


def pack(root, bootstrap, output, count):
    root, bootstrap, output = (p.resolve() for p in (root, bootstrap, output))
    assert root.name == ROOT and bootstrap.name == ROOT + "-bootstrap"
    assert not output.exists() and not output.is_relative_to(root) and not output.is_relative_to(bootstrap)
    report = root / "results.json"
    ready = report.is_file() and json.loads(report.read_bytes()).get("passed") is True
    index, removed = build_index(root, count) if ready else (None, {})
    assert root.is_dir() and bootstrap.is_dir()
    with TemporaryDirectory(prefix=".source-evidence-", dir=output.parent) as staging:
        bundle = Path(staging) / "bundle"
        bundle.mkdir()
        linked = 0
        for tree in (root, bootstrap):
            for source in sorted(tree.rglob("*")):
                relative = source.relative_to(tree).as_posix()
                assert not source.is_symlink()
                if source.is_dir() or (tree == root and relative in removed):
                    continue
                assert source.is_file()
                dest = bundle / tree.name / relative
                dest.parent.mkdir(parents=True, exist_ok=True)
                os.link(source, dest)
                linked += 1
        if index is not None:
            assert linked + len(removed) == count * 9 + 26
            with (bundle / ROOT / INDEX).open("xb") as f:
                f.write(index)
        os.rename(bundle, output)
    print(json.dumps(dict(requests=count, retained_files=linked, exact_response_aliases=len(removed))))


def restore_response_aliases(root, count):
    """Restore original logical files in an isolated, already checked extraction."""
    manifest = root / INDEX
    if not manifest.is_file():
        return 0
    assert not manifest.is_symlink()
    index = aliases(manifest.read_bytes(), (root / "results.json").read_bytes(), count)
    pairs = []
    checked = {}
    root = root.resolve()
    for dest, row in index.items():
        source, target = root / row["canonical"], root / dest
        assert source.resolve().is_relative_to(root) and target.resolve().is_relative_to(root)
        assert target.parent.is_dir() and not target.exists() and not target.is_symlink()
        wanted = (row["size"], row["sha256"])
        if source not in checked:
            checked[source] = (source.stat().st_size, digest(source))
        assert checked[source] == wanted
        pairs.append((source, target))
    for source, target in pairs:
        os.link(source, target)
    manifest.unlink()
    return len(pairs)


class LogicalSourceArchive:
    """Expose the original ZIP member paths over a verified packed transport."""

    def __init__(self, archive, count):
        self.archive = archive
        self.members = {row.filename: row for row in archive.infolist()}
        assert len(self.members) == len(archive.infolist())
        self.mirrors = {}
        for name, member in self.members.items():
            path = checked_path(name)
            assert path.parts[0] in (ROOT, ROOT + "-bootstrap") and len(path.parts) >= 2
            assert not member.is_dir() and not (member.flag_bits & 1)
            assert member.file_size <= MEMBER_LIMIT
        manifest = self.members.pop(f"{ROOT}/{INDEX}", None)
        if manifest is None:
            assert len(self.members) == count * 9 + 26
            return
        names = aliases(archive.read(manifest), archive.read(f"{ROOT}/results.json"), count)
        assert len(self.members) + len(names) == count * 9 + 26
        for name, row in names.items():
            source = self.members[f"{ROOT}/{row['canonical']}"]
            dest = f"{ROOT}/{name}"
            assert dest not in self.members and source.file_size == row["size"]
            member = copy(source)
            member.filename = member.orig_filename = dest
            self.members[dest] = member
            self.mirrors[dest] = (source, row["sha256"])

    def infolist(self):
        return list(self.members.values())

    def read(self, member):
        name = member if isinstance(member, str) else member.filename
        if name in self.mirrors:
            source, identity = self.mirrors[name]
            data = self.archive.read(source)
            assert len(data) == source.file_size and sha256(data).hexdigest() == identity
            return data
        return self.archive.read(self.members[name])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("bootstrap", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("count", type=int)
    args = parser.parse_args()
    pack(args.root, args.bootstrap, args.output, args.count)
