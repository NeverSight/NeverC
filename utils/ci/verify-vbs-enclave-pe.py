#!/usr/bin/env python3
"""Strict semantic verifier for the VBS enclave differential CI fixture."""

import argparse
import json
import pathlib
import re
import struct
import sys
from typing import Any, Dict, FrozenSet, List, Optional, Set, Tuple


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
CFW_INSTRUMENTED = 0x0200
CF_FUNCTION_TABLE_PRESENT = 0x0400
CF_LONGJUMP_TABLE_PRESENT = 0x10000
CF_EH_CONTINUATION_TABLE_PRESENT = 0x00400000
CF_XFG_ENABLED = 0x00800000
CF_PROTECT_DELAYLOAD_IAT = 0x1000
CF_DELAYLOAD_IAT_IN_ITS_OWN_SECTION = 0x2000
CF_FUNCTION_TABLE_SIZE_MASK = 0xF0000000
CF_FUNCTION_TABLE_SIZE_5BYTES = 0x10000000
CF_ALLOWED_FIXTURE_FLAGS = (CF_INSTRUMENTED | CF_FUNCTION_TABLE_PRESENT |
                            CF_PROTECT_DELAYLOAD_IAT |
                            CF_DELAYLOAD_IAT_IN_ITS_OWN_SECTION |
                            CF_FUNCTION_TABLE_SIZE_MASK)
GFID_FID_SUPPRESSED = 0x01
GFID_EXPORT_SUPPRESSED = 0x02
GFID_FID_LANGEXCPTHANDLER = 0x04
GFID_FID_XFG = 0x08
DIR64 = 10
MAX_DATA_DIRECTORIES = 16
EXPORT_DIRECTORY = 0
IMPORT_DIRECTORY = 1
RESOURCE_DIRECTORY = 2
EXCEPTION_DIRECTORY = 3
CERTIFICATE_DIRECTORY = 4
BASE_RELOCATION_DIRECTORY = 5
DEBUG_DIRECTORY = 6
ARCHITECTURE_DIRECTORY = 7
GLOBALPTR_DIRECTORY = 8
TLS_DIRECTORY = 9
LOAD_CONFIG_DIRECTORY = 10
BOUND_IMPORT_DIRECTORY = 11
IAT_DIRECTORY = 12
DELAY_IMPORT_DIRECTORY = 13
CLR_DIRECTORY = 14
RESERVED_DIRECTORY = 15
FORBIDDEN_DIRECTORIES = {
    RESOURCE_DIRECTORY: "resource",
    CERTIFICATE_DIRECTORY: "certificate",
    ARCHITECTURE_DIRECTORY: "architecture",
    GLOBALPTR_DIRECTORY: "global pointer",
    TLS_DIRECTORY: "TLS",
    BOUND_IMPORT_DIRECTORY: "bound import",
    DELAY_IMPORT_DIRECTORY: "delay import",
    CLR_DIRECTORY: "CLR",
    RESERVED_DIRECTORY: "reserved",
}
DEBUG_DIRECTORY_ENTRY_SIZE = 28
ALLOWED_DEBUG_TYPES = frozenset((2, 13))
REQUIRED_EXCEPTION_EXPORTS = (
    "GuardedIndirectCall", "GuardedExercise", "LegacyExercise",
)
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
    r"^\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8,16})\s+"
    r"(\S+)\s+([0-9A-Fa-f]{8,16})(?:\s|$)")
MapSymbols = Dict[str, FrozenSet[int]]
MapEntry = Tuple[int, int, str, int]


class CoffMap:
    def __init__(self, symbols: MapSymbols,
                 entries: Tuple[MapEntry, ...]):
        self.symbols = symbols
        self.entries = entries


