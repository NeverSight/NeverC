#!/usr/bin/env python3
"""
Sort OPTION entries in Options.td.h to match LLVM OptTable's required order.

LLVM's OptTable constructor (llvm/lib/Option/OptTable.cpp) performs a binary
search on options at runtime, so entries MUST be sorted using its custom
comparison function (StrCmpOptionNameIgnoreCase).  This script automates that
sorting so you never have to figure out the right position by hand.

Usage:
    python3 scripts/sort-options-td.py                    # dry-run (shows diff)
    python3 scripts/sort-options-td.py --write            # overwrite in place
    python3 scripts/sort-options-td.py --check            # exit 1 if unsorted (CI)
"""

import argparse
import difflib
import re
import sys
from functools import cmp_to_key
from pathlib import Path

DEFAULT_PATH = Path(__file__).resolve().parent.parent / \
    "neverc/include/neverc/Invoke/Options.td.h"

PREFIX_VALUES = {
    "prefix_0": [""],
    "prefix_1": ["-", ""],
    "prefix_2": ["-", "--", ""],
    "prefix_3": ["--", ""],
}

PREFIX_FIRST_LEN = {
    "prefix_0": 0,
    "prefix_1": 1,   # len("-")
    "prefix_2": 1,   # len("-")
    "prefix_3": 2,   # len("--")
}


def strip_prefix(entry: dict) -> str:
    """Return the unprefixed name, matching LLVM Info::getName()."""
    pfx_len = PREFIX_FIRST_LEN.get(entry["prefix"], 0)
    return entry["name"][pfx_len:]


def strcmp_option_name_ignore_case(a: str, b: str) -> int:
    """Replicate LLVM's StrCmpOptionNameIgnoreCase."""
    min_size = min(len(a), len(b))
    a_low = a[:min_size].lower()
    b_low = b[:min_size].lower()
    if a_low < b_low:
        return -1
    if a_low > b_low:
        return 1
    if len(a) == len(b):
        return 0
    # prefix sorts AFTER the string it prefixes (opposite of normal)
    return 1 if len(a) == min_size else -1


def strcmp_option_name(a: str, b: str) -> int:
    """Replicate LLVM's StrCmpOptionName (case-insensitive, then case-sensitive)."""
    n = strcmp_option_name_ignore_case(a, b)
    if n != 0:
        return n
    if a < b:
        return -1
    if a > b:
        return 1
    return 0


def compare_options(a: dict, b: dict) -> int:
    """Replicate LLVM's operator< for OptTable::Info.
    
    getName() strips the first prefix from PrefixedName, so the sort
    key is the unprefixed name, NOT the raw string from the OPTION macro.
    """
    a_name = strip_prefix(a)
    b_name = strip_prefix(b)

    n = strcmp_option_name(a_name, b_name)
    if n != 0:
        return n

    a_prefixes = PREFIX_VALUES.get(a["prefix"], [""])
    b_prefixes = PREFIX_VALUES.get(b["prefix"], [""])
    for pa, pb in zip(a_prefixes, b_prefixes):
        n = strcmp_option_name(pa, pb)
        if n != 0:
            return n

    if a["kind"] == "Joined" and b["kind"] != "Joined":
        return 1
    if b["kind"] == "Joined" and a["kind"] != "Joined":
        return -1
    return 0


def parse_option_entries(lines: list[str], start: int, end: int) -> list[dict]:
    """Parse multi-line OPTION(...) entries between start and end line indices."""
    entries = []
    i = start
    while i < end:
        line = lines[i]
        if not line.startswith("OPTION("):
            i += 1
            continue

        entry_lines = [line]
        depth = line.count("(") - line.count(")")
        j = i + 1
        while depth > 0 and j < end:
            entry_lines.append(lines[j])
            depth += lines[j].count("(") - lines[j].count(")")
            j += 1

        text = "\n".join(entry_lines)

        m = re.match(
            r'OPTION\(\s*(\w+)\s*,\s*"([^"]*)"\s*,\s*(\w+)\s*,\s*(\w+)',
            text,
        )
        if m:
            entries.append({
                "prefix": m.group(1),
                "name": m.group(2),
                "id": m.group(3),
                "kind": m.group(4),
                "text": text,
                "start": i,
                "end": j,
            })

        i = j

    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", default=str(DEFAULT_PATH))
    parser.add_argument("--write", action="store_true",
                        help="Overwrite the file in place")
    parser.add_argument("--check", action="store_true",
                        help="Exit 1 if options are not sorted (for CI)")
    args = parser.parse_args()

    filepath = Path(args.path)
    original = filepath.read_text()
    lines = original.splitlines()

    option_ifdef = None
    option_endif = None
    searchable_start = None

    for i, line in enumerate(lines):
        if line.strip() == "#ifdef OPTION":
            option_ifdef = i
        if line.strip() == "#endif // OPTION":
            option_endif = i
            break

    if option_ifdef is None or option_endif is None:
        print("ERROR: could not find #ifdef OPTION / #endif // OPTION", file=sys.stderr)
        sys.exit(1)

    for i in range(option_ifdef + 1, option_endif):
        if re.match(r"OPTION\(prefix_[1-9]", lines[i]):
            searchable_start = i
            break

    if searchable_start is None:
        print("ERROR: no searchable OPTION entries found", file=sys.stderr)
        sys.exit(1)

    header = lines[:searchable_start]
    footer = lines[option_endif:]

    entries = parse_option_entries(lines, searchable_start, option_endif)
    if not entries:
        print("No OPTION entries found to sort.")
        return

    sorted_entries = sorted(entries, key=cmp_to_key(compare_options))

    already_sorted = all(
        compare_options(sorted_entries[i], sorted_entries[i + 1]) < 0
        for i in range(len(sorted_entries) - 1)
    )

    was_sorted = all(
        entries[i]["text"] == sorted_entries[i]["text"]
        for i in range(len(entries))
    )

    if was_sorted:
        print(f"Options are already sorted ({len(entries)} entries).")
        if args.check:
            sys.exit(0)
        return

    new_lines = header[:]
    for entry in sorted_entries:
        new_lines.append(entry["text"])
    new_lines.extend(footer)

    new_content = "\n".join(new_lines) + "\n"

    if args.check:
        moved = sum(1 for i in range(len(entries)) if entries[i] is not sorted_entries[i])
        print(f"ERROR: {moved} of {len(entries)} entries are out of order.", file=sys.stderr)
        sys.exit(1)

    if args.write:
        filepath.write_text(new_content)
        print(f"Sorted {len(entries)} entries and wrote {filepath}")
    else:
        diff = difflib.unified_diff(
            original.splitlines(keepends=True),
            new_content.splitlines(keepends=True),
            fromfile=str(filepath),
            tofile=str(filepath) + " (sorted)",
            n=1,
        )
        diff_text = "".join(diff)
        if diff_text:
            print(diff_text)
            print(f"\n{len(entries)} entries parsed. Use --write to apply.")
        else:
            print("No changes needed.")


if __name__ == "__main__":
    main()
