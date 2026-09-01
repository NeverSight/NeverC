#!/usr/bin/env python3
"""Strict semantic verifier for the VBS enclave differential CI fixture."""

import argparse
import json
import pathlib
import struct
import sys
from typing import Any, Dict, List, Optional, Tuple


class VerificationError(Exception):
    pass


DLL = 0x2000
DYNAMIC_BASE = 0x0040
FORCE_INTEGRITY = 0x0080
GUARD_CF = 0x4000
CF_INSTRUMENTED = 0x0100
CF_FUNCTION_TABLE_PRESENT = 0x0400
PROTECT_DELAYLOAD_IAT = 0x1000
DELAYLOAD_IAT_IN_ITS_OWN_SECTION = 0x2000
CF_LONGJUMP_TABLE_PRESENT = 0x10000
CF_FUNCTION_TABLE_SIZE_MASK = 0xF0000000
CF_FUNCTION_TABLE_SIZE_5BYTES = 0x10000000
EXPECTED_GUARD_FLAGS = (
    CF_INSTRUMENTED | CF_FUNCTION_TABLE_PRESENT | PROTECT_DELAYLOAD_IAT |
    DELAYLOAD_IAT_IN_ITS_OWN_SECTION | CF_FUNCTION_TABLE_SIZE_5BYTES)
DIR64 = 10
EXPORT_DIRECTORY = 0
LOAD_CONFIG_DIRECTORY = 10
BASE_RELOCATION_DIRECTORY = 5
CERTIFICATE_DIRECTORY = 4
IAT_DIRECTORY = 12
FID_SUPPRESSED = 0x01
LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET = 0x70
LOAD_CONFIG_GUARD_DISPATCH_POINTER_OFFSET = 0x78
LOAD_CONFIG_GFID_TABLE_OFFSET = 0x80
LOAD_CONFIG_GIAT_TABLE_OFFSET = 0xA0
LOAD_CONFIG_GIAT_COUNT_OFFSET = 0xA8
LOAD_CONFIG_ENCLAVE_POINTER_OFFSET = 0xF8
LOAD_CONFIG_REQUIRED_SIZE = 0x100
ENCLAVE_CONFIG_SIZE = 80
ENCLAVE_IMPORT_SIZE = 80
EXPECTED_FAMILY_ID = bytes.fromhex("912d7418b6534c2a8ea45739c106fd22")
EXPECTED_IMAGE_ID = bytes.fromhex("37a8c5406fd149bb9a0ee31572bc489d")
EXPECTED_ENCLAVE_IMPORT_NAMES = (
    "ucrtbase_enclave.dll",
    "vertdll.dll",
)
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


