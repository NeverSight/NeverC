#!/usr/bin/env python3
"""Format, closure and transaction rejection tests; no compiler is required."""

from contextlib import contextmanager
from dataclasses import replace
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import uuid

sys.dont_write_bytecode = True

import RewriteSetupCoffSymbols as rewrite
from SetupGuidSymbols import GUID_RENAMES


GUIDS = tuple(GUID_RENAMES)


def object_bytes(names=GUIDS, *, contents=None, machine=0x8664,
                 section_name=".rdata", relocations=(), extras=()):
    """Build a tiny ordinary COFF fixture with explicit offsets and records.

    extras entries are (name, value, section, type, storage, auxiliary_slots).
    This byte fixture exercises parser rejection independently of llvm tools.
    """
    reverse = {new: old for old, new in GUID_RENAMES.items()}
    if contents is None:
        contents = b"".join(uuid.UUID(reverse.get(name, name)[6:].replace("_", "-")).bytes_le
                            for name in names)
    entries = [(name, index * 16, 1, 0, 2, ()) for index, name in enumerate(names)]
    entries.extend(extras)
    strings = bytearray(struct.pack("<I", 4))

    def encode_name(name):
        encoded = name.encode("utf-8")
        if len(encoded) <= 8:
            return encoded.ljust(8, b"\0")
        offset = len(strings)
        strings.extend(encoded + b"\0")
        return struct.pack("<II", 0, offset)

    symbols = bytearray()
    symbol_count = 0
    for name, value, section, kind, storage, auxiliary in entries:
        symbols.extend(encode_name(name))
        symbols.extend(struct.pack("<IHHBB", value, section & 0xFFFF, kind, storage, len(auxiliary)))
        for payload in auxiliary:
            if len(payload) != 18:
                raise ValueError("fixture auxiliary must occupy exactly one slot")
            symbols.extend(payload)
        symbol_count += 1 + len(auxiliary)
    struct.pack_into("<I", strings, 0, len(strings))
    data_at = 60 if contents else 0
    reloc_at = 60 + len(contents) if relocations else 0
    relocs = b"".join(struct.pack("<IIH", *item) for item in relocations)
    symbol_at = 60 + len(contents) + len(relocs)
    header = struct.pack("<HHIIIHH", machine, 1, 0, symbol_at, symbol_count, 0, 0)
    section = section_name.encode().ljust(8, b"\0")
    if len(section) != 8:
        raise ValueError("fixture section uses a short name")
    section += struct.pack("<IIIIIIHHI", 0, 0, len(contents), data_at, reloc_at, 0,
                           len(relocations), 0, 0x40300040)
    return header + section + contents + relocs + symbols + strings


def archive_bytes(members, indexed=None):
    """Emit both COFF indices, preserving duplicate member names by ordinal."""
    if indexed is None:
        indexed = [(name, number) for number, (_, _, exports) in enumerate(members) for name in exports]
    first_names = b"".join(name.encode() + b"\0" for name, _ in indexed)
    second_pairs = sorted(indexed)
    second_names = b"".join(name.encode() + b"\0" for name, _ in second_pairs)
    first_size = 4 + 4 * len(indexed) + len(first_names)
    second_size = 8 + 4 * len(members) + 2 * len(indexed) + len(second_names)
    offset = 8 + 60 + first_size + (first_size & 1) + 60 + second_size + (second_size & 1)
    offsets = []
    for _, payload, _ in members:
        offsets.append(offset)
        offset += 60 + len(payload) + (len(payload) & 1)

    def member(name, payload):
        header = (name.ljust(16, b" ") + b"0".ljust(12, b" ") +
                  b"0".ljust(6, b" ") + b"0".ljust(6, b" ") +
                  b"100644".ljust(8, b" ") + str(len(payload)).encode().ljust(10, b" ") + b"`\n")
        return header + payload + (b"\n" if len(payload) & 1 else b"")

    first = struct.pack(">I", len(indexed))
    first += b"".join(struct.pack(">I", offsets[number]) for _, number in indexed) + first_names
    second = struct.pack("<I", len(members)) + b"".join(struct.pack("<I", value) for value in offsets)
    second += struct.pack("<I", len(indexed))
    second += b"".join(struct.pack("<H", number + 1) for _, number in second_pairs) + second_names
    return (b"!<arch>\n" + member(b"/", first) + member(b"/", second) +
            b"".join(member(name.encode() + b"/", payload) for name, payload, _ in members))


def share_symbol_string(payload, symbol_number, target_number, suffix=0):
    result = bytearray(payload)
    symbol_at = struct.unpack_from("<I", payload, 8)[0]
    target_offset = struct.unpack_from("<I", payload, symbol_at + target_number * 18 + 4)[0]
    struct.pack_into("<II", result, symbol_at + symbol_number * 18, 0, target_offset + suffix)
    return bytes(result)


