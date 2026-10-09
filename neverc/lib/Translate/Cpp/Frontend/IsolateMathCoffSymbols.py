#!/usr/bin/env python3
"""Isolate known MSVC math templates while retaining their compiled SDK bodies."""

import argparse
from collections import Counter
from dataclasses import replace
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

from AuditArchive import symbol_rows
from RewriteSetupCoffSymbols import inspect_archive


MATH_RENAMES = {
    "??$log10@H$0A@@@YANH@Z": "??$neverc_cpp_log10@H$0A@@@YANH@Z",
    "??$log10@_J$0A@@@YAN_J@Z": "??$neverc_cpp_log10@_J$0A@@@YAN_J@Z",
    "??$pow@HH$0A@@@YANHH@Z": "??$neverc_cpp_pow@HH$0A@@@YANHH@Z",
    "??$pow@MH$0A@@@YANMH@Z": "??$neverc_cpp_pow@MH$0A@@@YANMH@Z",
    "??$pow@NH$0A@@@YANNH@Z": "??$neverc_cpp_pow@NH$0A@@@YANNH@Z",
}


def fail(message):
    raise ValueError("Math COFF isolation: " + message)


def checked_names(names):
    if set(names) & set(MATH_RENAMES.values()):
        fail("input already contains private math templates")
    unsupported = {name for name in names
                   if name.startswith(("??$log10@", "??$pow@"))
                   and name not in MATH_RENAMES}
    if unsupported:
        fail("unsupported SDK math template signatures: " +
             ", ".join(sorted(unsupported)))
    return {old: new for old, new in MATH_RENAMES.items() if old in names}


def zero_hash(size):
    value = hashlib.sha256()
    block = bytes(65536)
    while size:
        count = min(size, len(block))
        value.update(block[:count])
        size -= count
    return value.hexdigest()


def equivalent_section(before, after):
    if before == after:
        return True
    # LLVM 20 objcopy serializes zero bytes for COFF uninitialized storage.
    # Its size, flags and relocations must remain unchanged. Initialized data,
    # machine code, debug information and every other section stay byte exact.
    uninitialized = before.flags & 0x80 and not before.flags & (0x20 | 0x40)
    empty = hashlib.sha256(b"").hexdigest()
    if not uninitialized or replace(before, contents=after.contents) != after:
        return False
    zeros = zero_hash(before.size)
    return before.contents in (empty, zeros) and after.contents in (empty, zeros)


def verify_pair(before, after, mapping):
    if before.machine != after.machine or len(before.members) != len(after.members):
        fail("archive machine or member count changed")
    hits = Counter()
    for ordinal, ((old_name, old), (new_name, new)) in enumerate(
            zip(before.members, after.members)):
        label = "member " + str(ordinal) + " " + old_name
        if old_name != new_name:
            fail(label + ": name or order changed")
        if (old.machine, old.timestamp, old.characteristics, old.bigobj) != (
                new.machine, new.timestamp, new.characteristics, new.bigobj):
            fail(label + ": object header changed")
        if len(old.sections) != len(new.sections) or any(
                not equivalent_section(left, right)
                for left, right in zip(old.sections, new.sections)):
            fail(label + ": section bytes, flags or relocations changed")
        if len(old.symbols) != len(new.symbols):
            fail(label + ": symbol count changed")
        for left, right in zip(old.symbols, new.symbols):
            if right.name != mapping.get(left.name, left.name):
                fail(label + ": unexpected symbol rename")
            if (left.index, left.value, left.section, left.kind,
                    left.storage, left.aux) != (
                    right.index, right.value, right.section, right.kind,
                    right.storage, right.aux):
                fail(label + ": symbol identity or auxiliary record changed")
            if left.name in mapping:
                hits[left.name] += 1
        if old.guid_storage != new.guid_storage:
            fail(label + ": Setup GUID storage changed")
    for field in ("first_index", "second_index"):
        old_ordinals = {offset: i for i, offset in enumerate(before.member_offsets)}
        new_ordinals = {offset: i for i, offset in enumerate(after.member_offsets)}
        expected = Counter((mapping.get(name, name), old_ordinals[offset])
                           for name, offset, _ in getattr(before, field))
        actual = Counter((name, new_ordinals[offset])
                         for name, offset, _ in getattr(after, field))
        if expected != actual:
            fail(field + " representative or multiplicity changed")
    if set(hits) != set(mapping):
        fail("unobserved rename map entry")
    return dict(sorted(hits.items()))