def _checked_product(left: int, right: int, limit: int, what: str) -> int:
    if left < 0 or right < 0 or (left and right > limit // left):
        raise VerificationError("%s size overflows the image" % what)
    return left * right


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

    def c_string_at_rva(self, rva: int, what: str) -> str:
        """Read a nonempty printable ASCII C string that stays in raw section data."""
        if not rva:
            raise VerificationError("%s: zero RVA for %s" % (self.label, what))
        offset = self.rva_to_offset(rva, 1, what)
        section = self.initialized_section_for_rva(rva)
        if section is None:
            raise VerificationError("%s: %s RVA is not backed by raw section data" %
                                    (self.label, what))
        virtual_size = section["virtual_size"] or section["raw_size"]
        initialized_size = min(virtual_size, section["raw_size"])
        remaining = initialized_size - (rva - section["rva"])
        terminator = self.data.find(b"\0", offset, offset + remaining)
        if terminator < 0:
            raise VerificationError("%s: unterminated %s" % (self.label, what))
        encoded = self.data[offset:terminator]
        if not encoded or any(byte < 0x20 or byte > 0x7E for byte in encoded):
            raise VerificationError("%s: invalid %s" % (self.label, what))
        return encoded.decode("ascii")

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

    def inspect(self, expected_machine: Optional[str] = None,
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
        for field_offset, field_name in (
                (LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET,
                 "GuardCFCheckFunctionPointer"),
                (LOAD_CONFIG_GUARD_DISPATCH_POINTER_OFFSET,
                 "GuardCFDispatchFunctionPointer")):
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
            })
        if (guard_dispatch_slots[0]["slot_rva"] ==
                guard_dispatch_slots[1]["slot_rva"]):
            raise VerificationError(
                "%s: GuardCF check and dispatch pointers alias one slot" %
                self.label)

        guard_table_va = self.u64(
            load_offset + LOAD_CONFIG_GFID_TABLE_OFFSET,
            "GuardCFFunctionTable")
        guard_count = self.u64(load_offset + 0x88, "GuardCFFunctionCount")
        guard_flags = self.u32(load_offset + 0x90, "GuardFlags")
        if guard_flags != EXPECTED_GUARD_FLAGS:
            raise VerificationError(
                "%s: GuardFlags are 0x%08x, expected 0x%08x" %
                (self.label, guard_flags, EXPECTED_GUARD_FLAGS))
        if not guard_table_va or not guard_count:
            raise VerificationError("%s: empty CFG function table" % self.label)
        guard_stride = 4 + ((guard_flags & CF_FUNCTION_TABLE_SIZE_MASK) >> 28)
        if guard_stride != 5:
            raise VerificationError("%s: /GUARD:MIXED GFID stride is %d, expected 5" %
                                    (self.label, guard_stride))
        guard_bytes = _checked_product(guard_count, guard_stride, len(self.data),
                                       "CFG function table")
        guard_rva = self.va_to_rva(guard_table_va, "GuardCFFunctionTable")
        guard_offset = self.rva_to_offset(guard_rva, guard_bytes,
                                          "CFG function table")
        gfids = []
        gfid_flags = []
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
            if any(metadata):
                raise VerificationError(
                    "%s: fixture GFID entry has nonzero metadata" % self.label)
            gfid_flags.append(metadata[0])

        effective_gfids = {
            gfid for gfid, flags in zip(gfids, gfid_flags)
            if not flags & FID_SUPPRESSED
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
                    target["forwarder"] = self.c_string_at_rva(
                        target_rva, "export forwarder")
                else:
                    containing = self.initialized_section_for_rva(target_rva)
                    if containing is None:
                        raise VerificationError(
                            "%s: export target 0x%x is not backed by initialized image data" %
                            (self.label, target_rva))
                    executable = bool(containing["characteristics"] & 0x20000000)
                    target["kind"] = "function" if executable else "data"
                    target["gfid_covered"] = target_rva in effective_gfids
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
        seen_ordinal_indices = set()
        for index in range(name_count):
            name_rva = self.u32(names_offset + index * 4,
                                "export name pointer")
            name = self.c_string_at_rva(name_rva, "export name")
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
            if ordinal_index in seen_ordinal_indices:
                raise VerificationError("%s: export names alias one ordinal" %
                                        self.label)
            seen_ordinal_indices.add(ordinal_index)
            target = export_targets[ordinal_index]
            if target["kind"] == "absent":
                raise VerificationError("%s: named export %s has no target" %
                                        (self.label, name))
            exports.append({"name": name, **target})
        if seen_ordinal_indices != set(range(function_count)):
            raise VerificationError(
                "%s: export address table contains unnamed ordinals" % self.label)
        actual_exports = {
            entry["name"]: {
                "kind": entry["kind"],
                "ordinal": entry["ordinal"],
                "gfid_covered": entry["gfid_covered"],
            }
            for entry in exports
        }
        if actual_exports != EXPECTED_EXPORTS:
            raise VerificationError("%s: exports differ from fixture contract" %
                                    self.label)
        export_dll_name = self.c_string_at_rva(export_name_rva,
                                               "export DLL name")
        if (expected_export_name is not None and
                export_dll_name.lower() != expected_export_name.lower()):
            raise VerificationError(
                "%s: export DLL name is %s, expected %s" %
                (self.label, export_dll_name, expected_export_name))

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
        if import_count != len(EXPECTED_ENCLAVE_IMPORT_NAMES):
            raise VerificationError(
                "%s: enclave import count differs from fixture contract" %
                self.label)
        if not import_list or import_entry_size != ENCLAVE_IMPORT_SIZE:
            raise VerificationError(
                "%s: enclave import list does not use two 80-byte descriptors" %
                self.label)
        imports = []
        import_bytes = _checked_product(import_count, import_entry_size,
                                        len(self.data), "enclave import list")
        import_offset = self.rva_to_offset(import_list, import_bytes,
                                           "enclave import list")
        for index in range(import_count):
            entry_offset = import_offset + index * ENCLAVE_IMPORT_SIZE
            match_type = self.u32(entry_offset, "enclave import MatchType")
            minimum_security_version = self.u32(
                entry_offset + 4, "enclave import MinimumSecurityVersion")
            unique_or_author_id = self.data[entry_offset + 8:entry_offset + 40]
            family_id = self.data[entry_offset + 40:entry_offset + 56]
            image_id = self.data[entry_offset + 56:entry_offset + 72]
            import_name_rva = self.u32(entry_offset + 72,
                                       "enclave import ImportName")
            reserved = self.u32(entry_offset + 76, "enclave import Reserved")
            if (match_type or minimum_security_version or
                    any(unique_or_author_id) or any(family_id) or
                    any(image_id) or reserved):
                raise VerificationError(
                    "%s: enclave import identity fields are not all zero" %
                    self.label)
            imports.append({
                "match_type": match_type,
                "minimum_security_version": minimum_security_version,
                "unique_or_author_id": unique_or_author_id.hex(),
                "family_id": family_id.hex(),
                "image_id": image_id.hex(),
                "import_name_rva": import_name_rva,
                "name": self.c_string_at_rva(import_name_rva,
                                              "enclave import name"),
                "reserved": reserved,
            })
        if tuple(entry["name"] for entry in imports) != EXPECTED_ENCLAVE_IMPORT_NAMES:
            raise VerificationError(
                "%s: enclave import names/order differ from fixture contract" %
                self.label)
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
            "load_config": {
                "rva": load_rva,
                "directory_size": load_directory_size,
                "declared_size": load_declared_size,
                "guard_flags": guard_flags,
                "guard_table_rva": guard_rva,
                "guard_entry_size": guard_stride,
                "guard_function_count": guard_count,
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
                "imports": imports,
                "family_id": family_id.hex(),
                "image_id": image_id.hex(),
                "image_version": image_version,
                "security_version": security_version,
                "address_space_size": address_space_size,
                "number_of_threads": thread_count,
                "flags": enclave_flags,
            },
            "gfids": gfids,
            "gfid_flags": gfid_flags,
            "giat": {
                "table_rva": giat_table_rva,
                "entry_size": guard_stride,
                "count": giat_count,
                "entries": giats,
                "metadata": giat_flags,
            },
            "exports": exports,
            "export_dll_name": export_dll_name,
            "enclave_pointer_dir64_relocation": True,
            "guard_dispatch_dir64_relocations": True,
            "guard_pointer_dir64_relocation": True,
            "section_count": section_count,
        }


