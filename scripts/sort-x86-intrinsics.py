#!/usr/bin/env python3
"""
Sort x86 intrinsic tables in IntrinsicsX86.h and IntrinsicImpl.inc.

LLVM's intrinsic name lookup uses binary search, so the x86 intrinsic
entries MUST be sorted alphabetically by name.  This script reorders
the enum in IntrinsicsX86.h and the corresponding slices of every
per-intrinsic table in IntrinsicImpl.inc (name table, IIT_Table,
attribute map) so that names are in order.

Usage:
    python3 scripts/sort-x86-intrinsics.py             # dry-run (shows diff)
    python3 scripts/sort-x86-intrinsics.py --write      # overwrite in place
    python3 scripts/sort-x86-intrinsics.py --check       # exit 1 if unsorted (CI)
"""

import argparse
import difflib
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ENUM_PATH   = REPO / "llvm" / "include" / "llvm" / "IR" / "IntrinsicsX86.h"
INC_PATH    = REPO / "llvm" / "include" / "llvm" / "IR" / "IntrinsicImpl.inc"

# ── Helpers ──────────────────────────────────────────────────────────────

def extract_section(text, start_marker, end_marker):
    s = text.index(start_marker)
    e = text.index(end_marker, s) + len(end_marker)
    return s, e, text[s:e]


# ── Parse IntrinsicsX86.h ────────────────────────────────────────────────

def parse_enum(content):
    """Return list of (cpp_name, comment) tuples in enum order."""
    entries = []
    for m in re.finditer(
        r'^\s+(x86_\w+)\s*(?:=\s*\d+\s*)?,\s*//\s*(llvm\.x86\.\S+)',
        content, re.MULTILINE
    ):
        entries.append((m.group(1), m.group(2)))
    return entries


def rebuild_enum(entries, first_id):
    lines = []
    for i, (cpp, comment) in enumerate(entries):
        pad = max(1, 48 - len(cpp))
        if i == 0:
            lines.append(f"    {cpp} = {first_id},{' ' * (pad - len(str(first_id)) - 4)}// {comment}")
        else:
            lines.append(f"    {cpp},{' ' * pad}// {comment}")
    return lines


# ── Parse IntrinsicImpl.inc tables ───────────────────────────────────────

def parse_name_table(content):
    """Return list of all name strings in order."""
    start = content.index("#ifdef GET_INTRINSIC_NAME_TABLE")
    end = content.index("#endif", start)
    section = content[start:end]
    return re.findall(r'"(llvm\.[^"]+)"', section)


def parse_iit_table(content):
    """Return list of all IIT_Table entries as strings."""
    start = content.index("static const unsigned IIT_Table[] = {")
    brace = content.index("{", start)
    end = content.index("};", brace)
    inner = content[brace + 1:end]
    return [e.strip() for e in inner.split(",") if e.strip()]


def parse_attr_map(content):
    """Return list of (value, comment) for IntrinsicsToAttributesMap."""
    start = content.index("IntrinsicsToAttributesMap[] = {")
    brace = content.index("{", start)
    end = content.index("};", brace)
    inner = content[brace + 1:end]
    entries = []
    for line in inner.split("\n"):
        line = line.strip()
        if not line or line == "{" or line == "};":
            continue
        m = re.match(r"(\d+)\s*,?\s*(//.*)?", line)
        if m:
            entries.append((m.group(1), m.group(2) or ""))
    return entries


# ── Sort and rebuild ─────────────────────────────────────────────────────

