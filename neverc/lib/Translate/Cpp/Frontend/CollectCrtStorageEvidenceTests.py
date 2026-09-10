#!/usr/bin/env python3
"""Bounded CRT identity evidence tests; no compiler or LLVM executable needed."""

import argparse
from contextlib import ExitStack
import errno
import hashlib
import io
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

import AuditArchive as audit
import CollectCrtStorageEvidence as evidence


# Fixed witnesses are independent of the implementation's selectable set.
PRINTF = "?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA"
SCANF = "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@4_KA"
AVX = "_Avx2WmemEnabledWeakValue"
SYMBOLS = (PRINTF, SCANF, AVX)
REPORT_ENV = "NEVERC_CPP_CRT_STORAGE_REPORT_DIR"
MIB = 1024 * 1024


def coff_archive(objects, *, first_entries=None, second_entries=None):
    """Encode two COFF indices and opaque bodies, including duplicate names.

    Returns independently calculated physical (header, payload, length) tuples.
    Each object is (short member name as bytes, payload bytes, indexed names).
    """
    if first_entries is None:
        first_entries = [(name, ordinal)
                         for ordinal, (_, _, names) in enumerate(objects)
                         for name in names]
    if second_entries is None:
        second_entries = sorted(first_entries)

    def member(name, body):
        header = (name.ljust(16, b" ") + b"0".ljust(12, b" ") + b" " * 12 +
                  b"100644".ljust(8, b" ") +
                  str(len(body)).encode("ascii").ljust(10, b" ") + b"`\n")
        if len(header) != 60:
            raise ValueError("fixture member header must be 60 bytes")
        return header + body + (b"\n" if len(body) & 1 else b"")

    def indices(offsets):
        first = struct.pack(">I", len(first_entries))
        first += b"".join(struct.pack(">I", offsets[n]) for _, n in first_entries)
        first += b"".join(name.encode("utf-8") + b"\0" for name, _ in first_entries)
        second = struct.pack("<I", len(objects))
        second += b"".join(struct.pack("<I", offset) for offset in offsets)
        second += struct.pack("<I", len(second_entries))
        second += b"".join(struct.pack("<H", n + 1) for _, n in second_entries)
        second += b"".join(name.encode("utf-8") + b"\0" for name, _ in second_entries)
        return member(b"/", first) + member(b"/", second)

    position = 8 + len(indices([0] * len(objects)))
    locations, bodies = [], []
    for name, payload, _ in objects:
        locations.append((position, position + 60, len(payload)))
        body = member(name + b"/", payload)
        bodies.append(body)
        position += len(body)
    data = b"!<arch>\n" + indices([item[0] for item in locations]) + b"".join(bodies)
    return data, locations


class Clock:
    def __init__(self):
        self.now = 100.0

    def __call__(self):
        return self.now


class DirectChild:
    """Small Popen boundary double; no process or executable is launched."""
    def __init__(self, *, stdout=b"", stderr=b"", hanging=False,
                 kill_fails=False, reap_fails=False, returncode=0):
        self.stdout = io.BytesIO(stdout)
        self.stderr = io.BytesIO(stderr)
        self.hanging = hanging
        self.kill_fails = kill_fails
        self.reap_fails = reap_fails
        self.returncode = None if hanging else returncode
        self.kills = 0
        self.waits = []

    def poll(self):
        return self.returncode

    def kill(self):
        self.kills += 1
        if self.kill_fails:
            raise OSError("kill refused")
        self.hanging = False
        self.returncode = None if self.reap_fails else -9

    def wait(self, timeout=None):
        self.waits.append(timeout)
        if self.hanging or self.reap_fails:
            raise subprocess.TimeoutExpired("direct test child", timeout)
        return self.returncode


class TemporaryEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="neverc-crt-evidence-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.private = self.root / "private.lib"
        self.host = self.root / "host.lib"
        self.report = self.root / "report"
        self.host_dir = self.root / "host-build"
        self.host_dir.mkdir()
        self.private_nm = self.root / "private-tools/bin/llvm-nm.exe"
        self.private_readobj = self.private_nm.with_name("llvm-readobj.exe")
        self.host_nm = self.root / "host-tools/bin/llvm-nm.exe"

    def context(self, *, selected=SYMBOLS):
        return {
            "schema": "neverc.crt-storage-request.v1",
            "selected_symbols": list(selected),
            "private_archive": str(self.private),
            "host_archives": [str(self.host)],
            "private_nm": str(self.private_nm),
            "private_readobj": str(self.private_readobj),
            "host_nm": str(self.host_nm),
            "audit_outcome": "rejected",
            "source_sha": "3e8d5d63" + "0" * 32,
            "run_id": "123456",
            "run_attempt": "1",
            "target": "x86_64-pc-windows-msvc",
        }

    def args(self):
        return argparse.Namespace(host_lib_dir=self.host_dir, host_format="nm",
                                  archive=self.private, nm=str(self.private_nm),
                                  nm_file=None, prefix_header=None,
                                  coff_readobj=None, coff_readobj_file=None,
                                  host_nm=str(self.host_nm))

    def write_archive(self, path, objects, **options):
        data, locations = coff_archive(objects, **options)
        path.write_bytes(data)
        return data, locations


class AuditActivationTests(TemporaryEvidenceTest):
    def select(self, args, *, shared=SYMBOLS, private=SYMBOLS, host=SYMBOLS,
               archives=None):
        return audit.crt_storage_failure_context(
            args, set(shared), set(private), set(host),
            [self.host] if archives is None else archives,
            str(self.private_nm), str(self.private_readobj), str(self.host_nm))

    def test_only_exact_rejected_intersection_is_selected(self):
        with mock.patch.dict(os.environ, {REPORT_ENV: str(self.report)}, clear=True):
            result = self.select(self.args(),
                                 shared=[PRINTF, AVX, "prefix" + SCANF, "unrelated"],
                                 private=[PRINTF, AVX, "prefix" + SCANF],
                                 host=[PRINTF, "prefix" + SCANF])
        self.assertIsNotNone(result)
        report_root, context = result
        self.assertEqual(Path(report_root), self.report)
        self.assertEqual(context["selected_symbols"], [PRINTF])
        self.assertEqual(context["schema"], "neverc.crt-storage-request.v1")
        self.assertEqual(context["audit_outcome"], "rejected")
        self.assertEqual(context["private_archive"], str(self.private))
        self.assertEqual(context["private_nm"], str(self.private_nm))
        self.assertEqual(context["private_readobj"], str(self.private_readobj))
        self.assertEqual(context["host_nm"], str(self.host_nm))
        json.dumps(context)  # The subprocess request must be JSON-safe.

    def test_disabled_and_unrelated_audits_have_no_diagnostic_context(self):
        cases = ("no-env", "empty-env", "no-host", "coff-index", "no-shared",
                 "not-private-failure", "not-host-failure", "lookalike-only")
        for case in cases:
            with self.subTest(case=case):
                args = self.args()
                env = {REPORT_ENV: str(self.report)}
                options = {}
                if case == "no-env":
                    env = {}
                elif case == "empty-env":
                    env[REPORT_ENV] = ""
                elif case == "no-host":
                    args.host_lib_dir = None
                elif case == "coff-index":
                    args.host_format = "coff-index"
                elif case == "no-shared":
                    options["shared"] = []
                elif case == "not-private-failure":
                    options["private"] = []
                elif case == "not-host-failure":
                    options["host"] = []
                else:
                    options = {key: [AVX + "extra"] for key in ("shared", "private", "host")}
                with mock.patch.dict(os.environ, env, clear=True):
                    self.assertIsNone(self.select(args, **options))

    def test_context_freezes_archive_scope_before_later_mutation(self):
        archives = [self.host, self.root / "second.lib"]
        with mock.patch.dict(os.environ, {REPORT_ENV: str(self.report)}, clear=True):
            _, context = self.select(self.args(), archives=archives)
        archives.reverse()
        archives.append(self.root / "late.lib")
        self.assertEqual(context["host_archives"], [str(self.host), str(self.root / "second.lib")])

    def test_context_copies_ci_identity_without_dumping_environment(self):
        env = {REPORT_ENV: str(self.report), "GITHUB_SHA": "a" * 40,
               "GITHUB_RUN_ID": "456", "GITHUB_RUN_ATTEMPT": "2",
               "NEVERC_CPP_CRT_STORAGE_TARGET": "aarch64-pc-windows-msvc",
               "UNRELATED_SECRET": "must-never-enter-evidence"}
        with mock.patch.dict(os.environ, env, clear=True):
            _, context = self.select(self.args())
        self.assertEqual(context["source_sha"], "a" * 40)
        self.assertEqual(context["run_id"], "456")
        self.assertEqual(context["run_attempt"], "2")
        self.assertEqual(context["target"], "aarch64-pc-windows-msvc")
        self.assertNotIn("must-never-enter-evidence", json.dumps(context))

    def test_real_audit_failure_populates_only_structured_selected_context(self):
        args = self.args()
        inventories = {self.private: [("neverc_cpp_frontend_main", "T"), (AVX, "B")],
                       self.host: [(AVX, "W")]}

        def nm_output(_tool, paths, *options):
            rows = [row for path in paths for row in inventories[path]]
            if "--format=posix" in options:
                return "same.obj:\n" + "".join(f"{name} {kind} 0 0\n" for name, kind in rows)
            return "".join(name + "\n" for name, _ in rows)

        with mock.patch.dict(os.environ, {REPORT_ENV: str(self.report)}, clear=True), \
                mock.patch.object(audit, "nm_output", side_effect=nm_output), \
                mock.patch.object(audit, "host_archives", return_value=[self.host]), \
                mock.patch.object(sys, "stdout", new_callable=io.StringIO):
            with self.assertRaisesRegex(ValueError, "total=1"):
                audit.audit(args)
        report_root, context = args._crt_storage_failure
        self.assertEqual(Path(report_root), self.report)
        self.assertEqual(context["selected_symbols"], [AVX])
        self.assertEqual(context["host_archives"], [str(self.host)])

    def test_early_failure_clears_stale_context_without_probing(self):
        args = self.args()
        args._crt_storage_failure = (str(self.report), self.context())
        with mock.patch.object(audit, "decoded_symbols", side_effect=ValueError("early reader failure")), \
                mock.patch.object(audit, "crt_storage_failure_context") as prepare:
            with self.assertRaisesRegex(ValueError, "early reader failure"):
                audit.audit(args)
        self.assertIsNone(args._crt_storage_failure)
        prepare.assert_not_called()


