#!/usr/bin/env python3
"""Unit tests for bounded-memory DWARF and ELF evidence handling."""

import hashlib
import importlib.util
import io
from pathlib import Path
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


class DwarfMemoryTests(unittest.TestCase):
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