def serialized_section_number(before, after):
    if (before.storage != 3 or before.section <= 0 or before.kind or before.value
            or len(before.aux) != 1 or len(after.aux) != 1):
        return False
    original, changed = before.aux[0], after.aux[0]
    if (len(original) != 18 or len(changed) != 18
            or original[14] == 5 or original[12:14] != b"\0\0"
            or original[16:18] != b"\0\0"):
        return False
    expected = bytearray(original)
    struct.pack_into("<H", expected, 12, before.section & 0xFFFF)
    struct.pack_into("<H", expected, 16, before.section >> 16)
    return changed == expected


def restore_section_numbers(before, after, mapping, output):
    # LLVM 20 COFFWriter fills a non-associative section definition's Number
    # with its own section index. MSVC writes zero. Restore only that exact
    # serialization change, keeping the final auxiliary comparison byte exact.
    if before.machine != after.machine or len(before.members) != len(after.members):
        fail("archive machine or member count changed")
    restorations, members = [], []
    for ordinal, ((_, old), (name, new)) in enumerate(
            zip(before.members, after.members)):
        symbols = []
        if len(old.symbols) != len(new.symbols):
            fail("member " + str(ordinal) + ": symbol count changed")
        for left, right in zip(old.symbols, new.symbols):
            if left.aux != right.aux and serialized_section_number(left, right):
                restorations.append((ordinal, new.bigobj, left, right))
                right = replace(right, aux=left.aux)
            symbols.append(right)
        members.append((name, replace(new, symbols=tuple(symbols))))
    if not restorations:
        return 0
    # Validate every code byte, relocation, symbol identity and archive index
    # before changing the fresh output. Other auxiliary edits still fail here.
    verify_pair(before, replace(after, members=members), mapping)
    with Path(output).open("r+b") as stream:
        for ordinal, bigobj, left, right in restorations:
            member_at = after.member_offsets[ordinal] + 60
            stream.seek(member_at + (48 if bigobj else 8))
            symbol_at = struct.unpack("<I", stream.read(4))[0]
            aux_at = member_at + symbol_at + (right.index + 1) * (20 if bigobj else 18)
            stream.seek(aux_at)
            if stream.read(18) != right.aux[0]:
                fail("output section auxiliary record changed before restoration")
            stream.seek(aux_at + 12)
            stream.write(left.aux[0][12:14])
            stream.seek(aux_at + 16)
            stream.write(left.aux[0][16:18])
    return len(restorations)


def run(command):
    result = subprocess.run([str(argument) for argument in command],
                            capture_output=True, text=True, timeout=600,
                            encoding="utf-8", errors="replace", check=False)
    if result.returncode:
        fail("tool failed: " + result.stdout + result.stderr)
    return result.stdout


def renamed_object(data, obj, mapping):
    # Retain the original ordinary/bigobj header and every existing byte.
    # Only renamed symbol name slots and the string-table size may change.
    names = sorted({symbol.name for symbol in obj.symbols if symbol.name in mapping})
    if not names:
        return data
    symbol_at, symbol_count = struct.unpack_from("<II", data, 48 if obj.bigobj else 8)
    width = 20 if obj.bigobj else 18
    string_at = symbol_at + symbol_count * width
    string_size = struct.unpack_from("<I", data, string_at)[0]
    string_end = string_at + string_size
    result = bytearray(data[:string_end])
    offsets = {}
    for name in names:
        offsets[name] = len(result) - string_at
        result.extend(mapping[name].encode("utf-8") + b"\0")
    if len(result) + len(data) - string_end > 512 * 1024 * 1024:
        fail("renamed object exceeds object limit")
    for symbol in obj.symbols:
        if symbol.name in offsets:
            struct.pack_into("<II", result, symbol_at + symbol.index * width,
                             0, offsets[symbol.name])
    struct.pack_into("<I", result, string_at, len(result) - string_at)
    result.extend(data[string_end:])
    return bytes(result)


