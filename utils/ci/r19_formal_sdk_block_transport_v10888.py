"""Retain exact formal responses while sharing identical SDK dependency arrays."""

from collections import Counter
from hashlib import sha256
import json
import re


RESPONSE = re.compile(r"invocations/frontend/[1-9][0-9]*/response\.json")
SHA256_HEX = re.compile(r"[0-9a-f]{64}")
FIELD = b'\n  "sdk_dependencies": ['
BLOCK_MIN = 1024
BLOCK_MAX = 512 * 1024
MEMBER_MAX = 64 * 1024**2


def inventory(report, count):
    assert len(report) <= MEMBER_MAX
    results = json.loads(report)
    assert results["completed"] and results["passed"] and results["sdk_files_verified"] == 687
    assert len(results["exact_sdk_input_sha256"]) == 695
    assert results["actual_frontend_request_count"] == count
    responses = {}
    for row in results["actual_invocation_inventory"]:
        name = row["path"]
        if not RESPONSE.fullmatch(name):
            continue
        assert name not in responses
        assert type(row["bytes"]) is int and 0 < row["bytes"] <= MEMBER_MAX
        assert SHA256_HEX.fullmatch(row["sha256"])
        responses[name] = row
    assert len(responses) == count
    return responses


def source_block(data):
    field = data.find(FIELD)
    if field == -1:
        return None
    offset = field + len(FIELD) - 1
    if data[offset:offset + 2] == b"[]":
        return None
    end = data.find(b"\n  ]", offset) + 4
    assert end > offset and data[end - 1:end] == b"]"
    assert data.find(FIELD, end) == -1
    if not BLOCK_MIN <= end - offset <= BLOCK_MAX:
        return None
    return offset, data[offset:end]


def collect(report, count, read):
    matching = {}
    blocks = {}
    frequencies = Counter()
    for name, row in inventory(report, count).items():
        data = read(name)
        assert len(data) == row["bytes"] and sha256(data).hexdigest() == row["sha256"]
        candidate = source_block(data)
        if candidate is None:
            continue
        offset, block = candidate
        digest = sha256(block).hexdigest()
        if digest in blocks:
            assert block == blocks[digest]
        else:
            assert isinstance(json.loads(block), list)
            blocks[digest] = block
        matching[name] = (offset, digest)
        frequencies[digest] += 1
    shared = {key: value for key, value in blocks.items() if frequencies[key] > 1}
    rows = {key: value for key, value in matching.items() if value[1] in shared}
    assert rows
    manifest = json.dumps({
        "version": 1, "requests": count, "report_sha256": sha256(report).hexdigest(),
        "responses": rows, "blocks": {key: len(value) for key, value in shared.items()},
    }, separators=(",", ":")).encode()
    assert len(manifest) <= MEMBER_MAX
    return manifest, shared, rows


def strip_response(data, position, block):
    assert source_block(data) == (position, block)
    return data[:position] + data[position + len(block):]


def bind(manifest, report, count, block_reader):
    expected = inventory(report, count)
    assert len(manifest) <= MEMBER_MAX
    pack = json.loads(manifest)
    assert set(pack) == {"version", "requests", "report_sha256", "responses", "blocks"}
    assert pack["version"] == 1 and pack["requests"] == count
    assert pack["report_sha256"] == sha256(report).hexdigest()
    members, blocks = pack["responses"], pack["blocks"]
    assert isinstance(members, dict) and isinstance(blocks, dict)
    assert 0 < len(blocks) <= len(members) <= count and members.keys() <= expected.keys()
    assert set(blocks) == {row[1] for row in members.values()}
    for name, (offset, digest) in members.items():
        size = blocks[digest]
        assert type(size) is int and BLOCK_MIN <= size <= BLOCK_MAX
        assert type(offset) is int and 0 < offset < expected[name]["bytes"] - size
    assert sum(blocks.values()) <= MEMBER_MAX
    chunks = {}
    for identity, size in blocks.items():
        assert SHA256_HEX.fullmatch(identity)
        data = block_reader(identity, size)
        assert len(data) == size and sha256(data).hexdigest() == identity
        assert isinstance(json.loads(data), list)
        chunks[identity] = data

    def reassemble(name, data):
        if name in members:
            offset, identity = members[name]
            assert data[offset - len(FIELD) + 1:offset] == FIELD[:-1]
            data = data[:offset] + chunks[identity] + data[offset:]
        if name in expected:
            assert len(data) == expected[name]["bytes"]
            assert sha256(data).hexdigest() == expected[name]["sha256"]
        return data

    return reassemble
