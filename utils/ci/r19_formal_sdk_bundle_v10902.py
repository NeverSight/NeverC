"""Create a bounded text archive from completed formal runtime evidence."""

import argparse
from pathlib import Path
from tempfile import TemporaryDirectory
import json
import os
import shutil

import r19_formal_sdk_block_transport_v10888 as fragments


FORMAL = "cpp-private-runtime-formal"
BOOTSTRAP = "cpp-private-runtime-bootstrap"
CONTEXT = "response-sdk-dependencies-v1"
INDEX = "index.json"


def path_within(root, relative):
    rel = Path(relative)
    assert not rel.is_absolute() and rel.parts and ".." not in rel.parts
    path = root / rel
    assert path.resolve().is_relative_to(root) and not path.is_symlink()
    return path


def checked_file(root, name, limit=fragments.MEMBER_MAX):
    path = path_within(root, name)
    assert path.is_file() and 0 < path.stat().st_size <= limit
    return path.read_bytes()


def stage(formal, bootstrap, output, count):
    formal, bootstrap, output = (p.resolve() for p in (formal, bootstrap, output))
    assert formal.name == FORMAL and bootstrap.name == BOOTSTRAP
    assert formal.is_dir() and bootstrap.is_dir() and not output.exists()
    assert all(not output.is_relative_to(root) and not root.is_relative_to(output)
               for root in (formal, bootstrap))
    report = checked_file(formal, "results.json")
    index, blocks, rows = fragments.collect(report, count, lambda name: checked_file(formal, name))
    with TemporaryDirectory(prefix=".formal-text-evidence-", dir=output.parent) as folder:
        bundle = Path(folder) / "bundle"
        for root in (formal, bootstrap):
            for path in sorted(root.rglob("*")):
                assert not path.is_symlink()
                if path.is_dir():
                    continue
                assert path.is_file()
                name = path.relative_to(root).as_posix()
                dest = bundle / root.name / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                if root == formal and name in rows:
                    offset, key = rows[name]
                    with dest.open("xb") as out:
                        out.write(fragments.strip_response(checked_file(formal, name), offset, blocks[key]))
                else:
                    os.link(path, dest)
        context = bundle / FORMAL / CONTEXT
        context.mkdir()
        (context / INDEX).write_bytes(index)
        for sha, block in sorted(blocks.items()):
            (context / (sha + ".json")).write_bytes(block)
        os.rename(bundle, output)
    print(json.dumps({"formally_tested": count, "compacted_responses": len(rows), "shared_sdk_dependencies": len(blocks)}),
          flush=True)


def hydrate(destination, count):
    destination = destination.resolve()
    formal = destination / FORMAL
    context = formal / CONTEXT
    assert context.is_dir() and not context.is_symlink()
    index = checked_file(context, INDEX)
    data = json.loads(index)
    assert {path.name for path in context.iterdir()} == {INDEX, *(key + ".json" for key in data["blocks"])}
    report = checked_file(formal, "results.json")
    restore = fragments.bind(index, report, count, lambda sha, n: checked_file(context, sha + ".json", n))
    recovered_bytes = 0
    for name in data["responses"]:
        original = checked_file(formal, name)
        full = restore(name, original)
        recovered_bytes += len(full) - len(original)
        assert 0 < recovered_bytes <= fragments.MEMBER_MAX
        full_path = path_within(formal, name)
        with TemporaryDirectory(prefix=".formal-response-", dir=full_path.parent) as staging:
            temp = Path(staging) / "response.json"
            temp.write_bytes(full)
            os.replace(temp, full_path)
    shutil.rmtree(context)
    return {"recovered_response_bytes": recovered_bytes, "restored_responses": len(data["responses"])}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("formal", type=Path)
    parser.add_argument("bootstrap", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("count", type=int)
    args = parser.parse_args()
    stage(args.formal, args.bootstrap, args.output, args.count)