def inspect_path(path: pathlib.Path,
                 expected_machine: Optional[str] = None) -> Dict[str, Any]:
    return PEImage.from_path(path).inspect(expected_machine, path.name)


def compare(reference: Dict[str, Any], candidate: Dict[str, Any]) -> None:
    def enclave_configuration(image: Dict[str, Any]) -> Dict[str, Any]:
        return {key: value for key, value in image["enclave"].items()
                if key != "imports"}

    def comparable_imports(image: Dict[str, Any]) -> List[Dict[str, Any]]:
        return [{key: value for key, value in entry.items()
                 if key != "import_name_rva"}
                for entry in image["enclave"]["imports"]]

    def export_gfid_coverage(image: Dict[str, Any]) -> Dict[str, Dict[str, Any]]:
        return {entry["name"]: {
            "kind": entry["kind"],
            "ordinal": entry["ordinal"],
            "gfid_covered": entry["gfid_covered"],
        } for entry in image["exports"]}

    def comparable_giat(image: Dict[str, Any]) -> Dict[str, Any]:
        giat = image["giat"]
        return {
            "entry_size": giat["entry_size"],
            "count": giat["count"],
            "metadata": giat["metadata"],
        }

    checks = [
        ("machine", reference["machine"], candidate["machine"]),
        ("DLL flag", reference["is_dll"], candidate["is_dll"]),
        ("DYNAMIC_BASE", reference["dynamic_base"], candidate["dynamic_base"]),
        ("FORCE_INTEGRITY", reference["force_integrity"], candidate["force_integrity"]),
        ("GUARD_CF", reference["guard_cf"], candidate["guard_cf"]),
        ("load-config directory size", reference["load_config"]["directory_size"],
         candidate["load_config"]["directory_size"]),
        ("load-config declared size", reference["load_config"]["declared_size"],
         candidate["load_config"]["declared_size"]),
        ("GuardFlags", reference["load_config"]["guard_flags"],
         candidate["load_config"]["guard_flags"]),
        ("GFID entry size", reference["load_config"]["guard_entry_size"],
         candidate["load_config"]["guard_entry_size"]),
        ("GFID count", len(reference["gfids"]), len(candidate["gfids"])),
        ("GFID metadata flags", sorted(reference["gfid_flags"]),
         sorted(candidate["gfid_flags"])),
        ("export GFID coverage", export_gfid_coverage(reference),
         export_gfid_coverage(candidate)),
        ("GIAT semantics", comparable_giat(reference),
         comparable_giat(candidate)),
        ("enclave configuration", enclave_configuration(reference),
         enclave_configuration(candidate)),
        ("enclave import descriptors", comparable_imports(reference),
         comparable_imports(candidate)),
        ("enclave pointer DIR64 relocation",
         reference["enclave_pointer_dir64_relocation"],
         candidate["enclave_pointer_dir64_relocation"]),
        ("GFID pointer DIR64 relocation",
         reference["guard_pointer_dir64_relocation"],
         candidate["guard_pointer_dir64_relocation"]),
        ("CFG check/dispatch DIR64 relocations",
         reference["guard_dispatch_dir64_relocations"],
         candidate["guard_dispatch_dir64_relocations"]),
    ]
    mismatches = ["%s: reference=%r candidate=%r" % item
                  for item in checks if item[1] != item[2]]
    if mismatches:
        raise VerificationError("semantic PE mismatch:\n  " + "\n  ".join(mismatches))