class AuditRejectionTests(TemporaryEvidenceTest):
    def test_successful_audit_neither_collects_nor_deletes_aggregate(self):
        original = b"usable aggregate from successful audit"
        self.private.write_bytes(original)
        argv = ["AuditArchive.py", "--nm", str(self.private_nm), "--archive", str(self.private)]
        with mock.patch.object(sys, "argv", argv), \
                mock.patch.object(audit, "audit", return_value=None), \
                mock.patch.object(audit, "collect_crt_storage_failure") as collector:
            audit.main()
        collector.assert_not_called()
        self.assertEqual(self.private.read_bytes(), original)

    def invoke_main(self, failure, *, context=True):
        self.private.write_bytes(b"rejected aggregate must be removed")
        original = ValueError("original audit rejection with all findings")

        def refuse(args):
            args._crt_storage_failure = ((str(self.report), self.context()) if context else None)
            raise original

        def collect(*_args):
            self.assertTrue(self.private.is_file(), "collection must precede original unlink")
            if failure is not None:
                raise failure
            return "incomplete"

        argv = ["AuditArchive.py", "--nm", str(self.private_nm), "--archive", str(self.private)]
        with mock.patch.object(sys, "argv", argv), \
                mock.patch.object(audit, "audit", side_effect=refuse), \
                mock.patch.object(audit, "collect_crt_storage_failure", side_effect=collect) as collector, \
                mock.patch.object(sys, "stderr", new_callable=io.StringIO), \
                mock.patch.object(sys, "stdout", new_callable=io.StringIO):
            with self.assertRaises(SystemExit) as result:
                audit.main()
        self.assertEqual(result.exception.code, str(original))
        self.assertIs(result.exception.__cause__, original)
        self.assertFalse(self.private.exists())
        self.assertEqual(collector.call_count, 1 if context else 0)

    def test_diagnostic_failures_never_replace_original_error_or_cleanup(self):
        failures = (None, OSError("report full"), ValueError("bad evidence"),
                    SystemExit(9), KeyboardInterrupt(),
                    subprocess.TimeoutExpired("probe", 1))
        for failure in failures:
            with self.subTest(failure=type(failure).__name__):
                self.invoke_main(failure)

    def test_unrelated_error_does_not_call_collector(self):
        self.invoke_main(None, context=False)

    def test_missing_helper_still_removes_original_archive(self):
        self.private.write_bytes(b"rejected")

        def refuse(args):
            args._crt_storage_failure = (str(self.report), self.context())
            raise ValueError("original rejection")

        argv = ["AuditArchive.py", "--nm", str(self.private_nm), "--archive", str(self.private)]
        fallback = mock.Mock()
        with mock.patch.object(sys, "argv", argv), \
                mock.patch.object(audit, "audit", side_effect=refuse), \
                mock.patch.object(audit, "__file__", str(self.root / "isolated/AuditArchive.py")), \
                mock.patch.dict(sys.modules, {"CollectCrtStorageEvidence": fallback}), \
                mock.patch.object(sys, "stderr", new_callable=io.StringIO), \
                mock.patch.object(sys, "stdout", new_callable=io.StringIO):
            with self.assertRaises(SystemExit) as failure:
                audit.main()
        self.assertEqual(failure.exception.code, "original rejection")
        self.assertFalse(self.private.exists())
        fallback.collect_failure.assert_not_called()

    def test_broken_diagnostic_error_stream_preserves_original_rejection(self):
        self.private.write_bytes(b"rejected")

        def refuse(args):
            args._crt_storage_failure = (str(self.report), self.context())
            raise ValueError("original rejection")

        broken = mock.Mock()
        broken.write.side_effect = OSError("stderr closed")
        argv = ["AuditArchive.py", "--nm", str(self.private_nm), "--archive", str(self.private)]
        with mock.patch.object(sys, "argv", argv), \
                mock.patch.object(audit, "audit", side_effect=refuse), \
                mock.patch.object(audit, "collect_crt_storage_failure", side_effect=SystemExit(9)), \
                mock.patch.object(sys, "stderr", broken):
            with self.assertRaises(SystemExit) as failure:
                audit.main()
        self.assertEqual(failure.exception.code, "original rejection")
        self.assertFalse(self.private.exists())

    def test_adjacent_symlink_helper_is_rejected_before_module_loading(self):
        with mock.patch.object(Path, "is_symlink", return_value=True), \
                mock.patch.object(audit.importlib.util, "spec_from_file_location") as load:
            with self.assertRaisesRegex(ValueError, "adjacent"):
                audit.collect_crt_storage_failure(self.context(), self.report)
        load.assert_not_called()


class BudgetTests(unittest.TestCase):
    def test_one_deadline_cannot_be_restarted_between_operations(self):
        clock = Clock()
        budget = evidence.Budget(10, clock=clock)
        clock.now += 9
        budget.check()
        self.assertLessEqual(budget.remaining(), 1)
        clock.now += 1
        with self.assertRaises(evidence.EvidenceLimit):
            budget.check()
        with self.assertRaises(evidence.EvidenceLimit):
            budget.charge(1)

    def test_exact_total_text_boundary_and_one_byte_over(self):
        budget = evidence.Budget(10, clock=Clock())
        budget.charge(8 * MIB)
        with self.assertRaises(evidence.EvidenceLimit):
            budget.charge(1)


class ArchiveIdentityTests(TemporaryEvidenceTest):
    def inventory(self, path=None, selected=SYMBOLS):
        return evidence.inventory_archive(path or self.private, list(selected),
                                          evidence.Budget(10))

    def test_duplicate_basenames_bind_to_physical_member_not_name(self):
        objects = [(b"same.obj", b"first opaque payload", [PRINTF]),
                   (b"same.obj", b"second opaque payload", [PRINTF, SCANF]),
                   (b"unused.obj", b"third opaque payload", [])]
        data, locations = self.write_archive(
            self.private, objects,
            first_entries=[(PRINTF, 0), (PRINTF, 1), (SCANF, 1)])
        result = self.inventory(selected=(PRINTF, SCANF))
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["member_count"], 3)
        self.assertEqual(result["archive"]["sha256"], hashlib.sha256(data).hexdigest())
        reps = {record["symbol"]: record for record in result["representatives"]}
        self.assertEqual(set(reps), {PRINTF, SCANF})
        for name, ordinal in ((PRINTF, 0), (SCANF, 1)):
            record = reps[name]
            header, payload, size = locations[ordinal]
            self.assertEqual(record["member_ordinal"], ordinal)
            self.assertEqual(record["archive_member_ordinal"], ordinal + 2)
            self.assertEqual(record["header_offset"], header)
            self.assertEqual(record["payload_offset"], payload)
            self.assertEqual(record["payload_size"], size)
            self.assertEqual(record["payload_sha256"], hashlib.sha256(objects[ordinal][1]).hexdigest())
        self.assertNotEqual(reps[PRINTF]["payload_sha256"], reps[SCANF]["payload_sha256"])
        self.assertEqual(result["index"]["candidates_by_symbol"][PRINTF]["first"], 2)

    def test_unordered_first_index_is_rejected_before_representative_selection(self):
        self.write_archive(self.private,
                           [(b"same.obj", b"one", [PRINTF]),
                            (b"same.obj", b"two", [PRINTF])],
                           first_entries=[(PRINTF, 1), (PRINTF, 0)])
        result = self.inventory(selected=(PRINTF,))
        self.assertEqual(result["status"], "incomplete")
        self.assertTrue(result.get("error"))

    def test_coalesced_second_index_does_not_erase_first_index_candidates(self):
        self.write_archive(self.private,
                           [(b"same.obj", b"one", [PRINTF]),
                            (b"same.obj", b"two", [PRINTF])],
                           second_entries=[(PRINTF, 0)])
        result = self.inventory(selected=(PRINTF,))
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["index"]["candidates_by_symbol"][PRINTF], {"first": 2, "second": 1})
        self.assertEqual(len(result["representatives"]), 1)
        self.assertEqual(result["representatives"][0]["member_ordinal"], 0)

    def test_two_symbols_keep_distinct_bindings_to_one_physical_member(self):
        payload = b"opaque bytes, deliberately not an object file"
        _, locations = self.write_archive(self.private,
                                          [(b"both.obj", payload, [PRINTF, SCANF])])
        result = self.inventory(selected=(PRINTF, SCANF))
        self.assertEqual(result["status"], "complete")
        self.assertEqual({r["symbol"] for r in result["representatives"]}, {PRINTF, SCANF})
        for record in result["representatives"]:
            self.assertEqual(record["header_offset"], locations[0][0])
            self.assertEqual(record["payload_sha256"], hashlib.sha256(payload).hexdigest())
        self.assertNotIn("definition", result)
        self.assertNotIn("initializer", result)

    def test_conflicting_symbol_to_member_index_binding_is_incomplete(self):
        self.write_archive(self.private,
                           [(b"one.obj", b"one", [PRINTF]),
                            (b"two.obj", b"two", [])],
                           second_entries=[(PRINTF, 1)])
        result = self.inventory(selected=(PRINTF,))
        self.assertEqual(result["status"], "incomplete")
        self.assertTrue(result.get("error"))

    def test_unsupported_and_truncated_containers_are_not_complete(self):
        valid, positions = coff_archive([(b"one.obj", b"object", [PRINTF])])
        for data in (b"!<thin>\n", valid[:positions[0][0] + 30], b"not an archive"):
            with self.subTest(size=len(data)):
                self.private.write_bytes(data)
                result = self.inventory(selected=(PRINTF,))
                self.assertEqual(result["status"], "incomplete")
                self.assertTrue(result.get("error"))

    def test_expired_budget_cannot_claim_complete_archive_hash(self):
        self.write_archive(self.private, [(b"one.obj", b"opaque", [PRINTF])])
        clock = Clock()
        budget = evidence.Budget(1, clock=clock)
        clock.now += 2
        result = evidence.inventory_archive(self.private, [PRINTF], budget)
        self.assertEqual(result["status"], "incomplete")
        self.assertIsNone(result["archive"]["sha256"])

    def test_in_place_archive_change_cannot_produce_stable_complete_evidence(self):
        self.write_archive(self.private, [(b"one.obj", b"opaque", [PRINTF])])
        original_fstat = os.fstat
        observations = 0

        def changed_stat(descriptor):
            nonlocal observations
            observations += 1
            if observations == 2:
                with self.private.open("ab") as stream:
                    stream.write(b"changed after the first identity observation")
            return original_fstat(descriptor)

        with mock.patch.object(os, "fstat", side_effect=changed_stat):
            result = self.inventory(selected=(PRINTF,))
        self.assertGreaterEqual(observations, 2)
        self.assertEqual(result["status"], "incomplete")
        self.assertTrue(result.get("error"))

    def test_non_utf8_member_name_keeps_full_byte_identity_in_valid_json(self):
        name = b"opaque\xff.obj"
        self.write_archive(self.private, [(name, b"opaque", [PRINTF])])
        result = self.inventory(selected=(PRINTF,))
        self.assertEqual(result["status"], "complete")
        identity = result["representatives"][0]["member_name"]
        self.assertEqual(identity["bytes"], len(name))
        self.assertEqual(identity["sha256"], hashlib.sha256(name).hexdigest())
        self.assertLessEqual(len(identity["preview"]), 1024)
        json.dumps(result).encode("utf-8", errors="strict")

    def test_inventory_worker_never_launches_processes_for_opaque_payloads(self):
        self.write_archive(self.private, [(b"p.obj", b"private opaque", [PRINTF])])
        self.write_archive(self.host, [(b"h.obj", b"host opaque", [PRINTF])])
        request = self.root / "inventory-request.txt"
        output = self.root / "inventory-output.txt"
        request.write_text(json.dumps(self.context(selected=(PRINTF,))), encoding="utf-8")
        with ExitStack() as stack:
            forbidden = []
            for name in ("Popen", "run", "call", "check_call", "check_output"):
                forbidden.append(stack.enter_context(mock.patch.object(
                    subprocess, name, side_effect=AssertionError("inventory launched a process"))))
            for name in ("system", *(n for n in dir(os) if n.startswith(("exec", "spawn")))):
                forbidden.append(stack.enter_context(mock.patch.object(
                    os, name, side_effect=AssertionError("inventory executed a command"))))
            exit_code = evidence.main(["--inventory", str(request), str(output), "--seconds", "10"])
        self.assertEqual(exit_code, 0)
        result = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["evidence_scope"], "observed-index-representatives-only")
        self.assertEqual(result["counts"]["processed"], 2)
        self.assertEqual(result["counts"]["selected"], 2)
        for operation in forbidden:
            operation.assert_not_called()