def bigobj_bytes(*, extras=()):
    """Independent 56-byte header and 20-byte symbol/FILE fixture.

    Spell the raw wire magic here, rather than importing the parser constant;
    a mistaken GUID display/byte-order conversion must fail this positive case.
    """
    contents = b"".join(uuid.UUID(name[6:].replace("_", "-")).bytes_le for name in GUIDS)
    strings = bytearray(struct.pack("<I", 4))
    symbols = bytearray()
    for index, name in enumerate(GUIDS):
        symbols.extend(struct.pack("<II", 0, len(strings)))
        symbols.extend(struct.pack("<IiHBB", index * 16, 1, 0, 2, 0))
        strings.extend(name.encode() + b"\0")
    symbols.extend(b".file\0\0\0" + struct.pack("<IiHBB", 0, -2, 0, 103, 2))
    symbols.extend(b"source.cpp".ljust(20, b"\0") + bytes(20))
    symbol_count = 8
    for name, value, section, kind, storage, auxiliary in extras:
        symbols.extend(struct.pack("<II", 0, len(strings)))
        symbols.extend(struct.pack("<IiHBB", value, section, kind, storage, len(auxiliary)))
        strings.extend(name.encode("utf-8") + b"\0")
        for payload in auxiliary:
            if len(payload) != 20:
                raise ValueError("fixture bigobj auxiliary must occupy exactly one slot")
            symbols.extend(payload)
        symbol_count += 1 + len(auxiliary)
    struct.pack_into("<I", strings, 0, len(strings))
    header = bytes.fromhex("0000ffff02006486") + struct.pack("<I", 0)
    header += bytes.fromhex("c7a1bad1eebaa94baf20faf66aa4dcb8")
    header += struct.pack("<IIIIIII", 0, 0, 0, 0, 1, 96 + len(contents), symbol_count)
    section = b".rdata\0\0" + struct.pack("<IIIIIIHHI", 0, 0, len(contents), 96, 0, 0, 0, 0, 0x40300040)
    return header + section + contents + symbols + strings


