#!/usr/bin/env python3
"""Rename exactly five Setup GUID identifiers without rewriting COFF records.

Only approved 42-byte identifiers in parsed name strings are replaced. The
second COFF archive index is stably reordered with its paired member indices.
All other bytes stay identical. LLVM 20.1.8 nm/readobj independently inspect
the result; only a fresh staging output is published after the full ABI audit.

The build must exclusively own the output from publication through final
report confirmation. Rollback checks the staging file's identity before unlink,
but that check and unlink are not atomic against arbitrary concurrent writers.
Report I/O recovery is best effort; persistent failure exits unsuccessfully
without claiming that a failed report was durably saved.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import time
import uuid

sys.dont_write_bytecode = True

import HostCoffSymbols as archive_reader
from CoffWeakAliases import parse_resolved_aliases
from SetupGuidSymbols import (GUID_RENAMES, build_rename_map,
                             contains_old_guid_name, is_private_guid_name,
                             is_private_guid_metadata_name)


SCHEMA = "neverc.setup-coff-rewrite.v2"
MAX_OBJECT_BYTES = 512 * 1024 * 1024
MAX_SYMBOLS = 1_000_000
MAX_SECTIONS = 1_000_000
MAX_LINE = 1024 * 1024
MAX_OUTPUT = 4 * 1024 * 1024 * 1024
MAX_ERRORS = 1024 * 1024
COMMAND_TIMEOUT = 900
PUBLICATION_LIMIT = ("Cooperative exclusive output ownership is required from link through final "
                     "report confirmation; rollback lstat and unlink are not atomic against "
                     "arbitrary concurrent replacement.")
# Raw wire bytes from llvmorg-20.1.8 llvm/BinaryFormat/COFF.h, BigObjMagic.
BIGOBJ_MAGIC = bytes.fromhex("c7a1bad1eebaa94baf20faf66aa4dcb8")
MACHINES = {0x8664: "AMD64", 0xAA64: "ARM64"}


def fail(message):
    raise ValueError("Setup COFF rewrite: " + message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_hash(path):
    value = hashlib.sha256()
    with open(path, "rb") as stream:
        while chunk := stream.read(1024 * 1024):
            value.update(chunk)
    return value.hexdigest()


def text_name(data):
    value = data.decode("utf-8", "strict")
    if not value or any(ord(char) <= 32 or ord(char) == 127 for char in value):
        fail("empty or unsupported symbol/member name")
    return value


def diagnostic_name(name):
    """Bound the preview while retaining the complete UTF-8 name identity."""
    encoded = name.encode("utf-8")
    return {"text": name[:96], "chars": len(name), "utf8_bytes": len(encoded),
            "sha256": digest(encoded), "truncated": len(name) > 96}


class Regions:
    """Bound and disjoint all interpreted intervals; reject unknown payloads."""

    def __init__(self, data):
        self.data = data
        self.items = []

    def take(self, offset, size, label):
        if offset < 0 or size < 0 or offset > len(self.data) or size > len(self.data) - offset:
            fail(label + " extends beyond its object")
        if size:
            self.items.append((offset, offset + size, label))
        return self.data[offset:offset + size]

    def finish(self):
        end = 0
        for start, stop, label in sorted(self.items):
            if start < end:
                fail("overlapping object region: " + label)
            if any(self.data[end:start]):
                fail("uninterpreted nonzero object bytes before " + label)
            end = stop
        if any(self.data[end:]):
            fail("uninterpreted nonzero object trailer")


@dataclass(frozen=True)
class CoffSymbol:
    index: int
    name: str
    value: int
    section: int
    kind: int
    storage: int
    aux: tuple[bytes, ...]
    name_at: int

    @property
    def definition(self):
        return self.storage == 2 and (self.section != 0 or self.value != 0)

    @property
    def weak(self):
        return struct.unpack_from("<II", self.aux[0]) if self.storage == 105 else None

    @property
    def indexed(self):
        return self.definition or (self.weak is not None and self.weak[1] == 3)


@dataclass(frozen=True)
class CoffSection:
    name: str
    virtual_size: int
    virtual_address: int
    size: int
    flags: int
    contents: str
    relocations: tuple[tuple[int, int, int], ...]


@dataclass
class CoffObject:
    machine: int
    timestamp: int
    characteristics: int
    bigobj: bool
    symbols: tuple[CoffSymbol, ...]
    sections: tuple[CoffSection, ...]
    sha256: str
    size: int
    guid_storage: tuple[tuple[str, str, int, int], ...]


def inspect_object(data):
    """Validate native normal/bigobj COFF and retain a structural fingerprint.

    Symbol and auxiliary raw indices are preserved, including FILE records.
    Unknown aux layouts and legacy line tables are unsupported. Section bytes
    are hashed in their entirety, including debug, directives and addrsig.
    """
    if not 20 <= len(data) <= MAX_OBJECT_BYTES:
        fail("object size is unsupported")
    regions = Regions(data)
    bigobj = data[:4] == b"\0\0\xff\xff"
    if bigobj:
        header = regions.take(0, 56, "bigobj header")
        values = struct.unpack("<HHHHI16sIIIIIII", header)
        _, _, version, machine, timestamp, magic, size_data, flags, meta_size, meta_at, section_count, symbol_at, symbol_count = values
        if version != 2 or magic != BIGOBJ_MAGIC or any((size_data, flags, meta_size, meta_at)):
            fail("unsupported bigobj/import/LTCG header")
        header_size, symbol_size, characteristics = 56, 20, 0
    else:
        header = regions.take(0, 20, "COFF header")
        machine, section_count, timestamp, symbol_at, symbol_count, optional, characteristics = struct.unpack("<HHIIIHH", header)
        if optional or characteristics & 0x0003:
            fail("PE, executable or stripped COFF is unsupported")
        header_size, symbol_size = 20, 18
    if machine not in MACHINES or not 0 < section_count <= MAX_SECTIONS:
        fail("unsupported machine or section count")
    if not bigobj and section_count > 0xFEFF:
        fail("ordinary COFF section count enters the reserved range")
    if not 0 < symbol_count <= MAX_SYMBOLS or symbol_at == 0:
        fail("missing or excessive COFF symbols")
    section_data = regions.take(header_size, section_count * 40, "section headers")
    symbol_data = regions.take(symbol_at, symbol_count * symbol_size, "symbol table")
    string_at = symbol_at + symbol_count * symbol_size
    string_size = struct.unpack("<I", regions.take(string_at, 4, "string table size"))[0]
    if string_size < 4:
        fail("invalid string table size")
    strings = regions.take(string_at + 4, string_size - 4, "string table")

    def string(offset):
        if not 4 <= offset < string_size:
            fail("invalid string table offset")
        end = strings.find(b"\0", offset - 4)
        if end < 0:
            fail("unterminated string table name")
        return text_name(strings[offset - 4:end])

    def short_name(raw):
        name, separator, tail = raw.partition(b"\0")
        if separator and any(tail):
            fail("nonzero short-name padding")
        return text_name(name)

    symbols = []
    index = 0
    while index < symbol_count:
        raw = symbol_data[index * symbol_size:(index + 1) * symbol_size]
        if raw[:4] == b"\0" * 4:
            name_offset = struct.unpack_from("<I", raw, 4)[0]
            name = string(name_offset)
            name_at = string_at + name_offset
        else:
            name, name_at = short_name(raw[:8]), symbol_at + index * symbol_size
        value, section, kind, storage, aux_count = struct.unpack_from("<IiHBB" if bigobj else "<IHHBB", raw, 8)
        if not bigobj and section > 0xFEFF:
            if section not in (0xFFFE, 0xFFFF):
                fail("reserved ordinary COFF symbol section number")
            section -= 0x10000
        if section < -2 or section > section_count or aux_count > symbol_count - index - 1:
            fail("invalid symbol section or auxiliary count")
        auxiliary = []
        for number in range(aux_count):
            begin = (index + number + 1) * symbol_size
            payload = symbol_data[begin:begin + symbol_size]
            if bigobj and storage != 103 and any(payload[18:]):
                fail("nonzero bigobj auxiliary padding")
            auxiliary.append(payload if storage == 103 else payload[:18])
        if storage not in (2, 3, 6, 103, 105):
            fail("unsupported symbol storage class " + str(storage))
        if storage == 6 and (section <= 0 or aux_count):
            fail("unsupported label symbol")
        if storage == 2 and section == -2:
            fail("external symbol cannot use the debug section")
        if storage == 103:
            # Never trim FILE padding or recompute its original slot count.
            filename = b"".join(auxiliary).rstrip(b"\0")
            if section != -2 or kind or value or not filename or b"\0" in filename:
                fail("unsupported FILE auxiliary record")
        elif auxiliary:
            if aux_count != 1:
                fail("unsupported multiple auxiliary records")
            if storage == 105:
                target, search = struct.unpack_from("<II", auxiliary[0])
                # COFF weak functions use NULL base type with FUNCTION (2 << 4),
                # as in LLVM 20.1.8 lld/test/COFF/weak-external{,2}.test.
                if (section or value or kind not in (0, 0x20) or
                        search not in (1, 2, 3) or any(auxiliary[0][8:])):
                    # aux_count is exactly one, so payload retains that complete
                    # 18- or 20-byte wire slot after the earlier record checks.
                    rejected_fields = [field for field, rejected in (
                        ("section", section != 0), ("value", value != 0),
                        ("type", kind not in (0, 0x20)), ("search", search not in (1, 2, 3)),
                        ("reserved", any(auxiliary[0][8:]))) if rejected]
                    details = {
                        "symbol_index": index, "symbol_name": diagnostic_name(name),
                        "storage": storage, "aux_count": aux_count,
                        "section": section, "value": value, "type": kind,
                        "target": target, "search": search,
                        "aux_record_bytes": len(payload), "aux_hex": payload.hex(),
                        "rejected_fields": rejected_fields,
                    }
                    fail("unsupported weak external auxiliary record; weak_aux=" +
                         json.dumps(details, ensure_ascii=True, sort_keys=True))
            elif storage == 3 and section > 0 and kind == 0 and value == 0:
                length, relocs, lines, checksum, low, selection, reserved, high = struct.unpack("<IHHIHBBH", auxiliary[0])
                target = low | high << 16
                if lines or reserved or selection not in range(8):
                    fail("unsupported section auxiliary record")
                if selection == 5 and not 1 <= target <= section_count:
                    fail("invalid associative COMDAT target")
                if selection != 5 and target not in (0, section):
                    fail("unsupported non-associative section auxiliary Number")
            else:
                fail("unsupported function/token auxiliary record")
        elif storage in (103, 105):
            fail("missing required auxiliary record")
        if is_private_guid_metadata_name(name) and (storage != 3 or section <= 0 or aux_count):
            fail("private GUID metadata has external or unsupported linkage")
        symbols.append(CoffSymbol(index, name, value, section, kind, storage, tuple(auxiliary), name_at))
        index += 1 + aux_count
    by_index = {symbol.index: symbol for symbol in symbols}
    for symbol in symbols:
        if symbol.weak:
            target = by_index.get(symbol.weak[0])
            if target is None or target.storage not in (2, 3, 105):
                fail("weak target is not a supported primary symbol")
            if target.storage == 3 and target.section <= 0:
                fail("weak local target has no definition")

    sections = []
    section_locations = []
    for number in range(section_count):
        raw = section_data[number * 40:(number + 1) * 40]
        name_raw = raw[:8]
        if name_raw.startswith(b"/"):
            encoded = name_raw[1:].rstrip(b"\0")
            if not encoded or any(byte not in b"0123456789" for byte in encoded):
                fail("unsupported section-name offset encoding")
            name = string(int(encoded))
        else:
            name = short_name(name_raw)
        virtual_size, address, size, data_at, reloc_at, line_at, reloc_count, line_count, flags = struct.unpack_from("<IIIIIIHHI", raw, 8)
        if virtual_size or address or line_at or line_count:
            fail("non-relocatable section or legacy line-number table")
        if flags & 0x00F00000 == 0x00F00000:
            fail("invalid section alignment")
        if data_at:
            contents = regions.take(data_at, size, "section " + name)
        elif size:
            if not flags & 0x80:
                fail("initialized section has no contents")
            contents = b""
        else:
            contents = b""
        section_locations.append(data_at)
        if name == ".drectve":
            directive = contents.decode("utf-8", "strict")
            if contains_old_guid_name(directive) or any(new in directive for new in GUID_RENAMES.values()):
                fail("selected GUID name in unhandled linker directive")
        if name.startswith((".llvm.lto", ".llvmbc")):
            fail("embedded LLVM bitcode is unsupported")
        if flags & 0x01000000:
            if reloc_count != 0xFFFF or not reloc_at:
                fail("invalid relocation overflow header")
            count, sentinel_index, sentinel_type = struct.unpack("<IIH", regions.take(reloc_at, 10, "relocation overflow"))
            if count < 0x10000 or sentinel_index or sentinel_type:
                fail("invalid relocation overflow sentinel")
            reloc_count, reloc_at = count - 1, reloc_at + 10
        elif bool(reloc_count) != bool(reloc_at):
            fail("inconsistent relocation pointer/count")
        relocation_data = regions.take(reloc_at, reloc_count * 10, "relocations " + name) if reloc_count else b""
        relocations = tuple(struct.iter_unpack("<IIH", relocation_data))
        for offset, target, kind in relocations:
            if target not in by_index or offset >= size or kind > (16 if machine == 0x8664 else 17):
                fail("invalid relocation offset, type or primary-symbol index")
        sections.append(CoffSection(name, virtual_size, address, size, flags, digest(contents), relocations))
    section_definitions = {}
    for symbol in symbols:
        if symbol.section > 0 and symbol.value > sections[symbol.section - 1].size:
            fail("symbol value is outside its section")
        if symbol.storage == 3 and symbol.aux:
            section = sections[symbol.section - 1]
            length, count = struct.unpack_from("<IH", symbol.aux[0])
            selection = symbol.aux[0][14]
            if symbol.name != section.name or length != section.size:
                fail("section auxiliary identity/length mismatch")
            if count != min(len(section.relocations), 0xFFFF):
                fail("section auxiliary relocation count mismatch")
            if bool(selection) != bool(section.flags & 0x1000):
                fail("COMDAT selection/section flag mismatch")
            if symbol.section in section_definitions:
                fail("duplicate section auxiliary definition")
            section_definitions[symbol.section] = symbol
    for number, section in enumerate(sections, 1):
        if section.flags & 0x1000 and number not in section_definitions:
            fail("COMDAT section has no auxiliary definition")
    for number, symbol in section_definitions.items():
        active = set()
        while symbol.aux[0][14] == 5:
            if symbol.section in active:
                fail("associative COMDAT cycle")
            if len(active) >= 256:
                fail("associative COMDAT chain exceeds its limit")
            active.add(symbol.section)
            low = struct.unpack_from("<H", symbol.aux[0], 12)[0]
            high = struct.unpack_from("<H", symbol.aux[0], 16)[0]
            parent = low | high << 16
            symbol = section_definitions.get(parent)
            if symbol is None or not symbol.aux[0][14]:
                fail("associative COMDAT has no COMDAT parent")
    guid_storage = []
    original_names = {new: old for old, new in GUID_RENAMES.items()}
    for symbol in symbols:
        original = symbol.name if symbol.name in GUID_RENAMES else original_names.get(symbol.name)
        if original is None or not symbol.definition:
            continue
        if symbol.storage != 2 or symbol.kind or symbol.section <= 0:
            fail("GUID storage is not an external data definition")
        section = sections[symbol.section - 1]
        address = section_locations[symbol.section - 1]
        if section.relocations:
            fail("GUID storage section has link-time relocations")
        if (not address or not section.flags & 0x40 or section.flags & (0x20 | 0x80000000)
                or not section.flags & 0x40000000 or symbol.value > section.size - 16):
            fail("GUID storage is not sixteen readable constant data bytes")
        value = data[address + symbol.value:address + symbol.value + 16]
        expected = uuid.UUID(original[6:].replace("_", "-")).bytes_le
        if value != expected:
            fail("GUID storage bytes differ from its declared identifier")
        guid_storage.append((symbol.name, value.hex(), symbol.section, symbol.index))
    regions.finish()
    return CoffObject(machine, timestamp, characteristics, bigobj, tuple(symbols), tuple(sections), digest(data), len(data), tuple(guid_storage))


@dataclass
class ArchiveSnapshot:
    machine: int
    members: list[tuple[str, CoffObject]]
    sha256: str
    size: int
    member_offsets: tuple[int, ...] = ()
    first_index: tuple[tuple[str, int, int], ...] = ()
    second_index: tuple[tuple[str, int, int], ...] = ()
    second_tail_at: int = 0
    second_tail: bytes = b""
    second_padding: bytes = b""


def _inspect_archive_stream(stream, size):
    """Reuse the archive parser, then bind index representatives to definitions.

    LLVM20 ArchiveWriter::getSymbols uses try_emplace(Name, Index), retaining
    the first representative of duplicate COMDAT definitions. Thus every
    eligible *name* needs a representative, not every duplicate definition.
    Both indices must have equal (name, member-offset) Counters here. Preserve
    that exact representative selection and multiplicity through the rename.
    This is stricter than the existing index reader's set-consistency check.
    """
    archive_reader._defined_symbols(stream, size)
    entries = archive_reader._members(stream, size)
    start = 3 if entries[2].name == b"//" else 2
    long_names = archive_reader._table(stream, size, entries[2]) if start == 3 else b""
    objects = entries[start:]
    if len(objects) >= 0xFFFF:
        fail("archive exceeds the supported COFF member limit")
    members, actual, machines = [], set(), set()
    for ordinal, entry in enumerate(objects):
        raw_name = entry.name
        if raw_name.startswith(b"/"):
            begin = int(raw_name[1:])
            raw_name = long_names[begin:long_names.index(b"\0", begin)]
        else:
            raw_name = raw_name[:-1]
        name = text_name(raw_name)
        if entry.size > MAX_OBJECT_BYTES:
            fail("member exceeds object limit: " + name)
        data = archive_reader._read(stream, size, entry.offset + 60, entry.size)
        try:
            obj = inspect_object(data)
        except ValueError as error:
            fail("member " + str(ordinal) + " name=" +
                 json.dumps(diagnostic_name(name), ensure_ascii=True, sort_keys=True) +
                 ": " + str(error))
        members.append((name, obj))
        machines.add(obj.machine)
        actual.update((symbol.name, entry.offset) for symbol in obj.symbols if symbol.indexed)
    if len(machines) != 1:
        fail("mixed archive machines")

    first = archive_reader._Cursor(archive_reader._table(stream, size, entries[0]))
    count = first.u32(">")
    offsets = first.array(count, ">I", 4)
    name_at = entries[0].offset + 60 + first.position
    names = first.names(count)
    first_index = []
    for raw_name, offset in zip(names, offsets):
        first_index.append((text_name(raw_name), offset, name_at))
        name_at += len(raw_name) + 1

    second_data = archive_reader._table(stream, size, entries[1])
    second = archive_reader._Cursor(second_data)
    member_count = second.u32("<")
    member_offsets = second.array(member_count, "<I", 4)
    second_count = second.u32("<")
    tail_at = second.position
    indices = second.array(second_count, "<H", 2)
    name_at = entries[1].offset + 60 + second.position
    second_names = second.names(second_count)
    second_index = []
    for raw_name, member_index in zip(second_names, indices):
        second_index.append((text_name(raw_name), member_offsets[member_index - 1], name_at))
        name_at += len(raw_name) + 1
    names_end = name_at - entries[1].offset - 60
    first_pairs = Counter((name, offset) for name, offset, _ in first_index)
    second_pairs = Counter((name, offset) for name, offset, _ in second_index)
    if first_pairs != second_pairs:
        fail("linker index representative/multiplicity mismatch")
    if not first_pairs.keys() <= actual or {name for name, _ in actual} != {name for name, _ in first_pairs}:
        fail("archive index does not describe actual member definitions")
    stream.seek(0)
    value = hashlib.sha256()
    while chunk := stream.read(1024 * 1024):
        value.update(chunk)
    return ArchiveSnapshot(machines.pop(), members, value.hexdigest(), size,
                           tuple(member_offsets), tuple(first_index), tuple(second_index),
                           entries[1].offset + 60 + tail_at,
                           second_data[tail_at:], second_data[names_end:])


def inspect_archive(path):
    with Path(path).open("rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode):
            fail("input is not a regular archive")
        result = _inspect_archive_stream(stream, before.st_size)
        after = os.fstat(stream.fileno())
        if (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (after.st_size, after.st_mtime_ns, after.st_ctime_ns):
            fail("archive changed during structural inspection")
        return result


def inspect_archive_bytes(data):
    return _inspect_archive_stream(io.BytesIO(data), len(data))


def build_mapping(snapshot):
    symbols = [symbol for _, obj in snapshot.members for symbol in obj.symbols]
    definitions = {symbol.name for symbol in symbols if symbol.definition and symbol.section > 0}
    if set(GUID_RENAMES) - definitions:
        fail("missing original Setup GUID data definition: " + ", ".join(sorted(set(GUID_RENAMES) - definitions)))
    mapping = build_rename_map(symbol.name for symbol in symbols)
    if any(mapping.get(old) != new for old, new in GUID_RENAMES.items()):
        fail("incomplete five-GUID mapping")
    for symbol in symbols:
        if is_private_guid_metadata_name(mapping.get(symbol.name, symbol.name)):
            if symbol.storage != 3 or symbol.section <= 0 or symbol.aux:
                fail("GUID metadata has external or unsupported linkage")
    return mapping


@dataclass(frozen=True)
class NamePatch:
    offset: int
    old: bytes
    new: bytes
    kind: str


def _name_patches(name, offset, mapping):
    if name not in mapping:
        return []
    changed, patches = name, []
    for old, new in GUID_RENAMES.items():
        if (len(old) != 42 or len(new) != 42 or not old.isascii() or not new.isascii()
                or new != "neverc_cpp" + old[6:].replace("_", "")):
            fail("GUID policy is not the approved 42-byte spelling")
        start = 0
        while True:
            position = name.find(old, start)
            if position < 0:
                break
            # Prefixes in the approved complete manglings are ASCII. Calculate
            # byte offsets explicitly rather than assuming all names are ASCII.
            at = offset + len(name[:position].encode("utf-8"))
            patches.append(NamePatch(at, old.encode("ascii"), new.encode("ascii"), "GUID name"))
            start = position + len(old)
        changed = changed.replace(old, new)
    if not patches or changed != mapping[name] or len(name.encode("utf-8")) != len(changed.encode("utf-8")):
        fail("mapping is not exactly the approved equal-width GUID substitution")
    return patches


def _merge_patches(patches):
    """Merge shared string suffix writes only when every shared byte agrees."""
    merged = []
    for patch in sorted(patches, key=lambda item: item.offset):
        if patch.offset < 0 or len(patch.old) != len(patch.new) or not patch.old:
            fail("invalid equal-width patch interval")
        if not merged or patch.offset >= merged[-1].offset + len(merged[-1].old):
            merged.append(patch)
            continue
        previous = merged.pop()
        begin = patch.offset - previous.offset
        overlap = min(len(previous.old) - begin, len(patch.old))
        if (previous.old[begin:begin + overlap] != patch.old[:overlap]
                or previous.new[begin:begin + overlap] != patch.new[:overlap]):
            fail("overlapping name patches conflict")
        merged.append(NamePatch(previous.offset, previous.old + patch.old[overlap:],
                                previous.new + patch.new[overlap:], previous.kind))
    return merged


def _patch_plan(snapshot, mapping):
    if mapping != build_mapping(snapshot):
        fail("mapping does not cover the complete original symbol inventory")
    patches = []
    for member_offset, (_, obj) in zip(snapshot.member_offsets, snapshot.members):
        for symbol in obj.symbols:
            patches.extend(_name_patches(symbol.name, member_offset + 60 + symbol.name_at, mapping))
    for name, _, name_at in snapshot.first_index:
        patches.extend(_name_patches(name, name_at, mapping))

    # Sort by raw UTF-8 bytes, as the COFF index requires. Python's stable sort
    # preserves the original order of equal names, including duplicated pairs.
    # Never change a selected definition's member representative or drop a tie.
    pairs = [(mapping.get(name, name), offset) for name, offset, _ in snapshot.second_index]
    pairs.sort(key=lambda pair: pair[0].encode("utf-8"))
    ordinals = {offset: number + 1 for number, offset in enumerate(snapshot.member_offsets)}
    tail = b"".join(struct.pack("<H", ordinals[offset]) for _, offset in pairs)
    tail += b"".join(name.encode("utf-8") + b"\0" for name, _ in pairs)
    tail += snapshot.second_padding
    if len(tail) != len(snapshot.second_tail):
        fail("second-index stable sort changed its serialized size")
    patches.append(NamePatch(snapshot.second_tail_at, snapshot.second_tail, tail, "second index pairs"))
    return _merge_patches(patches)


def _verify_byte_streams(before, after, size, patches):
    """Compare every byte, allowing exactly the computed bounded name edits."""
    position = 0
    changed = 0

    def unchanged(stop):
        nonlocal position
        while position < stop:
            amount = min(1024 * 1024, stop - position)
            left, right = before.read(amount), after.read(amount)
            if len(left) != amount or len(right) != amount or left != right:
                fail("unapproved archive byte difference at " + str(position))
            position += amount

    before.seek(0)
    after.seek(0)
    for patch in patches:
        if patch.offset < position or patch.offset + len(patch.old) > size:
            fail("patch range is outside its archive")
        unchanged(patch.offset)
        if before.read(len(patch.old)) != patch.old or after.read(len(patch.new)) != patch.new:
            fail("name/index patch bytes do not match the approved plan")
        changed += sum(left != right for left, right in zip(patch.old, patch.new))
        position += len(patch.old)
    unchanged(size)
    if before.read(1) or after.read(1):
        fail("archive length changed")
    return {"status": "passed", "compared_bytes": size,
            "patch_intervals": len(patches), "changed_bytes": changed,
            "allowed_changes": "five equal-width GUID names; stable second-index name/member pairs"}


def verify_rewrite_bytes(before, after, mapping):
    """Independent full-byte and parsed-name proof, useful for CI fixtures."""
    original = inspect_archive_bytes(before)
    transformed = inspect_archive_bytes(after)
    proof = _verify_byte_streams(io.BytesIO(before), io.BytesIO(after), len(before),
                                 _patch_plan(original, mapping))
    verify_archive_pair(original, transformed, mapping)
    return proof


def rewrite_archive_bytes(data, mapping):
    """In-memory fixture entry point for the same restricted production plan."""
    snapshot = inspect_archive_bytes(data)
    output = bytearray(data)
    for patch in _patch_plan(snapshot, mapping):
        if data[patch.offset:patch.offset + len(patch.old)] != patch.old:
            fail("input name/index bytes disagree with the parsed inventory")
        output[patch.offset:patch.offset + len(patch.new)] = patch.new
    result = bytes(output)
    verify_rewrite_bytes(data, result, mapping)
    return result


def verify_archive_pair(before, after, mapping):
    """Require identical records, indices and metadata except approved names."""
    if before.machine != after.machine or len(before.members) != len(after.members):
        fail("archive machine/member count changed")
    if before.size != after.size or before.member_offsets != after.member_offsets:
        fail("archive length or member offsets changed")
    if before.first_index:
        expected_first = [(mapping.get(name, name), offset, at) for name, offset, at in before.first_index]
        if expected_first != list(after.first_index):
            fail("first-index order/representative/multiplicity changed")
        expected_second = [(mapping.get(name, name), offset) for name, offset, _ in before.second_index]
        expected_second.sort(key=lambda pair: pair[0].encode("utf-8"))
        if expected_second != [(name, offset) for name, offset, _ in after.second_index]:
            fail("second-index stable pairs/duplicate multiplicity changed")
    report = []
    for ordinal, ((old_name, old), (new_name, new)) in enumerate(zip(before.members, after.members)):
        label = "member " + str(ordinal) + " " + old_name
        if old_name != new_name:
            fail(label + ": member name/order changed")
        if (old.machine, old.timestamp, old.characteristics, old.bigobj) != (new.machine, new.timestamp, new.characteristics, new.bigobj):
            fail(label + ": header identity or object-format normalization")
        if old.sections != new.sections:
            fail(label + ": section bytes/flags/order/relocations changed")
        if len(old.symbols) != len(new.symbols):
            fail(label + ": symbol count changed")
        hits = Counter()
        for left, right in zip(old.symbols, new.symbols):
            expected = mapping.get(left.name, left.name)
            if right.name != expected:
                fail(label + ": incomplete/unexpected rename of " + left.name)
            if (left.index, left.value, left.section, left.kind, left.storage, left.aux, left.name_at) != (right.index, right.value, right.section, right.kind, right.storage, right.aux, right.name_at):
                fail(label + ": symbol/auxiliary/raw-index change at " + str(left.index))
            if contains_old_guid_name(right.name):
                fail(label + ": original GUID/template name remains")
            if left.name in mapping:
                hits[left.name] += 1
        if not hits and old.sha256 != new.sha256:
            fail(label + ": unselected member bytes changed")
        report.append({"ordinal": ordinal, "name": old_name, "input_sha256": old.sha256,
                       "output_sha256": new.sha256, "byte_identical": old.sha256 == new.sha256,
                       "symbol_count": len(old.symbols), "section_count": len(old.sections),
                       "input_guid_storage": old.guid_storage, "output_guid_storage": new.guid_storage,
                       "hits": dict(sorted(hits.items())), "structural_checks": "passed"})
    hits = Counter()
    for member in report:
        hits.update(member["hits"])
    if set(hits) != set(mapping):
        fail("rename map has unobserved entries")
    return report, dict(sorted(hits.items()))


def _run(command, report, consume=None, timeout=COMMAND_TIMEOUT):
    record = {"argv": list(map(str, command)), "status": "started"}
    report["commands"].append(record)
    with tempfile.TemporaryFile() as stdout, tempfile.TemporaryFile() as stderr:
        started = time.monotonic()
        try:
            process = subprocess.Popen(record["argv"], stdout=stdout, stderr=stderr)
        except OSError as error:
            record["status"], record["error"] = "failed", str(error)
            raise
        try:
            while True:
                if os.fstat(stdout.fileno()).st_size > MAX_OUTPUT or os.fstat(stderr.fileno()).st_size > MAX_ERRORS:
                    fail("tool output exceeds limit")
                if time.monotonic() - started >= timeout:
                    fail("tool command timed out")
                try:
                    code = process.wait(timeout=0.2)
                    break
                except subprocess.TimeoutExpired:
                    pass
        except (OSError, ValueError):
            record["status"] = "failed"
            raise
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            record["returncode"] = process.returncode
            record["seconds"] = round(time.monotonic() - started, 3)
            record["stdout_bytes"] = os.fstat(stdout.fileno()).st_size
            record["stderr_bytes"] = os.fstat(stderr.fileno()).st_size
        if record["stdout_bytes"] > MAX_OUTPUT or record["stderr_bytes"] > MAX_ERRORS:
            fail("tool output exceeds limit")
        stdout.seek(0)
        value = hashlib.sha256()
        while chunk := stdout.read(1024 * 1024):
            value.update(chunk)
        record["stdout_sha256"] = value.hexdigest()
        stderr.seek(0)
        error = stderr.read(MAX_ERRORS + 1).decode("utf-8", "strict")
        record["stderr"] = error
        if code or error.strip():
            record["status"] = "failed"
            stdout.seek(0)
            record["stdout_diagnostic"] = stdout.read(MAX_ERRORS).decode("utf-8", "replace")
            record["stdout_diagnostic_truncated"] = record["stdout_bytes"] > MAX_ERRORS
            fail("tool command failed: " + str(command[0]) + ": " + error[:4096])
        stdout.seek(0)
        try:
            result = consume(stdout) if consume else None
        except (OSError, ValueError, UnicodeError) as error:
            record["status"], record["error"] = "validation_failed", str(error)
            raise
        record["status"] = "passed"
        return result


def _lines(stream):
    while line := stream.readline(MAX_LINE + 1):
        if len(line) > MAX_LINE:
            fail("tool line exceeds limit")
        yield line.decode("utf-8", "strict")


def verify_native_readers(nm, readobj, archive, snapshot, report):
    symbols = [symbol for _, obj in snapshot.members for symbol in obj.symbols]
    external = [symbol for symbol in symbols if symbol.storage in (2, 105)]
    expected = Counter((symbol.name, "D" if symbol.indexed else "U") for symbol in external)

    def nm_rows(stream):
        observed = Counter()
        for raw in _lines(stream):
            line = raw.strip()
            if not line or line.endswith(":"):
                continue
            fields = line.split()
            if len(fields) != 4 or len(fields[1]) != 1:
                fail("unexpected llvm-nm POSIX record")
            observed[(fields[0], "U" if fields[1] in ("U", "w", "v") else "D")] += 1
        if observed != expected:
            fail("native nm and structural symbol inventories differ")

    _run([nm, "--extern-only", "--no-demangle", "--format=posix", archive], report, nm_rows)
    definitions = {symbol.name for symbol in external if symbol.definition}
    private = {symbol.name for symbol in external if is_private_guid_name(symbol.name)}
    resolved = _run([readobj, "--symbols", "--no-demangle", archive], report,
                    lambda stream: parse_resolved_aliases(_lines(stream), definitions, private))
    required = private - definitions - resolved
    if required:
        fail("private GUID/template reference has no private definition: " + ", ".join(sorted(required)))
    report["native_readers"] = {"status": "passed", "external_records": sum(expected.values()),
                                "private_names": len(private), "resolved_aliases": len(resolved)}


def transform(args, report):
    source, output = args.input.resolve(strict=True), args.output.absolute()
    if not output.parent.is_dir() or output.exists() or output.is_symlink():
        fail("output must be a fresh path in an existing directory")
    if source == output.resolve() or source == args.report.resolve():
        fail("input/output/report paths must differ")
    tools = {}
    for name in ("nm", "readobj"):
        path = getattr(args, name).resolve(strict=True)
        if not path.is_file() or path in (source, output.resolve(), args.report.resolve()):
            fail("invalid or overlapping tool path")
        version = _run([path, "--version"], report,
                       lambda stream: stream.read(65537).decode("utf-8", "strict"), timeout=30)
        if len(version) > 65536 or not re.search(r"\bLLVM version 20\.1\.8(?:\s|$)", version):
            fail(name + " must be pinned LLVM 20.1.8")
        tools[name] = path
        report["tools"][name] = {"path": str(path), "version": version.strip(), "sha256": file_hash(path)}
    before = inspect_archive(source)
    report["input"] = {"path": str(source), "sha256": before.sha256, "size": before.size}
    report["machine"] = MACHINES[before.machine]
    report["member_count"] = len(before.members)
    mapping = build_mapping(before)
    report["mapping"] = dict(sorted(mapping.items()))
    patches = _patch_plan(before, mapping)
    with tempfile.TemporaryDirectory(prefix="setup-coff-", dir=output.parent) as directory:
        temporary = Path(directory)
        staged = temporary / "rewritten.lib"
        shutil.copyfile(source, staged)
        if file_hash(staged) != before.sha256:
            fail("input changed while copying the staging archive")
        with staged.open("r+b") as stream:
            for patch in patches:
                stream.seek(patch.offset)
                if stream.read(len(patch.old)) != patch.old:
                    fail("staged name/index bytes disagree with the approved plan")
                stream.seek(patch.offset)
                stream.write(patch.new)
            stream.flush()
            os.fsync(stream.fileno())
        after = inspect_archive(staged)
        with source.open("rb") as original, staged.open("rb") as transformed:
            report["byte_proof"] = _verify_byte_streams(original, transformed, before.size, patches)
        report["members"], report["hits"] = verify_archive_pair(before, after, mapping)
        verify_native_readers(tools["nm"], tools["readobj"], staged, after, report)
        auditor = Path(__file__).resolve().with_name("AuditArchive.py")
        report["auditor"] = {"path": str(auditor), "sha256": file_hash(auditor)}
        report["audit_output"] = _run(
            [sys.executable, "-B", auditor, "--nm", tools["nm"], "--archive", staged,
             "--coff-readobj", tools["readobj"]], report,
            lambda stream: stream.read(MAX_ERRORS + 1).decode("utf-8", "strict"))
        if len(report["audit_output"].encode("utf-8")) > MAX_ERRORS:
            fail("ABI audit report exceeds its limit")
        if file_hash(source) != before.sha256 or file_hash(staged) != after.sha256:
            fail("input or staging archive changed during transaction")
        for name, path in tools.items():
            if file_hash(path) != report["tools"][name]["sha256"]:
                fail("pinned tool changed during transaction")
        # Same-filesystem hard link publishes exclusively; unlike replace or
        # rename this cannot overwrite a file created by a concurrent build.
        # Read identity from our staging file before link: looking up the output
        # afterwards could observe a file another actor has already substituted.
        identity = staged.stat()
        report["published_identity"] = {"device": identity.st_dev, "inode": identity.st_ino}
        os.link(staged, output)
        report["published"] = True
        report["output"] = {"path": str(output), "sha256": after.sha256, "size": after.size}
        report["status"] = "passed"


def _rollback_output(output, report):
    """Remove only the observed staging identity, under PUBLICATION_LIMIT."""
    if not report["published"]:
        report["rollback"] = {"status": "not-published"}
        return
    expected = report.get("published_identity")
    if expected is None:
        report["rollback"] = {"status": "identity-unavailable"}
        return
    try:
        observed = output.lstat()  # Do not follow a replacement symlink.
        identity = {"device": observed.st_dev, "inode": observed.st_ino}
        if identity != expected:
            report["published"] = False
            report["rollback"] = {"status": "preserved-replacement", "observed_identity": identity}
            return
        # The caller must exclude concurrent output mutations during this gap.
        output.unlink()
    except FileNotFoundError:
        report["published"] = False
        report["rollback"] = {"status": "already-absent"}
    except OSError as error:
        report["rollback"] = {"status": "failed", "error": str(error)}
    else:
        report["published"] = False
        report["rollback"] = {"status": "removed-owned-output"}


def _write_report(stream, report):
    stream.seek(0)
    stream.truncate()
    stream.write(json.dumps(report, indent=2, sort_keys=True) + "\n")
    stream.flush()
    os.fsync(stream.fileno())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("input", "output", "nm", "readobj", "report"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    report = {"schema": SCHEMA, "status": "started", "tools": {}, "commands": [], "published": False,
              "publication_limit": PUBLICATION_LIMIT}
    if args.report.resolve() in (args.input.resolve(), args.output.resolve()):
        parser.error("report must differ from input/output")
    try:
        # Reserve the report before performing any transaction work. Existing
        # reports are never overwritten, including reports from failed runs.
        with args.report.open("x", encoding="utf-8") as stream:
            try:
                transform(args, report)
            except (OSError, ValueError, UnicodeError, struct.error) as error:
                report["status"], report["error"] = "failed", str(error)
                _rollback_output(args.output, report)
            try:
                _write_report(stream, report)
            except (OSError, ValueError) as error:
                report["status"], report["error"] = "failed", "report persistence failed: " + str(error)
                _rollback_output(args.output, report)
                try:
                    # Keep the exclusively created stream open. A transient
                    # failure may have left a complete but false success JSON.
                    _write_report(stream, report)
                except (OSError, ValueError) as recovery_error:
                    raise SystemExit("Setup COFF rewrite: report durability unconfirmed: " +
                                     str(recovery_error)) from recovery_error
                raise SystemExit("Setup COFF rewrite: report/transaction failed: " + str(error)) from error
    except (OSError, ValueError) as error:
        report["status"], report["error"] = "failed", str(error)
        _rollback_output(args.output, report)
        raise SystemExit("Setup COFF rewrite: report/transaction failed: " + str(error)) from error
    if report["status"] != "passed":
        raise SystemExit(report.get("error", "Setup COFF rewrite failed"))
    print("Setup COFF rewrite: passed; " + str(len(report["mapping"])) + " exact names; " +
          str(report["member_count"]) + " checked members")


if __name__ == "__main__":
    main()
