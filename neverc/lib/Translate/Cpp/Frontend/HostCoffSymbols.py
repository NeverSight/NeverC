"""Read the complete public-definition index of a Microsoft COFF library.

The host librarian's two linker members are authoritative here: MSVC /GL
members are proprietary and cannot be inspected with llvm-nm. Ordinary COFF,
bigobj, LTCG and short/long import objects all use the same archive indices;
their payloads are deliberately opaque. This validates index completeness and
consistency, not the semantic honesty of an independently forged object file.

Format references:
https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#archive-library-file-format
llvm/lib/Object/Archive.cpp: Archive constructor, Symbol::getMember
llvm/lib/Object/ArchiveWriter.cpp: writeSymbolTable, writeSymbolMap,
computeStringTable (including their in-member alignment bytes).

GNU/BSD/thin/64-bit-index archives and EC/hybrid extensions require a different
reader and fail explicitly. An empty index is not evidence for the ABI gate.
"""

from __future__ import annotations

import os
from pathlib import Path
import stat
import struct
from typing import BinaryIO, NamedTuple


# COFF indices use 32-bit file offsets and 16-bit member indices. Additional
# resource limits fail closed rather than allocating from unchecked counts.
_MAX_ARCHIVE_BYTES = 0xFFFFFFFF
_MAX_MEMBERS = 0xFFFF
_MAX_TABLE_BYTES = 128 * 1024 * 1024
_MAX_SYMBOLS = 1_000_000
_HEADER_BYTES = 60


class _Member(NamedTuple):
    offset: int
    name: bytes
    size: int


def _error(message: str) -> ValueError:
    return ValueError("COFF archive index: " + message)