class StructureTests(unittest.TestCase):
    def snapshot(self, payload):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.lib"
            path.write_bytes(payload)
            return rewrite.inspect_archive(path)

    def test_five_originals_and_duplicate_member_occurrences(self):
        payload = object_bytes()
        snapshot = self.snapshot(archive_bytes([("same.obj", payload, GUIDS), ("same.obj", payload, GUIDS)]))
        self.assertEqual([name for name, _ in snapshot.members], ["same.obj", "same.obj"])
        self.assertEqual(rewrite.build_mapping(snapshot), GUID_RENAMES)
        self.assertEqual(len(snapshot.members[0][1].guid_storage), 5)

    def test_exact_pair_checks_every_selected_member(self):
        payload = archive_bytes([("one.obj", object_bytes(), GUIDS)])
        before = self.snapshot(payload)
        transformed = rewrite.rewrite_archive_bytes(payload, GUID_RENAMES)
        after = self.snapshot(transformed)
        members, hits = rewrite.verify_archive_pair(before, after, GUID_RENAMES)
        self.assertEqual(hits, {name: 1 for name in GUIDS})
        self.assertEqual(members[0]["structural_checks"], "passed")
        self.assertEqual(len(payload), len(transformed))
        self.assertEqual(rewrite.verify_rewrite_bytes(payload, transformed, GUID_RENAMES)["compared_bytes"], len(payload))

    def test_missing_original_definition(self):
        names = GUIDS[:-1]
        snapshot = self.snapshot(archive_bytes([("one.obj", object_bytes(names), names)]))
        with self.assertRaisesRegex(ValueError, "missing original Setup GUID"):
            rewrite.build_mapping(snapshot)

    def test_forged_archive_index_is_not_definition_evidence(self):
        payload = archive_bytes([("one.obj", object_bytes(), GUIDS)], indexed=[("forged", 0)])
        with self.assertRaisesRegex(ValueError, "actual member definitions"):
            self.snapshot(payload)

    def test_machine_and_unknown_object_formats(self):
        for payload in (b"BC\xc0\xde" + bytes(60), b"MZ" + bytes(62), object_bytes(machine=0xA641)):
            with self.subTest(prefix=payload[:4]):
                with self.assertRaises(ValueError):
                    rewrite.inspect_object(payload)

    def test_mixed_native_architectures(self):
        with self.assertRaisesRegex(ValueError, "mixed archive machines"):
            self.snapshot(archive_bytes([("x.obj", object_bytes(), GUIDS),
                                         ("a.obj", object_bytes(machine=0xAA64), GUIDS)]))

    def test_guid_bytes_must_be_correct(self):
        payload = bytearray(object_bytes())
        payload[60] ^= 1
        with self.assertRaisesRegex(ValueError, "storage bytes differ"):
            rewrite.inspect_object(payload)

    def test_guid_storage_cannot_have_link_time_relocation(self):
        with self.assertRaisesRegex(ValueError, "link-time relocations"):
            rewrite.inspect_object(object_bytes(relocations=((0, 0, 1),)))

    def test_selected_directive_fails_before_transform(self):
        payload = object_bytes(("ordinary",), contents=b" /include:" + GUIDS[0].encode(), section_name=".drectve")
        with self.assertRaisesRegex(ValueError, "unhandled linker directive"):
            rewrite.inspect_object(payload)

    def test_overlapping_regions_and_invalid_name_offsets(self):
        payload = bytearray(object_bytes())
        struct.pack_into("<I", payload, 40, 20)  # Section contents overlap header.
        with self.assertRaisesRegex(ValueError, "storage bytes differ|overlapping"):
            rewrite.inspect_object(payload)
        payload = bytearray(object_bytes())
        symbol_at = struct.unpack_from("<I", payload, 8)[0]
        struct.pack_into("<I", payload, symbol_at + 4, 0xFFFFFFFE)
        with self.assertRaisesRegex(ValueError, "string table offset"):
            rewrite.inspect_object(payload)

    def test_weak_target_cannot_address_auxiliary_slot(self):
        section_aux = struct.pack("<IHHIHBBH", 80, 0, 0, 0, 1, 0, 0, 0)
        weak_aux = struct.pack("<II", 6, 3) + bytes(10)
        extras = ((".rdata", 0, 1, 0, 3, (section_aux,)),
                  ("alias", 0, 0, 0, 105, (weak_aux,)))
        with self.assertRaisesRegex(ValueError, "primary symbol"):
            rewrite.inspect_object(object_bytes(extras=extras))

    def weak_aux_fixture(self, *, bigobj=False, name="weak_alias", value=0,
                         section=0, kind=0, target=0, search=3, reserved=bytes(10)):
        auxiliary = struct.pack("<II", target, search) + reserved
        if bigobj:
            auxiliary += bytes(2)
        extras = ((name, value, section, kind, 105, (auxiliary,)),)
        payload = bigobj_bytes(extras=extras) if bigobj else object_bytes(extras=extras)
        return payload, auxiliary

    def test_weak_aux_diagnostic_reports_each_rejected_field_and_full_wire_slot(self):
        cases = (
            ({"section": 1}, ["section"]),
            ({"value": 7}, ["value"]),
            ({"kind": 0x20}, ["type"]),
            ({"search": 0}, ["search"]),
            ({"search": 4}, ["search"]),
            ({"search": 0xFFFFFFFF}, ["search"]),
            ({"reserved": b"\xa5" + bytes(9)}, ["reserved"]),
            ({"reserved": bytes(9) + b"\xa5"}, ["reserved"]),
            ({"section": 1, "value": 7, "kind": 0x20, "search": 4,
              "reserved": bytes(9) + b"\xa5"},
             ["section", "value", "type", "search", "reserved"]),
        )
        for bigobj in (False, True):
            for changes, rejected in cases:
                with self.subTest(bigobj=bigobj, rejected=rejected):
                    payload, auxiliary = self.weak_aux_fixture(bigobj=bigobj, **changes)
                    with self.assertRaises(ValueError) as failure:
                        rewrite.inspect_object(payload)
                    prefix, encoded = str(failure.exception).split("; weak_aux=", 1)
                    self.assertEqual(prefix,
                                     "Setup COFF rewrite: unsupported weak external auxiliary record")
                    self.assertEqual(json.loads(encoded), {
                        "symbol_index": 8 if bigobj else 5,
                        "symbol_name": {"text": "weak_alias", "chars": 10,
                                        "utf8_bytes": 10, "truncated": False,
                                        "sha256": hashlib.sha256(b"weak_alias").hexdigest()},
                        "storage": 105, "aux_count": 1,
                        "section": changes.get("section", 0), "value": changes.get("value", 0),
                        "type": changes.get("kind", 0), "target": 0,
                        "search": changes.get("search", 3),
                        "aux_record_bytes": 20 if bigobj else 18,
                        "aux_hex": auxiliary.hex(), "rejected_fields": rejected,
                    })

    def test_weak_aux_diagnostics_preserve_supported_layouts(self):
        for bigobj in (False, True):
            for search in (1, 2, 3):
                with self.subTest(bigobj=bigobj, search=search):
                    payload, auxiliary = self.weak_aux_fixture(bigobj=bigobj, search=search)
                    symbol = rewrite.inspect_object(payload).symbols[-1]
                    self.assertEqual(symbol.index, 8 if bigobj else 5)
                    self.assertEqual(symbol.weak, (0, search))
                    self.assertEqual(symbol.aux, (auxiliary[:18],))

    def test_weak_aux_diagnostics_preserve_earlier_rejection_order(self):
        for bigobj in (False, True):
            slot = struct.pack("<II", 0, 4) + bytes(12 if bigobj else 10)
            builder = bigobj_bytes if bigobj else object_bytes
            for auxiliary, expected in (
                    ((slot, slot), "unsupported multiple auxiliary records"),
                    ((), "missing required auxiliary record")):
                with self.subTest(bigobj=bigobj, expected=expected):
                    payload = builder(extras=(("weak_alias", 0, 0, 0x20, 105, auxiliary),))
                    with self.assertRaises(ValueError) as failure:
                        rewrite.inspect_object(payload)
                    self.assertEqual(str(failure.exception), "Setup COFF rewrite: " + expected)
        slot = struct.pack("<II", 0, 4) + bytes(11) + b"\x01"
        with self.assertRaises(ValueError) as failure:
            rewrite.inspect_object(bigobj_bytes(extras=(("weak_alias", 0, 0, 0x20, 105, (slot,)),)))
        self.assertEqual(str(failure.exception), "Setup COFF rewrite: nonzero bigobj auxiliary padding")
        for bigobj in (False, True):
            payload, _ = self.weak_aux_fixture(bigobj=bigobj, target=0xFFFFFFFF)
            with self.assertRaises(ValueError) as failure:
                rewrite.inspect_object(payload)
            self.assertEqual(str(failure.exception),
                             "Setup COFF rewrite: weak target is not a supported primary symbol")
            payload, _ = self.weak_aux_fixture(bigobj=bigobj, target=0xFFFFFFFF, kind=0x20)
            with self.assertRaises(ValueError) as failure:
                rewrite.inspect_object(payload)
            details = json.loads(str(failure.exception).split("; weak_aux=", 1)[1])
            self.assertEqual(details["target"], 0xFFFFFFFF)
            self.assertEqual(details["rejected_fields"], ["type"])

    def test_weak_aux_diagnostic_escapes_names_and_retains_member_context(self):
        names = ("x" * 96, "x" * 97, "\U0001f4a1" * 1000,
                 'weak_"\\\u0085\u2028' + "x" * 4096)
        member_name = 'bad_"\u0085.obj'
        for bigobj in (False, True):
            for name in names:
                with self.subTest(bigobj=bigobj, name_length=len(name)):
                    payload, _ = self.weak_aux_fixture(bigobj=bigobj, name=name, kind=0x20)
                    archive = archive_bytes([("good.obj", object_bytes(), GUIDS),
                                             (member_name, payload, GUIDS)])
                    with self.assertRaises(ValueError) as failure:
                        rewrite.inspect_archive_bytes(archive)
                    message = str(failure.exception)
                    self.assertTrue(message.startswith("Setup COFF rewrite: member 1 name="))
                    self.assertTrue(message.isascii())
                    self.assertLess(len(message), 4096)
                    member, _ = json.JSONDecoder().raw_decode(message.split(" name=", 1)[1])
                    self.assertEqual(member["text"], member_name)
                    self.assertEqual(member["sha256"], hashlib.sha256(member_name.encode()).hexdigest())
                    details = json.loads(message.split("; weak_aux=", 1)[1])
                    self.assertEqual(details["symbol_index"], 8 if bigobj else 5)
                    self.assertEqual(details["rejected_fields"], ["type"])
                    self.assertEqual(details["symbol_name"], {
                        "text": name[:96], "chars": len(name),
                        "utf8_bytes": len(name.encode("utf-8")),
                        "sha256": hashlib.sha256(name.encode("utf-8")).hexdigest(),
                        "truncated": len(name) > 96,
                    })

    def test_truncated_diagnostic_names_keep_distinct_complete_identities(self):
        names = ("x" * 96 + "a", "x" * 96 + "b")
        details = [rewrite.diagnostic_name(name) for name in names]
        self.assertEqual(details[0]["text"], details[1]["text"])
        self.assertNotEqual(details[0]["sha256"], details[1]["sha256"])
        for name, record in zip(names, details):
            self.assertEqual(record["chars"], 97)
            self.assertEqual(record["utf8_bytes"], 97)
            self.assertTrue(record["truncated"])
            self.assertEqual(record["sha256"], hashlib.sha256(name.encode()).hexdigest())

    def test_unselected_member_must_also_preserve_payload(self):
        original = rewrite.inspect_object(object_bytes())
        changed_section = replace(original.sections[0], contents="0" * 64)
        changed = replace(original, sections=(changed_section,))
        before = rewrite.ArchiveSnapshot(0x8664, [("one.obj", original)], "", 0)
        after = rewrite.ArchiveSnapshot(0x8664, [("one.obj", changed)], "", 0)
        with self.assertRaisesRegex(ValueError, "section bytes"):
            rewrite.verify_archive_pair(before, after, {})

    def test_symbol_raw_index_drift_fails(self):
        original = rewrite.inspect_object(object_bytes())
        renamed = rewrite.inspect_object(object_bytes(tuple(GUID_RENAMES.values())))
        symbols = (replace(renamed.symbols[0], index=99), *renamed.symbols[1:])
        before = rewrite.ArchiveSnapshot(0x8664, [("one.obj", original)], "", 0)
        after = rewrite.ArchiveSnapshot(0x8664, [("one.obj", replace(renamed, symbols=symbols))], "", 0)
        with self.assertRaisesRegex(ValueError, "raw-index change"):
            rewrite.verify_archive_pair(before, after, GUID_RENAMES)


