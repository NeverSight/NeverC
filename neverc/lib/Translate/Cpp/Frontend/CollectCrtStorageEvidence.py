#!/usr/bin/env python3
"""Bounded, diagnostic-only CRT tool and COFF archive identities.

The audit remains rejected. Index representatives are not object definitions,
ABI evidence, or complete reference coverage. Native children only print their
version; the separately bounded inventory worker never launches a process.
Collection and export require a cooperatively exclusive CI report directory.
The 110 + 10 second budgets cover computation, not scheduling or upload time.
"""

from __future__ import annotations

import argparse
import codecs
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import queue
import re
import stat
import subprocess
import sys
import threading
import time

sys.dont_write_bytecode = True


SELECTED_SYMBOLS = (
    "?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA",
    "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@4_KA",
    "_Avx2WmemEnabledWeakValue",
)
FILE_LIMIT = 1024 * 1024
TOTAL_LIMIT = 8 * FILE_LIMIT
NAME_BYTE_LIMIT = FILE_LIMIT
NAME_WORK_LIMIT = TOTAL_LIMIT
MANIFEST_RESERVE = 256 * 1024
CHUNK = 64 * 1024
ROLES = ("private-nm", "private-readobj", "host-nm", "host-readobj",
         "host-dis", "host-bcanalyzer")
SCOPE = "observed-index-representatives-only"
SCHEMA = "neverc.crt-storage-evidence.v1"
REQUEST_SCHEMA = "neverc.crt-storage-request.v1"
INVENTORY_SCHEMA = "neverc.crt-storage-inventory.v1"
TEXT_NAMES = frozenset({"request.txt", "inventory.txt", "worker-stdout.txt",
                        "worker-stderr.txt"} | {
    f"tool-{role}-{channel}.txt" for role in ROLES
    for channel in ("stdout", "stderr")})


class EvidenceLimit(ValueError):
    pass


class Budget:
    def __init__(self, seconds, *, clock=None):
        self.clock = clock or time.monotonic
        self.start = self.clock()
        self.deadline = self.start + max(0.0, float(seconds))
        self.used_bytes = 0
        self.reserved_bytes = 0
        self.cleanup_incomplete = False

    def remaining(self):
        return max(0.0, self.deadline - self.clock())

    def check(self):
        if self.remaining() <= 0:
            raise EvidenceLimit("computation deadline exhausted")

    def charge(self, size):
        self.check()
        if type(size) is not int or size < 0:
            raise EvidenceLimit("invalid retained byte charge")
        if self.used_bytes + self.reserved_bytes + size > TOTAL_LIMIT:
            raise EvidenceLimit("total retained text limit exceeded")
        self.used_bytes += size


def _brief(error):
    # JSON escaping keeps diagnostic controls and surrogate paths out of logs.
    return json.dumps(str(error)[:240], ensure_ascii=True)[1:-1]


def _json_bytes(value):
    return (json.dumps(value, sort_keys=True, ensure_ascii=True,
                       separators=(",", ":")) + "\n").encode("ascii")


def _identity(value):
    return {"device": value.st_dev, "inode": value.st_ino,
            "size": value.st_size, "mtime_ns": value.st_mtime_ns,
            "ctime_ns": value.st_ctime_ns}


def _comparison_identity(value):
    result = _identity(value)
    if os.name == "nt":
        # CPython 3.12 path stat preserves creation time in ctime, while
        # fstat exposes Windows ChangeTime. Compare their common birthtime;
        # keep both raw ctime observations for same-API stability checks.
        birthtime = getattr(value, "st_birthtime_ns", None)
        if type(birthtime) is not int:
            raise ValueError("Windows file creation time is unavailable")
        del result["ctime_ns"]
        result["birthtime_ns"] = birthtime
    return result


def _regular_open(path):
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode):
        raise ValueError("input must be a regular non-symlink file")
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0)
    flags |= getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags)
    try:
        observed = os.fstat(descriptor)
        if (not stat.S_ISREG(observed.st_mode) or
                _comparison_identity(before) != _comparison_identity(observed)):
            raise ValueError("input identity changed while opening")
        stream = os.fdopen(descriptor, "rb")
        descriptor = None
        return stream, observed, before
    finally:
        if descriptor is not None:
            os.close(descriptor)


def _stable(path, stream, before, path_before):
    after = os.fstat(stream.fileno())
    path_after = path.lstat()
    if (not stat.S_ISREG(after.st_mode) or
            not stat.S_ISREG(path_after.st_mode) or
            _identity(before) != _identity(after) or
            _identity(path_before) != _identity(path_after) or
            _comparison_identity(before) != _comparison_identity(after) or
            _comparison_identity(path_before) != _comparison_identity(path_after) or
            _comparison_identity(after) != _comparison_identity(path_after)):
        raise ValueError("input changed or was replaced during collection")
    return _identity(after), _identity(path_after)