def write_lossless_archive(source, output, before, mapping):
    # Rebuild both linker indices as member offsets move. Preserve duplicate
    # representatives, member headers, long names and untouched object bytes.
    import HostCoffSymbols as archive_reader
    with Path(source).open("rb") as stream:
        entries = archive_reader._members(stream, before.size)
        start = 3 if entries[2].name == b"//" else 2
        ordinals = {offset: i for i, offset in enumerate(before.member_offsets)}
        first_pairs = [(mapping.get(name, name), ordinals[offset])
                       for name, offset, _ in before.first_index]
        second_pairs = sorted((mapping.get(name, name), ordinals[offset])
                              for name, offset, _ in before.second_index)
        first_tail = entries[0].offset + 60 + 4 + 4 * len(first_pairs)
        first_tail += sum(len(name.encode("utf-8")) + 1
                          for name, _, _ in before.first_index)
        first_padding = archive_reader._read(
            stream, before.size, first_tail,
            entries[0].offset + 60 + entries[0].size - first_tail)
        sizes = [entry.size for entry in entries]
        sizes[0] = 4 + 4 * len(first_pairs) + len(first_padding)
        sizes[0] += sum(len(name.encode("utf-8")) + 1 for name, _ in first_pairs)
        sizes[1] = 8 + 4 * len(before.members) + 2 * len(second_pairs)
        sizes[1] += sum(len(name.encode("utf-8")) + 1 for name, _ in second_pairs)
        sizes[1] += len(before.second_padding)
        for i, (_, obj) in enumerate(before.members):
            names = {symbol.name for symbol in obj.symbols if symbol.name in mapping}
            sizes[start + i] += sum(len(mapping[name].encode("utf-8")) + 1 for name in names)
        offsets, cursor = [], 8
        for size in sizes:
            offsets.append(cursor)
            cursor += 60 + size + (size & 1)
        if cursor > 0xFFFFFFFF:
            fail("renamed archive exceeds COFF offset limit")
        member_offsets = offsets[start:]
        first = struct.pack(">I", len(first_pairs))
        first += b"".join(struct.pack(">I", member_offsets[i]) for _, i in first_pairs)
        first += b"".join(name.encode("utf-8") + b"\0" for name, _ in first_pairs) + first_padding
        second = struct.pack("<I", len(member_offsets))
        second += b"".join(struct.pack("<I", offset) for offset in member_offsets)
        second += struct.pack("<I", len(second_pairs))
        second += b"".join(struct.pack("<H", i + 1) for _, i in second_pairs)
        second += b"".join(name.encode("utf-8") + b"\0" for name, _ in second_pairs)
        second += before.second_padding
        with Path(output).open("xb") as target:
            target.write(b"!<arch>\n")
            for i, entry in enumerate(entries):
                header = archive_reader._read(stream, before.size, entry.offset, 60)
                if i < 2:
                    payload = first if i == 0 else second
                else:
                    payload = archive_reader._read(stream, before.size, entry.offset + 60, entry.size)
                    if i >= start:
                        payload = renamed_object(payload, before.members[i - start][1], mapping)
                if len(payload) != sizes[i] or target.tell() != offsets[i]:
                    fail("archive layout differs from rename plan")
                target.write(header[:48] + str(len(payload)).encode().ljust(10, b" ") + header[58:])
                target.write(payload)
                if len(payload) & 1:
                    padding = (archive_reader._read(stream, before.size,
                               entry.offset + 60 + entry.size, 1)
                               if entry.size & 1 else b"\n")
                    target.write(padding)


def isolate(source, output, nm):
    source, output = Path(source), Path(output)
    if output.exists() or output.is_symlink():
        fail("output must be a fresh staging path")
    before = inspect_archive(source)
    names = {symbol.name for _, obj in before.members for symbol in obj.symbols}
    mapping = checked_names(names)
    inventory = Counter(symbol_rows(run([nm, "--extern-only", "--format=posix", source])))
    try:
        if mapping:
            write_lossless_archive(source, output, before, mapping)
        else:
            shutil.copyfile(source, output)
        after = inspect_archive(output)
        hits = verify_pair(before, after, mapping)
        expected = Counter()
        for (name, kind), count in inventory.items():
            expected[mapping.get(name, name), kind] += count
        actual = Counter(symbol_rows(run([nm, "--extern-only", "--format=posix", output])))
        if expected != actual:
            fail("native nm definition or reference inventory changed")
        value = hashlib.sha256()
        with source.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                value.update(chunk)
        if value.hexdigest() != before.sha256:
            fail("input archive changed during isolation")
        return {"schema": "neverc.math-coff-isolation.v1",
                "input_sha256": before.sha256, "output_sha256": after.sha256,
                "members": len(after.members), "renames": mapping,
                "restored_section_numbers": 0,
                "symbol_hits": hits, "section_and_auxiliary_checks": "passed",
                "native_nm_inventory": "passed"}
    except BaseException:
        output.unlink(missing_ok=True)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--nm", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    try:
        report = isolate(args.input, args.output, args.nm)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + "\n")
    print("Private MSVC math templates: compiled SDK bodies and references preserved")


if __name__ == "__main__":
    main()