class TransactionTests(unittest.TestCase):
    def arguments(self, directory):
        root = Path(directory)
        return ["RewriteSetupCoffSymbols.py", "--input", str(root / "input.lib"),
                "--output", str(root / "output.lib"), "--report", str(root / "report.json"),
                "--nm", str(root / "nm.exe"),
                "--readobj", str(root / "readobj.exe")]

    def fixture(self, directory):
        root = Path(directory)
        ordinary = object_bytes(("aaa",), contents=b"unchanged data!!")
        original = archive_bytes([("guid.obj", object_bytes(), GUIDS),
                                  ("aaa.obj", ordinary, ("aaa",))])
        # Build the expected result independently, without calling the writer,
        # its patch planner, or either of its byte-verification entry points.
        renamed = tuple(GUID_RENAMES.values())
        expected = archive_bytes([("guid.obj", object_bytes(renamed), renamed),
                                  ("aaa.obj", ordinary, ("aaa",))])
        (root / "input.lib").write_bytes(original)
        (root / "nm.exe").write_bytes(b"controlled native nm tool identity")
        (root / "readobj.exe").write_bytes(b"controlled native readobj tool identity")
        return root, original, expected

    @contextmanager
    def external_tools(self, *, audit_failure=False, native_failure=False):
        """Only tool boundaries are synthetic; transform and filesystem are real.

        These fixtures do not claim execution of LLVM or the no-host auditor.
        Their format/byte proof, staging lifetime and exclusive publication use
        the production implementation. Tool/parser coverage lives elsewhere.
        """
        def run(command, report, consume=None, timeout=None):
            argv = list(map(str, command))
            record = {"argv": argv, "status": "started"}
            report["commands"].append(record)
            if argv[1:] == ["--version"]:
                payload = b"LLVM version 20.1.8\n"
            elif len(argv) > 2 and Path(argv[2]).name == "AuditArchive.py":
                if audit_failure:
                    record.update(status="failed", error="controlled no-host audit rejection")
                    raise ValueError(record["error"])
                payload = b"controlled no-host audit accepted\n"
            else:
                raise AssertionError("unexpected tool boundary: " + repr(argv))
            record.update(status="passed", returncode=0, stdout_bytes=len(payload),
                          stdout_sha256=hashlib.sha256(payload).hexdigest())
            return consume(io.BytesIO(payload)) if consume else None

        def native(nm, readobj, archive, snapshot, report):
            if native_failure:
                raise ValueError("controlled native reader rejection")
            report["native_readers"] = {"status": "passed", "controlled_fixture": True}

        with mock.patch.object(rewrite, "_run", side_effect=run) as commands, mock.patch.object(
                rewrite, "verify_native_readers", side_effect=native) as readers:
            yield commands, readers

    def read_failed_report(self, root, original, reason):
        report = json.loads((root / "report.json").read_text())
        self.assertEqual(report["status"], "failed")
        self.assertFalse(report["published"])
        self.assertIn(reason, report["error"])
        self.assertEqual((root / "input.lib").read_bytes(), original)
        self.assertFalse(list(root.glob("setup-coff-*")))
        return report

    @contextmanager
    def final_report_fault(self, root, expected, operation, *, replace_output=False, persistent=False):
        """Inject only after a real link, retaining the actual exclusive stream."""
        output, report = root / "output.lib", root / "report.json"
        replacement = root / "competing.lib"
        if replace_output:
            # Allocate a distinct file before publication. Replacing atomically
            # avoids relying on whether an unlinked inode gets reused by the FS.
            replacement.write_bytes(b"later transaction owns this output")
        state = {"faults": 0, "links": 0, "report_opens": 0, "report_fd": None}
        open_file, fsync, link = Path.open, rewrite.os.fsync, rewrite.os.link

        def fault():
            if not state["links"] or (state["faults"] and not persistent):
                return
            if not state["faults"]:
                expected_output = b"later transaction owns this output" if replace_output else expected
                self.assertEqual(output.read_bytes(), expected_output)
            state["faults"] += 1
            raise OSError("controlled final report " + operation + " failure")

        class ReportStream:
            def __init__(self, stream):
                self.stream = stream

            def __enter__(self):
                self.stream.__enter__()
                return self

            def __exit__(self, *args):
                return self.stream.__exit__(*args)

            def __getattr__(self, name):
                return getattr(self.stream, name)

            def write(self, value):
                if operation == "write":
                    fault()
                return self.stream.write(value)

        def open_report(path, *args, **kwargs):
            stream = open_file(path, *args, **kwargs)
            mode = args[0] if args else kwargs.get("mode", "r")
            if path == report and mode == "x":
                state["report_opens"] += 1
                state["report_fd"] = stream.fileno()
                return ReportStream(stream)
            return stream

        def sync_report(descriptor):
            if operation == "fsync" and descriptor == state["report_fd"]:
                fault()
            return fsync(descriptor)

        def publish(source, destination):
            self.assertEqual(Path(source).read_bytes(), expected)
            link(source, destination)
            self.assertEqual(Path(source).stat().st_ino, output.stat().st_ino)
            state["links"] += 1
            if replace_output:
                # Replace before the link call returns to transform. Recording
                # identity from output after link would now capture the wrong
                # file and must not make rollback delete this replacement.
                replacement.replace(output)

        with mock.patch.object(Path, "open", new=open_report), mock.patch.object(
                rewrite.os, "fsync", side_effect=sync_report), mock.patch.object(
                rewrite.os, "link", side_effect=publish):
            yield state

    def report_failure_case(self, operation, *, replace_output=False, persistent=False):
        with tempfile.TemporaryDirectory() as directory:
            root, original, expected = self.fixture(directory)
            with self.external_tools() as (commands, readers), mock.patch.object(
                    sys, "argv", self.arguments(directory)), mock.patch.object(
                    rewrite, "_verify_byte_streams", wraps=rewrite._verify_byte_streams) as proof, self.final_report_fault(
                    root, expected, operation, replace_output=replace_output, persistent=persistent) as fault:
                reason = "report durability unconfirmed" if persistent else "controlled final report " + operation + " failure"
                with self.assertRaisesRegex(SystemExit, reason):
                    rewrite.main()
                proof.assert_called_once()
                readers.assert_called_once()
                self.assertEqual(commands.call_count, 3)
                self.assertEqual(fault["links"], 1)
                self.assertEqual(fault["report_opens"], 1)
                self.assertEqual(fault["faults"], 2 if persistent else 1)
            self.assertEqual((root / "input.lib").read_bytes(), original)
            self.assertFalse(list(root.glob("setup-coff-*")))
            output = root / "output.lib"
            if replace_output:
                self.assertEqual(output.read_bytes(), b"later transaction owns this output")
            else:
                self.assertFalse(output.exists())
            if persistent and operation == "write":
                # No JSON can be promised after a permanent write failure. The
                # command must fail and must not leave a stale success report.
                self.assertEqual((root / "report.json").read_bytes(), b"")
                return
            report = self.read_failed_report(root, original, "controlled final report " + operation + " failure")
            self.assertEqual(report["byte_proof"]["status"], "passed")
            self.assertIn("not atomic", report["publication_limit"])
            if replace_output:
                self.assertEqual(report["rollback"]["status"], "preserved-replacement")
                self.assertNotEqual(report["published_identity"], report["rollback"]["observed_identity"])
            else:
                self.assertEqual(report["rollback"]["status"], "removed-owned-output")

    def test_failure_records_reason_and_does_not_publish(self):
        with tempfile.TemporaryDirectory() as directory:
            root, original, _ = self.fixture(directory)
            copyfile = rewrite.shutil.copyfile

            def copied_but_changed(source, destination):
                copyfile(source, destination)
                with Path(destination).open("ab") as stream:
                    stream.write(b"unexpected copied byte")

            with self.external_tools() as (commands, readers), mock.patch.object(
                    sys, "argv", self.arguments(directory)), mock.patch.object(
                    rewrite.shutil, "copyfile", side_effect=copied_but_changed) as copied, mock.patch.object(
                    rewrite, "_verify_byte_streams", wraps=rewrite._verify_byte_streams) as proof, mock.patch.object(
                    rewrite.os, "link", wraps=rewrite.os.link) as linked:
                with self.assertRaisesRegex(SystemExit, "input changed while copying"):
                    rewrite.main()
                copied.assert_called_once()
                proof.assert_not_called()
                readers.assert_not_called()
                linked.assert_not_called()
                self.assertEqual(commands.call_count, 2)
            self.read_failed_report(root, original, "input changed while copying")
            self.assertFalse((root / "output.lib").exists())

    def test_existing_report_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root, original, _ = self.fixture(directory)
            path = root / "report.json"
            path.write_text("previous evidence")
            output = root / "output.lib"
            output.write_bytes(b"previous output")
            with self.external_tools() as (commands, readers), mock.patch.object(
                    sys, "argv", self.arguments(directory)), mock.patch.object(
                    rewrite.os, "link", wraps=rewrite.os.link) as linked:
                with self.assertRaises(SystemExit):
                    rewrite.main()
                commands.assert_not_called()
                readers.assert_not_called()
                linked.assert_not_called()
            self.assertEqual(path.read_text(), "previous evidence")
            self.assertEqual(output.read_bytes(), b"previous output")
            self.assertEqual((root / "input.lib").read_bytes(), original)

    def test_cleanup_only_removes_output_owned_by_transaction(self):
        with tempfile.TemporaryDirectory() as directory:
            root, original, _ = self.fixture(directory)
            output = root / "output.lib"
            output.write_bytes(b"previous output")
            with self.external_tools() as (commands, readers), mock.patch.object(
                    sys, "argv", self.arguments(directory)), mock.patch.object(
                    rewrite.os, "link", wraps=rewrite.os.link) as linked:
                with self.assertRaisesRegex(SystemExit, "output must be a fresh path"):
                    rewrite.main()
                commands.assert_not_called()
                readers.assert_not_called()
                linked.assert_not_called()
            self.read_failed_report(root, original, "output must be a fresh path")
            self.assertEqual(output.read_bytes(), b"previous output")

    def test_real_transform_copies_proves_and_publishes_exact_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root, original, expected = self.fixture(directory)
            output = root / "output.lib"
            link = rewrite.os.link
            published = []

            def publish(source, destination):
                self.assertEqual(Path(source).read_bytes(), expected)
                self.assertFalse(output.exists())
                link(source, destination)
                left, right = Path(source).stat(), Path(destination).stat()
                self.assertEqual((left.st_dev, left.st_ino), (right.st_dev, right.st_ino))
                self.assertGreaterEqual(left.st_nlink, 2)
                published.append(Path(source))

            with self.external_tools() as (commands, readers), mock.patch.object(
                    sys, "argv", self.arguments(directory)), mock.patch.object(
                    rewrite.shutil, "copyfile", wraps=rewrite.shutil.copyfile) as copied, mock.patch.object(
                    rewrite, "_verify_byte_streams", wraps=rewrite._verify_byte_streams) as proof, mock.patch.object(
                    rewrite, "verify_archive_pair", wraps=rewrite.verify_archive_pair) as structure, mock.patch.object(
                    rewrite.os, "link", side_effect=publish) as linked:
                rewrite.main()
                copied.assert_called_once()
                proof.assert_called_once()
                structure.assert_called_once()
                readers.assert_called_once()
                linked.assert_called_once()
                self.assertEqual(commands.call_count, 3)
            self.assertEqual(output.read_bytes(), expected)
            self.assertEqual((root / "input.lib").read_bytes(), original)
            self.assertEqual(len(published), 1)
            self.assertFalse(published[0].exists())
            self.assertFalse(list(root.glob("setup-coff-*")))
            report = json.loads((root / "report.json").read_text())
            self.assertEqual(report["schema"], "neverc.setup-coff-rewrite.v2")
            self.assertEqual(report["status"], "passed")
            self.assertTrue(report["published"])
            self.assertEqual(report["input"]["sha256"], hashlib.sha256(original).hexdigest())
            self.assertEqual(report["output"]["sha256"], hashlib.sha256(expected).hexdigest())
            self.assertEqual(report["byte_proof"]["compared_bytes"], len(original))
            self.assertEqual(report["byte_proof"]["status"], "passed")
            self.assertEqual(report["member_count"], 2)
            self.assertTrue(report["members"][1]["byte_identical"])
            self.assertTrue(all(command["status"] == "passed" for command in report["commands"]))

    def test_real_byte_proof_rejects_unapproved_staging_change(self):
        with tempfile.TemporaryDirectory() as directory:
            root, original, _ = self.fixture(directory)
            verify = rewrite._verify_byte_streams

            def changed_before_proof(source, transformed, size, patches):
                # The parsed object inventory is already available. Change an
                # archive timestamp outside every approved name/index interval;
                # the real complete-byte comparison, not a mock, must reject it.
                with Path(transformed.name).open("r+b") as stream:
                    stream.seek(24)
                    stream.write(b"1")
                return verify(source, transformed, size, patches)

            with self.external_tools() as (_, readers), mock.patch.object(
                    sys, "argv", self.arguments(directory)), mock.patch.object(
                    rewrite, "_verify_byte_streams", side_effect=changed_before_proof) as proof, mock.patch.object(
                    rewrite.os, "link", wraps=rewrite.os.link) as linked:
                with self.assertRaisesRegex(SystemExit, "unapproved archive byte difference"):
                    rewrite.main()
                proof.assert_called_once()
                readers.assert_not_called()
                linked.assert_not_called()
            report = self.read_failed_report(root, original, "unapproved archive byte difference")
            self.assertNotIn("byte_proof", report)
            self.assertFalse((root / "output.lib").exists())

    def test_native_reader_failure_does_not_publish_verified_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root, original, _ = self.fixture(directory)
            with self.external_tools(native_failure=True) as (commands, readers), mock.patch.object(
                    sys, "argv", self.arguments(directory)), mock.patch.object(
                    rewrite.os, "link", wraps=rewrite.os.link) as linked:
                with self.assertRaisesRegex(SystemExit, "controlled native reader rejection"):
                    rewrite.main()
                readers.assert_called_once()
                linked.assert_not_called()
                self.assertEqual(commands.call_count, 2)
            report = self.read_failed_report(root, original, "controlled native reader rejection")
            self.assertEqual(report["byte_proof"]["status"], "passed")
            self.assertFalse((root / "output.lib").exists())

    def test_no_host_audit_failure_does_not_publish_verified_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root, original, _ = self.fixture(directory)
            with self.external_tools(audit_failure=True) as (commands, readers), mock.patch.object(
                    sys, "argv", self.arguments(directory)), mock.patch.object(
                    rewrite.os, "link", wraps=rewrite.os.link) as linked:
                with self.assertRaisesRegex(SystemExit, "controlled no-host audit rejection"):
                    rewrite.main()
                readers.assert_called_once()
                linked.assert_not_called()
                self.assertEqual(commands.call_count, 3)
            report = self.read_failed_report(root, original, "controlled no-host audit rejection")
            self.assertEqual(report["byte_proof"]["status"], "passed")
            self.assertEqual(report["commands"][-1]["status"], "failed")
            self.assertFalse((root / "output.lib").exists())

    def test_real_link_race_preserves_competing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root, original, expected = self.fixture(directory)
            output = root / "output.lib"
            link = rewrite.os.link

            def competing_publish(source, destination):
                self.assertEqual(Path(source).read_bytes(), expected)
                output.write_bytes(b"output published by another transaction")
                link(source, destination)  # The real exclusive link must fail.

            with self.external_tools(), mock.patch.object(
                    sys, "argv", self.arguments(directory)), mock.patch.object(
                    rewrite.os, "link", side_effect=competing_publish) as linked:
                with self.assertRaises(SystemExit):
                    rewrite.main()
                linked.assert_called_once()
            report = self.read_failed_report(root, original, "")
            self.assertTrue(report["error"])
            self.assertEqual(report["byte_proof"]["status"], "passed")
            self.assertEqual(output.read_bytes(), b"output published by another transaction")

    def test_final_report_write_failure_recovers_failed_report(self):
        for replace_output in (False, True):
            with self.subTest(replace_output=replace_output):
                self.report_failure_case("write", replace_output=replace_output)

    def test_final_report_fsync_failure_recovers_failed_report(self):
        for replace_output in (False, True):
            with self.subTest(replace_output=replace_output):
                self.report_failure_case("fsync", replace_output=replace_output)

    def test_persistent_report_io_failure_does_not_claim_durability(self):
        for operation in ("write", "fsync"):
            with self.subTest(operation=operation):
                self.report_failure_case(operation, persistent=True)


