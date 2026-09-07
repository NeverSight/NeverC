#!/usr/bin/env python3
"""Source-only tests for the MSVC host archive-index ABI audit reader.

No compiler, SDK, llvm-nm, or generated build tree is needed. Fixtures encode
the Microsoft PE/COFF archive specification directly, including both linker
members, opaque object bodies and LLVM's documented writer alignment.
"""

import importlib.util
import io
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch


REPOSITORY = Path(__file__).resolve().parents[2]
MODULE_PATH = REPOSITORY / "neverc/lib/Translate/Cpp/Frontend/HostCoffSymbols.py"
SPEC = importlib.util.spec_from_file_location("neverc_host_coff_symbols", MODULE_PATH)
COFF = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = COFF
SPEC.loader.exec_module(COFF)


def member(name, body):
    header = (name.ljust(16, b" ") + b"0".ljust(12, b" ")
              + b" " * 12 + b"100644".ljust(8, b" ")
              + str(len(body)).encode("ascii").ljust(10, b" ") + b"`\n")
    if len(header) != 60:
        raise ValueError("test fixture member header width")
    return header + body + (b"\n" if len(body) & 1 else b"")


def aligned_index(body, align):
    return body + (b"\0" if align and len(body) & 1 else b"")


def archive(objects=None, *, first_entries=None, second_entries=None,
            indexed_members=None, long_names=None, align=True):
    if objects is None:
        objects = [(b"one.obj/", b"opaque /GL object", [b"?run@llvm@@YAXXZ"])]
    if first_entries is None:
        first_entries = [(symbol, index) for index, (_, _, names) in enumerate(objects)
                         for symbol in names]
    if second_entries is None:
        second_entries = sorted(first_entries)
    if indexed_members is None:
        indexed_members = list(range(len(objects)))

    def first_body(offsets):
        return aligned_index(
            struct.pack(">I", len(first_entries))
            + b"".join(struct.pack(">I", offsets[index]) for _, index in first_entries)
            + b"".join(symbol + b"\0" for symbol, _ in first_entries), align)

    def second_body(offsets):
        return aligned_index(
            struct.pack("<I", len(indexed_members))
            + b"".join(struct.pack("<I", offsets[index]) for index in indexed_members)
            + struct.pack("<I", len(second_entries))
            + b"".join(struct.pack("<H", indexed_members.index(index) + 1)
                       for _, index in second_entries)
            + b"".join(symbol + b"\0" for symbol, _ in second_entries), align)

    first = member(b"/", first_body([0] * len(objects)))
    second = member(b"/", second_body([0] * len(objects)))
    names = member(b"//", long_names) if long_names is not None else b""
    offset = 8 + len(first) + len(second) + len(names)
    offsets = []
    bodies = []
    for name, body, _ in objects:
        offsets.append(offset)
        data = member(name, body)
        bodies.append(data)
        offset += len(data)
    return (b"!<arch>\n" + member(b"/", first_body(offsets))
            + member(b"/", second_body(offsets)) + names + b"".join(bodies))


def member_locations(data):
    locations = []
    offset = 8
    while offset < len(data):
        size = int(data[offset + 48:offset + 58])
        locations.append((offset, offset + 60, size))
        offset += 60 + size + (size & 1)
    return locations


def overwrite(data, offset, value):
    return data[:offset] + value + data[offset + len(value):]