def _synthetic_image() -> bytes:
    data = bytearray(0xC00)
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
                     0x2300, 0xD9)
    struct.pack_into("<II", data, optional + 112 + LOAD_CONFIG_DIRECTORY * 8,
                     0x2000, 0x100)
    struct.pack_into("<II", data, optional + 112 + BASE_RELOCATION_DIRECTORY * 8,
                     0x3000, 24)
    struct.pack_into("<II", data, optional + 112 + IAT_DIRECTORY * 8,
                     0x2100, 16)
    sections = optional + 0xF0
    for index, values in enumerate((
            (b".text", 0x300, 0x1000, 0x200, 0x400, 0x60000020),
            (b".rdata", 0x400, 0x2000, 0x400, 0x600, 0x40000040),
            (b".reloc", 0x200, 0x3000, 0x200, 0xA00, 0x42000040))):
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
    struct.pack_into("<Q", data, load + 0x80, 0x180002260)
    struct.pack_into("<Q", data, load + 0x88, 2)
    struct.pack_into("<I", data, load + 0x90,
                     CF_INSTRUMENTED | CF_FUNCTION_TABLE_PRESENT |
                     PROTECT_DELAYLOAD_IAT |
                     DELAYLOAD_IAT_IN_ITS_OWN_SECTION |
                     CF_FUNCTION_TABLE_SIZE_5BYTES)
    struct.pack_into("<QQ", data, load + LOAD_CONFIG_GIAT_TABLE_OFFSET,
                     0x180002120, 2)
    struct.pack_into("<Q", data, load + 0xF8, 0x180002200)
    struct.pack_into("<QQ", data, 0x700, 0x180001000, 0x180001030)
    for index, iat_rva in enumerate((0x2100, 0x2108)):
        struct.pack_into("<I", data, 0x720 + index * 5, iat_rva)
    enclave = 0x800
    struct.pack_into("<IIIIII", data, enclave, 80, 76, 1, 2, 0x2130, 80)
    data[enclave + 24:enclave + 40] = EXPECTED_FAMILY_ID
    data[enclave + 40:enclave + 56] = EXPECTED_IMAGE_ID
    struct.pack_into("<IIQII", data, enclave + 56, 0x10000, 1,
                     0x20000000, 1, 1)
    for index, target_rva in enumerate((0x1000, 0x1030)):
        struct.pack_into("<I", data, 0x860 + index * 5, target_rva)
    for entry_offset, name_rva in ((0x730, 0x21D0), (0x780, 0x21E8)):
        struct.pack_into("<II", data, entry_offset + 72, name_rva, 0)
    data[0x7D0:0x7E5] = b"ucrtbase_enclave.dll\0"
    data[0x7E8:0x7F4] = b"vertdll.dll\0"
    export = 0x900
    export_names = (
        ("GuardedExercise", 0x1020),
        ("GuardedIndirectCall", 0x1010),
        ("GuardedTarget", 0x1000),
        ("LegacyAddressTaken", 0x23E0),
        ("LegacyExercise", 0x1040),
        ("LegacyTarget", 0x1030),
    )
    struct.pack_into("<IIHHIIIIIII", data, export, 0, 0, 0, 0, 0x2368, 1,
                     len(export_names), len(export_names), 0x2328, 0x2340,
                     0x2358)
    string_offset = 0x978
    data[0x968:0x974] = b"fixture.dll\0"
    for index, (name, target_rva) in enumerate(export_names):
        encoded_name = name.encode("ascii") + b"\0"
        struct.pack_into("<I", data, 0x928 + index * 4, target_rva)
        struct.pack_into("<I", data, 0x940 + index * 4,
                         0x2300 + string_offset - export)
        struct.pack_into("<H", data, 0x958 + index * 2, index)
        data[string_offset:string_offset + len(encoded_name)] = encoded_name
        string_offset += len(encoded_name)
    struct.pack_into("<IIHHHHHHHH", data, 0xA00, 0x2000, 24,
                     (DIR64 << 12) |
                     LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET,
                     (DIR64 << 12) |
                     LOAD_CONFIG_GUARD_DISPATCH_POINTER_OFFSET,
                     (DIR64 << 12) | LOAD_CONFIG_GFID_TABLE_OFFSET,
                     (DIR64 << 12) | LOAD_CONFIG_GIAT_TABLE_OFFSET,
                     (DIR64 << 12) | LOAD_CONFIG_ENCLAVE_POINTER_OFFSET,
                     (DIR64 << 12) | 0x100, (DIR64 << 12) | 0x108, 0)
    return bytes(data)


