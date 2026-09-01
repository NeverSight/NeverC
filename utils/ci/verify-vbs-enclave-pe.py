#!/usr/bin/env python3
"""Strict semantic verifier for the VBS enclave differential CI fixture."""

import argparse
import json
import pathlib
import re
import struct
import sys
from typing import Any, Dict, List, Optional, Tuple


class VerificationError(Exception):
    pass


DLL = 0x2000
RELOCS_STRIPPED = 0x0001
EXECUTABLE_IMAGE = 0x0002
DYNAMIC_BASE = 0x0040
FORCE_INTEGRITY = 0x0080
GUARD_CF = 0x4000
SECTION_MEM_READ = 0x40000000
SECTION_MEM_WRITE = 0x80000000
CF_INSTRUMENTED = 0x0100
CF_FUNCTION_TABLE_PRESENT = 0x0400
CF_LONGJUMP_TABLE_PRESENT = 0x10000
CF_EH_CONTINUATION_TABLE_PRESENT = 0x00400000
CF_EXPORT_SUPPRESSION_INFO_PRESENT = 0x4000
CF_FUNCTION_TABLE_SIZE_MASK = 0xF0000000
CF_FUNCTION_TABLE_SIZE_5BYTES = 0x10000000
GFID_FID_SUPPRESSED = 0x01
GFID_EXPORT_SUPPRESSED = 0x02
GFID_FID_LANGEXCPTHANDLER = 0x04
GFID_FID_XFG = 0x08
GFID_KNOWN_FLAGS = (GFID_FID_SUPPRESSED | GFID_EXPORT_SUPPRESSED |
                    GFID_FID_LANGEXCPTHANDLER | GFID_FID_XFG)
DIR64 = 10
EXPORT_DIRECTORY = 0
IMPORT_DIRECTORY = 1
LOAD_CONFIG_DIRECTORY = 10
BASE_RELOCATION_DIRECTORY = 5
CERTIFICATE_DIRECTORY = 4
IAT_DIRECTORY = 12
LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET = 0x70
LOAD_CONFIG_GUARD_DISPATCH_POINTER_OFFSET = 0x78
LOAD_CONFIG_GFID_TABLE_OFFSET = 0x80
LOAD_CONFIG_GIAT_TABLE_OFFSET = 0xA0
LOAD_CONFIG_GIAT_COUNT_OFFSET = 0xA8
LOAD_CONFIG_ENCLAVE_POINTER_OFFSET = 0xF8
LOAD_CONFIG_REQUIRED_SIZE = 0x100
ENCLAVE_CONFIG_SIZE = 80
ENCLAVE_IMPORT_SIZE = 80
IMPORT_DESCRIPTOR_SIZE = 20
MAX_IMPORT_NAME_SIZE = 4096
EXPECTED_IMPORT_NAMES = frozenset(("ucrtbase_enclave.dll", "vertdll.dll"))
EXPECTED_FAMILY_ID = bytes.fromhex("912d7418b6534c2a8ea45739c106fd22")
EXPECTED_IMAGE_ID = bytes.fromhex("37a8c5406fd149bb9a0ee31572bc489d")
EXPECTED_EXPORTS = {
    "GuardedExercise": {
        "kind": "function", "ordinal": 1, "gfid_covered": False,
    },
    "GuardedIndirectCall": {
        "kind": "function", "ordinal": 2, "gfid_covered": False,
    },
    "GuardedTarget": {
        "kind": "function", "ordinal": 3, "gfid_covered": True,
    },
    "LegacyAddressTaken": {
        "kind": "data", "ordinal": 4, "gfid_covered": False,
    },
    "LegacyExercise": {
        "kind": "function", "ordinal": 5, "gfid_covered": False,
    },
    "LegacyTarget": {
        "kind": "function", "ordinal": 6, "gfid_covered": True,
    },
}
GUARD_MAP_BINDINGS = (
    (LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET,
     "GuardCFCheckFunctionPointer", "__guard_check_icall_fptr",
     "_guard_check_icall_nop"),
    (LOAD_CONFIG_GUARD_DISPATCH_POINTER_OFFSET,
     "GuardCFDispatchFunctionPointer", "__guard_dispatch_icall_fptr",
     "_guard_dispatch_icall_nop"),
)
REQUIRED_MAP_SYMBOLS = frozenset(EXPECTED_EXPORTS) | frozenset(
    symbol
    for _, _, slot_symbol, target_symbol in GUARD_MAP_BINDINGS
    for symbol in (slot_symbol, target_symbol)
)
MAP_SYMBOL_LINE = re.compile(
    r"^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8,16}\s+"
    r"(\S+)\s+([0-9A-Fa-f]{8,16})(?:\s|$)")