class EqualWidthTests(unittest.TestCase):
    def test_bigobj_header_and_twenty_byte_file_slots_are_preserved(self):
        payload = archive_bytes([("big.obj", bigobj_bytes(), GUIDS)])
        before = rewrite.inspect_archive_bytes(payload)
        output = rewrite.rewrite_archive_bytes(payload, GUID_RENAMES)
        after = rewrite.inspect_archive_bytes(output)
        self.assertTrue(before.members[0][1].bigobj)
        self.assertTrue(after.members[0][1].bigobj)
        self.assertEqual(before.members[0][1].symbols[-1].aux, after.members[0][1].symbols[-1].aux)
        self.assertEqual(tuple(map(len, after.members[0][1].symbols[-1].aux)), (20, 20))

    def test_second_index_sort_moves_name_and_paired_member_together(self):
        other = object_bytes(("aaa",), contents=bytes(16))
        payload = archive_bytes([("guid.obj", object_bytes(), GUIDS), ("aaa.obj", other, ("aaa",))])
        before = rewrite.inspect_archive_bytes(payload)
        output = rewrite.rewrite_archive_bytes(payload, GUID_RENAMES)
        after = rewrite.inspect_archive_bytes(output)
        self.assertNotEqual(before.second_index[0][0], "aaa")
        self.assertEqual(after.second_index[0][0:2], ("aaa", after.member_offsets[1]))
        # Restore the original index array while keeping the sorted new names.
        # Both members remain valid definitions; their name->member binding is
        # now wrong and must be rejected by the index/actual-definition proof.
        corrupted = bytearray(output)
        count = len(before.second_index)
        at = before.second_tail_at
        corrupted[at:at + 2 * count] = before.second_tail[:2 * count]
        with self.assertRaises(ValueError):
            rewrite.verify_rewrite_bytes(payload, bytes(corrupted), GUID_RENAMES)

    def test_duplicate_ties_preserve_order_representatives_and_multiplicity(self):
        payload = archive_bytes([("same.obj", object_bytes(), GUIDS), ("same.obj", object_bytes(), GUIDS)])
        before = rewrite.inspect_archive_bytes(payload)
        output = rewrite.rewrite_archive_bytes(payload, GUID_RENAMES)
        after = rewrite.inspect_archive_bytes(output)
        for new in GUID_RENAMES.values():
            selected = [offset for name, offset, _ in after.second_index if name == new]
            self.assertEqual(selected, list(before.member_offsets))
        self.assertEqual(len(after.second_index), 10)
        # Change the second tied representative to the first, losing one pair
        # while duplicating another. Count/name-set checks alone cannot catch it.
        corrupted = bytearray(output)
        struct.pack_into("<H", corrupted, after.second_tail_at + 2, 1)
        with self.assertRaisesRegex(ValueError, "representative/multiplicity"):
            rewrite.inspect_archive_bytes(bytes(corrupted))

    def test_compatible_shared_symbol_string_is_written_once(self):
        names = GUIDS + (GUIDS[0],)
        obj = share_symbol_string(object_bytes(names), 5, 0)
        payload = archive_bytes([("shared.obj", obj, names)])
        output = rewrite.rewrite_archive_bytes(payload, GUID_RENAMES)
        snapshot = rewrite.inspect_archive_bytes(output)
        symbols = snapshot.members[0][1].symbols
        self.assertEqual(symbols[0].name, symbols[5].name)
        self.assertEqual(symbols[0].name_at, symbols[5].name_at)

    def test_suffix_sharing_must_not_change_unselected_symbol(self):
        obj = object_bytes(extras=(("unused", 0, 1, 0, 3, ()),))
        obj = share_symbol_string(obj, 5, 0, suffix=1)
        payload = archive_bytes([("suffix.obj", obj, GUIDS)])
        with self.assertRaisesRegex(ValueError, "incomplete/unexpected rename"):
            rewrite.rewrite_archive_bytes(payload, GUID_RENAMES)

    def test_suffix_sharing_must_not_change_section_name(self):
        obj = bytearray(object_bytes())
        symbol_at = struct.unpack_from("<I", obj, 8)[0]
        name_offset = struct.unpack_from("<I", obj, symbol_at + 4)[0]
        obj[20:28] = (b"/" + str(name_offset + 1).encode()).ljust(8, b"\0")
        payload = archive_bytes([("suffix.obj", bytes(obj), GUIDS)])
        with self.assertRaisesRegex(ValueError, "section bytes/flags/order"):
            rewrite.rewrite_archive_bytes(payload, GUID_RENAMES)

    def test_overlapping_write_conflict_is_rejected(self):
        first = rewrite.NamePatch(4, b"same", b"one!", "fixture")
        second = rewrite.NamePatch(4, b"same", b"two!", "fixture")
        with self.assertRaisesRegex(ValueError, "overlapping name patches conflict"):
            rewrite._merge_patches([first, second])
        self.assertEqual(rewrite._merge_patches([first, first]), [first])

    def test_preexisting_private_target_is_rejected(self):
        names = GUIDS + (GUID_RENAMES[GUIDS[0]],)
        payload = archive_bytes([("collision.obj", object_bytes(names), names)])
        with self.assertRaisesRegex(ValueError, "target-name collision"):
            rewrite.rewrite_archive_bytes(payload, GUID_RENAMES)

    def test_unapproved_archive_header_change_is_rejected(self):
        payload = archive_bytes([("one.obj", object_bytes(), GUIDS)])
        output = bytearray(rewrite.rewrite_archive_bytes(payload, GUID_RENAMES))
        snapshot = rewrite.inspect_archive_bytes(payload)
        # A valid archive timestamp is outside every approved edit interval.
        output[snapshot.member_offsets[0] + 16] = ord("1")
        with self.assertRaisesRegex(ValueError, "unapproved archive byte difference"):
            rewrite.verify_rewrite_bytes(payload, bytes(output), GUID_RENAMES)

    def test_file_aux_slots_and_unselected_bss_bytes_remain_identical(self):
        file_aux = (b"source.cc".ljust(18, b"\0"), bytes(18))
        selected = object_bytes(extras=((".file", 0, -2, 0, 103, file_aux),))
        bss = bytearray(object_bytes(("bss",), contents=b"", section_name=".bss"))
        struct.pack_into("<I", bss, 36, 8)  # Nonempty BSS; file pointer stays 0.
        struct.pack_into("<I", bss, 56, 0xC0300080)
        payload = archive_bytes([("guid.obj", selected, GUIDS), ("bss.obj", bytes(bss), ("bss",))])
        before = rewrite.inspect_archive_bytes(payload)
        output = rewrite.rewrite_archive_bytes(payload, GUID_RENAMES)
        after = rewrite.inspect_archive_bytes(output)
        self.assertEqual(before.members[1][1].sha256, after.members[1][1].sha256)
        self.assertEqual(before.members[0][1].symbols[-1].aux, after.members[0][1].symbols[-1].aux)

    def test_msvc_zero_section_aux_number_is_not_normalized(self):
        auxiliary = struct.pack("<IHHIHBBH", 80, 0, 0, 0, 0, 0, 0, 0)
        obj = object_bytes(extras=((".rdata", 0, 1, 0, 3, (auxiliary,)),))
        payload = archive_bytes([("msvc.obj", obj, GUIDS)])
        output = rewrite.rewrite_archive_bytes(payload, GUID_RENAMES)
        result = rewrite.inspect_archive_bytes(output)
        self.assertEqual(result.members[0][1].symbols[-1].aux, (auxiliary,))


if __name__ == "__main__":
    unittest.main()