class HostCoffSymbolsTests(unittest.TestCase):
    def read(self, data):
        with tempfile.TemporaryDirectory(prefix="neverc-coff-index-") as temporary:
            path = Path(temporary) / "host.lib"
            path.write_bytes(data)
            return COFF.read_defined_symbols(path)

    def rejected(self, data, message=None):
        if message:
            with self.assertRaisesRegex(ValueError, message):
                self.read(data)
        else:
            with self.assertRaises(ValueError):
                self.read(data)

    def test_complete_raw_symbols_across_members(self):
        symbols = [b"?method@Type@llvm@@QEAAXXZ", b"_Znwm", b"__imp_HeapAlloc",
                   b"?inspect@clang@@YAXXZ", b"_host_global"]
        objects = [(b"first.obj/", b"opaque LTCG", symbols[:2]),
                   (b"next.obj/", b"opaque COFF", symbols[2:])]
        self.assertEqual(self.read(archive(objects)), {s.decode() for s in symbols})

    def test_bigobj_ltcg_and_both_import_payloads_use_the_index(self):
        # The shared 00 00 ff ff prefix must not make the reader interpret an
        # import header as a bigobj header or try to decode proprietary /GL IR.
        bigobj = (b"\0\0\xff\xff\x02\0\x64\x86" + b"\0" * 4
                  + bytes.fromhex("c7a1bad1eebaa94baf20faf66aa4dcb8") + b"\0" * 28)
        imports = b"imported\0sample.dll\0"
        short_import = struct.pack("<HHHHIIHH", 0, 0xFFFF, 0, 0x8664,
                                   0, len(imports), 0, 4) + imports
        objects = [(b"big.obj/", bigobj, [b"big"]),
                   (b"ltcg.obj/", b"\0\0\xff\xffopaque compiler-specific IR", [b"ltcg"]),
                   (b"short.dll/", short_import, [b"imported", b"__imp_imported"]),
                   (b"long.dll/", b"\x64\x86" + b"\0" * 18, [b"__IMPORT_DESCRIPTOR_sample"])]
        self.assertEqual(self.read(archive(objects)),
                         {"big", "ltcg", "imported", "__imp_imported", "__IMPORT_DESCRIPTOR_sample"})

    def test_unindexed_object_still_appears_in_complete_member_directory(self):
        objects = [(b"a.obj/", b"object", [b"public"]),
                   (b"empty.obj/", b"debug-only object", [])]
        self.assertEqual(self.read(archive(objects)), {"public"})

    def test_duplicate_definitions_can_be_coalesced_in_preferred_index(self):
        objects = [(b"same.obj/", b"first", [b"comdat"]),
                   (b"same.obj/", b"second", [b"comdat"])]
        self.assertEqual(self.read(archive(objects, second_entries=[(b"comdat", 0)])),
                         {"comdat"})
        self.assertEqual(self.read(archive(objects)), {"comdat"})

    def test_non_utf8_symbols_round_trip_without_filtering_or_replacement(self):
        raw = b"?symbol\xff@scope@@"
        symbols = self.read(archive([(b"one.obj/", b"object", [raw, b"has space"])]))
        self.assertEqual({s.encode("utf-8", "surrogateescape") for s in symbols},
                         {raw, b"has space"})

    def test_optional_empty_longnames_member(self):
        self.assertEqual(self.read(archive(long_names=b"")), {"?run@llvm@@YAXXZ"})

    def test_long_names_and_both_valid_alignment_encodings(self):
        name = b"directory/long-object-name.obj\0"
        objects = [(b"/0", b"object", [b"xy"])]
        for align in (True, False):
            with self.subTest(align=align):
                self.assertEqual(self.read(archive(objects, long_names=name, align=align)), {"xy"})
        # Deliberately odd table length, followed by the LLVM writer's newline
        # counted inside the member size, instead of archive-level padding.
        names = b"a-long-object-name.obj\0b-long-object-name.obj\0x.obj\0"
        if not len(names) & 1:
            names = b"z" + names
        for padding in (b"\n", b"\0"):
            with self.subTest(padding=padding):
                self.assertEqual(self.read(archive(objects, long_names=names + padding)), {"xy"})

    def test_bad_or_unsupported_archive_signatures(self):
        for magic in (b"!<thin>\n", b"<bigaf>\n", b"not lib!", b"", b"!<arch>"):
            with self.subTest(magic=magic):
                self.rejected(magic)

    def test_both_linker_members_are_mandatory(self):
        data = archive()
        locations = member_locations(data)
        second = locations[1][0]
        objects = locations[2][0]
        for candidate in (b"!<arch>\n", data[:second],
                          data[:second] + data[objects:]):
            with self.subTest(size=len(candidate)):
                self.rejected(candidate, "both Microsoft COFF linker members")

    def test_unsupported_special_members_fail_instead_of_omitting_symbols(self):
        for name in (b"/SYM64/", b"#1/16", b"/<ECSYMBOLS>/",
                     b"/<HYBRIDMAP>/", b"/", b"//"):
            with self.subTest(name=name):
                data = archive([(b"first.obj/", b"object", [b"symbol"]),
                                (name, b"extension data", [])])
                self.rejected(data)
        for name in (b"__.SYMDEF/", b"__.SYMDEF_64/", b"/SYM64/"):
            with self.subTest(first_member=name):
                self.rejected(overwrite(archive(), 8, name.ljust(16, b" ")))

    def test_bad_member_header_fields(self):
        data = archive()
        for offset, value in ((8, b" " * 16), (8, b"bad\0"), (24, b"-1"),
                              (36, b"x"), (42, b"1 2"), (48, b"8"),
                              (56, b"-1"), (56, b" 1"), (66, b"xx")):
            with self.subTest(offset=offset, value=value):
                self.rejected(overwrite(data, offset, value))

    def test_truncation_and_trailing_data_at_every_boundary(self):
        data = archive()
        # Every prefix truncation must fail, including the last object byte.
        for length in range(len(data)):
            with self.subTest(length=length):
                self.rejected(data[:length])
        self.rejected(data + b"junk")
        self.rejected(overwrite(data, 56, b"9999999999"))

    def test_object_padding_must_be_newline(self):
        data = archive([(b"a.obj/", b"odd", [b"a"])])
        self.assertEqual(data[-1:], b"\n")
        self.rejected(data[:-1] + b"\0", "alignment")

    def test_empty_archive_or_empty_public_index_is_not_audit_evidence(self):
        self.rejected(archive([]))
        self.rejected(archive([(b"a.obj/", b"opaque", [])]), "public definitions")
        self.rejected(archive([(b"a.obj/", b"", [b"x"])]), "empty object")

    def test_count_array_bounds_are_checked_before_iteration(self):
        data = archive()
        locations = member_locations(data)
        first_body, second_body = locations[0][1], locations[1][1]
        for offset, packed in ((first_body, struct.pack(">I", 0xFFFFFFFF)),
                               (second_body, struct.pack("<I", 0xFFFFFFFF)),
                               (second_body + 8, struct.pack("<I", 0xFFFFFFFF))):
            with self.subTest(offset=offset):
                self.rejected(overwrite(data, offset, packed))
        self.rejected(overwrite(data, first_body, struct.pack(">I", 100)))

    def test_index_offsets_must_point_to_object_headers(self):
        data = archive()
        locations = member_locations(data)
        first_offset = locations[0][1] + 4
        second_offset = locations[1][1] + 4
        for wrong in (0, 8, locations[1][0], locations[2][1], len(data), 0xFFFFFFFF):
            with self.subTest(wrong=wrong):
                self.rejected(overwrite(data, first_offset, struct.pack(">I", wrong)))
                self.rejected(overwrite(data, second_offset, struct.pack("<I", wrong)))

    def test_indices_are_one_based_and_bounded(self):
        data = archive()
        index_offset = member_locations(data)[1][1] + 12
        for index in (0, 2, 0xFFFF):
            with self.subTest(index=index):
                self.rejected(overwrite(data, index_offset, struct.pack("<H", index)),
                              "1-based")

    def test_member_directory_cannot_omit_even_an_unindexed_object(self):
        objects = [(b"a.obj/", b"object", [b"a"]), (b"b.obj/", b"object", [])]
        self.rejected(archive(objects, indexed_members=[0]), "every object")

    def test_member_directory_cannot_repeat_or_reorder_members(self):
        data = archive([(b"a.obj/", b"object", [b"a"]), (b"b.obj/", b"object", [b"b"])])
        locations = member_locations(data)
        offset = locations[1][1] + 4
        first, second = locations[2][0], locations[3][0]
        for pair in ((first, first), (second, first)):
            with self.subTest(pair=pair):
                self.rejected(overwrite(data, offset, struct.pack("<II", *pair)),
                              "directory")

    def test_first_index_requires_member_order(self):
        objects = [(b"a.obj/", b"object", [b"a"]), (b"b.obj/", b"object", [b"b"])]
        self.rejected(archive(objects, first_entries=[(b"b", 1), (b"a", 0)]), "ascending")

    def test_second_index_requires_symbol_order(self):
        objects = [(b"a.obj/", b"object", [b"z", b"a"])]
        self.rejected(archive(objects, second_entries=[(b"z", 0), (b"a", 0)]), "lexically")

    def test_indices_must_agree_on_symbols_and_defining_members(self):
        objects = [(b"a.obj/", b"object", [b"a"]), (b"b.obj/", b"object", [b"b"])]
        self.rejected(archive(objects, second_entries=[(b"a", 0)]), "symbol set")
        self.rejected(archive(objects, second_entries=[(b"a", 0), (b"c", 1)]), "symbol set")
        self.rejected(archive(objects, second_entries=[(b"a", 1), (b"b", 0)]), "definition")

    def test_names_must_be_nonempty_nul_terminated_and_exactly_counted(self):
        data = archive([(b"a.obj/", b"object", [b"alpha"])], align=False)
        for _, body, size in member_locations(data)[:2]:
            with self.subTest(body=body):
                self.rejected(overwrite(data, body + size - 1, b"x"), "unterminated")
        self.rejected(archive([(b"a.obj/", b"object", [b""])]))
        self.rejected(archive([(b"a.obj/", b"object", [b"a\0hidden"])]), "extra data")

    def test_extra_alignment_is_not_an_unchecked_string_table(self):
        data = archive([(b"a.obj/", b"object", [b"xy"])])
        for _, body, size in member_locations(data)[:2]:
            # Both index bodies need one NUL pad after the name in this case.
            self.assertEqual(data[body + size - 2:body + size], b"\0\0")
            self.rejected(overwrite(data, body + size - 1, b"x"), "extra data")

    def test_invalid_long_name_references_and_string_tables(self):
        objects = [(b"/0", b"object", [b"a"])]
        self.rejected(archive(objects), "long name")
        self.rejected(archive(objects, long_names=b"unterminated"), "unterminated")
        self.rejected(archive(objects, long_names=b"\0"), "long member name")
        self.rejected(archive([(b"/1", b"object", [b"a"])], long_names=b"long-name\0"),
                      "complete name's start")
        self.rejected(archive([(b"/9999", b"object", [b"a"])], long_names=b"name\0"))

    def test_resource_limits_fail_closed(self):
        data = archive()
        with patch.object(COFF, "_MAX_TABLE_BYTES", 4):
            self.rejected(data, "byte limit")
        with patch.object(COFF, "_MAX_ARCHIVE_BYTES", 8):
            self.rejected(data, "32-bit-offset size")
        with patch.object(COFF, "_MAX_SYMBOLS", 0):
            self.rejected(data, "public definitions")

    def test_payloads_are_not_read_into_memory(self):
        data = archive([(b"a.obj/", b"large opaque payload" * 100, [b"a"])])
        payload_start = member_locations(data)[2][1]

        class IndexOnlyReader(io.BytesIO):
            def read(self, size=-1):
                if size < 0 or self.tell() + size > payload_start:
                    raise AssertionError("object payload was read")
                return super().read(size)

        self.assertEqual(COFF._defined_symbols(IndexOnlyReader(data), len(data)), {"a"})

    def test_missing_input_is_an_io_error(self):
        with tempfile.TemporaryDirectory(prefix="neverc-coff-index-") as temporary:
            with self.assertRaises(OSError):
                COFF.read_defined_symbols(Path(temporary) / "missing.lib")

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO input exists only on Unix")
    def test_fifo_input_is_rejected_without_waiting_for_a_writer(self):
        with tempfile.TemporaryDirectory(prefix="neverc-coff-index-") as temporary:
            path = Path(temporary) / "fifo.lib"
            os.mkfifo(path)
            with self.assertRaisesRegex(ValueError, "regular library"):
                COFF.read_defined_symbols(path)


if __name__ == "__main__":
    unittest.main(verbosity=2)
