#!/usr/bin/env python3
"""Isolate four MSVC math templates while retaining their compiled SDK bodies."""

import argparse
from collections import Counter
from dataclasses import replace
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from AuditArchive import symbol_rows
from RewriteSetupCoffSymbols import inspect_archive


MATH_RENAMES = {
    "??$log10@H$0A@@@YANH@Z": "??$neverc_cpp_log10@H$0A@@@YANH@Z",
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


def run(command):
    result = subprocess.run([str(argument) for argument in command],
                            capture_output=True, text=True, timeout=600,
                            encoding="utf-8", errors="replace", check=False)
    if result.returncode:
        fail("tool failed: " + result.stdout + result.stderr)
    return result.stdout


def isolate(source, output, objcopy, nm):
    source, output = Path(source), Path(output)
    if output.exists() or output.is_symlink():
        fail("output must be a fresh staging path")
    before = inspect_archive(source)
    names = {symbol.name for _, obj in before.members for symbol in obj.symbols}
    mapping = checked_names(names)
    inventory = Counter(symbol_rows(run([nm, "--extern-only", "--format=posix", source])))
    try:
        if mapping:
            run([objcopy, *("--redefine-sym=" + old + "=" + new
                            for old, new in mapping.items()), source, output])
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
                "symbol_hits": hits, "section_and_auxiliary_checks": "passed",
                "native_nm_inventory": "passed"}
    except BaseException:
        output.unlink(missing_ok=True)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--objcopy", required=True, type=Path)
    parser.add_argument("--nm", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    try:
        report = isolate(args.input, args.output, args.objcopy, args.nm)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + "\n")
    print("Private MSVC math templates: compiled SDK bodies and references preserved")


if __name__ == "__main__":
    main()