def self_test() -> None:
    valid = _synthetic_image()
    PEImage(valid, "synthetic-valid").inspect(
        expected_machine="x86_64", expected_export_name="fixture.dll")
    cases = []
    failures = []

    try:
        PEImage(valid, "self-test/wrong expected machine").inspect("arm64")
        failures.append("wrong expected machine")
    except VerificationError:
        pass

    wrong_export_name = bytearray(valid)
    wrong_export_name[0x968] = ord("x")
    try:
        PEImage(bytes(wrong_export_name),
                "self-test/wrong export DLL name").inspect(
                    expected_export_name="fixture.dll")
        failures.append("wrong export DLL name")
    except VerificationError:
        pass

    def add(name: str, offset: int, fmt: str, value: int) -> None:
        mutated = bytearray(valid)
        struct.pack_into(fmt, mutated, offset, value)
        cases.append((name, bytes(mutated)))

    optional = 0x98
    add("bad DOS signature", 0, "<H", 0)
    cases.append(("truncated PE", valid[:0x90]))
    add("missing DLL", 0x84 + 18, "<H", 0x22)
    add("missing DYNAMIC_BASE", optional + 70, "<H", FORCE_INTEGRITY | GUARD_CF)
    add("missing FORCE_INTEGRITY", optional + 70, "<H", DYNAMIC_BASE | GUARD_CF)
    add("missing GUARD_CF", optional + 70, "<H", DYNAMIC_BASE | FORCE_INTEGRITY)
    add("missing load config", optional + 112 + LOAD_CONFIG_DIRECTORY * 8,
        "<I", 0)
    add("short load-config directory",
        optional + 112 + LOAD_CONFIG_DIRECTORY * 8 + 4, "<I", 0xF8)
    add("short load config", 0x600, "<I", 0xF8)
    add("oversized load config", 0x600, "<I", 0x108)
    add("zero enclave pointer", 0x600 + 0xF8, "<Q", 0)
    add("wrong enclave pointer", 0x600 + 0xF8, "<Q", 0x180005000)
    add("short enclave config", 0x800, "<I", 76)
    add("wrong minimum config", 0x800 + 4, "<I", 8)
    add("non-debuggable enclave", 0x800 + 8, "<I", 0)
    zero_imports = bytearray(valid)
    struct.pack_into("<III", zero_imports, 0x800 + 12, 0, 0, 0)
    cases.append(("zero enclave imports", bytes(zero_imports)))
    add("zero enclave import list", 0x800 + 16, "<I", 0)
    add("bad enclave import entry size", 0x800 + 20, "<I", 79)
    add("nonzero enclave import MatchType", 0x730, "<I", 1)
    add("nonzero enclave import minimum security", 0x734, "<I", 1)
    add("nonzero enclave import unique identity", 0x738, "<B", 1)
    add("nonzero enclave import family identity", 0x758, "<B", 1)
    add("nonzero enclave import image identity", 0x768, "<B", 1)
    add("bad enclave import ImportName RVA", 0x778, "<I", 0x5000)
    add("wrong enclave import name", 0x7D0, "<B", ord("x"))
    add("empty enclave import name", 0x7D0, "<B", 0)
    reversed_imports = bytearray(valid)
    struct.pack_into("<II", reversed_imports, 0x778, 0x21E8, 0)
    struct.pack_into("<II", reversed_imports, 0x7C8, 0x21D0, 0)
    cases.append(("reversed enclave import names", bytes(reversed_imports)))
    add("nonzero enclave import reserved", 0x77C, "<I", 1)
    add("wrong family ID", 0x800 + 24, "<Q", 0)
    add("wrong image ID", 0x800 + 40, "<Q", 0)
    add("wrong image version", 0x800 + 56, "<I", 2)
    add("wrong security version", 0x800 + 60, "<I", 2)
    add("wrong enclave size", 0x800 + 64, "<Q", 0x10000000)
    add("wrong thread count", 0x800 + 72, "<I", 2)
    add("missing primary-image flag", 0x800 + 76, "<I", 0)
    add("zero guard table", 0x600 + 0x80, "<Q", 0)
    add("wrong guard table", 0x600 + 0x80, "<Q", 0x180005000)
    add("zero GuardCF check pointer",
        0x600 + LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET, "<Q", 0)
    add("zero GuardCF dispatch pointer",
        0x600 + LOAD_CONFIG_GUARD_DISPATCH_POINTER_OFFSET, "<Q", 0)
    add("GuardCF check pointer outside image",
        0x600 + LOAD_CONFIG_GUARD_CHECK_POINTER_OFFSET,
        "<Q", 0x180005000)
    add("aliased GuardCF check and dispatch slots",
        0x600 + LOAD_CONFIG_GUARD_DISPATCH_POINTER_OFFSET,
        "<Q", 0x180002100)
    add("zero GuardCF check target", 0x700, "<Q", 0)
    add("nonexecutable GuardCF check target", 0x700, "<Q", 0x180002000)
    add("zero guard count", 0x600 + 0x88, "<Q", 0)
    add("oversized guard count", 0x600 + 0x88, "<Q", 0xFFFFFFFFFFFFFFFF)
    add("missing GuardFlags", 0x600 + 0x90, "<I", 0)
    add("four-byte GFID stride", 0x600 + 0x90, "<I",
        CF_INSTRUMENTED | CF_FUNCTION_TABLE_PRESENT |
        PROTECT_DELAYLOAD_IAT | DELAYLOAD_IAT_IN_ITS_OWN_SECTION)
    add("extra GuardFlags", 0x600 + 0x90, "<I",
        EXPECTED_GUARD_FLAGS | 0x8000)
    add("implicit longjmp metadata", 0x600 + 0x90, "<I",
        CF_INSTRUMENTED | CF_FUNCTION_TABLE_PRESENT | CF_LONGJUMP_TABLE_PRESENT)
    add("invalid GFID", 0x860, "<I", 0x2000)
    add("undefined GFID flags", 0x864, "<B", 4)
    add("suppressed required GFID", 0x864, "<B", FID_SUPPRESSED)
    add("unsorted GFIDs", 0x865, "<I", 0x1000)
    add("wrong export Base", 0x910, "<I", 7)
    add("nonzero export Characteristics", 0x900, "<I", 1)
    swapped_ordinals = bytearray(valid)
    struct.pack_into("<HH", swapped_ordinals, 0x958, 1, 0)
    cases.append(("swapped export ordinals", bytes(swapped_ordinals)))
    add("aliased export ordinal", 0x95A, "<H", 0)
    add("aliased export target", 0x92C, "<I", 0x1020)
    virtual_tail_export = bytearray(valid)
    struct.pack_into("<I", virtual_tail_export, 0x860, 0x1250)
    struct.pack_into("<I", virtual_tail_export, 0x930, 0x1250)
    cases.append(("export and GFID in executable virtual tail",
                  bytes(virtual_tail_export)))
    unsorted_exports = bytearray(valid)
    first_name, second_name = struct.unpack_from("<II", unsorted_exports, 0x940)
    first_ordinal, second_ordinal = struct.unpack_from(
        "<HH", unsorted_exports, 0x958)
    struct.pack_into("<II", unsorted_exports, 0x940, second_name, first_name)
    struct.pack_into("<HH", unsorted_exports, 0x958,
                     second_ordinal, first_ordinal)
    cases.append(("unsorted export name table", bytes(unsorted_exports)))
    unnamed_export = bytearray(valid)
    unnamed_export[0x870:0x888] = valid[0x928:0x940]
    struct.pack_into("<I", unnamed_export, 0x888, 0x1050)
    struct.pack_into("<I", unnamed_export, 0x914, 7)
    struct.pack_into("<I", unnamed_export, 0x91C, 0x2270)
    cases.append(("extra unnamed export ordinal", bytes(unnamed_export)))
    add("GIAT pointer without count",
        0x600 + LOAD_CONFIG_GIAT_COUNT_OFFSET, "<Q", 0)
    add("GIAT count without pointer",
        0x600 + LOAD_CONFIG_GIAT_TABLE_OFFSET, "<Q", 0)
    add("GIAT table outside image",
        0x600 + LOAD_CONFIG_GIAT_TABLE_OFFSET, "<Q", 0x180005000)
    add("missing IAT for nonempty GIAT", optional + 112 + IAT_DIRECTORY * 8,
        "<I", 0)
    add("GIAT entry outside IAT", 0x720, "<I", 0x2200)
    add("misaligned GIAT entry", 0x720, "<I", 0x2104)
    add("unsorted GIAT", 0x725, "<I", 0x2100)
    add("nonzero GIAT metadata", 0x724, "<B", 1)
    four_byte_giat = bytearray(valid)
    struct.pack_into("<II", four_byte_giat, 0x720, 0x2100, 0x2108)
    cases.append(("four-byte GIAT packing", bytes(four_byte_giat)))
    add("section raw data out of bounds", 0x188 + 20, "<I", 0xC00)
    add("export directory in raw alignment padding", 0x1B0 + 8, "<I", 0x300)
    add("missing relocations", optional + 112 + BASE_RELOCATION_DIRECTORY * 8,
        "<I", 0)
    add("invalid relocation block", 0xA04, "<I", 10)
    add("missing GuardCF check field relocation", 0xA08, "<H", 0)
    add("wrong GuardCF check field relocation", 0xA08, "<H",
        (DIR64 << 12) | 0x88)
    add("missing GuardCF dispatch field relocation", 0xA0A, "<H", 0)
    add("wrong GuardCF dispatch field relocation", 0xA0A, "<H",
        (DIR64 << 12) | 0x90)
    add("missing GFID pointer relocation", 0xA0C, "<H", 0)
    add("wrong GFID pointer relocation", 0xA0C, "<H",
        (DIR64 << 12) | 0x88)
    add("missing GIAT pointer relocation", 0xA0E, "<H", 0)
    add("wrong GIAT pointer relocation", 0xA0E, "<H",
        (DIR64 << 12) | LOAD_CONFIG_GIAT_COUNT_OFFSET)
    add("wrong enclave relocation type", 0xA10, "<H",
        (3 << 12) | LOAD_CONFIG_ENCLAVE_POINTER_OFFSET)
    add("missing GuardCF check slot relocation", 0xA12, "<H", 0)
    add("missing GuardCF dispatch slot relocation", 0xA14, "<H", 0)
    data_export_virtual_tail = bytearray(valid)
    struct.pack_into("<I", data_export_virtual_tail, 0x1B0 + 8, 0x500)
    struct.pack_into("<I", data_export_virtual_tail, 0x934, 0x2450)
    cases.append(("data export in virtual-only section tail",
                  bytes(data_export_virtual_tail)))
    for name, image in cases:
        try:
            PEImage(image, "self-test/%s" % name).inspect()
            failures.append(name)
        except VerificationError:
            pass
    reference = PEImage(valid, "synthetic-reference").inspect()
    clone = PEImage(valid, "synthetic-clone").inspect()
    compare(reference, clone)
    expected_coverage = {
        "GuardedExercise": False,
        "GuardedIndirectCall": False,
        "GuardedTarget": True,
        "LegacyAddressTaken": False,
        "LegacyExercise": False,
        "LegacyTarget": True,
    }
    actual_coverage = {entry["name"]: entry["gfid_covered"]
                       for entry in reference["exports"]}
    if actual_coverage != expected_coverage:
        raise VerificationError("self-test export GFID coverage is incorrect")
    relocated = bytearray(valid)
    struct.pack_into("<I", relocated, 0x860, 0x1028)
    struct.pack_into("<I", relocated, 0x930, 0x1028)
    compare(reference, PEImage(bytes(relocated),
                               "synthetic-relocated-target").inspect())
    compare_cases = []
    extra_gfid = bytearray(valid)
    struct.pack_into("<Q", extra_gfid, 0x600 + 0x88, 3)
    struct.pack_into("<I", extra_gfid, 0x86A, 0x1050)
    compare_cases.append(("extra valid GFID", bytes(extra_gfid)))
    fewer_giats = bytearray(valid)
    struct.pack_into("<Q", fewer_giats,
                     0x600 + LOAD_CONFIG_GIAT_COUNT_OFFSET, 1)
    compare_cases.append(("fewer valid GIAT entries", bytes(fewer_giats)))
    larger_load_directory = bytearray(valid)
    struct.pack_into("<I", larger_load_directory,
                     optional + 112 + LOAD_CONFIG_DIRECTORY * 8 + 4, 0x108)
    compare_cases.append(("different valid load-config directory size",
                          bytes(larger_load_directory)))
    for name, image in compare_cases:
        try:
            compare(reference, PEImage(image, "self-test/%s" % name).inspect())
            failures.append(name)
        except VerificationError:
            pass
    if failures:
        raise VerificationError("self-test mutations unexpectedly passed: " +
                                ", ".join(failures))
    print("PASS: VBS enclave PE verifier self-test (%d mutations)" %
          (2 + len(cases) + len(compare_cases)))


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    inspect_parser = subparsers.add_parser("inspect")
    inspect_parser.add_argument("image", type=pathlib.Path)
    inspect_parser.add_argument("--machine", required=True,
                                choices=("x86_64", "arm64"))
    inspect_parser.add_argument("--json", dest="json_path", type=pathlib.Path)
    compare_parser = subparsers.add_parser("compare")
    compare_parser.add_argument("reference", type=pathlib.Path)
    compare_parser.add_argument("candidate", type=pathlib.Path)
    subparsers.add_parser("self-test")
    args = parser.parse_args(argv)
    try:
        if args.command == "self-test":
            self_test()
        elif args.command == "inspect":
            result = inspect_path(args.image, args.machine)
            encoded = json.dumps(result, indent=2, sort_keys=True)
            print(encoded)
            if args.json_path:
                args.json_path.parent.mkdir(parents=True, exist_ok=True)
                args.json_path.write_text(encoded + "\n", encoding="utf-8")
        else:
            reference = inspect_path(args.reference)
            candidate = inspect_path(args.candidate)
            compare(reference, candidate)
            print("PASS: %s semantically matches %s" %
                  (args.candidate, args.reference))
    except (OSError, VerificationError) as error:
        print("FAIL: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