def _hash_region(stream, offset, size, budget):
    budget.check()
    stream.seek(offset)
    digest = hashlib.sha256()
    remaining = size
    while remaining:
        budget.check()
        chunk = stream.read(min(CHUNK, remaining))
        if not chunk:
            raise ValueError("truncated input while hashing")
        digest.update(chunk)
        remaining -= len(chunk)
        budget.check()
    budget.check()
    return digest.hexdigest()


class _CheckedStream:
    """Chunk I/O around the existing container parser, without changing it.

    Its bounded table/CPU loops additionally have the parent's worker timeout;
    cooperative checks alone are not claimed to interrupt every parser loop.
    """
    def __init__(self, stream, budget):
        self.stream, self.budget = stream, budget

    def seek(self, offset):
        self.budget.check()
        return self.stream.seek(offset)

    def read(self, size):
        self.budget.check()
        if size < 0 or size > 128 * FILE_LIMIT:
            raise EvidenceLimit("unsupported container read size")
        chunks = []
        remaining = size
        while remaining:
            self.budget.check()
            data = self.stream.read(min(CHUNK, remaining))
            if not data:
                break
            chunks.append(data)
            remaining -= len(data)
        self.budget.check()
        return b"".join(chunks)


def _name_identity(name):
    return {"preview": repr(name[:160]), "bytes": len(name),
            "sha256": hashlib.sha256(name).hexdigest()}


def _load_coff_reader():
    """Load only the regular adjacent reader, including under Python -I."""
    path = Path(__file__).resolve().with_name("HostCoffSymbols.py")
    if not stat.S_ISREG(path.lstat().st_mode):
        raise ValueError("adjacent COFF reader must be a regular non-symlink file")
    name = "_neverc_crt_storage_coff_reader"
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ImportError("cannot load adjacent COFF reader")
    module = importlib.util.module_from_spec(spec)
    # Register the exact module before execution for dataclass/type resolution.
    # Never reuse an ambient module with either the ordinary or private name.
    sys.modules[name] = module
    try:
        spec.loader.exec_module(module)
    except BaseException:
        sys.modules.pop(name, None)
        raise
    return module


class _LongNameIdentities:
    """Bound first scans and hashes independently of retained report bytes."""
    def __init__(self, table, budget):
        self.table, self.budget = table, budget
        self.cache = {}
        self.work_bytes = 0

    def _charge_work(self, size):
        self.budget.check()
        if size > NAME_WORK_LIMIT - self.work_bytes:
            raise EvidenceLimit("long member name work limit exceeded")
        self.work_bytes += size

    def identity(self, offset):
        self.budget.check()
        if type(offset) is not int or not 0 <= offset < len(self.table):
            raise ValueError("long member name offset is out of bounds")
        if offset in self.cache:
            return self.cache[offset]
        position = offset
        search_limit = min(len(self.table), offset + NAME_BYTE_LIMIT + 1)
        end = -1
        while position < search_limit:
            self.budget.check()
            available = NAME_WORK_LIMIT - self.work_bytes
            if available <= 0:
                raise EvidenceLimit("long member name work limit exceeded")
            stop = min(search_limit, position + CHUNK, position + available)
            end = self.table.find(b"\0", position, stop)
            self._charge_work((end + 1 if end >= 0 else stop) - position)
            if end >= 0:
                break
            position = stop
        if end < 0:
            if search_limit > offset + NAME_BYTE_LIMIT:
                raise EvidenceLimit("long member name byte limit exceeded")
            raise ValueError("unterminated long member name")
        if end == offset:
            raise ValueError("empty long member name")
        size = end - offset
        # Charge the full first hash before copying even the bounded preview.
        # The view avoids a retained full-name copy for every physical member.
        self._charge_work(size)
        view = memoryview(self.table)
        digest = hashlib.sha256()
        for position in range(offset, end, CHUNK):
            self.budget.check()
            digest.update(view[position:min(end, position + CHUNK)])
        self.budget.check()
        identity = {"preview": repr(bytes(view[offset:min(end, offset + 160)])),
                    "bytes": size, "sha256": digest.hexdigest()}
        self.cache[offset] = identity
        return identity