def sort_x86_intrinsics():
    enum_text = ENUM_PATH.read_text()
    inc_text = INC_PATH.read_text()

    enum_entries = parse_enum(enum_text)
    all_names = parse_name_table(inc_text)
    all_iit = parse_iit_table(inc_text)
    all_attr = parse_attr_map(inc_text)

    # Determine x86 range from TargetInfos
    m = re.search(
        r'\{llvm::StringLiteral\("x86"\),\s*(\d+),\s*(\d+)\}',
        inc_text
    )
    x86_offset = int(m.group(1))
    x86_count = int(m.group(2))

    assert len(enum_entries) == x86_count, \
        f"Enum has {len(enum_entries)} entries but TargetInfo says {x86_count}"

    first_id = x86_offset + 1  # IDs are 1-based, offset is 0-based count before x86

    # Build mapping: index within x86 → (cpp_name, llvm_name, iit, attr)
    x86_records = []
    for i in range(x86_count):
        global_id = first_id + i
        iit_idx = global_id - 1  # IIT_Table indexed by (id - 1)
        name_idx = global_id - 1  # name table in .inc is 0-based (no "not_intrinsic" prefix)

        x86_records.append({
            "cpp": enum_entries[i][0],
            "name": all_names[name_idx] if name_idx < len(all_names) else enum_entries[i][1],
            "iit": all_iit[iit_idx] if iit_idx < len(all_iit) else "0",
            "attr_val": all_attr[iit_idx][0] if iit_idx < len(all_attr) else "11",
        })

    # Sort by name
    sorted_records = sorted(x86_records, key=lambda r: r["name"])

    already_sorted = all(
        x86_records[i]["name"] == sorted_records[i]["name"]
        for i in range(len(x86_records))
    )

    if already_sorted:
        return None, None, x86_count

    # Rebuild enum
    sorted_enum = [(r["cpp"], r["name"]) for r in sorted_records]
    enum_lines = rebuild_enum(sorted_enum, first_id)

    # Rebuild IntrinsicsX86.h
    enum_start = re.search(r"^enum X86Intrinsics.*\{", enum_text, re.MULTILINE)
    body_start = enum_text.index("\n", enum_start.end()) + 1
    # Skip the comment line
    if "// Enum values" in enum_text[body_start:body_start + 50]:
        body_start = enum_text.index("\n", body_start) + 1

    body_end = enum_text.index("}; // enum")
    new_enum_text = (
        enum_text[:body_start]
        + "\n".join(enum_lines) + "\n"
        + enum_text[body_end:]
    )

    # Rebuild IntrinsicImpl.inc - replace x86 slices in each table

    # 1. Name table
    name_start = inc_text.index("#ifdef GET_INTRINSIC_NAME_TABLE")
    name_end = inc_text.index("#endif", name_start)
    name_section = inc_text[name_start:name_end]

    # Find first and last x86 name in the section
    first_x86_name = x86_records[0]["name"]
    last_x86_name = x86_records[-1]["name"]
    x86_name_start = name_section.index(f'"{first_x86_name}"')
    x86_name_end = name_section.rindex(f'"{sorted_records[-1]["name"]}"')
    # Actually, find ALL x86 lines and replace them
    # Better approach: reconstruct the whole name section
    pre_x86 = all_names[:x86_offset]  # independent + aarch64 (0-based, no "not_intrinsic")
    x86_names_sorted = [r["name"] for r in sorted_records]

    new_name_lines = []
    for n in pre_x86:
        new_name_lines.append(f'  "{n}",')
    for n in x86_names_sorted:
        new_name_lines.append(f'  "{n}",')

    new_name_section = (
        "#ifdef GET_INTRINSIC_NAME_TABLE\n"
        + "\n".join(new_name_lines) + "\n"
    )

    new_inc = inc_text[:name_start] + new_name_section + inc_text[name_end:]

    # 2. IIT_Table - replace x86 slice
    iit_start_idx = first_id - 1  # 0-based index in IIT_Table
    pre_iit = all_iit[:iit_start_idx]
    sorted_iit = [r["iit"] for r in sorted_records]

    # Rebuild IIT_Table
    all_new_iit = pre_iit + sorted_iit
    iit_lines = []
    for i in range(0, len(all_new_iit), 8):
        chunk = all_new_iit[i:i + 8]
        iit_lines.append("  " + ", ".join(chunk) + ("," if i + 8 < len(all_new_iit) else ""))

    new_iit_text = "static const unsigned IIT_Table[] = {\n" + "\n".join(iit_lines) + "\n};"

    old_iit_start = new_inc.index("static const unsigned IIT_Table[] = {")
    old_iit_end = new_inc.index("};", old_iit_start) + 2
    new_inc = new_inc[:old_iit_start] + new_iit_text + new_inc[old_iit_end:]

    # 3. Attribute map - replace x86 slice
    pre_attr = all_attr[:iit_start_idx]
    sorted_attr = [(r["attr_val"], f"// {r['name']}") for r in sorted_records]

    new_attr_lines = []
    for val, comment in pre_attr:
        new_attr_lines.append(f"    {val}, {comment}")
    for val, comment in sorted_attr:
        new_attr_lines.append(f"    {val}, {comment}")

    attr_start = new_inc.index("IntrinsicsToAttributesMap[] = {")
    attr_brace = new_inc.index("{", attr_start)
    attr_end = new_inc.index("};", attr_brace)
    new_inc = (
        new_inc[:attr_brace + 1] + "\n"
        + "\n".join(new_attr_lines) + "\n"
        + new_inc[attr_end:]
    )

    return new_enum_text, new_inc, x86_count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true",
                      help="Overwrite files in place.")
    mode.add_argument("--check", action="store_true",
                      help="Exit 1 if not sorted (CI).")
    args = parser.parse_args()

    new_enum, new_inc, count = sort_x86_intrinsics()

    if new_enum is None:
        if not args.check:
            print(f"Already sorted ({count} x86 intrinsics). No changes needed.",
                  file=sys.stderr)
        return

    if args.check:
        print(f"ERROR: x86 intrinsics are not sorted. "
              f"Run: python3 scripts/sort-x86-intrinsics.py --write",
              file=sys.stderr)
        sys.exit(1)

    if args.write:
        ENUM_PATH.write_text(new_enum)
        INC_PATH.write_text(new_inc)
        print(f"Sorted {count} x86 intrinsics.", file=sys.stderr)
        print(f"  Updated {ENUM_PATH}", file=sys.stderr)
        print(f"  Updated {INC_PATH}", file=sys.stderr)
    else:
        orig_enum = ENUM_PATH.read_text()
        orig_inc = INC_PATH.read_text()

        for path, orig, new in [
            (str(ENUM_PATH), orig_enum, new_enum),
            (str(INC_PATH), orig_inc, new_inc),
        ]:
            diff = difflib.unified_diff(
                orig.splitlines(keepends=True),
                new.splitlines(keepends=True),
                fromfile=path,
                tofile=path + " (sorted)",
                n=1,
            )
            sys.stdout.writelines(diff)

        print(f"\n{count} x86 intrinsics parsed. Use --write to apply.",
              file=sys.stderr)


if __name__ == "__main__":
    main()