def _checked_product(left: int, right: int, limit: int, what: str) -> int:
    if left < 0 or right < 0 or (left and right > limit // left):
        raise VerificationError("%s size overflows the image" % what)
    return left * right


def parse_coff_map_text(text: str, label: str) -> CoffMap:
    public_vas: Dict[str, Set[int]] = {}
    static_vas: Dict[str, Set[int]] = {}
    required_public_vas: Dict[str, Set[int]] = {}
    public_occurrence_names: Set[str] = set()
    public_entries: List[MapEntry] = []
    static_entries: List[MapEntry] = []
    symbol_section: Optional[str] = None
    for line in text.splitlines():
        if "Publics by Value" in line:
            symbol_section = "public"
            continue
        if line.strip() == "Static symbols":
            symbol_section = "static"
            continue
        if "entry point at" in line.lower():
            symbol_section = None
            continue
        if symbol_section is None:
            continue
        match = MAP_SYMBOL_LINE.match(line)
        if match is None:
            continue
        encoded_segment, encoded_offset, name, encoded_va = match.groups()
        segment = int(encoded_segment, 16)
        va = int(encoded_va, 16)
        if symbol_section == "public":
            public_occurrence_names.add(name)
            if name in REQUIRED_MAP_SYMBOLS:
                required_public_vas.setdefault(name, set()).add(va)
        if name.startswith((".", "$", "@")) or "<absolute>" in line.lower():
            continue
        offset = int(encoded_offset, 16)
        target_vas = public_vas if symbol_section == "public" else static_vas
        target_entries = (public_entries if symbol_section == "public"
                          else static_entries)
        target_vas.setdefault(name, set()).add(va)
        target_entries.append((segment, offset, name, va))

    names = set(public_vas) | set(static_vas)
    symbols = {
        name: frozenset(public_vas.get(name, ()))
        if name in public_occurrence_names
        else frozenset(static_vas.get(name, ()))
        for name in names
        if (public_vas.get(name) if name in public_occurrence_names
            else static_vas.get(name))
    }
    for name in sorted(REQUIRED_MAP_SYMBOLS):
        vas = sorted(required_public_vas.get(name, ()))
        if len(vas) > 1:
            raise VerificationError(
                "%s: COFF map symbol %s has ambiguous VAs 0x%x and 0x%x" %
                (label, name, vas[0], vas[1]))
        selected_vas = sorted(symbols.get(name, ()))
        if len(selected_vas) > 1:
            raise VerificationError(
                "%s: COFF map symbol %s has ambiguous VAs 0x%x and 0x%x" %
                (label, name, selected_vas[0], selected_vas[1]))
    missing = sorted(REQUIRED_MAP_SYMBOLS - symbols.keys())
    if missing:
        raise VerificationError(
            "%s: COFF map is missing required symbols: %s" %
            (label, ", ".join(missing)))
    stable_or_required = {
        name for name, vas in symbols.items()
        if len(vas) == 1 or name in REQUIRED_MAP_SYMBOLS
    }
    retained_entries = tuple(
        entry for entry in public_entries
        if entry[2] in stable_or_required)
    retained_entries += tuple(
        entry for entry in static_entries
        if (entry[2] not in public_occurrence_names and
            entry[2] in stable_or_required))
    return CoffMap(symbols, retained_entries)


def parse_coff_map_path(path: pathlib.Path) -> CoffMap:
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

    def map_symbol_rva(self, coff_map: CoffMap, name: str) -> int:
        if name not in coff_map.symbols:
            raise VerificationError(
                "%s: COFF map is missing required symbol %s" %
                (self.label, name))
        symbol_vas = coff_map.symbols[name]
        if len(symbol_vas) != 1:
            raise VerificationError(
                "%s: COFF map symbol %s is ambiguous" % (self.label, name))
        if name in self.invalid_map_errors:
            raise VerificationError(self.invalid_map_errors[name])
        if name not in self.validated_map_names:
            raise VerificationError(
                "%s: COFF map symbol %s has no consistent initialized image record" %
                (self.label, name))
        rva = self.va_to_rva(next(iter(symbol_vas)),
                             "COFF map symbol %s" % name)
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
        iat_ranges: List[Tuple[int, int]] = []
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
            first_thunk = fields[4]
            if not first_thunk:
                raise VerificationError("%s: standard import has a zero FirstThunk RVA" %
                                        self.label)
            if (first_thunk & 7 or first_thunk < self.iat_rva or
                    first_thunk > self.iat_end - 8):
                raise VerificationError(
                    "%s: standard import FirstThunk is not an aligned IAT slot" %
                    self.label)
            self.rva_to_offset(first_thunk, 8, "standard import IAT")
            thunk_rva = first_thunk
            while True:
                if thunk_rva > self.iat_end - 8:
                    raise VerificationError(
                        "%s: unterminated standard import IAT" % self.label)
                thunk_offset = self.rva_to_offset(
                    thunk_rva, 8, "standard import IAT thunk")
                if not self.u64(thunk_offset, "standard import IAT thunk"):
                    break
                thunk_rva += 8
            thunk_end = thunk_rva + 8
            if any(max(first_thunk, start) < min(thunk_end, end)
                   for start, end in iat_ranges):
                raise VerificationError(
                    "%s: standard import IAT ranges overlap" % self.label)
            iat_ranges.append((first_thunk, thunk_end))
            self.rva_to_offset(fields[0] or first_thunk, 8,
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

    def require_read_only_range(self, rva: int, size: int, what: str) -> int:
        offset = self.rva_to_offset(rva, size, what)
        section = self.initialized_section_for_rva(rva)
        if (section is None or
                not section["characteristics"] & SECTION_MEM_READ or
                section["characteristics"] & SECTION_MEM_WRITE):
            raise VerificationError(
                "%s: %s is not in initialized read-only memory" %
                (self.label, what))
        return offset

    def inspect_iat(self) -> Tuple[int, int]:
        iat_rva, iat_size = self.directory(IAT_DIRECTORY)
        if not iat_rva or not iat_size:
            raise VerificationError("%s: missing IAT directory" % self.label)
        if iat_rva & 7 or iat_size < 8 or iat_size & 7:
            raise VerificationError(
                "%s: malformed PE32+ IAT directory shape" % self.label)
        self.rva_to_offset(iat_rva, iat_size, "IAT directory")
        if iat_rva > 0xFFFFFFFF - iat_size:
            raise VerificationError("%s: IAT directory range overflows" %
                                    self.label)
        self.iat_rva = iat_rva
        self.iat_end = iat_rva + iat_size
        return iat_rva, iat_size

    def inspect_debug_directory(self) -> int:
        debug_rva, debug_size = self.directory(DEBUG_DIRECTORY)
        if not debug_rva and not debug_size:
            return 0
        if (not debug_rva or not debug_size or
                debug_size % DEBUG_DIRECTORY_ENTRY_SIZE):
            raise VerificationError("%s: malformed debug directory" % self.label)
        debug_offset = self.require_read_only_range(
            debug_rva, debug_size, "debug directory")
        record_count = debug_size // DEBUG_DIRECTORY_ENTRY_SIZE
        for index in range(record_count):
            record = debug_offset + index * DEBUG_DIRECTORY_ENTRY_SIZE
            debug_type = self.u32(record + 12, "debug directory Type")
            if debug_type not in ALLOWED_DEBUG_TYPES:
                raise VerificationError(
                    "%s: debug directory type %d is not allowed" %
                    (self.label, debug_type))
            payload_size = self.u32(record + 16, "debug SizeOfData")
            payload_rva = self.u32(record + 20, "debug AddressOfRawData")
            payload_pointer = self.u32(record + 24, "debug PointerToRawData")
            if not payload_size or not payload_rva or not payload_pointer:
                raise VerificationError(
                    "%s: debug payload has incomplete location metadata" %
                    self.label)
            mapped_pointer = self.rva_to_offset(
                payload_rva, payload_size, "debug RVA payload")
            self.need(payload_pointer, payload_size, "debug file payload")
            if mapped_pointer != payload_pointer:
                raise VerificationError(
                    "%s: debug RVA and file payload locations disagree" %
                    self.label)
        return record_count

    def inspect_x64_unwind_info(self, unwind_rva: int) -> None:
        if not unwind_rva or unwind_rva & 3:
            raise VerificationError("%s: invalid x64 unwind-info RVA" % self.label)
        offset = self.require_read_only_range(
            unwind_rva, 4, "x64 unwind information")
        version_and_flags = self.data[offset]
        version = version_and_flags & 7
        flags = version_and_flags >> 3
        if version not in (1, 2) or flags & ~7 or (flags & 4 and flags & 3):
            raise VerificationError("%s: malformed x64 unwind information" %
                                    self.label)
        unwind_code_count = self.data[offset + 2]
        trailer_offset = (4 + unwind_code_count * 2 + 3) & ~3
        trailer_size = 4 if flags & 3 else 12 if flags & 4 else 0
        full_offset = self.require_read_only_range(
            unwind_rva, trailer_offset + trailer_size,
            "declared x64 unwind information")
        if flags & 3:
            handler_rva = self.u32(full_offset + trailer_offset,
                                   "x64 unwind handler")
            if not handler_rva or not self.executable_rva(handler_rva):
                raise VerificationError(
                    "%s: x64 unwind handler is not executable" % self.label)
        elif flags & 4:
            chained_begin, chained_end, chained_unwind = struct.unpack_from(
                "<III", self.data, full_offset + trailer_offset)
            if (not chained_begin or chained_begin >= chained_end or
                    not self.executable_rva(chained_begin) or
                    not self.executable_rva(chained_end - 1) or
                    not chained_unwind or chained_unwind & 3):
                raise VerificationError(
                    "%s: malformed chained x64 runtime function" % self.label)
            self.require_read_only_range(
                chained_unwind, 4, "chained x64 unwind information")

    def arm64_function_extent(self, begin_rva: int, unwind_data: int) -> int:
        flag = unwind_data & 3
        if flag == 3:
            raise VerificationError("%s: reserved ARM64 unwind-data flag" %
                                    self.label)
        if flag:
            function_length = (unwind_data >> 2) & 0x7FF
            if not function_length:
                raise VerificationError("%s: zero ARM64 packed function length" %
                                        self.label)
            return function_length * 4

        xdata_rva = unwind_data
        if not xdata_rva or xdata_rva & 3:
            raise VerificationError("%s: invalid ARM64 xdata RVA" % self.label)
        xdata_offset = self.require_read_only_range(
            xdata_rva, 4, "ARM64 xdata header")
        header = self.u32(xdata_offset, "ARM64 xdata header")
        function_length = header & 0x3FFFF
        version = (header >> 18) & 3
        has_handler = bool(header & (1 << 20))
        single_epilog = bool(header & (1 << 21))
        epilog_count = (header >> 22) & 0x1F
        code_words = (header >> 27) & 0x1F
        if not function_length or version:
            raise VerificationError("%s: malformed ARM64 xdata header" %
                                    self.label)
        header_size = 4
        if not epilog_count and not code_words:
            extended = self.u32(
                self.require_read_only_range(
                    xdata_rva, 8, "extended ARM64 xdata header") + 4,
                "extended ARM64 xdata header")
            if extended & 0xFF000000:
                raise VerificationError(
                    "%s: ARM64 xdata extended header has reserved bits" %
                    self.label)
            epilog_count = extended & 0xFFFF
            code_words = (extended >> 16) & 0xFF
            header_size = 8
        epilog_bytes = 0 if single_epilog else epilog_count * 4
        payload_size = (header_size + epilog_bytes + code_words * 4 +
                        (4 if has_handler else 0))
        payload_offset = self.require_read_only_range(
            xdata_rva, payload_size, "declared ARM64 xdata")
        if has_handler:
            handler_rva = self.u32(
                payload_offset + payload_size - 4, "ARM64 exception handler")
            if not handler_rva or not self.executable_rva(handler_rva):
                raise VerificationError(
                    "%s: ARM64 exception handler is not executable" % self.label)
        return function_length * 4

    def inspect_exception_directory(self, machine_name: str,
                                    coff_map: CoffMap) -> int:
        exception_rva, exception_size = self.directory(EXCEPTION_DIRECTORY)
        entry_size = 12 if machine_name == "x86_64" else 8
        if not exception_rva or not exception_size:
            raise VerificationError("%s: missing exception directory" % self.label)
        if exception_size % entry_size:
            raise VerificationError("%s: malformed exception directory size" %
                                    self.label)
        table_offset = self.require_read_only_range(
            exception_rva, exception_size, "exception directory")
        begins: List[int] = []
        previous_end = 0
        for index in range(exception_size // entry_size):
            entry_offset = table_offset + index * entry_size
            begin_rva = self.u32(entry_offset, "runtime-function BeginAddress")
            if machine_name == "x86_64":
                end_rva = self.u32(entry_offset + 4,
                                   "runtime-function EndAddress")
                unwind_rva = self.u32(entry_offset + 8,
                                      "runtime-function UnwindData")
                if not begin_rva or begin_rva >= end_rva:
                    raise VerificationError(
                        "%s: malformed x64 runtime-function range" % self.label)
                self.inspect_x64_unwind_info(unwind_rva)
            else:
                unwind_data = self.u32(entry_offset + 4,
                                       "runtime-function UnwindData")
                function_size = self.arm64_function_extent(begin_rva, unwind_data)
                if begin_rva > 0xFFFFFFFF - function_size:
                    raise VerificationError(
                        "%s: ARM64 runtime-function range overflows" % self.label)
                end_rva = begin_rva + function_size
            if (not self.executable_rva(begin_rva) or
                    not self.executable_rva(end_rva - 1)):
                raise VerificationError(
                    "%s: runtime-function range is not executable" % self.label)
            if begins and begin_rva <= begins[-1]:
                raise VerificationError(
                    "%s: exception directory is not strictly sorted" % self.label)
            if previous_end and begin_rva < previous_end:
                raise VerificationError(
                    "%s: runtime-function ranges overlap" % self.label)
            begins.append(begin_rva)
            previous_end = end_rva
        for name in REQUIRED_EXCEPTION_EXPORTS:
            target_rva = self.map_symbol_rva(coff_map, name)
            if begins.count(target_rva) != 1:
                raise VerificationError(
                    "%s: map-bound nonleaf export %s lacks one exact "
                    "runtime-function start" % (self.label, name))
        return len(begins)

    def inspect(self, coff_map: CoffMap,
                expected_machine: Optional[str] = None,
                expected_export_name: Optional[str] = None) -> Dict[str, Any]:
        self.sections = []
        self.directories = []
        self.validated_map_names = set()
        self.invalid_map_errors = {}
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
        if directory_count > MAX_DATA_DIRECTORIES:
            raise VerificationError(
                "%s: more than 16 PE data directories are not allowed" %
                self.label)
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

        entries_by_name: Dict[str, List[MapEntry]] = {}
        for entry in coff_map.entries:
            entries_by_name.setdefault(entry[2], []).append(entry)
        self.validated_map_names: Set[str] = set()
        self.invalid_map_errors: Dict[str, str] = {}
        for name, entries in entries_by_name.items():
            error = None
            for segment, offset, _, va in entries:
                if segment == 0:
                    if (va < self.image_base or
                        va >= self.image_base + self.size_of_image or
                        self.initialized_section_for_rva(
                            va - self.image_base) is None):
                        error = (
                            "%s: COFF map entry %s does not identify initialized "
                            "image data" % (self.label, name))
                    continue
                if segment > len(self.sections):
                    error = (
                        "%s: COFF map entry %s names missing section %04x" %
                        (self.label, name, segment))
                    continue
                section = self.sections[segment - 1]
                span = max(section["virtual_size"], section["raw_size"])
                expected_rva = section["rva"] + offset
                if offset > span or va != self.image_base + expected_rva:
                    error = (
                        "%s: COFF map entry %s has inconsistent segment:offset" %
                        (self.label, name))
                elif (offset == span or
                      self.initialized_section_for_rva(expected_rva) is None):
                    error = (
                        "%s: COFF map entry %s does not identify initialized "
                        "image data" % (self.label, name))
            if error is None:
                self.validated_map_names.add(name)
            else:
                self.invalid_map_errors[name] = error

        for directory_index, directory_name in FORBIDDEN_DIRECTORIES.items():
            directory_rva, directory_size = self.directory(directory_index)
            if directory_rva or directory_size:
                raise VerificationError(
                    "%s: forbidden %s data directory is nonzero" %
                    (self.label, directory_name))

        iat_rva, iat_size = self.inspect_iat()
        debug_record_count = self.inspect_debug_directory()
        exception_entry_count = self.inspect_exception_directory(
            machine_name, coff_map)
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
                coff_map, entry["slot_symbol"])
            if entry["slot_rva"] != expected_slot_rva:
                raise VerificationError(
                    "%s: %s slot RVA 0x%x does not match COFF map symbol "
                    "%s at RVA 0x%x" %
                    (self.label, entry["name"], entry["slot_rva"],
                     entry["slot_symbol"], expected_slot_rva))
            expected_target_rva = self.map_symbol_rva(
                coff_map, entry["target_symbol"])
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
        if guard_flags & CF_XFG_ENABLED:
            raise VerificationError(
                "%s: /GUARD:MIXED unexpectedly enabled XFG metadata" %
                self.label)
        unsupported_guard_flags = guard_flags & ~CF_ALLOWED_FIXTURE_FLAGS
        if unsupported_guard_flags:
            raise VerificationError(
                "%s: GuardFlags contain unsupported fixture bits 0x%x" %
                (self.label, unsupported_guard_flags))
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
        gfid_aliases = []
        stable_aliases_by_rva: Dict[int, List[str]] = {}
        for name, symbol_vas in coff_map.symbols.items():
            if len(symbol_vas) != 1 or name not in self.validated_map_names:
                continue
            va = next(iter(symbol_vas))
            if self.image_base <= va < self.image_base + self.size_of_image:
                stable_aliases_by_rva.setdefault(
                    va - self.image_base, []).append(name)
        stable_symbol_names = sorted(
            name for name, symbol_vas in coff_map.symbols.items()
            if len(symbol_vas) == 1 and name in self.validated_map_names)
        invalid_alias_errors_by_rva: Dict[int, List[str]] = {}
        for name, error in self.invalid_map_errors.items():
            symbol_vas = coff_map.symbols.get(name, ())
            if len(symbol_vas) == 1:
                va = next(iter(symbol_vas))
                if self.image_base <= va < self.image_base + self.size_of_image:
                    invalid_alias_errors_by_rva.setdefault(
                        va - self.image_base, []).append(error)
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
            if gfid in invalid_alias_errors_by_rva:
                raise VerificationError(
                    sorted(invalid_alias_errors_by_rva[gfid])[0])
            metadata = self.data[guard_offset + index * guard_stride + 4:
                                 guard_offset + (index + 1) * guard_stride]
            if any(metadata):
                raise VerificationError(
                    "%s: GFID metadata must be zero for this fixture" %
                    self.label)
            gfid_metadata.append(metadata.hex())
            aliases = sorted(stable_aliases_by_rva.get(gfid, ()))
            if not aliases:
                raise VerificationError(
                    "%s: GFID 0x%x has no globally unique stable COFF map alias" %
                    (self.label, gfid))
            gfid_aliases.append(aliases)

        gfid_set = set(gfids)

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
                        entry_rva > self.iat_end - 8):
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
                    target["gfid_covered"] = target_rva in gfid_set
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
            expected_target_rva = self.map_symbol_rva(coff_map, name)
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
            "signature_present": False,
            "data_directories": {
                "count": directory_count,
                "iat_rva": iat_rva,
                "iat_size": iat_size,
                "exception_entry_count": exception_entry_count,
                "debug_record_count": debug_record_count,
            },
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
            "gfid_aliases": gfid_aliases,
            "stable_symbol_names": stable_symbol_names,
            "gfid_metadata": gfid_metadata,
            "gfid_flags": [0] * len(gfids),
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

    common_stable_names = (
        set(reference["stable_symbol_names"]) &
        set(candidate["stable_symbol_names"]))

    def gfid_identity_signatures(
            image: Dict[str, Any]) -> Optional[List[Tuple[str, ...]]]:
        signatures = []
        for aliases in image["gfid_aliases"]:
            signature = tuple(sorted(set(aliases) & common_stable_names))
            if not signature:
                return None
            signatures.append(signature)
        return sorted(signatures)

    reference_gfid_signatures = gfid_identity_signatures(reference)
    candidate_gfid_signatures = gfid_identity_signatures(candidate)
    gfid_identities_match = (
        reference_gfid_signatures is not None and
        candidate_gfid_signatures is not None and
        reference_gfid_signatures == candidate_gfid_signatures)

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
        ("GFID stable symbol identities", True, gfid_identities_match),
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
    data = bytearray(0x1000)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    coff = 0x84
    struct.pack_into("<HHIIIHH", data, coff, 0x8664, 4, 0, 0, 0, 0xF0,
                     0x2022)
    optional = coff + 20
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<I", data, optional + 4, 0x200)
    struct.pack_into("<I", data, optional + 8, 0xA00)
    struct.pack_into("<I", data, optional + 16, 0x1000)
    struct.pack_into("<I", data, optional + 20, 0x1000)
    struct.pack_into("<Q", data, optional + 24, 0x180000000)
    struct.pack_into("<II", data, optional + 32, 0x1000, 0x200)
    struct.pack_into("<II", data, optional + 56, 0x5000, 0x400)
    struct.pack_into("<H", data, optional + 68, 3)
    struct.pack_into("<H", data, optional + 70,
                     DYNAMIC_BASE | FORCE_INTEGRITY | GUARD_CF)
    struct.pack_into("<I", data, optional + 108, 16)
    struct.pack_into("<II", data, optional + 112 + EXPORT_DIRECTORY * 8,
                     0x2400, 0xD9)
    struct.pack_into("<II", data, optional + 112 + IMPORT_DIRECTORY * 8,
                     0x22C0, 3 * IMPORT_DESCRIPTOR_SIZE)
    struct.pack_into("<II", data, optional + 112 + EXCEPTION_DIRECTORY * 8,
                     0x4000, 3 * 12)
    struct.pack_into("<II", data, optional + 112 + DEBUG_DIRECTORY * 8,
                     0x4040, DEBUG_DIRECTORY_ENTRY_SIZE)
    struct.pack_into("<II", data, optional + 112 + LOAD_CONFIG_DIRECTORY * 8,
                     0x2000, 0x100)
    struct.pack_into("<II", data, optional + 112 + BASE_RELOCATION_DIRECTORY * 8,
                     0x3000, 24)
    struct.pack_into("<II", data, optional + 112 + IAT_DIRECTORY * 8,
                     0x2100, 0x190)
    sections = optional + 0xF0
    for index, values in enumerate((
            (b".text", 0x200, 0x1000, 0x200, 0x400, 0x60000020),
            (b".rdata", 0x600, 0x2000, 0x600, 0x600, 0x40000040),
            (b".reloc", 0x200, 0x3000, 0x200, 0xC00, 0x42000040),
            (b".pdata", 0x200, 0x4000, 0x200, 0xE00, 0x40000040))):
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
    struct.pack_into("<IIIIIIIII", data, 0xE00,
                     0x1010, 0x1020, 0x4080,
                     0x1020, 0x1030, 0x4084,
                     0x1040, 0x1050, 0x4088)
    data[0xE80:0xE8C] = b"\x01\0\0\0" * 3
    struct.pack_into("<IIHHIIII", data, 0xE40,
                     0, 0, 0, 0, 2, 24, 0x4060, 0xE60)
    data[0xE60:0xE78] = b"RSDS" + bytes(20)
    return bytes(data)


def _synthetic_map_text() -> str:
    image_base = 0x180000000
    symbol_rvas = {
        "GuardedExercise": 0x1020,
        "GuardedIndirectCall": 0x1010,
        "GuardedTarget": 0x1000,
        "InternalTargetA": 0x1050,
        "InternalTargetB": 0x1080,
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
    arm64_cases: List[Tuple[str, bytes, str]] = []
    failures = []
    direct_negative_count = 7
    if reference.get("gfid_aliases") != [
            ["GuardedTarget"], ["LegacyTarget"]]:
        failures.append("GFID stable alias sets were not reported")
    reusable_image = PEImage(valid, "self-test/reusable PE image")
    try:
        first_inspection = reusable_image.inspect(map_symbols)
        second_inspection = reusable_image.inspect(map_symbols)
        if first_inspection["gfid_aliases"] != second_inspection["gfid_aliases"]:
            failures.append("repeated inspection changed GFID aliases")
    except VerificationError as error:
        failures.append("repeated inspection failed: %r" % str(error))
    if map_symbols.symbols.get("InternalTargetA") != frozenset({0x180001050}):
        failures.append("stable COFF map alias was not collected")
    noisy_map = parse_coff_map_text(
        map_text +
        " 0001:00000050       InternalTargetA            "
        "0000000180001050 f   duplicate.obj\n"
        " 0001:00000050       .bf                        "
        "0000000180001050 f   fixture.obj\n"
        " 0001:00000080       $$000000                   "
        "0000000180001080     fixture.obj\n"
        " 0001:00000080       @compiler                  "
        "0000000180001080     fixture.obj\n"
        " 0000:00001050       AbsoluteNoise              "
        "0000000180001050     <absolute>\n",
        "synthetic-noisy.map")
    if noisy_map.symbols.get("InternalTargetA") != frozenset({0x180001050}):
        failures.append("same-VA COFF map aliases did not collapse")
    if any(name in noisy_map.symbols for name in
           (".bf", "$$000000", "@compiler", "AbsoluteNoise")):
        failures.append("unstable COFF map noise was not filtered")

    unnamed_gfid = bytearray(valid)
    struct.pack_into("<Q", unnamed_gfid, 0x600 + 0x88, 3)
    struct.pack_into("<IBIBIB", unnamed_gfid, 0x8A0,
                     0x1000, 0, 0x1030, 0, 0x1050, 0)
    unnamed_map_symbols = CoffMap(
        {name: vas for name, vas in map_symbols.symbols.items()
         if name != "InternalTargetA"},
        tuple(entry for entry in map_symbols.entries
              if entry[2] != "InternalTargetA"))
    try:
        PEImage(bytes(unnamed_gfid), "self-test/unnamed GFID").inspect(
            unnamed_map_symbols)
        failures.append("unnamed GFID (unexpectedly passed)")
    except VerificationError as error:
        if "GFID 0x1050 has no globally unique stable COFF map alias" not in str(error):
            failures.append("unnamed GFID (wrong diagnostic: %r)" % str(error))

    linker_defined_map = parse_coff_map_text(
        map_text +
        " 0000:0000dead       LinkerDefinedTarget        "
        "0000000180001050     <linker-defined>\n",
        "synthetic-linker-defined.map")
    try:
        linker_defined_result = PEImage(
            bytes(unnamed_gfid),
            "self-test/linker-defined stable alias").inspect(
                linker_defined_map)
        if "LinkerDefinedTarget" not in linker_defined_result["gfid_aliases"][2]:
            failures.append("linker-defined stable alias was not reported")
    except VerificationError as error:
        failures.append("linker-defined stable alias failed: %r" % str(error))

    section_end_map = parse_coff_map_text(
        map_text +
        " 0001:00000200       EndOfText                  "
        "0000000180001200     <linker-defined>\n",
        "synthetic-section-end.map")
    try:
        PEImage(valid, "self-test/section-end COFF map row").inspect(
            section_end_map)
    except VerificationError as error:
        failures.append("section-end COFF map row failed: %r" % str(error))

    static_polluted_map = parse_coff_map_text(
        map_text +
        "\n entry point at         0001:00000000\n\n"
        " Static symbols\n\n"
        " 0001:00000080       InternalTargetA            "
        "0000000180001080 f   static.obj\n",
        "synthetic-static-pollution.map")
    try:
        static_polluted_result = PEImage(
            bytes(unnamed_gfid),
            "self-test/static same-name pollution").inspect(
                static_polluted_map)
        if static_polluted_result["gfid_aliases"][2] != ["InternalTargetA"]:
            failures.append("static same-name pollution changed public alias")
    except VerificationError as error:
        failures.append("static same-name pollution failed: %r" % str(error))

    unrelated_bad_map = parse_coff_map_text(
        map_text +
        " 0001:00000018       UnrelatedBrokenAlias       "
        "0000000180001080 f   unrelated.obj\n",
        "synthetic-unrelated-bad-row.map")
    try:
        unrelated_bad_result = PEImage(
            valid, "self-test/unrelated bad COFF map row").inspect(
                unrelated_bad_map)
        if "UnrelatedBrokenAlias" in unrelated_bad_result["stable_symbol_names"]:
            failures.append("unrelated bad COFF map row remained stable")
    except VerificationError as error:
        failures.append("unrelated bad COFF map row failed: %r" % str(error))

    inconsistent_map = parse_coff_map_text(
        map_text +
        " 0001:00000018       GuardedTarget              "
        "0000000180001000 f   inconsistent.obj\n",
        "synthetic-inconsistent.map")
    try:
        PEImage(valid, "self-test/inconsistent COFF map row").inspect(
            inconsistent_map)
        failures.append("inconsistent COFF map row (unexpectedly passed)")
    except VerificationError as error:
        if "COFF map entry GuardedTarget has inconsistent segment:offset" not in str(error):
            failures.append("inconsistent COFF map row (wrong diagnostic: %r)" %
                            str(error))

    short_directory_table = bytearray(valid)
    struct.pack_into("<I", short_directory_table, 0x98 + 108, 13)
    no_debug = bytearray(valid)
    struct.pack_into("<II", no_debug,
                     0x98 + 112 + DEBUG_DIRECTORY * 8, 0, 0)
    type_13_debug = bytearray(valid)
    struct.pack_into("<I", type_13_debug, 0xE40 + 12, 13)
    arm64_valid = bytearray(valid)
    struct.pack_into("<H", arm64_valid, 0x84, 0xAA64)
    struct.pack_into("<II", arm64_valid,
                     0x98 + 112 + EXCEPTION_DIRECTORY * 8, 0x4000, 3 * 8)
    arm64_valid[0xE00:0xE24] = bytes(0x24)
    struct.pack_into("<IIIIII", arm64_valid, 0xE00,
                     0x1010, 0x4080,
                     0x1020, (4 << 2) | 1,
                     0x1040, 0x4088)
    struct.pack_into("<II", arm64_valid, 0xE80,
                     (1 << 27) | (1 << 21) | 4, 0)
    struct.pack_into("<II", arm64_valid, 0xE88,
                     (1 << 27) | (1 << 21) | 4, 0)
    positive_images = (
        ("short data-directory table", bytes(short_directory_table), "x86_64"),
        ("absent optional debug directory", bytes(no_debug), "x86_64"),
        ("allowed type-13 debug directory", bytes(type_13_debug), "x86_64"),
        ("synthetic ARM64 exception directory", bytes(arm64_valid), "arm64"),
    )
    for name, image, expected_machine in positive_images:
        try:
            PEImage(image, "self-test/%s" % name).inspect(
                map_symbols, expected_machine)
        except VerificationError as error:
            failures.append("%s (unexpectedly failed: %r)" % (name, str(error)))

    map_cases = [
        ("missing COFF map symbols", "",
         "COFF map is missing required symbols"),
        ("ambiguous COFF map symbol",
         map_text +
         " 0001:00000018       GuardedTarget              "
         "0000000180001018 f   other.obj\n",
         "COFF map symbol GuardedTarget has ambiguous VAs"),
        ("required symbol ambiguous through absolute row",
         map_text +
         " 0000:00001018       GuardedTarget              "
         "0000000000001018     <absolute>\n",
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

    def mutate_arm64(name: str, expected_error: str,
                     changes: List[Tuple[int, str, int]]) -> None:
        mutated = bytearray(arm64_valid)
        for offset, fmt, value in changes:
            struct.pack_into(fmt, mutated, offset, value)
        arm64_cases.append((name, bytes(mutated), expected_error))

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
    add("too many data directories", optional + 108, "<I", 17,
        "more than 16 PE data directories are not allowed")
    add("forbidden TLS directory",
        optional + 112 + TLS_DIRECTORY * 8, "<I", 0x2000,
        "forbidden TLS data directory is nonzero")
    add("forbidden delay-import directory size",
        optional + 112 + DELAY_IMPORT_DIRECTORY * 8 + 4, "<I", 8,
        "forbidden delay import data directory is nonzero")
    add("forbidden CLR directory",
        optional + 112 + CLR_DIRECTORY * 8, "<I", 0x2000,
        "forbidden CLR data directory is nonzero")
    add("forged certificate directory",
        optional + 112 + CERTIFICATE_DIRECTORY * 8, "<I", 0x200,
        "forbidden certificate data directory is nonzero")

    add("missing exception directory",
        optional + 112 + EXCEPTION_DIRECTORY * 8, "<I", 0,
        "missing exception directory")
    add("misaligned x64 exception directory",
        optional + 112 + EXCEPTION_DIRECTORY * 8 + 4, "<I", 35,
        "malformed exception directory size")
    add("unsorted x64 exception directory", 0xE00 + 12, "<I", 0x1000,
        "exception directory is not strictly sorted")
    mutate("nonexecutable x64 runtime function",
           "runtime-function range is not executable",
           [(0xE00 + 12, "<I", 0x2000),
            (0xE00 + 16, "<I", 0x2010)])
    add("bad x64 unwind information", 0xE00 + 8, "<I", 0x2000,
        "malformed x64 unwind information")
    add("missing exact nonleaf runtime function", 0xE00, "<I", 0x1000,
        "map-bound nonleaf export GuardedIndirectCall lacks one exact")

    add("malformed debug directory size",
        optional + 112 + DEBUG_DIRECTORY * 8 + 4, "<I", 27,
        "malformed debug directory")
    add("disallowed debug directory type", 0xE40 + 12, "<I", 1,
        "debug directory type 1 is not allowed")
    add("debug RVA payload outside image", 0xE40 + 20, "<I", 0x5000,
        "debug RVA payload extends past SizeOfImage")
    add("debug file payload outside file", 0xE40 + 24, "<I", 0xFF0,
        "truncated or out-of-bounds debug file payload")
    add("debug RVA/file payload mismatch", 0xE40 + 24, "<I", 0xE68,
        "debug RVA and file payload locations disagree")

    add("missing IAT directory",
        optional + 112 + IAT_DIRECTORY * 8, "<I", 0,
        "missing IAT directory")
    add("misaligned IAT directory",
        optional + 112 + IAT_DIRECTORY * 8, "<I", 0x2104,
        "malformed PE32+ IAT directory shape")
    add("short IAT directory",
        optional + 112 + IAT_DIRECTORY * 8 + 4, "<I", 4,
        "malformed PE32+ IAT directory shape")
    add("IAT directory outside image",
        optional + 112 + IAT_DIRECTORY * 8, "<I", 0x4FF8,
        "IAT directory extends past SizeOfImage")
    add("standard import outside declared IAT",
        optional + 112 + IAT_DIRECTORY * 8 + 4, "<I", 16,
        "standard import FirstThunk is not an aligned IAT slot")
    add("unterminated standard import IAT",
        optional + 112 + IAT_DIRECTORY * 8 + 4, "<I", 0x188,
        "unterminated standard import IAT")
    mutate_arm64("bad ARM64 xdata header", "malformed ARM64 xdata header",
                 [(0xE80, "<I", 0)])
    mutate_arm64("reserved ARM64 unwind flag",
                 "reserved ARM64 unwind-data flag",
                 [(0xE04, "<I", 3)])
    mutate_arm64("unsorted ARM64 exception directory",
                 "exception directory is not strictly sorted",
                 [(0xE08, "<I", 0x1000)])
    mutate_arm64("nonexecutable ARM64 runtime function",
                 "runtime-function range is not executable",
                 [(0xE08, "<I", 0x2000)])
    mutate_arm64(
        "missing exact ARM64 nonleaf runtime function",
        "map-bound nonleaf export GuardedIndirectCall lacks one exact",
        [(0xE00, "<I", 0x1000)])

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
    add("unexpected CFW instrumentation", 0x600 + 0x90, "<I",
        (CF_INSTRUMENTED | CFW_INSTRUMENTED |
         CF_FUNCTION_TABLE_PRESENT | CF_FUNCTION_TABLE_SIZE_5BYTES),
        "GuardFlags contain unsupported fixture bits 0x200")
    add("unknown GuardFlags bit", 0x600 + 0x90, "<I",
        (CF_INSTRUMENTED | CF_FUNCTION_TABLE_PRESENT |
         CF_FUNCTION_TABLE_SIZE_5BYTES | 0x1),
        "GuardFlags contain unsupported fixture bits 0x1")
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
    add("implicit XFG metadata", 0x600 + 0x90, "<I",
        (CF_INSTRUMENTED | CF_FUNCTION_TABLE_PRESENT |
         CF_FUNCTION_TABLE_SIZE_5BYTES | CF_XFG_ENABLED),
        "/GUARD:MIXED unexpectedly enabled XFG metadata")
    add("invalid GFID", 0x8A0, "<I", 0x2000,
        "GFID 0x2000 is not executable")
    mutate("nonzero undefined GFID metadata", "GFID metadata must be zero",
           [(0x600 + 0x90, "<I", 0x10000500)],
           [(0x8A0, struct.pack("<IBIB", 0x1000, 0x10, 0x1030, 0))])
    add("language-exception-handler GFID", 0x8A4, "<B",
        GFID_FID_LANGEXCPTHANDLER,
        "GFID metadata must be zero")
    add("XFG GFID", 0x8A9, "<B", GFID_FID_XFG,
        "GFID metadata must be zero")
    add("unsorted GFIDs", 0x8A5, "<I", 0x1000,
        "GFID table is not strictly sorted")
    mutate("suppressed internal GFID", "GFID metadata must be zero",
           [(0x600 + 0x88, "<Q", 3)],
           [(0x8A0, struct.pack("<IBIBIB", 0x1000, 0,
                               0x1018, GFID_FID_SUPPRESSED, 0x1030, 0))])
    mutate("export-suppressed internal GFID", "GFID metadata must be zero",
           [(0x600 + 0x88, "<Q", 3)],
           [(0x8A0, struct.pack("<IBIBIB", 0x1000, 0,
                               0x1018, GFID_EXPORT_SUPPRESSED, 0x1030, 0))])
    add("export-suppression GuardFlags", 0x600 + 0x90, "<I",
        0x10000500 | 0x4000,
        "GuardFlags contain unsupported fixture bits 0x4000")
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
    add("GIAT entry outside IAT", 0xBC0, "<I", 0x2300,
        "GIAT entry 0x2300 is not an aligned PE32+ IAT slot")
    add("misaligned GIAT entry", 0xBC0, "<I", 0x2104,
        "GIAT entry 0x2104 is not an aligned PE32+ IAT slot")
    add("unsorted GIAT", 0xBC5, "<I", 0x2100,
        "GIAT is not strictly sorted")
    add("nonzero GIAT metadata", 0xBC4, "<B", 1,
        "GIAT entry has nonzero metadata")

    add("section raw data out of bounds", 0x188 + 20, "<I", 0xF00,
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

    for name, image, expected_error in arm64_cases:
        try:
            PEImage(image, "self-test/%s" % name).inspect(
                map_symbols, "arm64")
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
    relocated_symbols = dict(map_symbols.symbols)
    relocated_symbols["GuardedTarget"] = frozenset({0x180001028})
    relocated_map_symbols = CoffMap(
        relocated_symbols,
        tuple((segment, 0x28, name, 0x180001028)
              if name == "GuardedTarget" else entry
              for entry in map_symbols.entries
              for segment, _, name, _ in (entry,)))
    compare(reference, PEImage(bytes(relocated),
                               "synthetic-relocated-target").inspect(
                                   relocated_map_symbols))

    identity_reference_image = bytearray(valid)
    struct.pack_into("<Q", identity_reference_image, 0x600 + 0x88, 3)
    struct.pack_into("<IBIBIB", identity_reference_image, 0x8A0,
                     0x1000, 0, 0x1030, 0, 0x1050, 0)
    identity_reference = PEImage(
        bytes(identity_reference_image),
        "synthetic-internal-target-a").inspect(map_symbols)
    identity_candidate_image = bytearray(identity_reference_image)
    struct.pack_into("<I", identity_candidate_image, 0x8AA, 0x1080)
    identity_candidate = PEImage(
        bytes(identity_candidate_image),
        "synthetic-internal-target-b").inspect(map_symbols)
    try:
        compare(identity_reference, identity_candidate)
        failures.append("internal GFID replacement (unexpectedly compared equal)")
    except VerificationError as error:
        if "GFID stable symbol identities" not in str(error):
            failures.append("internal GFID replacement (wrong diagnostic: %r)" %
                            str(error))

    identity_relocated_image = bytearray(identity_reference_image)
    struct.pack_into("<I", identity_relocated_image, 0x8AA, 0x1058)
    identity_relocated_symbols = dict(map_symbols.symbols)
    identity_relocated_symbols["InternalTargetA"] = frozenset({0x180001058})
    identity_relocated_map = CoffMap(
        identity_relocated_symbols,
        tuple((segment, 0x58, name, 0x180001058)
              if name == "InternalTargetA" else entry
              for entry in map_symbols.entries
              for segment, _, name, _ in (entry,)))
    try:
        compare(identity_reference, PEImage(
            bytes(identity_relocated_image),
            "synthetic-relocated-internal-target-a").inspect(
                identity_relocated_map))
    except VerificationError as error:
        failures.append("same-name internal GFID relocation failed: %r" %
                        str(error))

    shared_alias_reference_map = parse_coff_map_text(
        map_text +
        " 0001:00000000       SharedGuardAlias           "
        "0000000180001000 f   fixture.obj\n",
        "synthetic-shared-alias-reference.map")
    shared_alias_candidate_map = parse_coff_map_text(
        map_text +
        " 0001:00000018       SharedGuardAlias           "
        "0000000180001018 f   fixture.obj\n",
        "synthetic-shared-alias-candidate.map")
    shared_alias_reference = PEImage(
        valid, "synthetic-shared-alias-reference").inspect(
            shared_alias_reference_map)
    shared_alias_candidate = PEImage(
        valid, "synthetic-shared-alias-candidate").inspect(
            shared_alias_candidate_map)
    try:
        compare(shared_alias_reference, shared_alias_candidate)
        failures.append("shared GFID alias coverage change "
                        "(unexpectedly compared equal)")
    except VerificationError as error:
        if "GFID stable symbol identities" not in str(error):
            failures.append("shared GFID alias coverage change "
                            "(wrong diagnostic: %r)" % str(error))
    try:
        compare(reference, shared_alias_reference)
    except VerificationError as error:
        failures.append("single-sided extra GFID alias failed: %r" % str(error))

    ambiguous_internal_map = parse_coff_map_text(
        map_text +
        " 0001:00000080       InternalTargetA            "
        "0000000180001080 f   other.obj\n",
        "synthetic-ambiguous-internal.map")
    try:
        PEImage(bytes(unnamed_gfid),
                "self-test/ambiguous internal alias").inspect(
                    ambiguous_internal_map)
        failures.append("ambiguous internal alias (unexpectedly passed)")
    except VerificationError as error:
        if "GFID 0x1050 has no globally unique stable COFF map alias" not in str(error):
            failures.append("ambiguous internal alias (wrong diagnostic: %r)" %
                            str(error))

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
                     0x1000, 0, 0x1018, 0, 0x1030, 0)
    extra_gfid_symbols = dict(map_symbols.symbols)
    extra_gfid_symbols["ExtraInternalTarget"] = frozenset({0x180001018})
    extra_gfid_map_symbols = CoffMap(
        extra_gfid_symbols,
        map_symbols.entries +
        ((1, 0x18, "ExtraInternalTarget", 0x180001018),))
    compare_cases.append((
        "extra valid GFID",
        PEImage(bytes(extra_gfid), "self-test/extra valid GFID").inspect(
            extra_gfid_map_symbols),
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
          (direct_negative_count + len(map_cases) + len(cases) +
           len(arm64_cases) + len(compare_cases)))


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