class CollectionBoundaryTests(TemporaryEvidenceTest):
    def test_existing_collection_claim_is_not_overwritten_or_restarted(self):
        self.report.mkdir()
        claim = self.report / "collection.claim"
        claim.mkdir()
        sentinel = claim / "first-owner.txt"
        sentinel.write_bytes(b"preserve first collection")
        with mock.patch.object(subprocess, "Popen") as launch:
            status = evidence.collect_failure(self.context(), self.report)
        launch.assert_not_called()
        self.assertIsInstance(status, str)
        self.assertLessEqual(len(status.encode("utf-8")), 1024)
        self.assertEqual(sentinel.read_bytes(), b"preserve first collection")
        self.assertEqual(list(self.report.iterdir()), [claim])

    def test_request_cannot_expand_symbols_or_override_rejected_outcome(self):
        cases = (
            {"selected_symbols": []},
            {"selected_symbols": [PRINTF, AVX + "extra"]},
            {"selected_symbols": ["unrelated"]},
            {"schema": "untrusted.request.v1"},
            {"audit_outcome": "passed"},
        )
        for number, changes in enumerate(cases):
            with self.subTest(changes=changes):
                context = self.context()
                context.update(changes)
                with mock.patch.object(subprocess, "Popen") as launch:
                    status = evidence.collect_failure(context, self.root / f"invalid-{number}")
                launch.assert_not_called()
                self.assertIsInstance(status, str)
                self.assertNotEqual(status, "complete")

    def test_report_failure_cannot_overwrite_an_archive_used_as_output_path(self):
        original = b"existing archive bytes"
        self.private.write_bytes(original)
        with mock.patch.object(subprocess, "Popen") as launch:
            try:
                status = evidence.collect_failure(self.context(), self.private)
            except OSError:
                pass
            else:
                self.assertNotEqual(status, "complete")
        launch.assert_not_called()
        self.assertEqual(self.private.read_bytes(), original)

    def test_missing_report_is_never_upload_ready(self):
        result = evidence.verify_report(self.report)
        self.assertFalse(result["upload_ready"])
        self.assertFalse((self.report / "upload").exists())


class DirectChildBoundaryTests(TemporaryEvidenceTest):
    def run_process(self, child, *, budget=None, timeout=0.01):
        self.process_number = getattr(self, "process_number", 0) + 1
        directory = self.root / f"process-{self.process_number}"
        directory.mkdir()
        stdout = directory / "stdout.txt"
        stderr = directory / "stderr.txt"
        argv = [str(self.host_nm), "--version"]
        with mock.patch.object(subprocess, "Popen", return_value=child) as launch:
            result = evidence.run_child(argv, stdout, stderr, budget or evidence.Budget(2),
                                        timeout_seconds=timeout, cleanup_seconds=0.05)
        return result, stdout, stderr, launch

    def test_completed_probe_retains_both_streams_and_confirmed_exit(self):
        child = DirectChild(stdout=b"LLVM version 22.1.8\n", stderr=b"version warning\n")
        result, stdout, stderr, launch = self.run_process(child, timeout=1)
        self.assertEqual(result["status"], "completed")
        self.assertEqual(result["returncode"], 0)
        self.assertTrue(result["cleanup"]["reaped"])
        self.assertTrue(result["cleanup"]["complete"])
        self.assertFalse(result["cleanup"]["kill_requested"])
        self.assertEqual(stdout.read_bytes(), b"LLVM version 22.1.8\n")
        self.assertEqual(stderr.read_bytes(), b"version warning\n")
        self.assertEqual(launch.call_args.args[0], [str(self.host_nm), "--version"])
        self.assertFalse(launch.call_args.kwargs.get("shell", False))

    def test_nonzero_exit_is_preserved_for_caller_classification(self):
        result, _, _, _ = self.run_process(DirectChild(returncode=7), timeout=1)
        self.assertEqual(result["status"], "completed")
        self.assertEqual(result["returncode"], 7)
        self.assertTrue(result["cleanup"]["reaped"])

    def test_timeout_kills_and_reaps_the_owned_child(self):
        child = DirectChild(hanging=True)
        result, _, _, _ = self.run_process(child)
        self.assertEqual(result["status"], "timeout")
        self.assertEqual(child.kills, 1)
        self.assertTrue(result["cleanup"]["kill_requested"])
        self.assertTrue(result["cleanup"]["reaped"])
        self.assertTrue(result["cleanup"]["complete"])
        self.assertEqual(result["returncode"], -9)
        self.assertTrue(child.waits)
        self.assertTrue(all(value is not None and 0 <= value <= 2 for value in child.waits))

    def test_kill_or_reap_failure_never_reports_cleanup_success(self):
        for changes in ({"kill_fails": True}, {"reap_fails": True}):
            with self.subTest(changes=changes):
                child = DirectChild(hanging=True, **changes)
                result, _, _, _ = self.run_process(child)
                self.assertEqual(result["status"], "cleanup_incomplete")
                self.assertFalse(result["cleanup"]["reaped"])
                self.assertFalse(result["cleanup"]["complete"])
                self.assertTrue(result["cleanup"]["error"])
                self.assertEqual(child.kills, 1)

    def test_stdout_overflow_stops_retention_and_reaps_active_child(self):
        child = DirectChild(stdout=b"x" * (MIB + 1), hanging=True)
        result, stdout, stderr, _ = self.run_process(child, timeout=1)
        self.assertEqual(result["status"], "output_limit")
        self.assertLessEqual(stdout.stat().st_size, MIB)
        self.assertLessEqual(stderr.stat().st_size, MIB)
        self.assertEqual(child.kills, 1)
        self.assertTrue(result["cleanup"]["reaped"])

    def test_combined_output_cannot_exceed_remaining_total_bytes(self):
        budget = evidence.Budget(2)
        retained = 8 * MIB - 1024
        budget.charge(retained)
        child = DirectChild(stdout=b"x" * 2048, hanging=True)
        result, stdout, stderr, _ = self.run_process(child, budget=budget, timeout=1)
        self.assertEqual(result["status"], "output_limit")
        self.assertLessEqual(retained + stdout.stat().st_size + stderr.stat().st_size, 8 * MIB)
        self.assertTrue(result["cleanup"]["reaped"])

    def test_expired_deadline_never_starts_a_child(self):
        clock = Clock()
        budget = evidence.Budget(1, clock=clock)
        clock.now += 2
        result, _, _, launch = self.run_process(DirectChild(), budget=budget)
        launch.assert_not_called()
        self.assertEqual(result["status"], "timeout")

    def test_startup_failure_has_no_fictitious_child_exit(self):
        with mock.patch.object(subprocess, "Popen", side_effect=OSError("missing executable")):
            result = evidence.run_child([str(self.host_nm), "--version"],
                                        self.root / "out.txt", self.root / "err.txt",
                                        evidence.Budget(10), timeout_seconds=1)
        self.assertEqual(result["status"], "startup_failed")
        self.assertIsNone(result["returncode"])