def inventory_archive(path, selected_symbols, budget, *, archive_ordinal=0,
                      side="private", representative_symbols=None):
    """Read normal COFF container indices, never the opaque object semantics."""
    path = Path(path)
    result = {"status": "incomplete", "side": side,
              "archive_ordinal": archive_ordinal,
              "archive": {"path": str(path), "before": None, "path_before": None,
                          "after": None,
                          "path_after": None, "sha256": None,
                          "sha256_status": "unavailable", "identity_stable": False},
              "member_count": 0, "member_inventory_sha256": None,
              "index": {}, "representatives": []}
    try:
        budget.check()
        if not selected_symbols or not set(selected_symbols) <= set(SELECTED_SYMBOLS):
            raise ValueError("unsupported selected symbols")
        coff = _load_coff_reader()
        stream, before, path_before = _regular_open(path)
        with stream:
            result["archive"]["before"] = _identity(before)
            result["archive"]["path_before"] = _identity(path_before)
            checked = _CheckedStream(stream, budget)
            coff._defined_symbols(checked, before.st_size)
            budget.check()
            members = coff._members(checked, before.st_size)
            first_object = 3 if members[2].name == b"//" else 2
            long_names = (coff._table(checked, before.st_size, members[2])
                          if first_object == 3 else b"")
            long_identities = _LongNameIdentities(long_names, budget)
            objects = members[first_object:]
            result["member_count"] = len(objects)
            by_offset = {member.offset: ordinal for ordinal, member in enumerate(objects)}
            names = []
            member_digest = hashlib.sha256()
            for ordinal, member in enumerate(objects):
                budget.check()
                if member.name.startswith(b"/"):
                    start = int(member.name[1:])
                    identity = long_identities.identity(start)
                else:
                    identity = _name_identity(member.name[:-1])
                names.append(identity)
                member_digest.update(_json_bytes([
                    ordinal, ordinal + first_object, member.offset,
                    member.offset + 60, member.size, identity]))
            result["member_inventory_sha256"] = member_digest.hexdigest()
            first = coff._Cursor(coff._table(checked, before.st_size, members[0]))
            first_count = first.u32(">")
            first_offsets = first.array(first_count, ">I", 4)
            first_names = first.names(first_count)
            second = coff._Cursor(coff._table(checked, before.st_size, members[1]))
            second_offsets = second.array(second.u32("<"), "<I", 4)
            second_count = second.u32("<")
            second_indices = second.array(second_count, "<H", 2)
            second_names = second.names(second_count)
            wanted = {name.encode("ascii"): name for name in selected_symbols}
            choose = set(selected_symbols if representative_symbols is None
                         else representative_symbols)
            counts = {name: {"first": 0, "second": 0} for name in selected_symbols}
            digests = {key: hashlib.sha256() for key in ("first", "second")}
            representatives = {}
            # The existing validator permits a coalesced preferred second
            # index. Preserve each index's order and multiplicity separately.
            for label, raw_names, offsets in (
                    ("first", first_names, first_offsets),
                    ("second", second_names,
                     [second_offsets[index - 1] for index in second_indices])):
                for index_ordinal, (raw_name, offset) in enumerate(zip(raw_names, offsets)):
                    budget.check()
                    if raw_name not in wanted:
                        continue
                    name = wanted[raw_name]
                    ordinal = by_offset[offset]
                    counts[name][label] += 1
                    digests[label].update(_json_bytes([index_ordinal, name, ordinal, offset]))
                    if label == "first" and name in choose:
                        candidate = (ordinal, index_ordinal)
                        if name not in representatives or candidate < representatives[name]:
                            representatives[name] = candidate
            result["index"] = {
                "first_count": first_count, "second_count": second_count,
                "first_candidate_count": sum(item["first"] for item in counts.values()),
                "second_candidate_count": sum(item["second"] for item in counts.values()),
                "first_candidates_sha256": digests["first"].hexdigest(),
                "second_candidates_sha256": digests["second"].hexdigest(),
                "candidates_by_symbol": counts}
            payload_hashes = {}
            for name in selected_symbols:
                if name not in representatives:
                    continue
                ordinal, index_ordinal = representatives[name]
                member = objects[ordinal]
                if ordinal not in payload_hashes:
                    payload_hashes[ordinal] = _hash_region(
                        stream, member.offset + 60, member.size, budget)
                result["representatives"].append({
                    "symbol": name, "index_source": "first-linker-member",
                    "first_index_ordinal": index_ordinal, "member_ordinal": ordinal,
                    "archive_member_ordinal": ordinal + first_object,
                    "header_offset": member.offset, "payload_offset": member.offset + 60,
                    "payload_size": member.size, "member_name": names[ordinal],
                    "payload_sha256": payload_hashes[ordinal]})
            if representatives:
                result["archive"]["sha256"] = _hash_region(stream, 0, before.st_size, budget)
                result["archive"]["sha256_status"] = "computed-unverified"
            else:
                result["archive"]["sha256_status"] = "not-selected"
            after, path_after = _stable(path, stream, before, path_before)
            result["archive"].update(after=after, path_after=path_after, identity_stable=True)
            if representatives:
                result["archive"]["sha256_status"] = "complete"
            budget.check()
            result["status"] = "complete"
    except (OSError, ValueError, ImportError) as error:
        result["error"] = _brief(error)
    return result


