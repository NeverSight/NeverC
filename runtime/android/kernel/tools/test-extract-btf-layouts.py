#!/usr/bin/env python3
"""Unit tests for bounded-memory DWARF and ELF evidence handling."""

import hashlib
import importlib.util
import io
from pathlib import Path
import struct
import unittest
from unittest import mock


TOOLS_ROOT = Path(__file__).resolve().parent


def load_tool(name, filename):
    spec = importlib.util.spec_from_file_location(name, TOOLS_ROOT / filename)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {filename}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


LAYOUT_TOOL = load_tool("nvk_extract_btf_layouts_test", "extract-btf-layouts.py")
MANIFEST_TOOL = load_tool(
    "nvk_generate_gki_manifest_test", "generate-gki-manifest.py"
)


class FakeDIE:
    tag = "DW_TAG_base_type"
    attributes = {}


class FakeAttribute:
    def __init__(self, value, form="DW_FORM_data1"):
        self.value = value
        self.form = form


class FixtureDIE:
    _next_offset = 1

    def __init__(self, tag, *, name="", size=None, location=None, target=None,
                 count=None, children=()):
        self.tag = tag
        self.offset = FixtureDIE._next_offset
        FixtureDIE._next_offset += 1
        self.attributes = {}
        if name:
            self.attributes["DW_AT_name"] = FakeAttribute(name.encode())
        if size is not None:
            self.attributes["DW_AT_byte_size"] = FakeAttribute(size)
        if location is not None:
            self.attributes["DW_AT_data_member_location"] = FakeAttribute(
                location
            )
        if count is not None:
            self.attributes["DW_AT_count"] = FakeAttribute(count)
        self.target = target
        self.children = tuple(children)

    def iter_children(self):
        yield from self.children

    def get_DIE_from_attribute(self, name):
        if name == "DW_AT_type":
            return self.target
        return None


class FakeCU:
    def __init__(self, index):
        self.index = index
        self._dielist = [FakeDIE(), FakeDIE()]
        self._diemap = [index * 2, index * 2 + 1]

    def iter_DIEs(self):
        yield from self._dielist


class FakeDWARF:
    structs = object()

    def __init__(self, count):
        self.count = count
        self.cus = []
        self._cu_cache = []
        self._cu_offsets_map = []

    def iter_CUs(self):
        for index in range(self.count):
            cu = FakeCU(index)
            self.cus.append(cu)
            self._cu_cache.append(cu)
            self._cu_offsets_map.append(index)
            yield cu


class FakeELF:
    def __init__(self, dwarf):
        self.dwarf = dwarf

    def has_dwarf_info(self):
        return True

    def get_dwarf_info(self):
        return self.dwarf


class FakeSection:
    def __init__(self, payload, *, compressed=False):
        self.payload = payload
        self.prefix = b"prefix"
        self.stream = io.BytesIO(self.prefix + payload + b"suffix")
        self.stream.seek(2)
        self.data_called = False
        self.values = {
            "sh_flags": MANIFEST_TOOL.SHF_COMPRESSED if compressed else 0,
            "sh_offset": len(self.prefix),
            "sh_size": len(payload),
        }

    def __getitem__(self, key):
        return self.values[key]

    def data(self):
        self.data_called = True
        return self.payload


def btf_dir_context_fixture():
    strings = b"\0dir_context\0actor\0pos\0s64\0"
    names = {
        name: strings.index(name.encode("ascii") + b"\0")
        for name in ("dir_context", "actor", "pos", "s64")
    }
    records = bytearray()

    # type 1: signed 64-bit integer
    records += struct.pack(
        "<III", names["s64"], LAYOUT_TOOL.BTF_KIND_INT << 24, 8
    )
    records += struct.pack("<I", 64)
    # type 2: pointer-sized filldir callback
    records += struct.pack(
        "<III", 0, LAYOUT_TOOL.BTF_KIND_PTR << 24, 0
    )
    # type 3: struct dir_context { actor@0; pos@8; }
    records += struct.pack(
        "<III",
        names["dir_context"],
        (LAYOUT_TOOL.BTF_KIND_STRUCT << 24) | 2,
        16,
    )
    records += struct.pack("<III", names["actor"], 2, 0)
    records += struct.pack("<III", names["pos"], 1, 64)

    header_size = 24
    header = struct.pack(
        "<HBBIIIII",
        LAYOUT_TOOL.BTF_MAGIC,
        1,
        0,
        header_size,
        0,
        len(records),
        len(records),
        len(strings),
    )
    return header + records + strings