def _read(stream: BinaryIO, file_size: int, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0 or offset > file_size or size > file_size - offset:
        raise _error("read extends beyond the archive")
    stream.seek(offset)
    data = stream.read(size)
    if len(data) != size:
        raise _error("truncated or changing archive")
    return data


def _number(field: bytes, label: str, base: int = 10,
            allow_blank: bool = False) -> int:
    value = field.rstrip(b" ")
    digits = b"01234567" if base == 8 else b"0123456789"
    if not value and allow_blank:
        return 0
    if not value or any(byte not in digits for byte in value):
        raise _error("invalid " + label + " field")
    return int(value, base)


def _members(stream: BinaryIO, file_size: int) -> list[_Member]:
    if _read(stream, file_size, 0, 8) != b"!<arch>\n":
        raise _error("unsupported archive signature (normal COFF required)")
    result = []
    offset = 8
    while offset < file_size:
        if len(result) >= _MAX_MEMBERS + 3:
            raise _error("too many archive members")
        header = _read(stream, file_size, offset, _HEADER_BYTES)
        if header[58:] != b"`\n":
            raise _error("invalid member header terminator")
        name = header[:16].rstrip(b" ")
        if not name or name.startswith(b" ") or b"\0" in name:
            raise _error("invalid archive member name")
        _number(header[16:28], "date", allow_blank=True)
        _number(header[28:34], "user ID", allow_blank=True)
        _number(header[34:40], "group ID", allow_blank=True)
        _number(header[40:48], "mode", base=8, allow_blank=True)
        size = _number(header[48:58], "size")
        body = offset + _HEADER_BYTES
        if size > file_size - body:
            raise _error("member extends beyond the archive")
        result.append(_Member(offset, name, size))
        offset = body + size
        if size & 1:
            if _read(stream, file_size, offset, 1) != b"\n":
                raise _error("invalid member alignment byte")
            offset += 1
    return result


def _table(stream: BinaryIO, file_size: int, member: _Member) -> bytes:
    if member.size > _MAX_TABLE_BYTES:
        raise _error("table exceeds the supported byte limit")
    return _read(stream, file_size, member.offset + _HEADER_BYTES, member.size)


class _Cursor:
    def __init__(self, data: bytes):
        self.data = data
        self.position = 0

    def array(self, count: int, code: str, width: int) -> list[int]:
        if count > (len(self.data) - self.position) // width:
            raise _error("truncated count or offset/index array")
        end = self.position + count * width
        values = [value[0] for value in
                  struct.iter_unpack(code, memoryview(self.data)[self.position:end])]
        self.position = end
        return values

    def u32(self, endian: str) -> int:
        return self.array(1, endian + "I", 4)[0]

    def names(self, count: int) -> list[bytes]:
        # Every nonempty name requires at least one byte and a terminating NUL.
        if count > _MAX_SYMBOLS or count > (len(self.data) - self.position) // 2:
            raise _error("invalid or excessive symbol count")
        names = []
        for _ in range(count):
            end = self.data.find(b"\0", self.position)
            if end < 0:
                raise _error("unterminated symbol name")
            if end == self.position:
                raise _error("empty symbol name")
            names.append(self.data[self.position:end])
            self.position = end + 1
        # LLVM's COFF writer counts a single NUL alignment byte in the table's
        # declared size. The unpadded encoding is also specified by Microsoft.
        tail = self.data[self.position:]
        if tail and not (self.position & 1 and tail == b"\0"):
            raise _error("extra data after the declared symbol names")
        return names


def _long_names(data: bytes) -> set[int]:
    starts = set()
    offset = 0
    while offset < len(data):
        # LLVM includes a final newline in an odd-length longnames member;
        # librarians may use a NUL for the equivalent in-member alignment.
        if offset & 1 and len(data) - offset == 1 and data[offset:] in (b"\n", b"\0"):
            break
        if len(starts) >= _MAX_MEMBERS:
            raise _error("too many long member names")
        end = data.find(b"\0", offset)
        if end < 0 or end == offset:
            raise _error("invalid or unterminated long member name")
        starts.add(offset)
        offset = end + 1
    return starts


def _defined_symbols(stream: BinaryIO, file_size: int) -> set[str]:
    if file_size > _MAX_ARCHIVE_BYTES:
        raise _error("archive exceeds the supported 32-bit-offset size")
    members = _members(stream, file_size)
    if len(members) < 3 or [member.name for member in members[:2]] != [b"/", b"/"]:
        raise _error("both Microsoft COFF linker members are required")

    first_object = 2
    long_names = set()
    if members[2].name == b"//":
        long_names = _long_names(_table(stream, file_size, members[2]))
        first_object = 3
    objects = members[first_object:]
    if not objects or len(objects) > _MAX_MEMBERS:
        raise _error("missing or excessive object members")
    for member in objects:
        if not member.size:
            raise _error("empty object member")
        if member.name.startswith(b"/"):
            # Reject all unsupported special members, including EC/HYBRID maps,
            # rather than silently losing their additional public definitions.
            reference = _number(member.name[1:], "long member name offset")
            if reference not in long_names:
                raise _error("long name offset is not a complete name's start")
        elif (not member.name.endswith(b"/") or len(member.name) == 1
              or b"/" in member.name[:-1] or member.name.startswith(b"#")):
            raise _error("unsupported object member name format")

    object_offsets = [member.offset for member in objects]
    object_offset_set = set(object_offsets)
    first = _Cursor(_table(stream, file_size, members[0]))
    first_count = first.u32(">")
    if not 0 < first_count <= _MAX_SYMBOLS:
        raise _error("missing or excessive public definitions in first index")
    first_offsets = first.array(first_count, ">I", 4)
    first_names = first.names(first_count)
    if any(offset not in object_offset_set for offset in first_offsets):
        raise _error("first index does not point to object member headers")
    if any(left > right for left, right in zip(first_offsets, first_offsets[1:])):
        raise _error("first index member offsets are not ascending")

    second = _Cursor(_table(stream, file_size, members[1]))
    member_count = second.u32("<")
    if member_count != len(objects):
        raise _error("second index does not cover every object member")
    second_offsets = second.array(member_count, "<I", 4)
    if second_offsets != object_offsets:
        raise _error("second index member directory is incomplete or unordered")
    second_count = second.u32("<")
    if not 0 < second_count <= _MAX_SYMBOLS:
        raise _error("missing or excessive public definitions in second index")
    indices = second.array(second_count, "<H", 2)
    second_names = second.names(second_count)
    if any(index == 0 or index > member_count for index in indices):
        raise _error("second index has an invalid 1-based member index")
    if any(left > right for left, right in zip(second_names, second_names[1:])):
        raise _error("second index symbol names are not lexically ordered")

    # A librarian may coalesce duplicate definitions in its preferred index.
    # Preserve every symbol, and require each selected definition to exist in
    # the first index; matching only the counts would miss substitution.
    if set(first_names) != set(second_names):
        raise _error("linker members disagree about the public symbol set")
    first_definitions = set(zip(first_names, first_offsets))
    for name, index in zip(second_names, indices):
        if (name, second_offsets[index - 1]) not in first_definitions:
            raise _error("linker members disagree about a symbol's definition")
    return {name.decode("utf-8", errors="surrogateescape") for name in first_names}


def read_defined_symbols(path: Path) -> set[str]:
    """Return all raw public definitions; reject incomplete/unsupported input.

    Raises ValueError for invalid archives and OSError for I/O failures. The
    archive itself is streamed; only bounded index/name tables enter memory.
    O_NONBLOCK prevents a FIFO masquerading as a library from blocking open.
    """
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NONBLOCK", 0)
    descriptor = os.open(path, flags)
    try:
        with os.fdopen(descriptor, "rb") as stream:
            descriptor = None
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode):
                raise _error("input must be a regular library file")
            symbols = _defined_symbols(stream, before.st_size)
            after = os.fstat(stream.fileno())
            if (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
                    after.st_size, after.st_mtime_ns, after.st_ctime_ns):
                raise _error("archive changed during the audit")
            return symbols
    finally:
        if descriptor is not None:
            os.close(descriptor)