def _validate_context(context):
    keys = {"schema", "selected_symbols", "private_archive", "host_archives",
            "private_nm", "private_readobj", "host_nm", "audit_outcome",
            "source_sha", "run_id", "run_attempt", "target"}
    if not isinstance(context, dict) or set(context) != keys:
        raise ValueError("request fields do not match the fixed schema")
    if context["schema"] != REQUEST_SCHEMA or context["audit_outcome"] != "rejected":
        raise ValueError("request must describe an already rejected audit")
    selected = context["selected_symbols"]
    if (not isinstance(selected, list) or not selected or len(selected) > 3 or
            any(type(name) is not str or name not in SELECTED_SYMBOLS for name in selected)
            or len(set(selected)) != len(selected)):
        raise ValueError("request must select distinct fixed CRT storage names")
    hosts = context["host_archives"]
    if not isinstance(hosts, list) or not hosts or len(hosts) > 4096:
        raise ValueError("missing or excessive frozen host archive scope")
    paths = [context[key] for key in ("private_archive", "private_nm", "host_nm")]
    paths += hosts
    if context["private_readobj"] is not None:
        paths.append(context["private_readobj"])
    if any(type(path) is not str or not path or len(path) > 32768 or
           "\0" in path or not Path(path).is_absolute() for path in paths):
        raise ValueError("request paths must be bounded absolute paths")
    if not re.fullmatch(r"[0-9a-f]{40}", str(context["source_sha"])):
        raise ValueError("missing exact source SHA")
    if any(not re.fullmatch(r"[0-9]{1,24}", str(context[key]))
           for key in ("run_id", "run_attempt")):
        raise ValueError("invalid CI run identity")
    if context["target"] not in ("x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc"):
        raise ValueError("unsupported target")


def inventory_request(context, budget):
    """Pure worker entry: no subprocess, executable, object reader or decoder."""
    result = {"schema": INVENTORY_SCHEMA, "status": "incomplete",
              "evidence_scope": SCOPE, "archives": [], "representatives": [],
              "counts": {"scope": 0, "processed": 0, "omitted": 0,
                         "eligible_first": 0, "eligible_second": 0, "selected": 0}}
    try:
        _validate_context(context)
        scope = [("private", 0, context["private_archive"])] + [
            ("host", ordinal, path) for ordinal, path in enumerate(context["host_archives"])]
        result["counts"].update(scope=len(scope), omitted=len(scope))
        result["scope_sha256"] = hashlib.sha256(_json_bytes(scope)).hexdigest()
        selected = context["selected_symbols"]
        remaining = {side: set(selected) for side in ("private", "host")}
        for side, ordinal, path in scope:
            budget.check()
            record = inventory_archive(path, selected, budget, archive_ordinal=ordinal,
                                       side=side, representative_symbols=remaining[side])
            prior_counts = dict(result["counts"])
            prior_representatives = len(result["representatives"])
            result["archives"].append(record)
            result["counts"]["processed"] += 1
            result["counts"]["omitted"] -= 1
            for label in ("first", "second"):
                result["counts"]["eligible_" + label] += record["index"].get(
                    label + "_candidate_count", 0)
            for representative in record["representatives"]:
                result["representatives"].append(dict(
                    representative, side=side, archive_ordinal=ordinal))
            result["counts"]["selected"] = len(result["representatives"])
            # Keep the serialized worker result bounded while accumulating,
            # rather than discovering an overflow after an unbounded report.
            if len(_json_bytes(result)) > FILE_LIMIT - 2048:
                result["archives"].pop()
                result["counts"] = prior_counts
                del result["representatives"][prior_representatives:]
                raise EvidenceLimit("inventory text limit reached")
            for representative in record["representatives"]:
                remaining[side].discard(representative["symbol"])
            if record["status"] != "complete":
                raise ValueError("archive inventory incomplete: " + record.get("error", "unknown"))
        result["missing_index_representatives"] = {
            side: sorted(names) for side, names in remaining.items()}
        # An absent index representative is not proof of symbol absence from
        # opaque object payloads. This stage only completes the index scan.
        budget.check()
        result["status"] = "complete"
    except (OSError, ValueError) as error:
        result["error"] = _brief(error)
    return result