def btf_filename_fixture():
    strings = b"\0filename\0name\0uptr\0refcnt\0aname\0iname\0int\0"
    names = {
        name: strings.index(name.encode("ascii") + b"\0")
        for name in (
            "filename",
            "name",
            "uptr",
            "refcnt",
            "aname",
            "iname",
            "int",
        )
    }
    records = bytearray()

    records += struct.pack(
        "<III", names["int"], LAYOUT_TOOL.BTF_KIND_INT << 24, 4
    )
    records += struct.pack("<I", 32)
    records += struct.pack(
        "<III", 0, LAYOUT_TOOL.BTF_KIND_PTR << 24, 1
    )
    records += struct.pack(
        "<III", 0, LAYOUT_TOOL.BTF_KIND_ARRAY << 24, 0
    )
    records += struct.pack("<III", 1, 1, 0)
    records += struct.pack(
        "<III",
        names["filename"],
        (LAYOUT_TOOL.BTF_KIND_STRUCT << 24) | 5,
        32,
    )
    for member, type_id, bit_offset in (
        ("name", 2, 0),
        ("uptr", 2, 64),
        ("refcnt", 1, 128),
        ("aname", 2, 192),
        ("iname", 3, 256),
    ):
        records += struct.pack(
            "<III", names[member], type_id, bit_offset
        )

    header_size = 24
    header = struct.pack(
        "<HBBIIIII",
        LAYOUT_TOOL.BTF_MAGIC,
        1,
        0,
        header_size,
        0,
        len(records),
        len(records),
        len(strings),
    )
    return header + records + strings