def _checked_product(left: int, right: int, limit: int, what: str) -> int:
    if left < 0 or right < 0 or (left and right > limit // left):
        raise VerificationError("%s size overflows the image" % what)
    return left * right


def parse_coff_map_text(text: str, label: str) -> Dict[str, int]:
    symbols: Dict[str, int] = {}
    for line in text.splitlines():
        match = MAP_SYMBOL_LINE.match(line)
        if match is None:
            continue
        name, encoded_va = match.groups()
        if name not in REQUIRED_MAP_SYMBOLS:
            continue
        va = int(encoded_va, 16)
        previous = symbols.get(name)
        if previous is not None and previous != va:
            raise VerificationError(
                "%s: COFF map symbol %s has ambiguous VAs 0x%x and 0x%x" %
                (label, name, previous, va))
        symbols[name] = va
    missing = sorted(REQUIRED_MAP_SYMBOLS - symbols.keys())
    if missing:
        raise VerificationError(
            "%s: COFF map is missing required symbols: %s" %
            (label, ", ".join(missing)))
    return symbols


def parse_coff_map_path(path: pathlib.Path) -> Dict[str, int]:
    try:
        text = path.read_bytes().decode("utf-8-sig", errors="replace")
    except OSError as error:
        raise VerificationError("cannot read %s: %s" % (path, error))
    return parse_coff_map_text(text, str(path))


class PEImage:
    def __init__(self, data: bytes, label: str):
        self.data = data
        self.label = label
        self.sections: List[Dict[str, int]] = []
        self.directories: List[Tuple[int, int]] = []

    @classmethod
    def from_path(cls, path: pathlib.Path) -> "PEImage":
        try:
            return cls(path.read_bytes(), str(path))
        except OSError as error:
            raise VerificationError("cannot read %s: %s" % (path, error))

    def need(self, offset: int, size: int, what: str) -> None:
        if offset < 0 or size < 0 or offset > len(self.data) - size:
            raise VerificationError("%s: truncated or out-of-bounds %s" %
                                    (self.label, what))

    def u16(self, offset: int, what: str) -> int:
        self.need(offset, 2, what)
        return struct.unpack_from("<H", self.data, offset)[0]

    def u32(self, offset: int, what: str) -> int:
        self.need(offset, 4, what)
        return struct.unpack_from("<I", self.data, offset)[0]

    def u64(self, offset: int, what: str) -> int:
        self.need(offset, 8, what)
        return struct.unpack_from("<Q", self.data, offset)[0]

    def rva_to_offset(self, rva: int, size: int, what: str) -> int:
        if rva < 0 or size < 0 or rva > 0xFFFFFFFF - size:
            raise VerificationError("%s: invalid RVA for %s" %
                                    (self.label, what))
        if rva + size > self.size_of_image:
            raise VerificationError("%s: %s extends past SizeOfImage" %
                                    (self.label, what))
        if rva < self.size_of_headers:
            if rva + size > self.size_of_headers:
                raise VerificationError("%s: %s crosses PE headers" %
                                        (self.label, what))
            self.need(rva, size, what)
            return rva
        matches = []
        for section in self.sections:
            virtual_size = section["virtual_size"] or section["raw_size"]
            span = min(virtual_size, section["raw_size"])
            start = section["rva"]
            if rva >= start and rva + size <= start + span:
                matches.append(section)
        if len(matches) != 1:
            raise VerificationError("%s: %s RVA is not in exactly one section" %
                                    (self.label, what))
        section = matches[0]
        delta = rva - section["rva"]
        if delta + size > section["raw_size"]:
            raise VerificationError("%s: %s points into uninitialized data" %
                                    (self.label, what))
        offset = section["raw_pointer"] + delta
        self.need(offset, size, what)
        return offset

    def va_to_rva(self, va: int, what: str) -> int:
        if va < self.image_base or va - self.image_base > 0xFFFFFFFF:
            raise VerificationError("%s: %s VA is outside the image" %
                                    (self.label, what))
        return va - self.image_base

    def map_symbol_rva(self, map_symbols: Dict[str, int], name: str) -> int:
        if name not in map_symbols:
            raise VerificationError(
                "%s: COFF map is missing required symbol %s" %
                (self.label, name))
        rva = self.va_to_rva(map_symbols[name], "COFF map symbol %s" % name)
        if rva >= self.size_of_image:
            raise VerificationError(
                "%s: COFF map symbol %s is outside SizeOfImage" %
                (self.label, name))
        return rva

    def cstring_at_rva(self, rva: int, what: str) -> str:
        """Read a bounded printable ASCII C string from initialized raw data."""
        if not rva:
            raise VerificationError("%s: zero RVA for %s" % (self.label, what))
        section = self.initialized_section_for_rva(rva)
        if section is None:
            raise VerificationError(
                "%s: %s RVA is not backed by initialized raw section data" %
                (self.label, what))
        delta = rva - section["rva"]
        virtual_size = section["virtual_size"] or section["raw_size"]
        initialized_size = min(virtual_size, section["raw_size"])
        available = initialized_size - delta
        limit = min(available, MAX_IMPORT_NAME_SIZE)
        offset = self.rva_to_offset(rva, 1, what)
        terminator = self.data.find(b"\0", offset, offset + limit)
        if terminator < 0:
            if available > MAX_IMPORT_NAME_SIZE:
                raise VerificationError(
                    "%s: %s exceeds the %d-byte limit" %
                    (self.label, what, MAX_IMPORT_NAME_SIZE))
            raise VerificationError(
                "%s: unterminated %s before the end of its raw-mapped section" %
                (self.label, what))
        raw_value = self.data[offset:terminator]
        if not raw_value:
            raise VerificationError("%s: empty %s" % (self.label, what))
        if any(byte < 0x20 or byte > 0x7E for byte in raw_value):
            raise VerificationError("%s: non-printable ASCII %s" %
                                    (self.label, what))
        return raw_value.decode("ascii")

    def inspect_standard_imports(self) -> List[str]:
        import_rva, import_size = self.directory(IMPORT_DIRECTORY)
        if not import_rva or import_size < IMPORT_DESCRIPTOR_SIZE:
            raise VerificationError("%s: missing or short standard import directory" %
                                    self.label)
        if import_size % IMPORT_DESCRIPTOR_SIZE:
            raise VerificationError("%s: misaligned standard import directory size" %
                                    self.label)
        import_offset = self.rva_to_offset(import_rva, import_size,
                                           "standard import directory")
        names: List[str] = []
        normalized_names = set()
        terminated = False
        descriptor_count = import_size // IMPORT_DESCRIPTOR_SIZE
        for index in range(descriptor_count):
            descriptor = import_offset + index * IMPORT_DESCRIPTOR_SIZE
            fields = struct.unpack_from("<IIIII", self.data, descriptor)
            if not any(fields):
                trailing = self.data[descriptor + IMPORT_DESCRIPTOR_SIZE:
                                     import_offset + import_size]
                if any(trailing):
                    raise VerificationError(
                        "%s: nonzero data follows the standard import terminator" %
                        self.label)
                terminated = True
                break
            name_rva = fields[3]
            if not name_rva:
                raise VerificationError("%s: standard import has a zero Name RVA" %
                                        self.label)
            if not fields[4]:
                raise VerificationError("%s: standard import has a zero FirstThunk RVA" %
                                        self.label)
            self.rva_to_offset(fields[4], 8, "standard import IAT")
            self.rva_to_offset(fields[0] or fields[4], 8,
                               "standard import lookup table")
            name = self.cstring_at_rva(name_rva, "standard import DLL name")
            normalized = name.lower()
            if normalized in normalized_names:
                raise VerificationError(
                    "%s: duplicate standard import DLL name %r" %
                    (self.label, name))
            normalized_names.add(normalized)
            names.append(normalized)
        if not terminated:
            raise VerificationError("%s: unterminated standard import directory" %
                                    self.label)
        if normalized_names != EXPECTED_IMPORT_NAMES:
            raise VerificationError(
                "%s: standard imports differ from the fixture contract: %r" %
                (self.label, sorted(normalized_names)))
        return sorted(names)

    def executable_rva(self, rva: int) -> bool:
        section = self.initialized_section_for_rva(rva)
        return bool(section and section["characteristics"] & 0x20000000)

    def initialized_section_for_rva(self, rva: int) -> Optional[Dict[str, int]]:
        matches = []
        for section in self.sections:
            virtual_size = section["virtual_size"] or section["raw_size"]
            initialized_size = min(virtual_size, section["raw_size"])
            if section["rva"] <= rva < section["rva"] + initialized_size:
                matches.append(section)
        return matches[0] if len(matches) == 1 else None

    def directory(self, index: int) -> Tuple[int, int]:
        if index >= len(self.directories):
            return (0, 0)
        return self.directories[index]

    def inspect(self, map_symbols: Dict[str, int],
                expected_machine: Optional[str] = None,
                expected_export_name: Optional[str] = None) -> Dict[str, Any]:
        self.need(0, 0x40, "DOS header")
        if self.data[:2] != b"MZ":
            raise VerificationError("%s: missing MZ signature" % self.label)
        pe_offset = self.u32(0x3C, "e_lfanew")
        self.need(pe_offset, 24, "PE/COFF header")
        if self.data[pe_offset:pe_offset + 4] != b"PE\0\0":
            raise VerificationError("%s: missing PE signature" % self.label)

        coff = pe_offset + 4
        machine = self.u16(coff, "Machine")
        machines = {0x8664: "x86_64", 0xAA64: "arm64"}
        if machine not in machines:
            raise VerificationError("%s: unsupported machine 0x%04x" %
                                    (self.label, machine))
        machine_name = machines[machine]
        if expected_machine is not None and machine_name != expected_machine:
            raise VerificationError(
                "%s: machine is %s, expected %s" %
                (self.label, machine_name, expected_machine))
        section_count = self.u16(coff + 2, "NumberOfSections")
        if section_count == 0 or section_count > 96:
            raise VerificationError("%s: invalid section count %d" %
                                    (self.label, section_count))
        optional_size = self.u16(coff + 16, "SizeOfOptionalHeader")
        characteristics = self.u16(coff + 18, "Characteristics")
        if not characteristics & DLL:
            raise VerificationError("%s: image is not a DLL" % self.label)
        if not characteristics & EXECUTABLE_IMAGE:
            raise VerificationError("%s: image is not marked executable" %
                                    self.label)
        if characteristics & RELOCS_STRIPPED:
            raise VerificationError("%s: image marks base relocations stripped" %
                                    self.label)

        optional = coff + 20
        self.need(optional, optional_size, "optional header")
        if optional_size < 112 or self.u16(optional, "OptionalHeader.Magic") != 0x20B:
            raise VerificationError("%s: expected a PE32+ optional header" % self.label)
        self.image_base = self.u64(optional + 24, "ImageBase")
        self.size_of_image = self.u32(optional + 56, "SizeOfImage")
        self.size_of_headers = self.u32(optional + 60, "SizeOfHeaders")
        dll_characteristics = self.u16(optional + 70, "DllCharacteristics")
        for bit, name in ((DYNAMIC_BASE, "DYNAMIC_BASE"),
                          (FORCE_INTEGRITY, "FORCE_INTEGRITY"),
                          (GUARD_CF, "GUARD_CF")):
            if not dll_characteristics & bit:
                raise VerificationError("%s: missing %s" % (self.label, name))
        if not self.image_base or not self.size_of_image:
            raise VerificationError("%s: zero image base or size" % self.label)
        if not self.size_of_headers or self.size_of_headers > len(self.data):
            raise VerificationError("%s: invalid SizeOfHeaders" % self.label)

        directory_count = self.u32(optional + 108, "NumberOfRvaAndSizes")
        available_directories = max(0, (optional_size - 112) // 8)
        if directory_count > available_directories:
            raise VerificationError("%s: data directories exceed optional header" %
                                    self.label)
        for index in range(directory_count):
            entry = optional + 112 + index * 8
            self.directories.append((self.u32(entry, "directory RVA"),
                                     self.u32(entry + 4, "directory size")))

        section_table = optional + optional_size
        self.need(section_table, section_count * 40, "section table")
        if section_table + section_count * 40 > self.size_of_headers:
            raise VerificationError("%s: section table exceeds SizeOfHeaders" %
                                    self.label)
        virtual_ranges = []
        for index in range(section_count):
            offset = section_table + index * 40
            name = self.data[offset:offset + 8].split(b"\0", 1)[0].decode(
                "ascii", errors="replace")
            virtual_size = self.u32(offset + 8, "section VirtualSize")
            rva = self.u32(offset + 12, "section VirtualAddress")
            raw_size = self.u32(offset + 16, "section SizeOfRawData")
            raw_pointer = self.u32(offset + 20, "section PointerToRawData")
            section_flags = self.u32(offset + 36, "section Characteristics")
            if raw_size:
                self.need(raw_pointer, raw_size, "section %s raw data" % name)
            span = max(virtual_size, raw_size)
            if span and (rva >= self.size_of_image or
                         span > self.size_of_image - rva):
                raise VerificationError("%s: section %s exceeds SizeOfImage" %
                                        (self.label, name))
            for start, end in virtual_ranges:
                if span and max(start, rva) < min(end, rva + span):
                    raise VerificationError("%s: overlapping virtual sections" %
                                            self.label)
            if span:
                virtual_ranges.append((rva, rva + span))
            self.sections.append({
                "name": name, "virtual_size": virtual_size, "rva": rva,
                "raw_size": raw_size, "raw_pointer": raw_pointer,
                "characteristics": section_flags,
            })

        certificate_offset, certificate_size = self.directory(CERTIFICATE_DIRECTORY)
        if bool(certificate_offset) != bool(certificate_size):
            raise VerificationError("%s: malformed certificate directory" % self.label)
        if certificate_size:
            self.need(certificate_offset, certificate_size, "certificate table")

        standard_imports = self.inspect_standard_imports()

        load_rva, load_directory_size = self.directory(LOAD_CONFIG_DIRECTORY)
        if not load_rva or load_directory_size < LOAD_CONFIG_REQUIRED_SIZE:
            raise VerificationError("%s: missing or short load-config directory" %
                                    self.label)
        load_offset = self.rva_to_offset(load_rva, LOAD_CONFIG_REQUIRED_SIZE,
                                         "load configuration")
        load_declared_size = self.u32(load_offset, "load-config Size")
        if load_declared_size < LOAD_CONFIG_REQUIRED_SIZE:
            raise VerificationError("%s: load-config Size does not cover enclave pointer" %
                                    self.label)
        if load_declared_size > load_directory_size:
            raise VerificationError("%s: load-config Size exceeds its directory" %
                                    self.label)
        self.rva_to_offset(load_rva, load_declared_size,
                           "declared load configuration")

        guard_dispatch_slots = []
        for (field_offset, field_name, slot_symbol,
             target_symbol) in GUARD_MAP_BINDINGS:
            slot_va = self.u64(load_offset + field_offset, field_name)
            if not slot_va:
                raise VerificationError("%s: zero %s" %
                                        (self.label, field_name))
            slot_rva = self.va_to_rva(slot_va, field_name)
            if slot_rva & 7:
                raise VerificationError("%s: %s slot is not 8-byte aligned" %
                                        (self.label, field_name))
            slot_offset = self.rva_to_offset(
                slot_rva, 8, "%s slot" % field_name)
            slot_section = self.initialized_section_for_rva(slot_rva)
            if (slot_section is None or
                    not slot_section["characteristics"] & SECTION_MEM_READ or
                    slot_section["characteristics"] & SECTION_MEM_WRITE):
                raise VerificationError(
                    "%s: %s slot is not in initialized read-only memory" %
                    (self.label, field_name))
            target_va = self.u64(slot_offset, "%s target" % field_name)
            if not target_va:
                raise VerificationError("%s: zero %s target" %
                                        (self.label, field_name))
            target_rva = self.va_to_rva(target_va, "%s target" % field_name)
            if not self.executable_rva(target_rva):
                raise VerificationError(
                    "%s: %s target is not initialized executable image data" %
                    (self.label, field_name))
            guard_dispatch_slots.append({
                "field_offset": field_offset,
                "name": field_name,
                "slot_rva": slot_rva,
                "target_rva": target_rva,
                "slot_symbol": slot_symbol,
                "target_symbol": target_symbol,
            })
        if (guard_dispatch_slots[0]["slot_rva"] ==
                guard_dispatch_slots[1]["slot_rva"]):
            raise VerificationError(
                "%s: GuardCF check and dispatch pointers alias one slot" %
                self.label)
        for entry in guard_dispatch_slots:
            expected_slot_rva = self.map_symbol_rva(
                map_symbols, entry["slot_symbol"])
            if entry["slot_rva"] != expected_slot_rva:
                raise VerificationError(
                    "%s: %s slot RVA 0x%x does not match COFF map symbol "
                    "%s at RVA 0x%x" %
                    (self.label, entry["name"], entry["slot_rva"],
                     entry["slot_symbol"], expected_slot_rva))
            expected_target_rva = self.map_symbol_rva(
                map_symbols, entry["target_symbol"])
            if entry["target_rva"] != expected_target_rva:
                raise VerificationError(
                    "%s: %s target RVA 0x%x does not match COFF map symbol "
                    "%s at RVA 0x%x" %
                    (self.label, entry["name"], entry["target_rva"],
                     entry["target_symbol"], expected_target_rva))

        guard_table_va = self.u64(
            load_offset + LOAD_CONFIG_GFID_TABLE_OFFSET,
            "GuardCFFunctionTable")
        guard_count = self.u64(load_offset + 0x88, "GuardCFFunctionCount")
        guard_flags = self.u32(load_offset + 0x90, "GuardFlags")
        required_guard_flags = CF_INSTRUMENTED | CF_FUNCTION_TABLE_PRESENT
        if guard_flags & required_guard_flags != required_guard_flags:
            raise VerificationError("%s: GuardFlags lack CFG instrumentation/table bits" %
                                    self.label)
        if guard_flags & CF_LONGJUMP_TABLE_PRESENT:
            raise VerificationError("%s: /GUARD:MIXED unexpectedly enabled longjmp metadata" %
                                    self.label)
        if guard_flags & CF_EH_CONTINUATION_TABLE_PRESENT:
            raise VerificationError(
                "%s: /GUARD:MIXED unexpectedly enabled EH continuation metadata" %
                self.label)
        if not guard_table_va or not guard_count:
            raise VerificationError("%s: empty CFG function table" % self.label)
        guard_stride = 4 + ((guard_flags & CF_FUNCTION_TABLE_SIZE_MASK) >> 28)
        if guard_stride != 5:
            raise VerificationError("%s: /GUARD:MIXED requires a 5-byte GFID stride, got %d" %
                                    (self.label, guard_stride))
        guard_bytes = _checked_product(guard_count, guard_stride, len(self.data),
                                       "CFG function table")
        guard_rva = self.va_to_rva(guard_table_va, "GuardCFFunctionTable")
        guard_offset = self.rva_to_offset(guard_rva, guard_bytes,
                                          "CFG function table")
        gfids = []
        gfid_metadata = []
        gfid_flags_by_rva = {}
        has_export_suppression = False
        for index in range(guard_count):
            gfid = self.u32(guard_offset + index * guard_stride,
                            "CFG function entry")
            if not gfid or not self.executable_rva(gfid):
                raise VerificationError("%s: GFID 0x%x is not executable" %
                                        (self.label, gfid))
            if gfids and gfid <= gfids[-1]:
                raise VerificationError("%s: GFID table is not strictly sorted" %
                                        self.label)
            gfids.append(gfid)
            metadata = self.data[guard_offset + index * guard_stride + 4:
                                 guard_offset + (index + 1) * guard_stride]
            gfid_flags = metadata[0] if metadata else 0
            if gfid_flags & ~GFID_KNOWN_FLAGS:
                raise VerificationError("%s: GFID entry has undefined flags" %
                                        self.label)
            if gfid_flags & GFID_FID_LANGEXCPTHANDLER:
                raise VerificationError(
                    "%s: GFID entry unexpectedly marks a language exception "
                    "handler" % self.label)
            if gfid_flags & GFID_FID_XFG:
                raise VerificationError(
                    "%s: GFID entry unexpectedly marks an XFG target" %
                    self.label)
            if any(metadata[1:]):
                raise VerificationError("%s: GFID entry has undefined metadata" %
                                        self.label)
            gfid_metadata.append(metadata.hex())
            gfid_flags_by_rva[gfid] = gfid_flags
            has_export_suppression |= bool(
                gfid_flags & GFID_EXPORT_SUPPRESSED)
        if (has_export_suppression and
                not guard_flags & CF_EXPORT_SUPPRESSION_INFO_PRESENT):
            raise VerificationError(
                "%s: export-suppressed GFID lacks GuardFlags suppression info" %
                self.label)

        usable_gfid_set = {
            gfid for gfid in gfids
            if not gfid_flags_by_rva[gfid] &
            (GFID_FID_SUPPRESSED | GFID_EXPORT_SUPPRESSED)
        }

        giat_table_va = self.u64(
            load_offset + LOAD_CONFIG_GIAT_TABLE_OFFSET,
            "GuardAddressTakenIatEntryTable")
        giat_count = self.u64(
            load_offset + LOAD_CONFIG_GIAT_COUNT_OFFSET,
            "GuardAddressTakenIatEntryCount")
        if bool(giat_table_va) != bool(giat_count):
            raise VerificationError(
                "%s: GIAT table pointer and count are inconsistent" % self.label)
        giat_table_rva = 0
        giats = []
        giat_flags = []
        if giat_count:
            iat_rva, iat_size = self.directory(IAT_DIRECTORY)
            if not iat_rva or not iat_size:
                raise VerificationError(
                    "%s: nonempty GIAT lacks an IAT data directory" % self.label)
            if iat_rva & 7 or iat_size & 7:
                raise VerificationError(
                    "%s: PE32+ IAT directory is not 8-byte aligned" % self.label)
            self.rva_to_offset(iat_rva, iat_size, "IAT directory")
            if iat_rva > 0xFFFFFFFF - iat_size:
                raise VerificationError("%s: IAT directory range overflows" %
                                        self.label)
            iat_end = iat_rva + iat_size
            giat_bytes = _checked_product(giat_count, guard_stride,
                                          len(self.data), "GIAT")
            giat_table_rva = self.va_to_rva(
                giat_table_va, "GuardAddressTakenIatEntryTable")
            giat_offset = self.rva_to_offset(giat_table_rva, giat_bytes,
                                             "GIAT")
            for index in range(giat_count):
                entry_offset = giat_offset + index * guard_stride
                entry_rva = self.u32(entry_offset, "GIAT entry")
                if (entry_rva & 7 or entry_rva < iat_rva or
                        entry_rva > iat_end - 8):
                    raise VerificationError(
                        "%s: GIAT entry 0x%x is not an aligned PE32+ IAT slot" %
                        (self.label, entry_rva))
                if giats and entry_rva <= giats[-1]:
                    raise VerificationError(
                        "%s: GIAT is not strictly sorted" % self.label)
                metadata = self.data[entry_offset + 4:
                                     entry_offset + guard_stride]
                if any(metadata):
                    raise VerificationError(
                        "%s: GIAT entry has nonzero metadata" % self.label)
                giats.append(entry_rva)
                giat_flags.append(list(metadata))

        export_rva, export_size = self.directory(EXPORT_DIRECTORY)
        if not export_rva or export_size < 40:
            raise VerificationError("%s: missing or short export directory" %
                                    self.label)
        export_offset = self.rva_to_offset(export_rva, export_size,
                                           "export directory")
        if self.u32(export_offset, "export Characteristics") != 0:
            raise VerificationError("%s: export Characteristics is nonzero" %
                                    self.label)
        export_name_rva = self.u32(export_offset + 12, "export DLL Name")
        export_base = self.u32(export_offset + 16, "export ordinal Base")
        function_count = self.u32(export_offset + 20,
                                  "export NumberOfFunctions")
        name_count = self.u32(export_offset + 24, "export NumberOfNames")
        functions_rva = self.u32(export_offset + 28,
                                 "export AddressOfFunctions")
        names_rva = self.u32(export_offset + 32, "export AddressOfNames")
        ordinals_rva = self.u32(export_offset + 36,
                                "export AddressOfNameOrdinals")
        expected_export_count = len(EXPECTED_EXPORTS)
        if export_base != 1:
            raise VerificationError("%s: export ordinal Base is not 1" %
                                    self.label)
        if (function_count != expected_export_count or
                name_count != expected_export_count):
            raise VerificationError(
                "%s: export function/name counts differ from fixture contract" %
                self.label)
        if not functions_rva or not names_rva or not ordinals_rva:
            raise VerificationError("%s: missing export address table" % self.label)
        function_bytes = _checked_product(function_count, 4, len(self.data),
                                          "export address table")
        name_bytes = _checked_product(name_count, 4, len(self.data),
                                      "export name pointer table")
        ordinal_bytes = _checked_product(name_count, 2, len(self.data),
                                         "export ordinal table")
        functions_offset = self.rva_to_offset(functions_rva, function_bytes,
                                              "export address table")
        names_offset = self.rva_to_offset(names_rva, name_bytes,
                                          "export name pointer table")
        ordinals_offset = self.rva_to_offset(ordinals_rva, ordinal_bytes,
                                             "export ordinal table")
        export_end = export_rva + export_size
        export_targets = []
        for index in range(function_count):
            target_rva = self.u32(functions_offset + index * 4,
                                  "export address entry")
            target = {
                "ordinal": export_base + index,
                "target_rva": target_rva,
                "kind": "absent",
                "gfid_covered": False,
            }
            if target_rva:
                if export_rva <= target_rva < export_end:
                    target["kind"] = "forwarder"
                    target["forwarder"] = self.cstring_at_rva(
                        target_rva, "export forwarder")
                else:
                    containing = self.initialized_section_for_rva(target_rva)
                    if containing is None:
                        raise VerificationError(
                            "%s: export target 0x%x is not backed by initialized image data" %
                            (self.label, target_rva))
                    executable = bool(containing["characteristics"] & 0x20000000)
                    target["kind"] = "function" if executable else "data"
                    target["gfid_covered"] = target_rva in usable_gfid_set
            export_targets.append(target)
        concrete_export_targets = [
            target["target_rva"] for target in export_targets
            if target["target_rva"] and target["kind"] != "forwarder"
        ]
        if len(concrete_export_targets) != len(set(concrete_export_targets)):
            raise VerificationError("%s: exports alias one target RVA" %
                                    self.label)

        exports = []
        seen_export_names = set()
        seen_export_ordinals = set()
        seen_export_targets = set()
        for index in range(name_count):
            name_rva = self.u32(names_offset + index * 4,
                                "export name pointer")
            name = self.cstring_at_rva(name_rva, "export name")
            if exports and name <= exports[-1]["name"]:
                raise VerificationError(
                    "%s: export name pointer table is not strictly lexical" %
                    self.label)
            if name in seen_export_names:
                raise VerificationError("%s: duplicate export name %s" %
                                        (self.label, name))
            seen_export_names.add(name)
            ordinal_index = self.u16(ordinals_offset + index * 2,
                                     "export name ordinal")
            if ordinal_index >= function_count:
                raise VerificationError("%s: export name ordinal is out of range" %
                                        self.label)
            if ordinal_index in seen_export_ordinals:
                raise VerificationError("%s: export names alias the same ordinal" %
                                        self.label)
            seen_export_ordinals.add(ordinal_index)
            target = export_targets[ordinal_index]
            if target["kind"] == "absent":
                raise VerificationError("%s: named export %s has no target" %
                                        (self.label, name))
            if target["target_rva"] in seen_export_targets:
                raise VerificationError("%s: export names alias the same target" %
                                        self.label)
            seen_export_targets.add(target["target_rva"])
            exports.append({"name": name, **target})
        if seen_export_ordinals != set(range(function_count)):
            raise VerificationError("%s: export ordinal table is incomplete" %
                                    self.label)
        actual_exports = {
            entry["name"]: {
                "kind": entry["kind"],
                "ordinal": entry["ordinal"],
                "gfid_covered": entry["gfid_covered"],
            }
            for entry in exports
        }
        actual_export_coverage = {
            entry["name"]: entry["gfid_covered"] for entry in exports
        }
        expected_export_coverage = {
            name: entry["gfid_covered"]
            for name, entry in EXPECTED_EXPORTS.items()
        }
        if actual_export_coverage != expected_export_coverage:
            raise VerificationError(
                "%s: required export GFID coverage differs from fixture contract" %
                self.label)
        if actual_exports != EXPECTED_EXPORTS:
            raise VerificationError("%s: exports differ from fixture contract" %
                                    self.label)
        exports_by_name = {entry["name"]: entry for entry in exports}
        for name in sorted(EXPECTED_EXPORTS):
            expected_target_rva = self.map_symbol_rva(map_symbols, name)
            actual_target_rva = exports_by_name[name]["target_rva"]
            if actual_target_rva != expected_target_rva:
                raise VerificationError(
                    "%s: export %s target RVA 0x%x does not match COFF map "
                    "symbol at RVA 0x%x" %
                    (self.label, name, actual_target_rva,
                     expected_target_rva))
        export_dll_name = self.cstring_at_rva(export_name_rva,
                                              "export DLL name")
        if (expected_export_name is not None and
                export_dll_name.lower() != expected_export_name.lower()):
            raise VerificationError(
                "%s: export DLL name is %s, expected %s" %
                (self.label, export_dll_name, expected_export_name))

        legacy_slot_rva = exports_by_name["LegacyAddressTaken"]["target_rva"]
        if legacy_slot_rva & 7:
            raise VerificationError(
                "%s: LegacyAddressTaken data slot is not 8-byte aligned" %
                self.label)
        legacy_slot_offset = self.rva_to_offset(
            legacy_slot_rva, 8, "LegacyAddressTaken data slot")
        legacy_target_va = self.u64(
            legacy_slot_offset, "LegacyAddressTaken target VA")
        if not legacy_target_va:
            raise VerificationError(
                "%s: LegacyAddressTaken contains a zero target VA" %
                self.label)
        legacy_target_rva = self.va_to_rva(
            legacy_target_va, "LegacyAddressTaken target")
        expected_legacy_target_rva = exports_by_name["LegacyTarget"]["target_rva"]
        if legacy_target_rva != expected_legacy_target_rva:
            raise VerificationError(
                "%s: LegacyAddressTaken does not point to LegacyTarget" %
                self.label)

        enclave_pointer_offset = load_offset + LOAD_CONFIG_ENCLAVE_POINTER_OFFSET
        enclave_va = self.u64(enclave_pointer_offset,
                              "EnclaveConfigurationPointer")
        if not enclave_va:
            raise VerificationError("%s: zero EnclaveConfigurationPointer" % self.label)
        enclave_rva = self.va_to_rva(enclave_va, "EnclaveConfigurationPointer")
        enclave_offset = self.rva_to_offset(enclave_rva, ENCLAVE_CONFIG_SIZE,
                                            "enclave configuration")
        enclave_size = self.u32(enclave_offset, "enclave Size")
        minimum_size = self.u32(enclave_offset + 4,
                                "enclave MinimumRequiredConfigSize")
        if enclave_size < ENCLAVE_CONFIG_SIZE or minimum_size < 8 or minimum_size > enclave_size:
            raise VerificationError("%s: invalid enclave configuration size" % self.label)
        self.rva_to_offset(enclave_rva, enclave_size,
                           "declared enclave configuration")
        policy_flags = self.u32(enclave_offset + 8, "enclave PolicyFlags")
        import_count = self.u32(enclave_offset + 12, "enclave NumberOfImports")
        import_list = self.u32(enclave_offset + 16, "enclave ImportList")
        import_entry_size = self.u32(enclave_offset + 20,
                                     "enclave ImportEntrySize")
        enclave_imports = []
        normalized_enclave_imports = set()
        if import_count:
            if not import_list or not import_entry_size:
                raise VerificationError("%s: invalid enclave import list" % self.label)
            if import_list & 3:
                raise VerificationError("%s: enclave import list is not 4-byte aligned" %
                                        self.label)
            if import_entry_size < ENCLAVE_IMPORT_SIZE:
                raise VerificationError(
                    "%s: enclave ImportEntrySize is smaller than %d" %
                    (self.label, ENCLAVE_IMPORT_SIZE))
            if import_entry_size & 3:
                raise VerificationError(
                    "%s: enclave ImportEntrySize is not 4-byte aligned" %
                    self.label)
            import_bytes = _checked_product(import_count, import_entry_size,
                                            len(self.data), "enclave import list")
            import_offset = self.rva_to_offset(import_list, import_bytes,
                                               "enclave import list")
            for index in range(import_count):
                descriptor = import_offset + index * import_entry_size
                match_type = self.u32(descriptor, "enclave import MatchType")
                minimum_security_version = self.u32(
                    descriptor + 4, "enclave import MinimumSecurityVersion")
                unique_or_author_id = self.data[descriptor + 8:descriptor + 40]
                import_family_id = self.data[descriptor + 40:descriptor + 56]
                import_image_id = self.data[descriptor + 56:descriptor + 72]
                import_name_rva = self.u32(descriptor + 72,
                                           "enclave import ImportName")
                reserved = self.u32(descriptor + 76, "enclave import Reserved")
                if match_type != 0:
                    raise VerificationError(
                        "%s: enclave import MatchType is not NONE" % self.label)
                if minimum_security_version:
                    raise VerificationError(
                        "%s: enclave import MinimumSecurityVersion is nonzero" %
                        self.label)
                if any(unique_or_author_id) or any(import_family_id) or any(import_image_id):
                    raise VerificationError(
                        "%s: enclave import identity fields are nonzero" % self.label)
                if reserved:
                    raise VerificationError("%s: enclave import Reserved is nonzero" %
                                            self.label)
                if not import_name_rva:
                    raise VerificationError(
                        "%s: enclave import has a zero ImportName RVA" % self.label)
                import_name = self.cstring_at_rva(
                    import_name_rva, "enclave import DLL name")
                normalized_name = import_name.lower()
                if normalized_name in normalized_enclave_imports:
                    raise VerificationError(
                        "%s: duplicate enclave import DLL name %r" %
                        (self.label, import_name))
                normalized_enclave_imports.add(normalized_name)
                enclave_imports.append({
                    "match_type": match_type,
                    "minimum_security_version": minimum_security_version,
                    "unique_or_author_id": unique_or_author_id.hex(),
                    "family_id": import_family_id.hex(),
                    "image_id": import_image_id.hex(),
                    "name": normalized_name,
                    "reserved": reserved,
                })
        elif import_list or import_entry_size:
            raise VerificationError("%s: zero imports have nonzero list metadata" %
                                    self.label)
        if normalized_enclave_imports != set(standard_imports):
            raise VerificationError(
                "%s: enclave imports do not exactly match live standard imports: %r" %
                (self.label, sorted(normalized_enclave_imports)))
        if normalized_enclave_imports != EXPECTED_IMPORT_NAMES:
            raise VerificationError(
                "%s: enclave imports differ from the fixture contract: %r" %
                (self.label, sorted(normalized_enclave_imports)))
        enclave_imports.sort(key=lambda item: item["name"])
        family_id = self.data[enclave_offset + 24:enclave_offset + 40]
        image_id = self.data[enclave_offset + 40:enclave_offset + 56]
        image_version = self.u32(enclave_offset + 56, "enclave ImageVersion")
        security_version = self.u32(enclave_offset + 60,
                                    "enclave SecurityVersion")
        address_space_size = self.u64(enclave_offset + 64, "enclave EnclaveSize")
        thread_count = self.u32(enclave_offset + 72, "enclave NumberOfThreads")
        enclave_flags = self.u32(enclave_offset + 76, "enclave EnclaveFlags")
        expected = (
            minimum_size == 76 and policy_flags == 1 and
            family_id == EXPECTED_FAMILY_ID and image_id == EXPECTED_IMAGE_ID and
            image_version == 0x00010000 and security_version == 1 and
            address_space_size == 0x20000000 and thread_count == 1 and
            enclave_flags == 1)
        if not expected:
            raise VerificationError("%s: enclave configuration differs from fixture contract" %
                                    self.label)

        reloc_rva, reloc_size = self.directory(BASE_RELOCATION_DIRECTORY)
        if not reloc_rva or reloc_size < 8:
            raise VerificationError("%s: missing base relocation directory" % self.label)
        reloc_offset = self.rva_to_offset(reloc_rva, reloc_size,
                                          "base relocation directory")
        relocations = []
        seen_relocation_rvas = set()
        cursor = 0
        while cursor < reloc_size:
            if reloc_size - cursor < 8:
                raise VerificationError("%s: truncated base relocation block" % self.label)
            page_rva = self.u32(reloc_offset + cursor, "base relocation PageRVA")
            block_size = self.u32(reloc_offset + cursor + 4,
                                  "base relocation BlockSize")
            if page_rva & 0xFFF or block_size < 8 or block_size & 1 or block_size > reloc_size - cursor:
                raise VerificationError("%s: malformed base relocation block" % self.label)
            for entry_offset in range(cursor + 8, cursor + block_size, 2):
                entry = self.u16(reloc_offset + entry_offset,
                                 "base relocation entry")
                reloc_type = entry >> 12
                target_rva = page_rva + (entry & 0xFFF)
                if reloc_type:
                    if target_rva >= self.size_of_image:
                        raise VerificationError("%s: relocation target exceeds image" %
                                                self.label)
                    if target_rva in seen_relocation_rvas:
                        raise VerificationError(
                            "%s: duplicate non-ABS base relocation target 0x%x" %
                            (self.label, target_rva))
                    seen_relocation_rvas.add(target_rva)
                    relocations.append((target_rva, reloc_type))
            cursor += block_size
        enclave_pointer_rva = load_rva + LOAD_CONFIG_ENCLAVE_POINTER_OFFSET
        if (enclave_pointer_rva, DIR64) not in relocations:
            raise VerificationError("%s: enclave pointer lacks a DIR64 base relocation" %
                                    self.label)
        guard_pointer_rva = load_rva + LOAD_CONFIG_GFID_TABLE_OFFSET
        if (guard_pointer_rva, DIR64) not in relocations:
            raise VerificationError(
                "%s: nonempty GFID pointer lacks a DIR64 base relocation" %
                self.label)
        for entry in guard_dispatch_slots:
            field_rva = load_rva + entry["field_offset"]
            if (field_rva, DIR64) not in relocations:
                raise VerificationError(
                    "%s: %s field lacks a DIR64 base relocation" %
                    (self.label, entry["name"]))
            if (entry["slot_rva"], DIR64) not in relocations:
                raise VerificationError(
                    "%s: %s slot lacks a DIR64 base relocation" %
                    (self.label, entry["name"]))
        giat_pointer_rva = load_rva + LOAD_CONFIG_GIAT_TABLE_OFFSET
        if giat_count and (giat_pointer_rva, DIR64) not in relocations:
            raise VerificationError(
                "%s: nonempty GIAT pointer lacks a DIR64 base relocation" %
                self.label)
        if (legacy_slot_rva, DIR64) not in relocations:
            raise VerificationError(
                "%s: LegacyAddressTaken data slot lacks a unique DIR64 base "
                "relocation" % self.label)

        return {
            "path": self.label,
            "machine": machine_name,
            "file_characteristics": characteristics,
            "dll_characteristics": dll_characteristics,
            "is_dll": True,
            "dynamic_base": True,
            "force_integrity": True,
            "guard_cf": True,
            "signature_present": bool(certificate_size),
            "standard_imports": standard_imports,
            "load_config": {
                "rva": load_rva,
                "directory_size": load_directory_size,
                "declared_size": load_declared_size,
                "guard_flags": guard_flags,
                "guard_table_rva": guard_rva,
                "guard_entry_size": guard_stride,
                "guard_function_count": guard_count,
                "guard_semantics": {
                    "cf_instrumented": True,
                    "function_table_present": True,
                    "longjmp_table_present": False,
                    "function_table_nonempty": True,
                    "gfids_strictly_sorted_unique_executable": True,
                    "check_dispatch_slots_valid": True,
                },
                "guard_check_slot_rva": guard_dispatch_slots[0]["slot_rva"],
                "guard_check_target_rva": guard_dispatch_slots[0]["target_rva"],
                "guard_dispatch_slot_rva": guard_dispatch_slots[1]["slot_rva"],
                "guard_dispatch_target_rva":
                    guard_dispatch_slots[1]["target_rva"],
                "enclave_pointer_rva": enclave_rva,
            },
            "enclave": {
                "size": enclave_size,
                "minimum_required_size": minimum_size,
                "policy_flags": policy_flags,
                "number_of_imports": import_count,
                "import_entry_size": import_entry_size,
                "imports": enclave_imports,
                "family_id": family_id.hex(),
                "image_id": image_id.hex(),
                "image_version": image_version,
                "security_version": security_version,
                "address_space_size": address_space_size,
                "number_of_threads": thread_count,
                "flags": enclave_flags,
            },
            "gfids": gfids,
            "gfid_metadata": gfid_metadata,
            "gfid_flags": [gfid_flags_by_rva[gfid] for gfid in gfids],
            "giat": {
                "table_rva": giat_table_rva,
                "entry_size": guard_stride,
                "count": giat_count,
                "entries": giats,
                "metadata": giat_flags,
                "semantics": {
                    "present": bool(giat_count),
                    "entries_strictly_sorted_unique_iat_slots": True,
                    "metadata_zero": True,
                    "pointer_dir64_relocation": True,
                },
            },
            "exports": exports,
            "export_dll_name": export_dll_name,
            "legacy_address_taken": {
                "slot_rva": legacy_slot_rva,
                "target_export": "LegacyTarget",
                "unique_dir64_relocation": True,
            },
            "enclave_pointer_dir64_relocation": True,
            "guard_dispatch_dir64_relocations": True,
            "guard_pointer_dir64_relocation": True,
            "section_count": section_count,
        }


def inspect_path(path: pathlib.Path, map_path: pathlib.Path,
                 expected_machine: Optional[str] = None) -> Dict[str, Any]:
    map_symbols = parse_coff_map_path(map_path)
    return PEImage.from_path(path).inspect(
        map_symbols, expected_machine, path.name)


def compare(reference: Dict[str, Any], candidate: Dict[str, Any]) -> None:
    # Size and ImportEntrySize describe extensible container layouts.  Each
    # image has already been bounds-checked above; compare only the known
    # enclave policy and import semantics across linker implementations.
    def enclave_semantics(image: Dict[str, Any]) -> Dict[str, Any]:
        return {key: value for key, value in image["enclave"].items()
                if key not in ("size", "import_entry_size")}

    def export_gfid_coverage(image: Dict[str, Any]) -> Dict[str, Dict[str, Any]]:
        return {entry["name"]: {
            "kind": entry["kind"],
            "ordinal": entry["ordinal"],
            "gfid_covered": entry["gfid_covered"],
        } for entry in image["exports"]}

    checks = [
        ("machine", reference["machine"], candidate["machine"]),
        ("DLL flag", reference["is_dll"], candidate["is_dll"]),
        ("DYNAMIC_BASE", reference["dynamic_base"], candidate["dynamic_base"]),
        ("FORCE_INTEGRITY", reference["force_integrity"], candidate["force_integrity"]),
        ("GUARD_CF", reference["guard_cf"], candidate["guard_cf"]),
        ("CFG safety semantics", reference["load_config"]["guard_semantics"],
         candidate["load_config"]["guard_semantics"]),
        ("GFID cardinality", len(reference["gfids"]),
         len(candidate["gfids"])),
        ("export GFID coverage", export_gfid_coverage(reference),
         export_gfid_coverage(candidate)),
        ("GIAT safety semantics", reference["giat"]["semantics"],
         candidate["giat"]["semantics"]),
        ("GIAT count", reference["giat"]["count"],
         candidate["giat"]["count"]),
        ("standard imports", reference["standard_imports"],
         candidate["standard_imports"]),
        ("enclave configuration", enclave_semantics(reference),
         enclave_semantics(candidate)),
        ("enclave pointer DIR64 relocation",
         reference["enclave_pointer_dir64_relocation"],
         candidate["enclave_pointer_dir64_relocation"]),
        ("GFID pointer DIR64 relocation",
         reference["guard_pointer_dir64_relocation"],
         candidate["guard_pointer_dir64_relocation"]),
        ("CFG check/dispatch DIR64 relocations",
         reference["guard_dispatch_dir64_relocations"],
         candidate["guard_dispatch_dir64_relocations"]),
        ("LegacyAddressTaken semantics",
         reference["legacy_address_taken"]["target_export"],
         candidate["legacy_address_taken"]["target_export"]),
        ("LegacyAddressTaken DIR64 relocation",
         reference["legacy_address_taken"]["unique_dir64_relocation"],
         candidate["legacy_address_taken"]["unique_dir64_relocation"]),
    ]
    mismatches = ["%s: reference=%r candidate=%r" % item
                  for item in checks if item[1] != item[2]]
    if mismatches:
        raise VerificationError("semantic PE mismatch:\n  " + "\n  ".join(mismatches))


def _synthetic_image() -> bytes:
    data = bytearray(0xE00)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    coff = 0x84
    struct.pack_into("<HHIIIHH", data, coff, 0x8664, 3, 0, 0, 0, 0xF0,
                     0x2022)
    optional = coff + 20
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<I", data, optional + 4, 0x200)
    struct.pack_into("<I", data, optional + 8, 0x600)
    struct.pack_into("<I", data, optional + 16, 0x1000)
    struct.pack_into("<I", data, optional + 20, 0x1000)
    struct.pack_into("<Q", data, optional + 24, 0x180000000)
    struct.pack_into("<II", data, optional + 32, 0x1000, 0x200)
    struct.pack_into("<II", data, optional + 56, 0x4000, 0x400)
    struct.pack_into("<H", data, optional + 68, 3)
    struct.pack_into("<H", data, optional + 70,
                     DYNAMIC_BASE | FORCE_INTEGRITY | GUARD_CF)
    struct.pack_into("<I", data, optional + 108, 16)
    struct.pack_into("<II", data, optional + 112 + EXPORT_DIRECTORY * 8,
                     0x2400, 0xD9)
    struct.pack_into("<II", data, optional + 112 + IMPORT_DIRECTORY * 8,
                     0x22C0, 3 * IMPORT_DESCRIPTOR_SIZE)
    struct.pack_into("<II", data, optional + 112 + LOAD_CONFIG_DIRECTORY * 8,
                     0x2000, 0x100)
    struct.pack_into("<II", data, optional + 112 + BASE_RELOCATION_DIRECTORY * 8,
                     0x3000, 24)
    struct.pack_into("<II", data, optional + 112 + IAT_DIRECTORY * 8,
                     0x2100, 16)
    sections = optional + 0xF0
    for index, values in enumerate((
            (b".text", 0x200, 0x1000, 0x200, 0x400, 0x60000020),
            (b".rdata", 0x600, 0x2000, 0x600, 0x600, 0x40000040),
            (b".reloc", 0x200, 0x3000, 0x200, 0xC00, 0x42000040))):
        offset = sections + index * 40
        name, virtual_size, rva, raw_size, raw_pointer, flags = values
        data[offset:offset + len(name)] = name
        struct.pack_into("<IIIIIIHHI", data, offset + 8, virtual_size, rva,
                         raw_size, raw_pointer, 0, 0, 0, 0, flags)
    load = 0x600
    struct.pack_into("<I", data, load, 0x100)
    struct.pack_into("<QQ", data,
                     load + LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET,
                     0x180002100, 0x180002108)
    struct.pack_into("<Q", data, load + 0x80, 0x1800022A0)
    struct.pack_into("<Q", data, load + 0x88, 2)
    struct.pack_into("<I", data, load + 0x90,
                     CF_INSTRUMENTED | CF_FUNCTION_TABLE_PRESENT |
                     CF_FUNCTION_TABLE_SIZE_5BYTES)
    struct.pack_into("<QQ", data, load + LOAD_CONFIG_GIAT_TABLE_OFFSET,
                     0x1800025C0, 2)
    struct.pack_into("<Q", data, load + 0xF8, 0x180002200)
    struct.pack_into("<QQ", data, 0x700, 0x180001060, 0x180001070)
    enclave = 0x800
    struct.pack_into("<IIIIII", data, enclave, 80, 76, 1, 2, 0x2300,
                     ENCLAVE_IMPORT_SIZE)
    data[enclave + 24:enclave + 40] = EXPECTED_FAMILY_ID
    data[enclave + 40:enclave + 56] = EXPECTED_IMAGE_ID
    struct.pack_into("<IIQII", data, enclave + 56, 0x10000, 1,
                     0x20000000, 1, 1)
    for offset, ordinal in ((0x850, 1), (0x860, 1),
                            (0x870, 2), (0x880, 2)):
        struct.pack_into("<QQ", data, offset,
                         0x8000000000000000 | ordinal, 0)
    struct.pack_into("<IBIB", data, 0x8A0, 0x1000, 0, 0x1030, 0)
    struct.pack_into("<IIIII", data, 0x8C0,
                     0x2250, 0, 0, 0x23A0, 0x2260)
    struct.pack_into("<IIIII", data, 0x8D4,
                     0x2270, 0, 0, 0x23C0, 0x2280)
    struct.pack_into("<I", data, 0x900 + 72, 0x23A0)
    struct.pack_into("<I", data, 0x950 + 72, 0x23C0)
    data[0x9A0:0x9B5] = b"ucrtbase_enclave.dll\0"
    data[0x9C0:0x9CC] = b"vertdll.dll\0"
    struct.pack_into("<IBIB", data, 0xBC0, 0x2100, 0, 0x2108, 0)
    export = 0xA00
    export_names = (
        ("GuardedExercise", 0x1020),
        ("GuardedIndirectCall", 0x1010),
        ("GuardedTarget", 0x1000),
        ("LegacyAddressTaken", 0x25E0),
        ("LegacyExercise", 0x1040),
        ("LegacyTarget", 0x1030),
    )
    struct.pack_into("<IIHHIIIIIII", data, export, 0, 0, 0, 0, 0x2468, 1,
                     len(export_names), len(export_names), 0x2428, 0x2440,
                     0x2458)
    string_offset = 0xA78
    data[0xA68:0xA74] = b"fixture.dll\0"
    for index, (name, target_rva) in enumerate(export_names):
        encoded_name = name.encode("ascii") + b"\0"
        struct.pack_into("<I", data, 0xA28 + index * 4, target_rva)
        struct.pack_into("<I", data, 0xA40 + index * 4,
                         0x2400 + string_offset - export)
        struct.pack_into("<H", data, 0xA58 + index * 2, index)
        data[string_offset:string_offset + len(encoded_name)] = encoded_name
        string_offset += len(encoded_name)
    struct.pack_into("<Q", data, 0xBE0, 0x180001030)
    struct.pack_into("<IIHHHHHHHH", data, 0xC00, 0x2000, 24,
                     (DIR64 << 12) |
                     LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET,
                     (DIR64 << 12) |
                     LOAD_CONFIG_GUARD_DISPATCH_POINTER_OFFSET,
                     (DIR64 << 12) | LOAD_CONFIG_GFID_TABLE_OFFSET,
                     (DIR64 << 12) | LOAD_CONFIG_GIAT_TABLE_OFFSET,
                     (DIR64 << 12) | LOAD_CONFIG_ENCLAVE_POINTER_OFFSET,
                     (DIR64 << 12) | 0x100, (DIR64 << 12) | 0x108,
                     (DIR64 << 12) | 0x5E0)
    return bytes(data)


def _synthetic_map_text() -> str:
    image_base = 0x180000000
    symbol_rvas = {
        "GuardedExercise": 0x1020,
        "GuardedIndirectCall": 0x1010,
        "GuardedTarget": 0x1000,
        "LegacyAddressTaken": 0x25E0,
        "LegacyExercise": 0x1040,
        "LegacyTarget": 0x1030,
        "__guard_check_icall_fptr": 0x2100,
        "__guard_dispatch_icall_fptr": 0x2108,
        "_guard_check_icall_nop": 0x1060,
        "_guard_dispatch_icall_nop": 0x1070,
    }
    lines = [
        " fixture",
        "",
        "  Address         Publics by Value              Rva+Base",
        "",
    ]
    for name, rva in sorted(symbol_rvas.items()):
        section = 2 if rva >= 0x2000 else 1
        section_offset = rva - (0x2000 if section == 2 else 0x1000)
        suffix = " f   fixture.obj" if section == 1 else "     <linker-defined>"
        lines.append(
            " %04x:%08x       %-26s %016x%s" %
            (section, section_offset, name, image_base + rva, suffix))
    return "\n".join(lines) + "\n"


def self_test() -> None:
    valid = _synthetic_image()
    map_text = _synthetic_map_text()
    map_symbols = parse_coff_map_text(map_text, "synthetic-valid.map")
    reference = PEImage(valid, "synthetic-valid").inspect(
        map_symbols,
        expected_machine="x86_64", expected_export_name="fixture.dll")
    cases: List[Tuple[str, bytes, str]] = []
    failures = []

    map_cases = [
        ("missing COFF map symbols", "",
         "COFF map is missing required symbols"),
        ("ambiguous COFF map symbol",
         map_text +
         " 0001:00000018       GuardedTarget              "
         "0000000180001018 f   other.obj\n",
         "COFF map symbol GuardedTarget has ambiguous VAs"),
    ]
    for name, mutated_map, expected_error in map_cases:
        try:
            parse_coff_map_text(mutated_map, "self-test/%s" % name)
            failures.append("%s (unexpectedly passed)" % name)
        except VerificationError as error:
            if expected_error not in str(error):
                failures.append("%s (expected %r, got %r)" %
                                (name, expected_error, str(error)))

    try:
        PEImage(valid, "self-test/wrong expected machine").inspect(
            map_symbols, "arm64")
        failures.append("wrong expected machine (unexpectedly passed)")
    except VerificationError as error:
        if "machine is x86_64, expected arm64" not in str(error):
            failures.append("wrong expected machine (wrong diagnostic: %r)" %
                            str(error))

    wrong_export_name = bytearray(valid)
    wrong_export_name[0xA68] = ord("x")
    try:
        PEImage(bytes(wrong_export_name),
                "self-test/wrong export DLL name").inspect(
                    map_symbols, expected_export_name="fixture.dll")
        failures.append("wrong export DLL name (unexpectedly passed)")
    except VerificationError as error:
        if "export DLL name is xixture.dll, expected fixture.dll" not in str(error):
            failures.append("wrong export DLL name (wrong diagnostic: %r)" %
                            str(error))

    def mutate(name: str, expected_error: str, changes: List[Tuple[int, str, int]],
               byte_changes: Optional[List[Tuple[int, bytes]]] = None) -> None:
        mutated = bytearray(valid)
        for offset, fmt, value in changes:
            struct.pack_into(fmt, mutated, offset, value)
        for offset, value in byte_changes or []:
            mutated[offset:offset + len(value)] = value
        cases.append((name, bytes(mutated), expected_error))

    def add(name: str, offset: int, fmt: str, value: int,
            expected_error: str) -> None:
        mutate(name, expected_error, [(offset, fmt, value)])

    optional = 0x98
    add("bad DOS signature", 0, "<H", 0, "missing MZ signature")
    cases.append(("truncated PE", valid[:0x90], "truncated or out-of-bounds"))
    add("missing DLL", 0x84 + 18, "<H", 0x22, "image is not a DLL")
    add("missing executable-image flag", 0x84 + 18, "<H", 0x2020,
        "image is not marked executable")
    add("relocations marked stripped", 0x84 + 18, "<H", 0x2023,
        "image marks base relocations stripped")
    add("missing DYNAMIC_BASE", optional + 70, "<H", FORCE_INTEGRITY | GUARD_CF,
        "missing DYNAMIC_BASE")
    add("missing FORCE_INTEGRITY", optional + 70, "<H", DYNAMIC_BASE | GUARD_CF,
        "missing FORCE_INTEGRITY")
    add("missing GUARD_CF", optional + 70, "<H", DYNAMIC_BASE | FORCE_INTEGRITY,
        "missing GUARD_CF")

    add("missing standard imports", optional + 112 + IMPORT_DIRECTORY * 8,
        "<I", 0, "missing or short standard import directory")
    add("misaligned standard import directory",
        optional + 112 + IMPORT_DIRECTORY * 8 + 4, "<I", 59,
        "misaligned standard import directory size")
    add("unterminated standard import directory",
        optional + 112 + IMPORT_DIRECTORY * 8 + 4, "<I", 40,
        "unterminated standard import directory")
    mutate("data after standard import terminator",
           "nonzero data follows the standard import terminator",
           [(optional + 112 + IMPORT_DIRECTORY * 8 + 4, "<I", 80)],
           [(0x8FC, b"x")])
    add("zero standard import name", 0x8C0 + 12, "<I", 0,
        "standard import has a zero Name RVA")
    add("zero standard import IAT", 0x8C0 + 16, "<I", 0,
        "standard import has a zero FirstThunk RVA")
    add("standard import name outside raw section", 0x8C0 + 12, "<I", 0x2600,
        "standard import DLL name RVA is not backed by initialized raw section data")
    mutate("unterminated standard import name",
           "unterminated standard import DLL name",
           [(0x8C0 + 12, "<I", 0x25FF)], [(0xBFF, b"x")])
    mutate("non-printable standard import name",
           "non-printable ASCII standard import DLL name", [],
           [(0x9A0, b"\x1f")])
    add("duplicate standard import", 0x8D4 + 12, "<I", 0x23A0,
        "duplicate standard import DLL name")
    mutate("unexpected standard import", "standard imports differ from the fixture contract",
           [(0x8D4 + 12, "<I", 0x2550)], [(0xB50, b"other.dll\0")])

    add("missing export directory", optional + 112 + EXPORT_DIRECTORY * 8,
        "<I", 0, "missing or short export directory")
    add("nonzero export Characteristics", 0xA00, "<I", 1,
        "export Characteristics is nonzero")
    add("wrong export base", 0xA00 + 16, "<I", 2,
        "export ordinal Base is not 1")
    add("unnamed export ordinal", 0xA00 + 20, "<I", 7,
        "export function/name counts differ from fixture contract")
    add("export ordinal out of range", 0xA58, "<H", 6,
        "export name ordinal is out of range")
    add("aliased export ordinal", 0xA58 + 2, "<H", 0,
        "export names alias the same ordinal")
    add("aliased export target", 0xA28 + 4, "<I", 0x1020,
        "exports alias one target RVA")
    mutate("missing required export",
           "required export GFID coverage differs from fixture contract", [],
           [(0xA78, b"A")])
    mutate("swapped export ordinals", "exports differ from fixture contract",
           [(0xA58, "<H", 1), (0xA5A, "<H", 0)])
    mutate("swapped same-semantics export targets",
           "export GuardedExercise target RVA 0x1010 does not match COFF map",
           [(0xA28, "<I", 0x1010), (0xA2C, "<I", 0x1020)])
    mutate("unsorted export name table",
           "export name pointer table is not strictly lexical",
           [(0xA40, "<I", 0x2488), (0xA44, "<I", 0x2478),
            (0xA58, "<H", 1), (0xA5A, "<H", 0)])
    mutate("non-printable export name", "non-printable ASCII export name", [],
           [(0xA78, b"\x1f")])
    mutate("GFID and export in executable virtual tail",
           "GFID 0x1250 is not executable",
           [(0x188 + 8, "<I", 0x300), (0x8A0, "<I", 0x1250),
            (0xA30, "<I", 0x1250)])
    mutate("data export in virtual-only section tail",
           "export target 0x2650 is not backed by initialized image data",
           [(0x1B0 + 8, "<I", 0x700), (0xA34, "<I", 0x2650)])

    add("missing load config", optional + 112 + LOAD_CONFIG_DIRECTORY * 8,
        "<I", 0, "missing or short load-config directory")
    add("short load-config directory",
        optional + 112 + LOAD_CONFIG_DIRECTORY * 8 + 4, "<I", 0xF8,
        "missing or short load-config directory")
    add("short load config", 0x600, "<I", 0xF8,
        "load-config Size does not cover enclave pointer")
    add("oversized load config", 0x600, "<I", 0x108,
        "load-config Size exceeds its directory")
    add("zero enclave pointer", 0x600 + 0xF8, "<Q", 0,
        "zero EnclaveConfigurationPointer")
    add("wrong enclave pointer", 0x600 + 0xF8, "<Q", 0x180005000,
        "enclave configuration extends past SizeOfImage")
    add("short enclave config", 0x800, "<I", 76,
        "invalid enclave configuration size")
    add("wrong minimum config", 0x800 + 4, "<I", 8,
        "enclave configuration differs from fixture contract")
    add("non-debuggable enclave", 0x800 + 8, "<I", 0,
        "enclave configuration differs from fixture contract")
    add("zero enclave import count", 0x800 + 12, "<I", 0,
        "zero imports have nonzero list metadata")
    add("missing enclave import list", 0x800 + 16, "<I", 0,
        "invalid enclave import list")
    add("misaligned enclave import list", 0x800 + 16, "<I", 0x2302,
        "enclave import list is not 4-byte aligned")
    add("short enclave import entry", 0x800 + 20, "<I", 79,
        "enclave ImportEntrySize is smaller than 80")
    add("misaligned enclave import entry", 0x800 + 20, "<I", 81,
        "enclave ImportEntrySize is not 4-byte aligned")
    add("oversized enclave import count", 0x800 + 12, "<I", 0xFFFFFFFF,
        "enclave import list size overflows the image")
    add("non-NONE enclave import", 0x900, "<I", 1,
        "enclave import MatchType is not NONE")
    add("enclave import minimum security", 0x900 + 4, "<I", 1,
        "enclave import MinimumSecurityVersion is nonzero")
    add("enclave import identity", 0x900 + 8, "<I", 1,
        "enclave import identity fields are nonzero")
    add("zero enclave import name", 0x900 + 72, "<I", 0,
        "enclave import has a zero ImportName RVA")
    add("enclave import name outside raw section", 0x900 + 72, "<I", 0x2600,
        "enclave import DLL name RVA is not backed by initialized raw section data")
    mutate("unterminated enclave import name", "unterminated enclave import DLL name",
           [(0x900 + 72, "<I", 0x25FF)], [(0xBFF, b"x")])
    add("nonzero enclave import reserved", 0x900 + 76, "<I", 1,
        "enclave import Reserved is nonzero")
    add("duplicate enclave import", 0x950 + 72, "<I", 0x23A0,
        "duplicate enclave import DLL name")
    mutate("enclave import mismatch", "do not exactly match live standard imports",
           [(0x950 + 72, "<I", 0x2550)], [(0xB50, b"other.dll\0")])
    add("wrong family ID", 0x800 + 24, "<Q", 0,
        "enclave configuration differs from fixture contract")
    add("wrong image ID", 0x800 + 40, "<Q", 0,
        "enclave configuration differs from fixture contract")
    add("wrong image version", 0x800 + 56, "<I", 2,
        "enclave configuration differs from fixture contract")
    add("wrong security version", 0x800 + 60, "<I", 2,
        "enclave configuration differs from fixture contract")
    add("wrong enclave size", 0x800 + 64, "<Q", 0x10000000,
        "enclave configuration differs from fixture contract")
    add("wrong thread count", 0x800 + 72, "<I", 2,
        "enclave configuration differs from fixture contract")
    add("missing primary-image flag", 0x800 + 76, "<I", 0,
        "enclave configuration differs from fixture contract")

    add("zero GuardCF check pointer",
        0x600 + LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET, "<Q", 0,
        "zero GuardCFCheckFunctionPointer")
    add("zero GuardCF dispatch pointer",
        0x600 + LOAD_CONFIG_GUARD_DISPATCH_POINTER_OFFSET, "<Q", 0,
        "zero GuardCFDispatchFunctionPointer")
    add("GuardCF check pointer outside image",
        0x600 + LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET,
        "<Q", 0x180005000,
        "GuardCFCheckFunctionPointer slot extends past SizeOfImage")
    add("misaligned GuardCF check slot",
        0x600 + LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET,
        "<Q", 0x180002101,
        "GuardCFCheckFunctionPointer slot is not 8-byte aligned")
    add("aliased GuardCF check and dispatch slots",
        0x600 + LOAD_CONFIG_GUARD_DISPATCH_POINTER_OFFSET,
        "<Q", 0x180002100,
        "GuardCF check and dispatch pointers alias one slot")
    add("zero GuardCF check target", 0x700, "<Q", 0,
        "zero GuardCFCheckFunctionPointer target")
    add("nonexecutable GuardCF check target", 0x700, "<Q", 0x180002000,
        "GuardCFCheckFunctionPointer target is not initialized executable image data")
    mutate("GuardCF check target in executable virtual tail",
           "GuardCFCheckFunctionPointer target is not initialized executable image data",
           [(0x188 + 8, "<I", 0x300), (0x700, "<Q", 0x180001250)])
    add("writable GuardCF slots", 0x1B0 + 36, "<I", 0xC0000040,
        "GuardCFCheckFunctionPointer slot is not in initialized read-only memory")
    mutate("swapped GuardCF slot fields",
           "GuardCFCheckFunctionPointer slot RVA 0x2108 does not match COFF map",
           [(0x600 + LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET,
             "<Q", 0x180002108),
            (0x600 + LOAD_CONFIG_GUARD_DISPATCH_POINTER_OFFSET,
             "<Q", 0x180002100)])
    mutate("swapped GuardCF slot targets",
           "GuardCFCheckFunctionPointer target RVA 0x1070 does not match COFF map",
           [(0x700, "<Q", 0x180001070),
            (0x708, "<Q", 0x180001060)])
    add("arbitrary executable GuardCF check target", 0x700, "<Q",
        0x180001010,
        "GuardCFCheckFunctionPointer target RVA 0x1010 does not match COFF map")

    add("zero LegacyAddressTaken target", 0xBE0, "<Q", 0,
        "LegacyAddressTaken contains a zero target VA")
    add("wrong LegacyAddressTaken target", 0xBE0, "<Q", 0x180001000,
        "LegacyAddressTaken does not point to LegacyTarget")

    add("zero guard table", 0x600 + 0x80, "<Q", 0,
        "empty CFG function table")
    add("wrong guard table", 0x600 + 0x80, "<Q", 0x180005000,
        "CFG function table extends past SizeOfImage")
    add("zero guard count", 0x600 + 0x88, "<Q", 0,
        "empty CFG function table")
    add("oversized guard count", 0x600 + 0x88, "<Q", 0xFFFFFFFFFFFFFFFF,
        "CFG function table size overflows the image")
    add("missing GuardFlags", 0x600 + 0x90, "<I", 0,
        "GuardFlags lack CFG instrumentation/table bits")
    add("four-byte GFID stride", 0x600 + 0x90, "<I", 0x500,
        "/GUARD:MIXED requires a 5-byte GFID stride, got 4")
    add("unsupported GFID stride", 0x600 + 0x90, "<I", 0x20000500,
        "/GUARD:MIXED requires a 5-byte GFID stride, got 6")
    add("implicit longjmp metadata", 0x600 + 0x90, "<I",
        CF_INSTRUMENTED | CF_FUNCTION_TABLE_PRESENT | CF_LONGJUMP_TABLE_PRESENT,
        "/GUARD:MIXED unexpectedly enabled longjmp metadata")
    add("implicit EH continuation metadata", 0x600 + 0x90, "<I",
        (CF_INSTRUMENTED | CF_FUNCTION_TABLE_PRESENT |
         CF_FUNCTION_TABLE_SIZE_5BYTES | CF_EH_CONTINUATION_TABLE_PRESENT),
        "/GUARD:MIXED unexpectedly enabled EH continuation metadata")
    add("invalid GFID", 0x8A0, "<I", 0x2000,
        "GFID 0x2000 is not executable")
    mutate("undefined GFID flags", "GFID entry has undefined flags",
           [(0x600 + 0x90, "<I", 0x10000500)],
           [(0x8A0, struct.pack("<IBIB", 0x1000, 0x10, 0x1030, 0))])
    add("language-exception-handler GFID", 0x8A4, "<B",
        GFID_FID_LANGEXCPTHANDLER,
        "GFID entry unexpectedly marks a language exception handler")
    add("XFG GFID", 0x8A9, "<B", GFID_FID_XFG,
        "GFID entry unexpectedly marks an XFG target")
    add("unsorted GFIDs", 0x8A5, "<I", 0x1000,
        "GFID table is not strictly sorted")
    mutate("suppressed required export GFID",
           "required export GFID coverage differs from fixture contract",
           [(0x600 + 0x90, "<I", 0x10000500)],
           [(0x8A0, struct.pack("<IBIB", 0x1000, GFID_FID_SUPPRESSED,
                               0x1030, 0)),
            (0xBC0, struct.pack("<IBIB", 0x2100, 0, 0x2108, 0))])
    mutate("export-suppressed GFID without GuardFlags info",
           "export-suppressed GFID lacks GuardFlags suppression info",
           [(0x600 + 0x90, "<I", 0x10000500)],
           [(0x8A0, struct.pack("<IBIB", 0x1000, GFID_EXPORT_SUPPRESSED,
                               0x1030, 0))])
    mutate("export-suppressed required export GFID",
           "required export GFID coverage differs from fixture contract",
           [(0x600 + 0x90, "<I",
             0x10000500 | CF_EXPORT_SUPPRESSION_INFO_PRESENT)],
           [(0x8A0, struct.pack("<IBIB", 0x1000, GFID_EXPORT_SUPPRESSED,
                               0x1030, 0)),
            (0xBC0, struct.pack("<IBIB", 0x2100, 0, 0x2108, 0))])
    add("deleted required export GFID", 0x600 + 0x88, "<Q", 1,
        "required export GFID coverage differs from fixture contract")
    add("replaced required export GFID", 0x8A5, "<I", 0x1050,
        "required export GFID coverage differs from fixture contract")
    add("GIAT pointer without count",
        0x600 + LOAD_CONFIG_GIAT_COUNT_OFFSET, "<Q", 0,
        "GIAT table pointer and count are inconsistent")
    add("GIAT count without pointer",
        0x600 + LOAD_CONFIG_GIAT_TABLE_OFFSET, "<Q", 0,
        "GIAT table pointer and count are inconsistent")
    add("GIAT table outside image",
        0x600 + LOAD_CONFIG_GIAT_TABLE_OFFSET, "<Q", 0x180005000,
        "GIAT extends past SizeOfImage")
    add("oversized GIAT count", 0x600 + LOAD_CONFIG_GIAT_COUNT_OFFSET,
        "<Q", 0xFFFFFFFFFFFFFFFF, "GIAT size overflows the image")
    add("missing IAT for nonempty GIAT",
        optional + 112 + IAT_DIRECTORY * 8, "<I", 0,
        "nonempty GIAT lacks an IAT data directory")
    add("misaligned PE32+ IAT",
        optional + 112 + IAT_DIRECTORY * 8, "<I", 0x2104,
        "PE32+ IAT directory is not 8-byte aligned")
    add("GIAT entry outside IAT", 0xBC0, "<I", 0x2200,
        "GIAT entry 0x2200 is not an aligned PE32+ IAT slot")
    add("misaligned GIAT entry", 0xBC0, "<I", 0x2104,
        "GIAT entry 0x2104 is not an aligned PE32+ IAT slot")
    add("unsorted GIAT", 0xBC5, "<I", 0x2100,
        "GIAT is not strictly sorted")
    add("nonzero GIAT metadata", 0xBC4, "<B", 1,
        "GIAT entry has nonzero metadata")

    add("section raw data out of bounds", 0x188 + 20, "<I", 0xE00,
        "truncated or out-of-bounds section .text raw data")
    add("missing relocations", optional + 112 + BASE_RELOCATION_DIRECTORY * 8,
        "<I", 0, "missing base relocation directory")
    add("invalid relocation block", 0xC04, "<I", 10,
        "malformed base relocation block")
    add("missing GuardCF check field relocation", 0xC08, "<H", 0,
        "GuardCFCheckFunctionPointer field lacks a DIR64 base relocation")
    add("wrong GuardCF check field relocation", 0xC08, "<H",
        (DIR64 << 12) | 0x88,
        "GuardCFCheckFunctionPointer field lacks a DIR64 base relocation")
    add("missing GuardCF dispatch field relocation", 0xC0A, "<H", 0,
        "GuardCFDispatchFunctionPointer field lacks a DIR64 base relocation")
    add("wrong GuardCF dispatch field relocation", 0xC0A, "<H",
        (DIR64 << 12) | 0x90,
        "GuardCFDispatchFunctionPointer field lacks a DIR64 base relocation")
    add("missing GFID pointer relocation", 0xC0C, "<H", 0,
        "nonempty GFID pointer lacks a DIR64 base relocation")
    add("wrong GFID pointer relocation", 0xC0C, "<H",
        (DIR64 << 12) | 0x88,
        "nonempty GFID pointer lacks a DIR64 base relocation")
    add("missing GIAT pointer relocation", 0xC0E, "<H", 0,
        "nonempty GIAT pointer lacks a DIR64 base relocation")
    add("wrong GIAT pointer relocation", 0xC0E, "<H",
        (DIR64 << 12) | LOAD_CONFIG_GIAT_COUNT_OFFSET,
        "nonempty GIAT pointer lacks a DIR64 base relocation")
    add("wrong enclave relocation type", 0xC10, "<H", (3 << 12) | 0xF8,
        "enclave pointer lacks a DIR64 base relocation")
    add("missing GuardCF check slot relocation", 0xC12, "<H", 0,
        "GuardCFCheckFunctionPointer slot lacks a DIR64 base relocation")
    add("missing GuardCF dispatch slot relocation", 0xC14, "<H", 0,
        "GuardCFDispatchFunctionPointer slot lacks a DIR64 base relocation")
    add("missing LegacyAddressTaken relocation", 0xC16, "<H", 0,
        "LegacyAddressTaken data slot lacks a unique DIR64 base relocation")
    add("duplicate non-ABS relocation", 0xC16, "<H",
        (DIR64 << 12) | LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET,
        "duplicate non-ABS base relocation target 0x2070")
    for name, image, expected_error in cases:
        try:
            PEImage(image, "self-test/%s" % name).inspect(map_symbols)
            failures.append("%s (unexpectedly passed)" % name)
        except VerificationError as error:
            if expected_error not in str(error):
                failures.append("%s (expected %r, got %r)" %
                                (name, expected_error, str(error)))

    semantic_variant = bytearray(valid)
    struct.pack_into("<II", semantic_variant,
                     optional + 112 + LOAD_CONFIG_DIRECTORY * 8,
                     0x2000, 0x108)
    struct.pack_into("<I", semantic_variant, 0x600, 0x108)
    struct.pack_into("<Q", semantic_variant, 0x600 + 0x80, 0x180002580)
    struct.pack_into("<Q", semantic_variant, 0x600 + 0x88, 2)
    # The delay-load bits are permitted representation details.  The fixture
    # contract requires only CFG instrumentation/table, mixed stride, and no
    # longjmp metadata.
    struct.pack_into("<I", semantic_variant, 0x600 + 0x90, 0x10003500)
    struct.pack_into("<IBIB", semantic_variant, 0xB80,
                     0x1000, 0, 0x1030, 0)
    struct.pack_into("<IBIB", semantic_variant, 0xBC0,
                     0x2100, 0, 0x2108, 0)

    first_descriptor = bytes(semantic_variant[0x900:0x950])
    second_descriptor = bytes(semantic_variant[0x950:0x9A0])
    semantic_variant[0x900:0x9D0] = bytes(0xD0)
    semantic_variant[0x904:0x954] = first_descriptor
    semantic_variant[0x958:0x9A8] = second_descriptor
    struct.pack_into("<II", semantic_variant, 0x800 + 16, 0x2304, 84)
    struct.pack_into("<I", semantic_variant, 0x8C0 + 12, 0x2500)
    struct.pack_into("<I", semantic_variant, 0x8D4 + 12, 0x2520)
    # Descriptor order and the extensible entry stride are representation
    # details; both names still have to match the live import set exactly.
    struct.pack_into("<I", semantic_variant, 0x904 + 72, 0x2520)
    struct.pack_into("<I", semantic_variant, 0x958 + 72, 0x2500)
    semantic_variant[0xB00:0xB15] = b"UCRTBASE_ENCLAVE.DLL\0"
    semantic_variant[0xB20:0xB2C] = b"vertdll.dll\0"

    variant = PEImage(bytes(semantic_variant),
                      "synthetic-semantic-variant").inspect(map_symbols)
    reference_shape = (
        reference["load_config"]["directory_size"],
        reference["load_config"]["declared_size"],
        reference["load_config"]["guard_flags"],
        reference["load_config"]["guard_entry_size"],
        reference["load_config"]["guard_function_count"],
        reference["load_config"]["guard_table_rva"],
        reference["enclave"]["import_entry_size"],
    )
    variant_shape = (
        variant["load_config"]["directory_size"],
        variant["load_config"]["declared_size"],
        variant["load_config"]["guard_flags"],
        variant["load_config"]["guard_entry_size"],
        variant["load_config"]["guard_function_count"],
        variant["load_config"]["guard_table_rva"],
        variant["enclave"]["import_entry_size"],
    )
    if reference_shape != (0x100, 0x100, 0x10000500, 5, 2, 0x22A0, 80):
        raise VerificationError("self-test reference CFG shape is incorrect")
    if variant_shape != (0x108, 0x108, 0x10003500, 5, 2, 0x2580, 84):
        raise VerificationError("self-test did not construct distinct legal layouts")
    compare(reference, variant)

    relocated = bytearray(valid)
    struct.pack_into("<I", relocated, 0x8A0, 0x1028)
    struct.pack_into("<I", relocated, 0xA30, 0x1028)
    relocated_map_symbols = dict(map_symbols)
    relocated_map_symbols["GuardedTarget"] = 0x180001028
    compare(reference, PEImage(bytes(relocated),
                               "synthetic-relocated-target").inspect(
                                   relocated_map_symbols))

    compare_cases = []
    changed_coverage = json.loads(json.dumps(reference))
    changed_coverage["exports"][0]["gfid_covered"] = True
    compare_cases.append(("export coverage change", changed_coverage,
                          "export GFID coverage"))
    changed_imports = json.loads(json.dumps(reference))
    changed_imports["standard_imports"].append("other.dll")
    compare_cases.append(("standard import change", changed_imports,
                          "standard imports"))
    changed_enclave = json.loads(json.dumps(reference))
    changed_enclave["enclave"]["policy_flags"] = 0
    compare_cases.append(("enclave policy change", changed_enclave,
                          "enclave configuration"))
    changed_cfg = json.loads(json.dumps(reference))
    changed_cfg["load_config"]["guard_semantics"]["longjmp_table_present"] = True
    compare_cases.append(("CFG semantic change", changed_cfg,
                          "CFG safety semantics"))
    changed_giat = json.loads(json.dumps(reference))
    changed_giat["giat"]["semantics"]["present"] = False
    compare_cases.append(("GIAT semantic change", changed_giat,
                          "GIAT safety semantics"))

    extra_gfid = bytearray(valid)
    struct.pack_into("<Q", extra_gfid, 0x600 + 0x88, 3)
    struct.pack_into("<IBIBIB", extra_gfid, 0x8A0,
                     0x1000, 0, 0x1018, GFID_FID_SUPPRESSED, 0x1030, 0)
    compare_cases.append((
        "extra valid GFID",
        PEImage(bytes(extra_gfid), "self-test/extra valid GFID").inspect(
            map_symbols),
        "GFID cardinality"))
    fewer_giats = bytearray(valid)
    struct.pack_into("<Q", fewer_giats,
                     0x600 + LOAD_CONFIG_GIAT_COUNT_OFFSET, 1)
    compare_cases.append((
        "fewer valid GIAT entries",
        PEImage(bytes(fewer_giats),
                "self-test/fewer valid GIAT entries").inspect(map_symbols),
        "GIAT count"))

    for name, candidate, expected_error in compare_cases:
        try:
            compare(reference, candidate)
            failures.append("%s (unexpectedly compared equal)" % name)
        except VerificationError as error:
            if expected_error not in str(error):
                failures.append("%s (expected %r, got %r)" %
                                (name, expected_error, str(error)))

    if failures:
        raise VerificationError("self-test mutation failures:\n  " +
                                "\n  ".join(failures))
    print("PASS: VBS enclave PE verifier self-test (%d negative mutations; "
          "representation-independent 5-byte comparison)" %
          (2 + len(map_cases) + len(cases) + len(compare_cases)))


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    inspect_parser = subparsers.add_parser("inspect")
    inspect_parser.add_argument("image", type=pathlib.Path)
    inspect_parser.add_argument("--map", dest="map_path", type=pathlib.Path,
                                required=True)
    inspect_parser.add_argument("--machine", required=True,
                                choices=("x86_64", "arm64"))
    inspect_parser.add_argument("--json", dest="json_path", type=pathlib.Path)
    compare_parser = subparsers.add_parser("compare")
    compare_parser.add_argument("reference", type=pathlib.Path)
    compare_parser.add_argument("candidate", type=pathlib.Path)
    compare_parser.add_argument("--reference-map", type=pathlib.Path,
                                required=True)
    compare_parser.add_argument("--candidate-map", type=pathlib.Path,
                                required=True)
    subparsers.add_parser("self-test")
    args = parser.parse_args(argv)
    try:
        if args.command == "self-test":
            self_test()
        elif args.command == "inspect":
            result = inspect_path(args.image, args.map_path, args.machine)
            encoded = json.dumps(result, indent=2, sort_keys=True)
            print(encoded)
            if args.json_path:
                args.json_path.parent.mkdir(parents=True, exist_ok=True)
                args.json_path.write_text(encoded + "\n", encoding="utf-8")
        else:
            reference = inspect_path(args.reference, args.reference_map)
            candidate = inspect_path(args.candidate, args.candidate_map)
            compare(reference, candidate)
            print("PASS: %s semantically matches %s" %
                  (args.candidate, args.reference))
    except (OSError, VerificationError) as error:
        print("FAIL: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