def run_child(argv, stdout_path, stderr_path, budget, *, timeout_seconds,
              cleanup_seconds=3):
    """Own one direct child; stream bounded output and confirm its cleanup."""
    started = time.monotonic()
    result = {"status": "error", "returncode": None, "elapsed_seconds": 0.0,
              "cleanup": {"reaped": False, "kill_requested": False,
                          "complete": True, "error": None}}
    process = None
    streams = []
    stop = threading.Event()
    events = queue.Queue(maxsize=8)
    threads = []

    def reader(channel, pipe):
        try:
            while not stop.is_set():
                data = pipe.read(CHUNK)
                event = (channel, data, None)
                while not stop.is_set():
                    try:
                        events.put(event, timeout=0.02)
                        break
                    except queue.Full:
                        pass
                if not data:
                    return
        except Exception as error:
            while not stop.is_set():
                try:
                    events.put((channel, b"", _brief(error)), timeout=0.02)
                    return
                except queue.Full:
                    pass

    try:
        budget.check()
        if budget.cleanup_incomplete or budget.remaining() <= cleanup_seconds:
            raise EvidenceLimit("insufficient time for confirmed cleanup")
        active_seconds = min(float(timeout_seconds), budget.remaining() - cleanup_seconds)
        if active_seconds <= 0:
            raise EvidenceLimit("no active command budget")
        active_deadline = time.monotonic() + active_seconds
        for path in (stdout_path, stderr_path):
            streams.append(Path(path).open("xb"))
        try:
            process = subprocess.Popen(list(argv), shell=False, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE)
        except OSError as error:
            result.update(status="startup_failed", error=_brief(error))
            return result
        decoders = [codecs.getincrementaldecoder("utf-8")("backslashreplace") for _ in range(2)]
        sizes = [0, 0]
        ended = set()
        for channel, pipe in enumerate((process.stdout, process.stderr)):
            thread = threading.Thread(target=reader, args=(channel, pipe), daemon=True)
            thread.start()
            threads.append(thread)
        while True:
            budget.check()
            if time.monotonic() >= active_deadline:
                result["status"] = "timeout"
                break
            if len(ended) == 2 and process.poll() is not None:
                result["status"] = "completed"
                break
            try:
                channel, data, read_error = events.get(timeout=min(
                    0.02, max(0.001, active_deadline - time.monotonic())))
            except queue.Empty:
                continue
            if read_error:
                raise OSError(read_error)
            text = decoders[channel].decode(data, final=not data)
            encoded = text.encode("utf-8")
            if sizes[channel] + len(encoded) > FILE_LIMIT:
                result["status"] = "output_limit"
                break
            try:
                budget.charge(len(encoded))
            except EvidenceLimit:
                result["status"] = "timeout" if budget.remaining() <= 0 else "output_limit"
                break
            streams[channel].write(encoded)
            sizes[channel] += len(encoded)
            if not data:
                ended.add(channel)
    except EvidenceLimit as error:
        result.update(status="timeout", error=_brief(error))
    except Exception as error:
        result.update(status="error", error=_brief(error))
    finally:
        stop.set()
        if process is not None:
            cleanup = result["cleanup"]
            errors = []
            try:
                if process.poll() is None:
                    cleanup["kill_requested"] = True
                    process.kill()
            except Exception as error:
                errors.append("kill: " + _brief(error))
            try:
                wait = min(max(0.0, float(cleanup_seconds)), budget.remaining())
                result["returncode"] = process.wait(timeout=wait)
                cleanup["reaped"] = True
            except Exception as error:
                errors.append("wait: " + _brief(error))
            if errors:
                cleanup.update(complete=False, error="; ".join(errors))
                result["status"] = "cleanup_incomplete"
                budget.cleanup_incomplete = True
            # Pipe reads cannot block the controller or extend its deadline.
            # A failed reap remains explicit; it is not a process-tree claim.
            for thread in threads:
                thread.join(timeout=0)
            if all(not thread.is_alive() for thread in threads):
                for pipe in (process.stdout, process.stderr):
                    pipe.close()
        for stream in streams:
            try:
                stream.close()
            except OSError as error:
                result.update(status="error", error=_brief(error))
        result["elapsed_seconds"] = time.monotonic() - started
    return result


def _read_text(path, budget):
    budget.check()
    stream, before, path_before = _regular_open(path)
    with stream:
        if before.st_size > FILE_LIMIT:
            raise EvidenceLimit("text file exceeds 1 MiB")
        data = _CheckedStream(stream, budget).read(before.st_size)
        if len(data) != before.st_size:
            raise ValueError("truncated text file")
        _stable(path, stream, before, path_before)
    data.decode("utf-8", errors="strict")
    budget.check()
    return data