class DwarfMemoryTests(unittest.TestCase):
    def test_manifest_extracts_bounded_task_process_and_thread_evidence(self):
        self.assertIn("signal_struct", MANIFEST_TOOL.STRUCTURES)
        self.assertEqual(
            MANIFEST_TOOL.MEMBER_SIZE_MEMBERS["task_struct"],
            frozenset({
                "comm",
                "flags",
                "group_leader",
                "mm",
                "parent",
                "pid",
                "real_cred",
                "real_parent",
                "signal",
                "stack",
                "stack_refcount",
                "tasks",
                "thread_node",
                "thread_pid",
                "usage",
            }),
        )
        self.assertEqual(
            MANIFEST_TOOL.MEMBER_SIZE_MEMBERS["cred"],
            frozenset({
                "egid", "euid", "fsgid", "fsuid", "gid", "sgid",
                "suid", "uid",
            }),
        )
        self.assertEqual(
            MANIFEST_TOOL.MEMBER_SIZE_MEMBERS["pt_regs"],
            frozenset({"pc", "pstate", "regs", "sp"}),
        )
        self.assertEqual(
            MANIFEST_TOOL.MEMBER_SIZE_MEMBERS["signal_struct"],
            frozenset({"thread_head"}),
        )
        self.assertEqual(
            MANIFEST_TOOL.FILTERED_STRUCTURE_MEMBERS["signal_struct"],
            frozenset({"thread_head"}),
        )

    def test_manifest_extracts_bounded_filename_name_evidence(self):
        self.assertIn("filename", MANIFEST_TOOL.STRUCTURES)
        self.assertEqual(
            MANIFEST_TOOL.MEMBER_SIZE_MEMBERS["filename"],
            frozenset({"name"}),
        )
        self.assertEqual(
            MANIFEST_TOOL.FILTERED_STRUCTURE_MEMBERS["filename"],
            frozenset({"name"}),
        )

    def test_btf_records_filename_name_width_evidence(self):
        layout = LAYOUT_TOOL.parse_btf(
            btf_filename_fixture(), {"filename"}, pointer_size=8
        )["filename"]

        self.assertEqual(layout["size"], 32)
        self.assertEqual(layout["members"]["name"], 0)
        self.assertEqual(layout["member_sizes"]["name"], 8)

    def test_dwarf_records_filename_name_width_evidence(self):
        name_pointer = FixtureDIE("DW_TAG_pointer_type", size=8)
        name = FixtureDIE(
            "DW_TAG_member", name="name", location=0, target=name_pointer
        )
        filename = FixtureDIE(
            "DW_TAG_structure_type",
            name="filename",
            size=32,
            children=(name,),
        )

        layout = LAYOUT_TOOL.dwarf_structure_layout(
            filename, expression_parser=object(), pointer_size=8
        )
        self.assertEqual(
            layout,
            {
                "member_sizes": {"name": 8},
                "members": {"name": 0},
                "size": 32,
            },
        )

    def test_btf_layout_records_member_width_evidence(self):
        layouts = LAYOUT_TOOL.parse_btf(
            btf_dir_context_fixture(), {"dir_context"}, pointer_size=8
        )

        self.assertEqual(
            layouts["dir_context"],
            {
                "member_sizes": {"actor": 8, "pos": 8},
                "members": {"actor": 0, "pos": 8},
                "size": 16,
            },
        )

    def test_dwarf_layout_records_member_width_evidence(self):
        callback_pointer = FixtureDIE("DW_TAG_pointer_type", size=8)
        signed_64 = FixtureDIE("DW_TAG_base_type", size=8)
        actor = FixtureDIE(
            "DW_TAG_member", name="actor", location=0,
            target=callback_pointer,
        )
        pos = FixtureDIE(
            "DW_TAG_member", name="pos", location=8, target=signed_64
        )
        context = FixtureDIE(
            "DW_TAG_structure_type", name="dir_context", size=16,
            children=(actor, pos),
        )

        self.assertEqual(
            LAYOUT_TOOL.dwarf_structure_layout(
                context, expression_parser=object(), pointer_size=8
            ),
            {
                "member_sizes": {"actor": 8, "pos": 8},
                "members": {"actor": 0, "pos": 8},
                "size": 16,
            },
        )

    def test_dwarf_layout_records_array_member_width_evidence(self):
        unsigned_64 = FixtureDIE("DW_TAG_base_type", size=8)
        subrange = FixtureDIE("DW_TAG_subrange_type", count=31)
        regs_type = FixtureDIE(
            "DW_TAG_array_type", target=unsigned_64, children=(subrange,)
        )
        regs = FixtureDIE(
            "DW_TAG_member", name="regs", location=0, target=regs_type
        )
        context = FixtureDIE(
            "DW_TAG_structure_type", name="pt_regs", size=336,
            children=(regs,),
        )

        self.assertEqual(
            LAYOUT_TOOL.dwarf_structure_layout(
                context, expression_parser=object(), pointer_size=8
            )["member_sizes"]["regs"],
            31 * 8,
        )

    def test_extract_dwarf_layouts_releases_each_cu_cache(self):
        dwarf = FakeDWARF(4)
        with mock.patch.object(LAYOUT_TOOL, "DWARFExprParser", return_value=object()):
            layouts = LAYOUT_TOOL.extract_dwarf_layouts(
                FakeELF(dwarf), {"missing"}
            )

        self.assertEqual(layouts, {})
        self.assertEqual(dwarf._cu_cache, [])
        self.assertEqual(dwarf._cu_offsets_map, [])
        for cu in dwarf.cus:
            self.assertEqual(cu._dielist, [])
            self.assertEqual(cu._diemap, [])

    def test_uncompressed_section_hash_is_streamed_and_restores_position(self):
        payload = b"large-debug-section" * 1024
        section = FakeSection(payload)

        actual = MANIFEST_TOOL.elf_section_sha256(section, chunk_size=97)

        self.assertEqual(actual, hashlib.sha256(payload).hexdigest())
        self.assertFalse(section.data_called)
        self.assertEqual(section.stream.tell(), 2)

    def test_compressed_section_hash_uses_pyelftools_decompression(self):
        payload = b"decompressed-debug-section"
        section = FakeSection(payload, compressed=True)

        actual = MANIFEST_TOOL.elf_section_sha256(section)

        self.assertEqual(actual, hashlib.sha256(payload).hexdigest())
        self.assertTrue(section.data_called)


if __name__ == "__main__":
    unittest.main()