class ToolIdentityTests(TemporaryEvidenceTest):
    def test_missing_tool_records_no_hash_and_launches_nothing(self):
        self.report.mkdir()
        with mock.patch.object(subprocess, "Popen") as launch:
            result = evidence.probe_tool("host-nm", self.host_nm, "22.1.8",
                                         evidence.Budget(10), self.report)
        launch.assert_not_called()
        self.assertIsNone(result["sha256"])
        self.assertIsNone(result["version"])
        self.assertEqual(Path(result["path"]), self.host_nm)

    def test_script_wrapper_is_rejected_before_a_version_probe(self):
        self.report.mkdir()
        wrapper = self.host_nm.with_suffix(".cmd")
        wrapper.parent.mkdir(parents=True)
        wrapper.write_bytes(b"@echo LLVM version 22.1.8\r\n")
        with mock.patch.object(subprocess, "Popen") as launch:
            result = evidence.probe_tool("host-nm", wrapper, "22.1.8",
                                         evidence.Budget(10), self.report)
        launch.assert_not_called()
        self.assertIsNone(result["version"])

    def test_probe_binds_hash_and_version_without_reading_an_object(self):
        self.report.mkdir()
        self.host_nm.parent.mkdir(parents=True)
        payload = b"MZ inert test fixture; never executable in this test"
        self.host_nm.write_bytes(payload)
        child = DirectChild(stdout=b"LLVM version 22.1.8\n")
        with mock.patch.object(subprocess, "Popen", return_value=child) as launch:
            result = evidence.probe_tool("host-nm", self.host_nm, "22.1.8",
                                         evidence.Budget(10), self.report)
        self.assertEqual(result["sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(result["size"], len(payload))
        self.assertEqual(result["version"], "22.1.8")
        self.assertEqual(launch.call_count, 1)
        self.assertEqual(launch.call_args.args[0], [str(self.host_nm), "--version"])

    def test_malformed_and_mismatched_versions_never_match_a_valid_probe(self):
        self.report.mkdir()
        self.host_nm.parent.mkdir(parents=True)
        self.host_nm.write_bytes(b"MZ inert test fixture")
        results = []
        for index, output in enumerate((b"LLVM version 22.1.8\n",
                                         b"LLVM version 20.1.8\n", b"not a version\n")):
            directory = self.report / str(index)
            directory.mkdir()
            with mock.patch.object(subprocess, "Popen", return_value=DirectChild(stdout=output)):
                results.append(evidence.probe_tool("host-nm", self.host_nm, "22.1.8",
                                                   evidence.Budget(10), directory))
        self.assertEqual(results[0]["version"], "22.1.8")
        self.assertEqual(results[1]["version"], "20.1.8")
        self.assertIsNone(results[2]["version"])
        self.assertNotEqual(results[0]["status"], results[1]["status"])
        self.assertNotEqual(results[0]["status"], results[2]["status"])

    def test_cleanup_failure_prevents_all_subsequent_probe_and_worker_launches(self):
        for tool in (self.private_nm, self.private_readobj, self.host_nm,
                     self.host_nm.with_name("llvm-readobj.exe"),
                     self.host_nm.with_name("llvm-dis.exe"),
                     self.host_nm.with_name("llvm-bcanalyzer.exe")):
            tool.parent.mkdir(parents=True, exist_ok=True)
            tool.write_bytes(b"MZ inert test fixture")

        def fail_cleanup(_argv, stdout, stderr, _budget, **_options):
            stdout.write_bytes(b"")
            stderr.write_bytes(b"")
            return {"status": "cleanup_incomplete", "returncode": None,
                    "elapsed_seconds": 0.01,
                    "cleanup": {"reaped": False, "kill_requested": True,
                                "complete": False, "error": "wait did not confirm exit"}}

        with mock.patch.object(evidence, "run_child", side_effect=fail_cleanup) as launch, \
                mock.patch.object(subprocess, "Popen", side_effect=AssertionError("real process")):
            evidence.collect_failure(self.context(), self.report)
        self.assertEqual(launch.call_count, 1)
        manifest = json.loads((self.report / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["status"], "incomplete")
        self.assertEqual(manifest["audit_outcome"], "rejected")


class ArtifactBoundaryTests(TemporaryEvidenceTest):
    def collected_report(self):
        """Use missing tools and a pure worker double, never a native process."""
        self.write_archive(self.private, [(b"p.obj", b"private opaque", list(SYMBOLS))])
        self.write_archive(self.host, [(b"h.obj", b"host opaque", list(SYMBOLS))])

        def worker(argv, stdout, stderr, budget, **_options):
            self.assertIn("--inventory", argv)
            position = argv.index("--inventory")
            request, output = map(Path, argv[position + 1:position + 3])
            context = json.loads(request.read_text(encoding="utf-8"))
            inventory = evidence.inventory_request(context, evidence.Budget(10))
            output.write_text(json.dumps(inventory, sort_keys=True) + "\n", encoding="utf-8")
            stdout.write_bytes(b"")
            stderr.write_bytes(b"")
            return {"status": "completed", "returncode": 0, "elapsed_seconds": 0.001,
                    "cleanup": {"reaped": True, "kill_requested": False,
                                "complete": True, "error": None}}

        with mock.patch.object(evidence, "run_child", side_effect=worker), \
                mock.patch.object(subprocess, "Popen", side_effect=AssertionError("real process")):
            status = evidence.collect_failure(self.context(), self.report)
        self.assertIsInstance(status, str)
        self.assertLessEqual(len(status.encode("utf-8")), 1024)
        manifest_path = self.report / "manifest.json"
        self.assertTrue(manifest_path.is_file())
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(manifest["schema"], "neverc.crt-storage-evidence.v1")
        self.assertEqual(manifest["audit_outcome"], "rejected")
        self.assertEqual(manifest["status"], "incomplete")
        self.assertEqual(manifest["evidence_scope"], "observed-index-representatives-only")
        return manifest

    def test_explicitly_incomplete_diagnostics_can_be_verified_without_becoming_abi_success(self):
        manifest = self.collected_report()
        expected = {"manifest.json", *(entry["name"] for entry in manifest["files"])}
        original_archive = self.private.read_bytes()
        result = evidence.verify_report(self.report)
        self.assertTrue(result["upload_ready"])
        self.assertEqual(result["status"], "ready")
        upload = self.report / "upload"
        self.assertEqual({path.name for path in upload.iterdir()}, expected)
        for name in expected:
            path = upload / name
            self.assertTrue(path.is_file())
            self.assertFalse(path.is_symlink())
            self.assertLessEqual(path.stat().st_size, MIB)
            path.read_bytes().decode("utf-8", errors="strict")
            self.assertFalse((self.report / name).exists(), "export must move rather than duplicate")
            self.assertTrue(name == "manifest.json" or name.endswith(".txt"))
        uploaded = json.loads((upload / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(uploaded["status"], "incomplete")
        self.assertEqual(uploaded["audit_outcome"], "rejected")
        self.assertEqual(self.private.read_bytes(), original_archive)
        self.assertLessEqual(sum(path.stat().st_size for path in upload.iterdir()), 8 * MIB)

    def test_content_tampering_cannot_be_uploaded(self):
        manifest = self.collected_report()
        target = self.report / manifest["files"][0]["name"]
        target.write_bytes(target.read_bytes() + b" changed")
        result = evidence.verify_report(self.report)
        self.assertFalse(result["upload_ready"])
        self.assertEqual(result["status"], "invalid")

    def test_unlisted_file_prevents_export(self):
        self.collected_report()
        (self.report / "unexpected.obj").write_bytes(b"MZ binary must not be published")
        result = evidence.verify_report(self.report)
        self.assertFalse(result["upload_ready"])
        self.assertFalse((self.report / "upload" / "unexpected.obj").exists())

    def test_hash_correct_invalid_utf8_is_rejected(self):
        manifest = self.collected_report()
        entry = manifest["files"][0]
        payload = b"\xff\xfe invalid UTF-8"
        (self.report / entry["name"]).write_bytes(payload)
        entry.update(size=len(payload), sha256=hashlib.sha256(payload).hexdigest())
        (self.report / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        result = evidence.verify_report(self.report)
        self.assertFalse(result["upload_ready"])

    def test_hash_correct_oversized_text_is_rejected(self):
        manifest = self.collected_report()
        entry = manifest["files"][0]
        payload = b"x" * (MIB + 1)
        (self.report / entry["name"]).write_bytes(payload)
        entry.update(size=len(payload), sha256=hashlib.sha256(payload).hexdigest())
        (self.report / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        result = evidence.verify_report(self.report)
        self.assertFalse(result["upload_ready"])

    def test_duplicate_or_traversing_inventory_names_are_not_accepted(self):
        for mode in ("duplicate", "traversal"):
            with self.subTest(mode=mode):
                self.report = self.root / ("report-" + mode)
                manifest = self.collected_report()
                if mode == "duplicate":
                    manifest["files"].append(dict(manifest["files"][0]))
                else:
                    manifest["files"][0]["name"] = "../private.lib"
                (self.report / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
                result = evidence.verify_report(self.report)
                self.assertFalse(result["upload_ready"])
                self.assertTrue(self.private.is_file())

    def test_verification_is_one_shot_even_after_a_rejected_report_is_repaired(self):
        self.collected_report()
        unexpected = self.report / "unexpected.txt"
        unexpected.write_bytes(b"unlisted")
        first = evidence.verify_report(self.report)
        self.assertFalse(first["upload_ready"])
        unexpected.unlink()
        second = evidence.verify_report(self.report)
        self.assertFalse(second["upload_ready"])

def long_name_archive(table, objects, first_entries, second_entries):
    """Independent COFF container with a // table and opaque object bodies.

    Objects are (long-table byte offset, body); returned locations retain every
    physical member, including repeated names and repeated index entries.
    """
    def member(name, body):
        header = (name.ljust(16, b" ") + b"0".ljust(12, b" ") + b" " * 12 +
                  b"100644".ljust(8, b" ") +
                  str(len(body)).encode("ascii").ljust(10, b" ") + b"`\n")
        if len(header) != 60:
            raise ValueError("fixture header length")
        return header + body + (b"\n" if len(body) % 2 else b"")

    def indices(offsets):
        first = struct.pack(">I", len(first_entries))
        first += b"".join(struct.pack(">I", offsets[n]) for _, n in first_entries)
        first += b"".join(name.encode("ascii") + b"\0" for name, _ in first_entries)
        second = struct.pack("<I", len(objects))
        second += b"".join(struct.pack("<I", offset) for offset in offsets)
        second += struct.pack("<I", len(second_entries))
        second += b"".join(struct.pack("<H", n + 1) for _, n in second_entries)
        second += b"".join(name.encode("ascii") + b"\0" for name, _ in second_entries)
        return member(b"/", first) + member(b"/", second)

    long_member = member(b"//", table)
    position = 8 + len(indices([0] * len(objects))) + len(long_member)
    locations, bodies = [], []
    for offset, payload in objects:
        locations.append((position, position + 60, len(payload)))
        body = member(b"/" + str(offset).encode("ascii"), payload)
        bodies.append(body)
        position += len(body)
    return (b"!<arch>\n" + indices([item[0] for item in locations]) +
            long_member + b"".join(bodies)), locations


class LongNameBudgetTests(unittest.TestCase):
    def test_repeated_offset_reuses_identity_without_work_or_retained_charge(self):
        budget = evidence.Budget(10)
        names = evidence._LongNameIdentities(b"abc\0abc\0", budget)
        first = names.identity(0)
        self.assertEqual(first, {"preview": "b'abc'", "bytes": 3,
                                 "sha256": hashlib.sha256(b"abc").hexdigest()})
        for _ in range(100):
            self.assertIs(names.identity(0), first)
        self.assertEqual(names.work_bytes, 7)
        self.assertEqual(set(names.cache), {0})
        self.assertEqual(budget.used_bytes, 0)
        second = names.identity(4)
        self.assertEqual(second, first)
        self.assertIsNot(second, first)
        self.assertEqual(names.work_bytes, 14)
        self.assertEqual(set(names.cache), {0, 4})
        self.assertEqual(budget.used_bytes, 0)

    def test_single_name_exact_limit_accepts_and_one_byte_over_rejects(self):
        with mock.patch.object(evidence, "NAME_BYTE_LIMIT", 3):
            good = evidence._LongNameIdentities(b"abc\0", evidence.Budget(10))
            self.assertEqual(good.identity(0)["bytes"], 3)
            self.assertEqual(good.work_bytes, 7)
            bad = evidence._LongNameIdentities(b"abcd\0", evidence.Budget(10))
            with self.assertRaisesRegex(evidence.EvidenceLimit, "name byte limit"):
                bad.identity(0)
            self.assertEqual(bad.cache, {})
            self.assertEqual(bad.work_bytes, 4)

    def test_cumulative_work_exact_limit_and_cached_read_at_limit(self):
        with mock.patch.object(evidence, "NAME_WORK_LIMIT", 14):
            names = evidence._LongNameIdentities(b"abc\0abc\0z\0", evidence.Budget(10))
            names.identity(0)
            names.identity(4)
            self.assertEqual(names.work_bytes, 14)
            self.assertIs(names.identity(0), names.cache[0])
            with self.assertRaisesRegex(evidence.EvidenceLimit, "name work limit"):
                names.identity(8)
            self.assertEqual(set(names.cache), {0, 4})
            self.assertEqual(names.work_bytes, 14)
        with mock.patch.object(evidence, "NAME_WORK_LIMIT", 6):
            names = evidence._LongNameIdentities(b"abc\0", evidence.Budget(10))
            with self.assertRaisesRegex(evidence.EvidenceLimit, "name work limit"):
                names.identity(0)
            self.assertEqual(names.work_bytes, 4)
            self.assertEqual(names.cache, {})

    def test_chunk_scan_hashes_full_name_and_retains_only_bounded_preview(self):
        raw = b"a" * 159 + b"\xff" + b"z" * 13
        with mock.patch.object(evidence, "CHUNK", 7):
            names = evidence._LongNameIdentities(raw + b"\0", evidence.Budget(10))
            result = names.identity(0)
        self.assertEqual(result, {"preview": repr(raw[:160]), "bytes": 173,
                                  "sha256": hashlib.sha256(raw).hexdigest()})
        self.assertEqual(names.work_bytes, 347)
        self.assertEqual(set(names.cache[0]), {"preview", "bytes", "sha256"})

    def test_missing_nul_empty_name_and_invalid_offsets_reject(self):
        for table, message in ((b"abc", "unterminated"), (b"\0", "empty")):
            with self.subTest(table=table):
                names = evidence._LongNameIdentities(table, evidence.Budget(10))
                with self.assertRaisesRegex(ValueError, message):
                    names.identity(0)
                self.assertEqual(names.cache, {})
        for offset in (-1, 4, True, "0"):
            with self.subTest(offset=offset):
                names = evidence._LongNameIdentities(b"abc\0", evidence.Budget(10))
                with self.assertRaisesRegex(ValueError, "out of bounds"):
                    names.identity(offset)
                self.assertEqual(names.work_bytes, 0)

    def test_deadline_is_checked_even_for_a_cached_identity(self):
        clock = Clock()
        budget = evidence.Budget(10, clock=clock)
        names = evidence._LongNameIdentities(b"abc\0", budget)
        names.identity(0)
        clock.now += 10
        with self.assertRaisesRegex(evidence.EvidenceLimit, "deadline"):
            names.identity(0)
        self.assertEqual(names.work_bytes, 7)


class LongNameArchiveTests(TemporaryEvidenceTest):
    def fixture(self, *, second_entries=None):
        name = b"same-long-member-name.obj"
        objects = [(0, b"opaque first"), (0, b"opaque second"),
                   (len(name) + 1, b"opaque third")]
        first = [(PRINTF, 0), (PRINTF, 0), (PRINTF, 1), (SCANF, 1), (AVX, 2)]
        second = sorted(first) if second_entries is None else second_entries
        data, locations = long_name_archive(name + b"\0" + name + b"\0",
                                            objects, first, second)
        self.private.write_bytes(data)
        return name, objects, first, second, data, locations

    @staticmethod
    def rows_digest(rows):
        # Independent serialization oracle for the documented ordered digest.
        data = b"".join((json.dumps(row, sort_keys=True, ensure_ascii=True,
                                   separators=(",", ":")) + "\n").encode("ascii")
                        for row in rows)
        return hashlib.sha256(data).hexdigest()

    def test_long_table_preserves_all_physical_members_indices_and_digests(self):
        name, objects, first, second, data, locations = self.fixture()
        instances = []
        original = evidence._LongNameIdentities

        def observe(table, budget):
            instance = original(table, budget)
            instances.append(instance)
            return instance

        budget = evidence.Budget(10)
        with mock.patch.object(evidence, "_LongNameIdentities", side_effect=observe):
            result = evidence.inventory_archive(self.private, list(SYMBOLS), budget)
        self.assertEqual(result["status"], "complete", result.get("error"))
        self.assertEqual(result["member_count"], 3)
        self.assertEqual(result["archive"]["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(result["archive"]["sha256_status"], "complete")
        identity = {"preview": repr(name), "bytes": len(name),
                    "sha256": hashlib.sha256(name).hexdigest()}
        expected_members = [[n, n + 3, header, payload, size, identity]
                            for n, (header, payload, size) in enumerate(locations)]
        self.assertEqual(result["member_inventory_sha256"],
                         self.rows_digest(expected_members))
        for label, entries in (("first", first), ("second", second)):
            rows = [[i, symbol, n, locations[n][0]]
                    for i, (symbol, n) in enumerate(entries)]
            self.assertEqual(result["index"][label + "_count"], 5)
            self.assertEqual(result["index"][label + "_candidate_count"], 5)
            self.assertEqual(result["index"][label + "_candidates_sha256"],
                             self.rows_digest(rows))
        self.assertEqual(result["index"]["candidates_by_symbol"], {
            PRINTF: {"first": 3, "second": 3}, SCANF: {"first": 1, "second": 1},
            AVX: {"first": 1, "second": 1}})
        representatives = {item["symbol"]: item for item in result["representatives"]}
        for symbol, n, index in ((PRINTF, 0, 0), (SCANF, 1, 3), (AVX, 2, 4)):
            item = representatives[symbol]
            self.assertEqual((item["member_ordinal"], item["archive_member_ordinal"],
                              item["first_index_ordinal"]), (n, n + 3, index))
            self.assertEqual((item["header_offset"], item["payload_offset"],
                              item["payload_size"]), locations[n])
            self.assertEqual(item["member_name"], identity)
            self.assertEqual(item["payload_sha256"],
                             hashlib.sha256(objects[n][1]).hexdigest())
        self.assertEqual(len(instances), 1)
        self.assertEqual(set(instances[0].cache), {0, len(name) + 1})
        self.assertEqual(instances[0].work_bytes, 2 * (2 * len(name) + 1))
        self.assertEqual(budget.used_bytes, 0)

    def test_coalesced_second_index_keeps_its_own_multiplicity(self):
        second = sorted([(PRINTF, 1), (SCANF, 1), (AVX, 2)])
        self.fixture(second_entries=second)
        result = evidence.inventory_archive(self.private, list(SYMBOLS), evidence.Budget(10))
        self.assertEqual(result["status"], "complete", result.get("error"))
        self.assertEqual(result["index"]["first_count"], 5)
        self.assertEqual(result["index"]["second_count"], 3)
        self.assertEqual(result["index"]["candidates_by_symbol"][PRINTF],
                         {"first": 3, "second": 1})
        item = next(item for item in result["representatives"] if item["symbol"] == PRINTF)
        self.assertEqual(item["member_ordinal"], 0)

    def test_long_name_limits_produce_incomplete_inventory(self):
        name, *_ = self.fixture()
        for setting, limit in (("NAME_BYTE_LIMIT", len(name) - 1),
                               ("NAME_WORK_LIMIT", 2 * (2 * len(name) + 1) - 1)):
            with self.subTest(setting=setting), mock.patch.object(evidence, setting, limit):
                result = evidence.inventory_archive(self.private, list(SYMBOLS),
                                                    evidence.Budget(10))
                self.assertEqual(result["status"], "incomplete")
                self.assertIn("limit", result["error"])
                self.assertNotEqual(result["archive"]["sha256_status"], "complete")
                self.assertEqual(result["representatives"], [])

    def test_bad_long_table_references_do_not_bypass_container_validation(self):
        for table, offset in ((b"name", 0), (b"name\0", 5), (b"name\0", 1)):
            with self.subTest(table=table, offset=offset):
                entries = [(PRINTF, 0)]
                data, _ = long_name_archive(table, [(offset, b"opaque")], entries, entries)
                self.private.write_bytes(data)
                result = evidence.inventory_archive(self.private, [PRINTF], evidence.Budget(10))
                self.assertEqual(result["status"], "incomplete")
                self.assertIn("long", result["error"])
                self.assertEqual(result["representatives"], [])


class AdjacentReaderTests(TemporaryEvidenceTest):
    def reader_location(self):
        directory = self.root / "isolated"
        directory.mkdir()
        directory = directory.resolve()
        return directory / "CollectCrtStorageEvidence.py", directory / "HostCoffSymbols.py"

    def test_loader_uses_regular_sibling_and_replaces_both_module_cache_baits(self):
        script, reader = self.reader_location()
        reader.write_text("from dataclasses import dataclass\n"
                          "@dataclass\nclass Witness:\n    value: int = 7\n",
                          encoding="utf-8")
        ordinary_bait, private_bait = object(), object()
        bait_dir = self.root / "bait"
        bait_dir.mkdir()
        (bait_dir / "HostCoffSymbols.py").write_text("raise AssertionError('ambient reader')\n",
                                                    encoding="utf-8")
        cache_name = "_neverc_crt_storage_coff_reader"
        with mock.patch.object(evidence, "__file__", str(script)), \
                mock.patch.dict(sys.modules, {"HostCoffSymbols": ordinary_bait,
                                              cache_name: private_bait}), \
                mock.patch.dict(os.environ, {"PYTHONPATH": str(bait_dir)}):
            first = evidence._load_coff_reader()
            self.assertEqual(Path(first.__file__), reader)
            self.assertEqual(first.Witness().value, 7)
            self.assertIs(sys.modules[cache_name], first)
            self.assertIs(sys.modules["HostCoffSymbols"], ordinary_bait)
            second = evidence._load_coff_reader()
            self.assertIsNot(second, first)
            self.assertIs(sys.modules[cache_name], second)
        self.assertEqual(list(self.root.rglob("__pycache__")), [])
        self.assertEqual(list(self.root.rglob("*.pyc")), [])

    def test_missing_or_directory_sibling_never_falls_back(self):
        script, reader = self.reader_location()
        cache_name = "_neverc_crt_storage_coff_reader"
        for directory in (False, True):
            with self.subTest(directory=directory):
                if directory:
                    reader.mkdir()
                bait = object()
                with mock.patch.object(evidence, "__file__", str(script)), \
                        mock.patch.dict(sys.modules, {"HostCoffSymbols": bait, cache_name: bait}), \
                        mock.patch.object(evidence.importlib.util, "spec_from_file_location") as spec:
                    with self.assertRaises((OSError, ValueError)):
                        evidence._load_coff_reader()
                    spec.assert_not_called()
                    self.assertIs(sys.modules[cache_name], bait)

    def test_symlink_sibling_is_rejected_before_loading(self):
        script, reader = self.reader_location()
        target = self.root / "real-reader.py"
        target.write_text("raise AssertionError('symlink reader executed')\n", encoding="utf-8")
        try:
            reader.symlink_to(target)
        except NotImplementedError as error:
            self.skipTest("symlink creation unavailable: " + str(error))
        except OSError as error:
            if error.errno in (errno.EPERM, errno.EACCES, errno.ENOSYS, errno.ENOTSUP) or \
                    getattr(error, "winerror", None) == 1314:
                self.skipTest("symlink creation unavailable: " + str(error))
            raise
        with mock.patch.object(evidence, "__file__", str(script)), \
                mock.patch.object(evidence.importlib.util, "spec_from_file_location") as spec:
            with self.assertRaisesRegex(ValueError, "regular non-symlink"):
                evidence._load_coff_reader()
            spec.assert_not_called()

    def test_failed_sibling_execution_clears_private_cache_without_fallback(self):
        script, reader = self.reader_location()
        reader.write_text("raise RuntimeError('owned reader failure')\n", encoding="utf-8")
        cache_name = "_neverc_crt_storage_coff_reader"
        bait = object()
        with mock.patch.object(evidence, "__file__", str(script)), \
                mock.patch.dict(sys.modules, {"HostCoffSymbols": bait, cache_name: bait}):
            with self.assertRaisesRegex(RuntimeError, "owned reader failure"):
                evidence._load_coff_reader()
            self.assertNotIn(cache_name, sys.modules)
            self.assertIs(sys.modules["HostCoffSymbols"], bait)
        self.assertEqual(list(self.root.rglob("*.pyc")), [])

    def test_real_collection_launches_an_isolated_worker_with_only_adjacent_reader(self):
        # CI executes a real, bounded Python child, never a compiler/LLVM tool.
        # Production and isolated workers must load the same regular adjacent reader.
        source = Path(evidence.__file__).resolve()
        real_reader = source.with_name("HostCoffSymbols.py")
        self.assertTrue(real_reader.is_file())
        self.assertFalse(real_reader.is_symlink())
        script, reader = self.reader_location()
        script.write_bytes(source.read_bytes())
        reader.write_bytes(real_reader.read_bytes())
        bait_dir = self.root / "bait"
        bait_dir.mkdir()
        for name in ("HostCoffSymbols.py", "sitecustomize.py"):
            (bait_dir / name).write_text("raise RuntimeError('PYTHONPATH bait executed')\n",
                                         encoding="utf-8")
        entries = [(PRINTF, 0)]
        data, _ = long_name_archive(b"long-member.obj\0", [(0, b"opaque")], entries, entries)
        self.private.write_bytes(data)
        self.host.write_bytes(data)
        original = evidence.run_child

        def bounded_real_child(argv, stdout, stderr, budget, **options):
            self.assertEqual(argv[:4], [sys.executable, "-I", "-B", str(script)])
            self.assertEqual(argv[4], "--inventory")
            options["timeout_seconds"] = min(15, options["timeout_seconds"])
            return original(argv, stdout, stderr, budget, **options)

        with mock.patch.object(evidence, "__file__", str(script)), \
                mock.patch.dict(os.environ, {"PYTHONPATH": str(bait_dir)}), \
                mock.patch.object(evidence, "run_child", side_effect=bounded_real_child) as launch:
            evidence.collect_failure(self.context(selected=(PRINTF,)), self.report)
        self.assertEqual(launch.call_count, 1)
        manifest = json.loads((self.report / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["status"], "incomplete", "missing tools are not ABI evidence")
        self.assertEqual(manifest["audit_outcome"], "rejected")
        self.assertEqual(len(manifest["commands"]), 1)
        command = manifest["commands"][0]
        self.assertEqual(command["argv"][:5], [sys.executable, "-I", "-B", str(script), "--inventory"])
        self.assertEqual(command["status"], "completed", command)
        self.assertEqual(command["returncode"], 0, command)
        self.assertTrue(command["cleanup"]["complete"])
        self.assertTrue(command["cleanup"]["reaped"])
        inventory = json.loads((self.report / "inventory.txt").read_text(encoding="utf-8"))
        self.assertEqual(inventory["status"], "complete", inventory)
        self.assertEqual(inventory["evidence_scope"], "observed-index-representatives-only")
        self.assertEqual(inventory["counts"]["processed"], 2)
        self.assertEqual(inventory["counts"]["selected"], 2)
        self.assertEqual({item["side"] for item in inventory["representatives"]}, {"private", "host"})
        self.assertEqual(list(self.root.rglob("__pycache__")), [])
        self.assertEqual(list(self.root.rglob("*.pyc")), [])


class ManifestConsistencyTests(TemporaryEvidenceTest):
    def collected_report(self, *, create_tools=True, worker_status="completed"):
        """Exercise the producer; only native/process boundaries are doubles."""
        self.write_archive(self.private, [(b"p.obj", b"opaque private", list(SYMBOLS))])
        self.write_archive(self.host, [(b"h.obj", b"opaque host", list(SYMBOLS))])
        tool_paths = (self.private_nm, self.private_readobj, self.host_nm,
                      self.host_nm.with_name("llvm-readobj.exe"),
                      self.host_nm.with_name("llvm-dis.exe"),
                      self.host_nm.with_name("llvm-bcanalyzer.exe"))
        if create_tools:
            for path in tool_paths:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"MZ inert version fixture, never executed")

        def child(argv, stdout, stderr, budget, **options):
            is_worker = "--inventory" in argv
            if is_worker:
                self.assertEqual(argv[1:3], ["-I", "-B"])
                self.assertEqual(argv[4], "--inventory")
                request, output = map(Path, argv[5:7])
                context = json.loads(request.read_text(encoding="utf-8"))
                inventory = evidence.inventory_request(context, evidence.Budget(10))
                self.assertEqual(inventory["status"], "complete", inventory)
                output.write_text(json.dumps(inventory, sort_keys=True) + "\n", encoding="utf-8")
                stdout.write_bytes(b"")
            else:
                self.assertEqual(argv[1:], ["--version"])
                self.assertIn(Path(argv[0]), tool_paths)
                version = "20.1.8" if Path(argv[0]) in tool_paths[:2] else "22.1.8"
                stdout.write_bytes(("LLVM version " + version + "\n").encode("ascii"))
            stderr.write_bytes(b"")
            status = worker_status if is_worker else "completed"
            return {"status": status, "returncode": 0 if status == "completed" else -9,
                    "elapsed_seconds": 0.001,
                    "cleanup": {"reaped": True, "kill_requested": status != "completed",
                                "complete": True, "error": None}}

        with mock.patch.object(evidence, "run_child", side_effect=child), \
                mock.patch.object(subprocess, "Popen", side_effect=AssertionError("real process")):
            evidence.collect_failure(self.context(), self.report)
        manifest = json.loads((self.report / "manifest.json").read_text(encoding="utf-8"))
        expected = "complete" if create_tools and worker_status == "completed" else "incomplete"
        self.assertEqual(manifest["status"], expected, manifest)
        return manifest

    def save_manifest(self, manifest):
        (self.report / "manifest.json").write_text(json.dumps(manifest, sort_keys=True) + "\n",
                                                    encoding="utf-8")

    def test_complete_producer_report_is_verifiable_without_claiming_abi_success(self):
        self.collected_report()
        result = evidence.verify_report(self.report)
        self.assertTrue(result["upload_ready"], result)
        uploaded = json.loads((self.report / "upload/manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(uploaded["status"], "complete")
        self.assertEqual(uploaded["audit_outcome"], "rejected")
        self.assertEqual(uploaded["evidence_scope"], "observed-index-representatives-only")

    def test_promoting_missing_tool_report_to_complete_is_rejected(self):
        manifest = self.collected_report(create_tools=False)
        original_inventory = (self.report / "inventory.txt").read_bytes()
        manifest["status"] = "complete"
        self.save_manifest(manifest)
        result = evidence.verify_report(self.report)
        self.assertFalse(result["upload_ready"])
        self.assertIn("producer stages", result["error"])
        self.assertEqual((self.report / "inventory.txt").read_bytes(), original_inventory)

    def test_summary_file_status_and_type_sensitive_counts_must_match_inventory(self):
        for change in ("file", "status", "count-value", "count-type"):
            with self.subTest(change=change):
                self.report = self.root / ("summary-" + change)
                manifest = self.collected_report()
                original_inventory = (self.report / "inventory.txt").read_bytes()
                original_files = json.dumps(manifest["files"], sort_keys=True)
                summary = manifest["inventory"]
                if change == "file":
                    summary["file"] = "request.txt"
                elif change == "status":
                    summary["status"] = "incomplete"
                elif change == "count-value":
                    summary["counts"]["processed"] += 1
                else:
                    summary["counts"]["processed"] = float(summary["counts"]["processed"])
                self.save_manifest(manifest)
                self.assertEqual(json.dumps(manifest["files"], sort_keys=True), original_files)
                result = evidence.verify_report(self.report)
                self.assertFalse(result["upload_ready"])
                self.assertIn("inventory summary", result["error"])
                self.assertEqual((self.report / "inventory.txt").read_bytes(), original_inventory)

    def test_complete_report_requires_consistent_stages_and_successful_isolated_worker(self):
        for change in ("role", "tool-status", "tool-command", "returncode-type",
                       "worker-flags", "worker-missing", "cleanup", "summary-missing"):
            with self.subTest(change=change):
                self.report = self.root / ("stages-" + change)
                manifest = self.collected_report()
                if change == "role":
                    manifest["tools"][0]["role"] = "host-nm"
                elif change == "tool-status":
                    manifest["tools"][0]["status"] = "incomplete"
                elif change == "tool-command":
                    manifest["tools"][0]["command"]["returncode"] = False
                elif change == "returncode-type":
                    manifest["commands"][-1]["returncode"] = False
                elif change == "worker-flags":
                    manifest["commands"][-1]["argv"][1] = "-E"
                elif change == "worker-missing":
                    manifest["commands"].pop()
                elif change == "cleanup":
                    manifest["cleanup"]["complete"] = False
                else:
                    manifest["inventory"] = None
                self.save_manifest(manifest)
                result = evidence.verify_report(self.report)
                self.assertFalse(result["upload_ready"])
                self.assertIn("complete manifest", result["error"])
                self.assertFalse((self.report / "upload").exists())

    def test_failed_worker_may_leave_valid_inventory_without_a_summary(self):
        manifest = self.collected_report(worker_status="timeout")
        self.assertIsNone(manifest["inventory"])
        self.assertTrue((self.report / "inventory.txt").is_file())
        result = evidence.verify_report(self.report)
        self.assertTrue(result["upload_ready"], result)
        uploaded = json.loads((self.report / "upload/manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(uploaded["status"], "incomplete")
        self.assertEqual(uploaded["commands"][-1]["status"], "timeout")
        self.assertEqual(uploaded["audit_outcome"], "rejected")

    def test_real_isolated_worker_with_missing_tools_remains_verifiable(self):
        # This additional integration case reaches export after the real child.
        self.write_archive(self.private, [(b"p.obj", b"opaque private", [PRINTF])])
        self.write_archive(self.host, [(b"h.obj", b"opaque host", [PRINTF])])
        original = evidence.run_child

        def bounded_child(argv, stdout, stderr, budget, **options):
            self.assertEqual(argv[1:3], ["-I", "-B"])
            self.assertEqual(argv[4], "--inventory")
            options["timeout_seconds"] = min(15, options["timeout_seconds"])
            return original(argv, stdout, stderr, budget, **options)

        with mock.patch.object(evidence, "run_child", side_effect=bounded_child) as launch:
            evidence.collect_failure(self.context(selected=(PRINTF,)), self.report)
        self.assertEqual(launch.call_count, 1)
        manifest = json.loads((self.report / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["status"], "incomplete")
        self.assertEqual(manifest["inventory"]["status"], "complete")
        self.assertEqual(manifest["commands"][0]["status"], "completed")
        self.assertEqual(manifest["commands"][0]["returncode"], 0)
        result = evidence.verify_report(self.report)
        self.assertTrue(result["upload_ready"], result)
        self.assertEqual(list(self.root.rglob("*.pyc")), [])


class FileIdentityCompatibilityTests(TemporaryEvidenceTest):
    """Keep path/descriptor comparisons distinct from same-API stability."""

    def metadata(self, **changes):
        values = {"st_mode": stat.S_IFREG | 0o600, "st_dev": 17,
                  "st_ino": (1 << 100) + 23, "st_size": 7,
                  "st_mtime_ns": 300, "st_ctime_ns": 100,
                  "st_birthtime_ns": 100}
        values.update(changes)
        return SimpleNamespace(**values)

    def os_view(self, *, name="nt", fstat=None):
        # Do not mutate the process-wide os.name: pathlib must keep using
        # the native path class while only the collector selects its policy.
        return SimpleNamespace(
            name=name, O_RDONLY=os.O_RDONLY,
            O_BINARY=getattr(os, "O_BINARY", 0),
            O_NONBLOCK=getattr(os, "O_NONBLOCK", 0),
            O_NOFOLLOW=getattr(os, "O_NOFOLLOW", 0),
            open=mock.Mock(wraps=os.open),
            fstat=os.fstat if fstat is None else fstat,
            fdopen=os.fdopen, close=mock.Mock(wraps=os.close))

    def assert_failed_open_closed_descriptor(self, view):
        view.close.assert_called_once()
        descriptor = view.close.call_args.args[0]
        with self.assertRaises(OSError) as caught:
            os.fstat(descriptor)
        self.assertEqual(caught.exception.errno, errno.EBADF)

    def test_windows_cross_api_times_allow_read_hash_and_preserve_raw_fields(self):
        payload = b"payload"
        self.private.write_bytes(payload)
        path_before = self.metadata()
        fd_before = self.metadata(st_ctime_ns=200)
        view = self.os_view(fstat=mock.Mock(side_effect=[fd_before, fd_before]))
        with mock.patch.object(evidence, "os", view), \
                mock.patch.object(Path, "lstat", return_value=path_before):
            stream, observed, path_observed = evidence._regular_open(self.private)
            with stream:
                self.assertEqual(stream.read(), payload)
                digest = evidence._hash_region(stream, 0, len(payload),
                                               evidence.Budget(10))
                fd_after, path_after = evidence._stable(
                    self.private, stream, observed, path_observed)
            self.assertEqual(evidence._comparison_identity(observed),
                             {"device": 17, "inode": (1 << 100) + 23,
                              "size": 7, "mtime_ns": 300,
                              "birthtime_ns": 100})
        self.assertEqual(digest, hashlib.sha256(payload).hexdigest())
        self.assertIs(observed, fd_before)
        self.assertIs(path_observed, path_before)
        self.assertEqual(fd_after, {"device": 17, "inode": (1 << 100) + 23,
                                    "size": 7, "mtime_ns": 300,
                                    "ctime_ns": 200})
        self.assertEqual(path_after, {"device": 17, "inode": (1 << 100) + 23,
                                      "size": 7, "mtime_ns": 300,
                                      "ctime_ns": 100})

    def test_windows_cross_api_identity_mismatch_is_rejected_and_closed(self):
        self.private.write_bytes(b"payload")
        changes = {"st_dev": 18, "st_ino": (1 << 101) + 23,
                   "st_size": 8, "st_mtime_ns": 301,
                   "st_birthtime_ns": 101}
        for field, value in changes.items():
            with self.subTest(field=field):
                path_before = self.metadata()
                fd_before = self.metadata(st_ctime_ns=200, **{field: value})
                view = self.os_view(fstat=mock.Mock(return_value=fd_before))
                with mock.patch.object(evidence, "os", view), \
                        mock.patch.object(Path, "lstat", return_value=path_before):
                    with self.assertRaisesRegex(
                            ValueError, "input identity changed while opening"):
                        evidence._regular_open(self.private)
                self.assert_failed_open_closed_descriptor(view)

    def test_windows_same_api_ctime_and_birthtime_changes_are_rejected(self):
        self.private.write_bytes(b"payload")
        for side in ("path", "fd"):
            for field in ("st_ctime_ns", "st_birthtime_ns"):
                with self.subTest(side=side, field=field):
                    path_before = self.metadata()
                    fd_before = self.metadata(st_ctime_ns=200)
                    path_after, fd_after = path_before, fd_before
                    if side == "path":
                        path_after = self.metadata(**{field: 999})
                    else:
                        values = {"st_ctime_ns": 200, field: 999}
                        fd_after = self.metadata(**values)
                    view = self.os_view(fstat=mock.Mock(
                        side_effect=[fd_before, fd_after]))
                    with mock.patch.object(evidence, "os", view), \
                            mock.patch.object(Path, "lstat", side_effect=[
                                path_before, path_after]):
                        stream, observed, path_observed = evidence._regular_open(
                            self.private)
                        with stream, self.assertRaisesRegex(
                                ValueError, "input changed or was replaced"):
                            evidence._stable(self.private, stream, observed,
                                             path_observed)

    def test_stable_requires_final_path_and_descriptor_to_name_same_object(self):
        self.private.write_bytes(b"payload")
        fd_before = self.metadata(st_ctime_ns=200)
        path_before = self.metadata(st_ino=(1 << 101) + 23)
        view = self.os_view(fstat=mock.Mock(return_value=fd_before))
        # Both API-specific baselines remain unchanged. Their disagreement
        # must still fail the final cross-API check.
        with self.private.open("rb") as stream, \
                mock.patch.object(evidence, "os", view), \
                mock.patch.object(Path, "lstat", return_value=path_before):
            with self.assertRaisesRegex(ValueError, "input changed or was replaced"):
                evidence._stable(self.private, stream, fd_before, path_before)

    def test_posix_cross_api_ctime_difference_is_still_rejected(self):
        self.private.write_bytes(b"payload")
        path_before = self.metadata()
        fd_before = self.metadata(st_ctime_ns=200)
        view = self.os_view(name="posix", fstat=mock.Mock(return_value=fd_before))
        with mock.patch.object(evidence, "os", view), \
                mock.patch.object(Path, "lstat", return_value=path_before):
            self.assertEqual(evidence._comparison_identity(path_before),
                             {"device": 17, "inode": (1 << 100) + 23,
                              "size": 7, "mtime_ns": 300, "ctime_ns": 100})
            with self.assertRaisesRegex(ValueError,
                                        "input identity changed while opening"):
                evidence._regular_open(self.private)
        self.assert_failed_open_closed_descriptor(view)

    def test_windows_missing_or_noninteger_birthtime_fails_closed(self):
        self.private.write_bytes(b"payload")
        for side in ("path", "fd"):
            for value in ("missing", None, True, 100.0, "100"):
                with self.subTest(side=side, value=value):
                    path_before = self.metadata()
                    fd_before = self.metadata(st_ctime_ns=200)
                    invalid = path_before if side == "path" else fd_before
                    if value == "missing":
                        del invalid.st_birthtime_ns
                    else:
                        invalid.st_birthtime_ns = value
                    view = self.os_view(fstat=mock.Mock(return_value=fd_before))
                    with mock.patch.object(evidence, "os", view), \
                            mock.patch.object(Path, "lstat", return_value=path_before):
                        with self.assertRaisesRegex(
                                ValueError, "Windows file creation time is unavailable"):
                            evidence._regular_open(self.private)
                    self.assert_failed_open_closed_descriptor(view)

    def test_real_regular_file_reads_hashes_and_retains_both_baselines(self):
        payload = b"owned real file; no native tool execution\n"
        self.private.write_bytes(payload)
        stream, fd_before, path_before = evidence._regular_open(self.private)
        with stream:
            self.assertEqual(stream.read(), payload)
            digest = evidence._hash_region(stream, 0, len(payload),
                                           evidence.Budget(10))
            fd_after, path_after = evidence._stable(
                self.private, stream, fd_before, path_before)
        self.assertEqual(digest, hashlib.sha256(payload).hexdigest())
        self.assertEqual(fd_after["inode"], fd_before.st_ino)
        self.assertEqual(path_after["inode"], path_before.st_ino)
        self.assertEqual(fd_after["ctime_ns"], fd_before.st_ctime_ns)
        self.assertEqual(path_after["ctime_ns"], path_before.st_ctime_ns)
        self.assertEqual(fd_after["size"], len(payload))
        self.assertEqual(path_after["size"], len(payload))

    def test_tool_final_check_uses_path_baseline_and_rejects_path_changes(self):
        self.host_nm.parent.mkdir(parents=True)
        payload = b"MZ inert fixture; version execution is mocked"
        self.host_nm.write_bytes(payload)
        original_lstat, original_fstat = Path.lstat, os.fstat
        for change in (None, "st_ctime_ns", "st_birthtime_ns"):
            with self.subTest(change=change):
                directory = self.root / ("probe-" + str(change))
                directory.mkdir()
                version_finished = False

                def observation(raw, ctime):
                    return self.metadata(st_mode=raw.st_mode, st_dev=raw.st_dev,
                                         st_ino=raw.st_ino, st_size=raw.st_size,
                                         st_mtime_ns=raw.st_mtime_ns,
                                         st_ctime_ns=ctime)

                def path_stat(path):
                    result = observation(original_lstat(path), 100)
                    if path == self.host_nm and version_finished and change:
                        setattr(result, change, 999)
                    return result

                def fd_stat(descriptor):
                    return observation(original_fstat(descriptor), 200)

                def child(argv, stdout, stderr, _budget, **_options):
                    nonlocal version_finished
                    self.assertEqual(argv, [str(self.host_nm), "--version"])
                    stdout.write_bytes(b"LLVM version 22.1.8\n")
                    stderr.write_bytes(b"")
                    version_finished = True
                    return {"status": "completed", "returncode": 0,
                            "cleanup": {"reaped": True, "kill_requested": False,
                                        "complete": True}}

                view = self.os_view(fstat=fd_stat)
                with mock.patch.object(evidence, "os", view), \
                        mock.patch.object(Path, "lstat", path_stat), \
                        mock.patch.object(evidence, "run_child", side_effect=child) as launch, \
                        mock.patch.object(subprocess, "Popen", side_effect=AssertionError(
                            "native tools must not execute in this test")):
                    result = evidence.probe_tool("host-nm", self.host_nm, "22.1.8",
                                                 evidence.Budget(10), directory)
                launch.assert_called_once()
                self.assertEqual(result["sha256"], hashlib.sha256(payload).hexdigest())
                self.assertEqual(result["version"], "22.1.8")
                self.assertEqual(result["before"]["ctime_ns"], 200)
                self.assertEqual(result["path_before"]["ctime_ns"], 100)
                self.assertEqual(result["after"]["ctime_ns"], 200)
                self.assertEqual(result["path_after"]["ctime_ns"], 100)
                if change is None:
                    self.assertEqual(result["status"], "complete", result)
                else:
                    self.assertEqual(result["status"], "incomplete", result)
                    self.assertIn("tool changed during version probe", result["error"])


class VerifierDiagnosticCliTests(TemporaryEvidenceTest):
    def invoke_verifier_cli(self, output_name="github-output.txt"):
        output = self.root / output_name
        output.write_bytes(b"existing=preserved\n")
        stdout, stderr = io.StringIO(), io.StringIO()
        environment = {
            "GITHUB_OUTPUT": str(output),
            "NEVERC_TEST_SECRET": "ENVIRONMENT_SENTINEL_MUST_NOT_APPEAR",
        }
        with mock.patch.dict(os.environ, environment, clear=True), \
                mock.patch.object(sys, "stdout", stdout), \
                mock.patch.object(sys, "stderr", stderr):
            code = evidence.main(["--verify-report", str(self.report)])
        return code, stdout.getvalue(), stderr.getvalue(), output.read_bytes()

    def test_real_verifier_error_truncates_raw_characters_before_ascii_escape(self):
        self.report.mkdir()
        (self.report / "collection.claim").mkdir()
        error = ValueError("\U0001f600" * 241 + "DISCARDED_ERROR_SUFFIX")
        with mock.patch.object(evidence, "_read_text", side_effect=error) as read:
            code, stdout, stderr, output = self.invoke_verifier_cli()
        read.assert_called_once()
        self.assertEqual(read.call_args.args[0], self.report / "manifest.json")
        expected = "CRT identity report error: " + r"\ud83d\ude00" * 240 + "\n"
        self.assertEqual(stderr, expected)
        self.assertEqual(len(stderr.encode("ascii")), 2908)
        self.assertEqual(stderr.count("\n"), 1)
        self.assertNotIn("DISCARDED_ERROR_SUFFIX", stdout + stderr)
        self.assertNotIn("ENVIRONMENT_SENTINEL_MUST_NOT_APPEAR", stdout + stderr)
        self.assertEqual(code, 1)
        self.assertEqual(stdout, "CRT identity report: invalid; upload_ready=false\n")
        self.assertEqual(output, b"existing=preserved\nupload_ready=false\n")
        self.assertEqual({path.name for path in self.report.iterdir()},
                         {"collection.claim", "verification.claim"})

    def test_real_verifier_escapes_controls_and_literal_backslashes_only_once(self):
        self.report.mkdir()
        (self.report / "collection.claim").mkdir()
        error = ValueError('line\n\t\r\0"\\literal\\n中é\u2028\u2029\U0001f600')
        with mock.patch.object(evidence, "_read_text", side_effect=error) as read:
            code, stdout, stderr, output = self.invoke_verifier_cli()
        read.assert_called_once()
        expected_error = (r'line\n\t\r\u0000\"\\literal\\n\u4e2d\u00e9'
                          r'\u2028\u2029\ud83d\ude00')
        self.assertEqual(stderr, "CRT identity report error: " + expected_error + "\n")
        self.assertTrue(stderr.isascii())
        self.assertEqual(stderr.count("\n"), 1)
        self.assertNotIn("\r", stderr)
        self.assertNotIn("\0", stderr)
        self.assertNotIn("ENVIRONMENT_SENTINEL_MUST_NOT_APPEAR", stdout + stderr)
        self.assertEqual(code, 1)
        self.assertEqual(stdout, "CRT identity report: invalid; upload_ready=false\n")
        self.assertEqual(output, b"existing=preserved\nupload_ready=false\n")

    def test_silent_statuses_preserve_summary_upload_output_and_exit_code(self):
        cases = [
            ({"status": "invalid", "upload_ready": False}, 1, "false"),
            ({"status": "invalid", "upload_ready": False, "error": ""}, 1, "false"),
            ({"status": "invalid", "upload_ready": False, "error": None}, 1, "false"),
            ({"status": "ready", "upload_ready": True, "error": "ignored"}, 0, "true"),
            ({"status": "absent", "upload_ready": False, "error": "ignored"}, 0, "false"),
        ]
        for number, (result, expected_code, ready) in enumerate(cases):
            with self.subTest(result=result), \
                    mock.patch.object(evidence, "verify_report", return_value=result) as verify:
                code, stdout, stderr, output = self.invoke_verifier_cli(
                    "github-output-" + str(number) + ".txt")
            verify.assert_called_once_with(self.report)
            self.assertEqual(code, expected_code)
            self.assertEqual(stderr, "")
            self.assertEqual(stdout, "CRT identity report: " + result["status"] +
                             "; upload_ready=" + ready + "\n")
            self.assertEqual(output, b"existing=preserved\nupload_ready=" +
                             ready.encode("ascii") + b"\n")

    def test_real_absent_report_is_silent_and_does_not_create_report_directory(self):
        code, stdout, stderr, output = self.invoke_verifier_cli()
        self.assertEqual(code, 0)
        self.assertEqual(stdout, "CRT identity report: absent; upload_ready=false\n")
        self.assertEqual(stderr, "")
        self.assertEqual(output, b"existing=preserved\nupload_ready=false\n")
        self.assertFalse(self.report.exists())


if __name__ == "__main__":
    unittest.main()