def probe_tool(role, path, expected_version, budget, root):
    record = {"role": role, "path": str(path) if path is not None else None,
              "status": "unavailable", "exists": None, "size": None, "sha256": None,
              "version": None, "expected_version": expected_version, "command": None}
    try:
        budget.check()
        if role not in ROLES or path is None:
            raise ValueError("tool path unavailable")
        path = Path(path)
        basename = "llvm-" + role.split("-", 1)[1]
        if not path.is_absolute() or path.name not in (basename, basename + ".exe"):
            raise ValueError("tool must be an explicit LLVM binary, without wrapper or PATH lookup")
        try:
            path.lstat()
            record["exists"] = True
        except FileNotFoundError:
            record["exists"] = False
            raise
        stream, before, path_before = _regular_open(path)
        with stream:
            magic = stream.read(4)
            if not (magic.startswith(b"MZ") or magic == b"\x7fELF" or magic in (
                    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
                    b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")):
                raise ValueError("tool is not a recognized native binary")
            record.update(size=before.st_size,
                          sha256=_hash_region(stream, 0, before.st_size, budget),
                          before=_identity(before), path_before=_identity(path_before))
            after, path_after = _stable(path, stream, before, path_before)
            record.update(after=after, path_after=path_after)
        stdout = Path(root) / f"tool-{role}-stdout.txt"
        stderr = Path(root) / f"tool-{role}-stderr.txt"
        command = run_child([str(path), "--version"], stdout, stderr, budget,
                            timeout_seconds=5)
        command["argv"] = [str(path), "--version"]
        record["command"] = command
        if command["status"] != "completed" or command["returncode"] != 0:
            record["status"] = "probe-failed"
            return record
        output = _read_text(stdout, budget).decode("utf-8")
        versions = set(re.findall(r"\bLLVM version ([0-9]+\.[0-9]+\.[0-9]+)\b", output))
        if len(versions) != 1:
            raise ValueError("missing or ambiguous LLVM version")
        record["version"] = versions.pop()
        if expected_version is not None and record["version"] != expected_version:
            record["status"] = "version-mismatch"
        else:
            record["status"] = "complete"
        path_final = path.lstat()
        if (not stat.S_ISREG(path_final.st_mode) or
                _identity(path_final) != record["path_before"] or
                _comparison_identity(path_final) != _comparison_identity(path_before)):
            raise ValueError("tool changed during version probe")
    except (OSError, ValueError) as error:
        record.update(status="incomplete", error=_brief(error))
    return record


def _write_text(path, data, budget):
    if len(data) > FILE_LIMIT:
        raise EvidenceLimit("generated text file exceeds 1 MiB")
    budget.charge(len(data))
    with path.open("xb") as stream:
        for position in range(0, len(data), CHUNK):
            budget.check()
            stream.write(data[position:position + CHUNK])
        stream.flush()
    budget.check()


def collect_failure(context, report_root):
    """Best-effort one-shot diagnostics; never changes the rejection outcome."""
    budget = Budget(110)
    root = Path(report_root)
    manifest = {"schema": SCHEMA, "status": "incomplete", "audit_outcome": "rejected",
                "evidence_scope": SCOPE, "context": context, "tools": [], "commands": [],
                "inventory": None, "cleanup": {"complete": True}, "files": [],
                "limits": {"collection_seconds": 110, "verification_seconds": 10,
                           "file_bytes": FILE_LIMIT, "total_retained_bytes": TOTAL_LIMIT,
                           "representatives_per_side_and_symbol": 1,
                           "manifest_reserved_bytes": MANIFEST_RESERVE}}
    claimed = False
    try:
        _validate_context(context)
        root.mkdir(parents=True, exist_ok=True)
        if not stat.S_ISDIR(root.lstat().st_mode):
            return "incomplete: report root is not a regular directory"
        try:
            (root / "collection.claim").mkdir()
        except FileExistsError:
            return "already-claimed"
        claimed = True
        if {path.name for path in root.iterdir()} != {"collection.claim"}:
            return "incomplete: report directory is not fresh"
        budget.reserved_bytes = MANIFEST_RESERVE + FILE_LIMIT
        _write_text(root / "request.txt", _json_bytes(context), budget)
        host = Path(context["host_nm"])
        suffix = ".exe" if host.suffix == ".exe" else ""
        paths = {"private-nm": context["private_nm"],
                 "private-readobj": context["private_readobj"], "host-nm": str(host)}
        paths.update({role: str(host.with_name("llvm-" + role.split("-", 1)[1] + suffix))
                      for role in ROLES if role.startswith("host-") and role != "host-nm"})
        host_version = None
        stopped = False
        for role in ROLES:
            if budget.remaining() <= 7:
                stopped = True
                break
            expected = "20.1.8" if role.startswith("private-") else host_version
            record = probe_tool(role, paths[role], expected, budget, root)
            manifest["tools"].append(record)
            if role == "host-nm" and record["status"] == "complete":
                host_version = record["version"]
            if role.startswith("host-") and role != "host-nm" and host_version is None:
                record.update(status="incomplete", error="host-nm version unavailable for comparison")
            command = record["command"]
            if command is not None:
                manifest["commands"].append(command)
                if command["status"] != "completed" or not command["cleanup"]["complete"]:
                    stopped = True
                    break
        if not stopped and budget.remaining() > 7:
            worker_seconds = max(0.001, budget.remaining() - 7)
            argv = [sys.executable, "-I", "-B", str(Path(__file__).resolve()), "--inventory",
                    str(root / "request.txt"), str(root / "inventory.txt"),
                    "--seconds", str(worker_seconds)]
            command = run_child(argv, root / "worker-stdout.txt", root / "worker-stderr.txt",
                                budget, timeout_seconds=worker_seconds)
            command["argv"] = argv
            manifest["commands"].append(command)
            if (command["status"] == "completed" and command["returncode"] == 0 and
                    command["cleanup"]["complete"]):
                data = _read_text(root / "inventory.txt", budget)
                budget.reserved_bytes -= FILE_LIMIT
                budget.charge(len(data))
                inventory = json.loads(data)
                if inventory.get("schema") != INVENTORY_SCHEMA:
                    raise ValueError("worker inventory schema mismatch")
                # The complete inventory is retained once, not embedded again.
                manifest["inventory"] = {"file": "inventory.txt", "status": inventory["status"],
                                         "counts": inventory["counts"]}
            else:
                stopped = True
        manifest["cleanup"]["complete"] = all(
            command["cleanup"]["complete"] for command in manifest["commands"])
        if (not stopped and len(manifest["tools"]) == len(ROLES) and
                all(tool["status"] == "complete" for tool in manifest["tools"]) and
                manifest["inventory"] is not None and
                manifest["inventory"]["status"] == "complete"):
            manifest["status"] = "complete"
    except Exception as error:
        manifest["error"] = _brief(error)
    if not claimed:
        return "incomplete: " + manifest.get("error", "report not claimed")
    try:
        budget.check()
        retained = 0
        for path in sorted(root.iterdir()):
            if path.name == "collection.claim":
                continue
            if path.name not in TEXT_NAMES:
                raise ValueError("unexpected report file")
            data = _read_text(path, budget)
            retained += len(data)
            manifest["files"].append({"name": path.name, "size": len(data),
                                      "sha256": hashlib.sha256(data).hexdigest()})
        manifest["cleanup"]["complete"] = all(
            command["cleanup"]["complete"] for command in manifest["commands"])
        if not manifest["cleanup"]["complete"]:
            manifest["status"] = "incomplete"
        manifest["elapsed_seconds"] = budget.clock() - budget.start
        data = _json_bytes(manifest)
        if len(data) > MANIFEST_RESERVE or retained + len(data) > TOTAL_LIMIT:
            raise EvidenceLimit("manifest or final retained byte limit exceeded")
        budget.reserved_bytes = 0
        budget.used_bytes = retained
        _write_text(root / "manifest.json", data, budget)
        return manifest["status"]
    except Exception as error:
        return "incomplete: report unavailable: " + _brief(error)


def verify_report(root, *, seconds=10):
    """Validate one flat text report, then move it into the upload directory."""
    budget = Budget(min(10, max(0, seconds)))
    root = Path(root)
    result = {"upload_ready": False, "status": "invalid", "elapsed_seconds": 0.0}
    try:
        if not root.exists():
            result["status"] = "absent"
            return result
        budget.check()
        if not stat.S_ISDIR(root.lstat().st_mode):
            raise ValueError("report root is not a regular directory")
        (root / "verification.claim").mkdir()
        for name in ("collection.claim", "verification.claim"):
            claim = root / name
            if not stat.S_ISDIR(claim.lstat().st_mode) or any(claim.iterdir()):
                raise ValueError("invalid or nonempty one-shot claim")
        raw_manifest = _read_text(root / "manifest.json", budget)
        manifest = json.loads(raw_manifest)
        if (not isinstance(manifest, dict) or manifest.get("schema") != SCHEMA or
                manifest.get("audit_outcome") != "rejected" or
                manifest.get("evidence_scope") != SCOPE or
                manifest.get("status") not in ("complete", "incomplete")):
            raise ValueError("invalid evidence manifest schema or outcome")
        _validate_context(manifest["context"])
        files = manifest.get("files")
        if not isinstance(files, list) or len(files) > len(TEXT_NAMES):
            raise ValueError("invalid file inventory")
        expected = {"manifest.json"}
        total = len(raw_manifest)
        inventory = None
        identities = {"manifest.json": _identity((root / "manifest.json").lstat())}
        for entry in files:
            budget.check()
            if not isinstance(entry, dict) or set(entry) != {"name", "size", "sha256"}:
                raise ValueError("invalid file inventory entry")
            name = entry["name"]
            if type(name) is not str or name not in TEXT_NAMES or name in expected:
                raise ValueError("duplicate or unsupported evidence file name")
            if (type(entry["size"]) is not int or not 0 <= entry["size"] <= FILE_LIMIT or
                    not re.fullmatch(r"[0-9a-f]{64}", str(entry["sha256"]))):
                raise ValueError("invalid evidence size or hash")
            data = _read_text(root / name, budget)
            if len(data) != entry["size"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
                raise ValueError("evidence file hash or size mismatch")
            if name == "request.txt" and json.loads(data) != manifest["context"]:
                raise ValueError("request does not match manifest context")
            if name == "inventory.txt":
                inventory = json.loads(data)
                if (inventory.get("schema") != INVENTORY_SCHEMA or
                        inventory.get("evidence_scope") != SCOPE or
                        inventory.get("status") not in ("complete", "incomplete")):
                    raise ValueError("invalid worker inventory schema")
            total += len(data)
            if total > TOTAL_LIMIT:
                raise EvidenceLimit("total retained text limit exceeded")
            identities[name] = _identity((root / name).lstat())
            expected.add(name)
        if "request.txt" not in expected:
            raise ValueError("missing fixed request")
        summary = manifest.get("inventory")
        if summary is not None:
            if (not isinstance(summary, dict) or
                    set(summary) != {"file", "status", "counts"} or
                    summary["file"] != "inventory.txt" or inventory is None or
                    summary["status"] != inventory["status"] or
                    not isinstance(summary["counts"], dict) or
                    _json_bytes(summary["counts"]) != _json_bytes(inventory.get("counts"))):
                raise ValueError("manifest inventory summary does not match inventory text")
        # A failed worker may leave valid inventory text without a summary.
        # Only the complete state requires every producer stage to be present.
        # These are internal consistency checks, not proof of command execution.
        if manifest["status"] == "complete":
            tools = manifest.get("tools")
            commands = manifest.get("commands")
            if (summary is None or summary["status"] != "complete" or
                    not isinstance(tools, list) or len(tools) != len(ROLES) or
                    any(not isinstance(tool, dict) for tool in tools) or
                    [tool.get("role") for tool in tools] != list(ROLES) or
                    any(tool.get("status") != "complete" for tool in tools) or
                    not isinstance(commands, list) or len(commands) != len(ROLES) + 1 or
                    not isinstance(manifest.get("cleanup"), dict) or
                    manifest["cleanup"].get("complete") is not True):
                raise ValueError("complete manifest lacks completed producer stages")
            tool_commands = [tool.get("command") for tool in tools]
            if (any(not isinstance(command, dict) or not command for command in tool_commands) or
                    _json_bytes(commands[:-1]) != _json_bytes(tool_commands)):
                raise ValueError("complete manifest tool commands disagree")
            for command in commands:
                if (not isinstance(command, dict) or command.get("status") != "completed" or
                        type(command.get("returncode")) is not int or command["returncode"] != 0 or
                        not isinstance(command.get("cleanup"), dict) or
                        command["cleanup"].get("complete") is not True or
                        command["cleanup"].get("reaped") is not True):
                    raise ValueError("complete manifest contains an unsuccessful command")
            argv = commands[-1].get("argv")
            if (not isinstance(argv, list) or len(argv) != 9 or
                    any(type(arg) is not str for arg in argv) or
                    argv[1:3] != ["-I", "-B"] or argv[4] != "--inventory" or
                    argv[7] != "--seconds"):
                raise ValueError("complete manifest lacks the isolated inventory worker")
        actual = {path.name for path in root.iterdir()}
        if actual != expected | {"collection.claim", "verification.claim"}:
            raise ValueError("unlisted report files or directories")
        budget.check()
        upload = root / "upload"
        upload.mkdir()
        for name in sorted(expected):
            budget.check()
            path = root / name
            if _identity(path.lstat()) != identities[name] or not stat.S_ISREG(path.lstat().st_mode):
                raise ValueError("evidence changed before export")
            path.rename(upload / name)
        budget.check()
        result.update(upload_ready=True, status="ready")
    except Exception as error:
        result["error"] = _brief(error)
    finally:
        result["elapsed_seconds"] = budget.clock() - budget.start
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--inventory", nargs=2, metavar=("REQUEST", "OUTPUT"))
    group.add_argument("--verify-report", type=Path)
    parser.add_argument("--seconds", type=float, default=100)
    args = parser.parse_args(argv)
    if args.inventory is not None:
        # The only subprocess import in this module is inert on this branch.
        # No reader, version probe, or collector function is called here.
        budget = Budget(min(110, max(0, args.seconds)))
        request, output = map(Path, args.inventory)
        try:
            context = json.loads(_read_text(request, budget))
            result = inventory_request(context, budget)
            data = _json_bytes(result)
            _write_text(output, data, budget)
            return 0
        except Exception as error:
            print("CRT inventory incomplete: " + _brief(error), file=sys.stderr)
            return 1
    result = verify_report(args.verify_report)
    ready = "true" if result["upload_ready"] else "false"
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8", newline="\n") as stream:
            stream.write("upload_ready=" + ready + "\n")
    print("CRT identity report: " + result["status"] + "; upload_ready=" + ready)
    if result["status"] not in ("ready", "absent") and result.get("error"):
        print("CRT identity report error: " + result["error"], file=sys.stderr)
    return 0 if result["status"] in ("ready", "absent") else 1


if __name__ == "__main__":
    raise SystemExit(main())
