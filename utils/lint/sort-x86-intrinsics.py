#!/usr/bin/env python3
"""
Sort x86 intrinsic tables and synchronise X86GenDAGISel.inc.

LLVM's intrinsic name lookup uses binary search, so the x86 intrinsic
entries MUST be sorted alphabetically by name.  This script reorders
the enum in IntrinsicsX86.h and the corresponding slices of every
per-intrinsic table in IntrinsicImpl.inc (name table, IIT_Table,
attribute map) so that names are in order.

After sorting (or when entries are added in already-sorted order),
the hardcoded intrinsic IDs inside X86GenDAGISel.inc must be updated
to match the new enum values.  This script handles that too.

Usage:
    python3 utils/lint/sort-x86-intrinsics.py             # dry-run (shows diff)
    python3 utils/lint/sort-x86-intrinsics.py --write      # overwrite in place
    python3 utils/lint/sort-x86-intrinsics.py --check       # exit 1 if unsorted (CI)
"""

import argparse
import difflib
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ENUM_PATH   = REPO / "llvm" / "include" / "llvm" / "IR" / "IntrinsicsX86.h"
INC_PATH    = REPO / "llvm" / "include" / "llvm" / "IR" / "IntrinsicImpl.inc"
DAGISEL_PATH = REPO / "llvm" / "lib" / "Target" / "X86" / "X86GenDAGISel.inc"

X86_BASE = 1803  # first x86 intrinsic enum value

# ── Helpers ──────────────────────────────────────────────────────────────

def extract_section(text, start_marker, end_marker):
    s = text.index(start_marker)
    e = text.index(end_marker, s) + len(end_marker)
    return s, e, text[s:e]


# ── VBR codec (for X86GenDAGISel.inc) ────────────────────────────────────

def decode_vbr_tokens(tokens):
    """Decode VBR from list of string tokens like ['84|128', '46']."""
    val = 0
    shift = 0
    for tok in tokens:
        tok = tok.strip().rstrip(",")
        if "|" in tok:
            parts = tok.split("|")
            b = int(parts[0]) | int(parts[1])
        else:
            b = int(tok)
        val |= (b & 0x7F) << shift
        shift += 7
        if not (b & 0x80):
            break
    return val


def encode_vbr(val):
    """Encode integer as VBR byte tokens (e.g. ['10|128', '47'])."""
    tokens = []
    while True:
        b = val & 0x7F
        val >>= 7
        if val:
            tokens.append(f"{b}|128")
        else:
            tokens.append(str(b))
            break
    return tokens


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


def parse_enum_names(content):
    """Return list of cpp_name strings in enum order."""
    return [m.group(1) for m in re.finditer(r'(x86_\w+)\s*[,=]', content)]


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

    pre_x86 = all_names[:x86_offset]
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


# ── X86GenDAGISel.inc ID synchronisation ─────────────────────────────────

_VBR_RE = re.compile(
    r'(OPC_CheckChild1Integer,\s*)'
    r'((?:\d+\|128,\s*)*\d+)'
    r'(,?\s*)'
)

def _find_baseline(current_count):
    """Find the baseline enum and commit before recent intrinsic additions.

    Multiple commits may have added entries without updating DAGISel.
    Walk backwards through ALL commits that modified IntrinsicsX86.h,
    tracking the smallest parent count.  Stop when the count stops
    decreasing or we hit a commit that also updated DAGISel (sync point).

    Returns (old_names, baseline_commit) where baseline_commit is the
    oldest addition commit (whose parent has the correct DAGISel).
    Returns (None, None) if no baseline is found.
    """
    rel_enum = str(ENUM_PATH.relative_to(REPO))
    rel_dag = str(DAGISEL_PATH.relative_to(REPO))
    try:
        log = subprocess.check_output(
            ["git", "-C", str(REPO), "log", "--format=%H",
             "--diff-filter=M", "--follow", "--", rel_enum],
            text=True, stderr=subprocess.DEVNULL).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None, None
    if not log:
        return None, None

    best_names = None
    best_count = current_count
    best_commit = None

    for commit in log.split("\n"):
        commit = commit.strip()
        if not commit:
            continue

        # If this commit also updated DAGISel, it's a sync point.
        try:
            changed = subprocess.check_output(
                ["git", "-C", str(REPO), "diff-tree", "--no-commit-id",
                 "-r", "--name-only", commit],
                text=True, stderr=subprocess.DEVNULL)
            if rel_dag in changed.split("\n"):
                if best_names is not None:
                    return best_names, best_commit
                this_text = subprocess.check_output(
                    ["git", "-C", str(REPO), "show", f"{commit}:{rel_enum}"],
                    text=True, stderr=subprocess.DEVNULL)
                names = parse_enum_names(this_text)
                if len(names) < current_count:
                    return names, commit
                return None, None
        except subprocess.CalledProcessError:
            pass

        try:
            parent_text = subprocess.check_output(
                ["git", "-C", str(REPO), "show", f"{commit}^:{rel_enum}"],
                text=True, stderr=subprocess.DEVNULL)
            names = parse_enum_names(parent_text)
            count = len(names)
            if count < best_count:
                best_names = names
                best_count = count
                best_commit = commit
            elif best_names is not None:
                break
        except subprocess.CalledProcessError:
            continue

    return best_names, best_commit


def _build_remap(old_names, new_names):
    """Build {old_id: new_id} for intrinsics that shifted."""
    new_pos = {name: X86_BASE + i for i, name in enumerate(new_names)}
    remap = {}
    for i, name in enumerate(old_names):
        old_id = X86_BASE + i
        new_id = new_pos.get(name)
        if new_id is not None and new_id != old_id:
            remap[old_id] = new_id
    return remap


def _patch_dagisel(text, remap):
    """Rewrite OPC_CheckChild1Integer VBR values per the remap dict."""
    count = 0
    def replacer(m):
        nonlocal count
        prefix, vbr_text, suffix = m.group(1), m.group(2), m.group(3)
        byte_strs = [s.strip() for s in vbr_text.split(",") if s.strip()]
        encoded = decode_vbr_tokens(byte_strs)
        if encoded % 2 != 0:
            return m.group(0)
        intrinsic_id = encoded // 2
        if intrinsic_id not in remap:
            return m.group(0)
        new_encoded = remap[intrinsic_id] * 2
        new_vbr = encode_vbr(new_encoded)
        count += 1
        return f"{prefix}{','.join(new_vbr)}{suffix}"
    new_text = _VBR_RE.sub(replacer, text)
    return new_text, count


def _get_original_dagisel(baseline_commit):
    """Get the correct DAGISel text from before intrinsic additions started."""
    rel = str(DAGISEL_PATH.relative_to(REPO))
    try:
        return subprocess.check_output(
            ["git", "-C", str(REPO), "show", f"{baseline_commit}^:{rel}"],
            text=True, stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        return None


def check_dagisel():
    """Check if X86GenDAGISel.inc IDs match the current enum.

    Computes the golden DAGISel by applying the remap to the original
    (pre-addition) file and compares with the current file.
    Returns (is_ok, stale_count, message).
    """
    if not DAGISEL_PATH.exists():
        return True, 0, "X86GenDAGISel.inc not found, skipping"

    current_names = parse_enum_names(ENUM_PATH.read_text())
    old_names, baseline_commit = _find_baseline(len(current_names))

    if old_names is None:
        return True, 0, "X86GenDAGISel.inc IDs are consistent"

    if len(old_names) == len(current_names):
        return True, 0, "X86GenDAGISel.inc IDs are consistent"

    remap = _build_remap(old_names, current_names)
    if not remap:
        return True, 0, "X86GenDAGISel.inc IDs are consistent"

    original = _get_original_dagisel(baseline_commit)
    if original is None:
        return True, 0, "cannot get original DAGISel, skipping"

    golden, count = _patch_dagisel(original, remap)
    current = DAGISEL_PATH.read_text()

    if golden == current:
        return True, 0, "X86GenDAGISel.inc IDs are consistent"

    return False, count, (
        f"X86GenDAGISel.inc has {count} stale intrinsic ID(s). "
        f"Run: python3 utils/lint/sort-x86-intrinsics.py --write"
    )


def sync_dagisel(write=False):
    """Synchronise X86GenDAGISel.inc intrinsic IDs with the current enum.

    Always patches from the ORIGINAL DAGISel (before additions) to ensure
    idempotence — avoids the overlapping-ID problem of in-place remapping.

    Returns (patched_count, message).
    """
    if not DAGISEL_PATH.exists():
        return 0, "X86GenDAGISel.inc not found, skipping"

    current_names = parse_enum_names(ENUM_PATH.read_text())
    old_names, baseline_commit = _find_baseline(len(current_names))

    if old_names is None:
        return 0, "cannot determine old enum (no git history)"

    if len(old_names) == len(current_names):
        return 0, "enum count unchanged, nothing to remap"

    remap = _build_remap(old_names, current_names)
    if not remap:
        return 0, "no ID shifts detected"

    original = _get_original_dagisel(baseline_commit)
    if original is None:
        return 0, "cannot get original DAGISel from git"

    new_text, count = _patch_dagisel(original, remap)

    if count == 0:
        return 0, "no OPC_CheckChild1Integer entries needed patching"

    if new_text == DAGISEL_PATH.read_text():
        return 0, "X86GenDAGISel.inc already up to date"

    if write:
        DAGISEL_PATH.write_text(new_text)

    return count, f"{'patched' if write else 'would patch'} {count} entries in X86GenDAGISel.inc"


# ── Main ─────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true",
                      help="Overwrite files in place.")
    mode.add_argument("--check", action="store_true",
                      help="Exit 1 if unsorted or DAGISel stale (CI).")
    args = parser.parse_args()

    errors = []

    # ── Sort check ───────────────────────────────────────────────────────
    new_enum, new_inc, count = sort_x86_intrinsics()

    if new_enum is not None:
        # Enum needs sorting
        if args.check:
            errors.append(f"x86 intrinsics are not sorted ({count} entries)")
        elif args.write:
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
    else:
        if not args.check:
            print(f"Already sorted ({count} x86 intrinsics).", file=sys.stderr)

    # ── DAGISel sync ─────────────────────────────────────────────────────
    dag_ok, dag_stale, dag_msg = check_dagisel()

    if not dag_ok:
        if args.check:
            errors.append(dag_msg)
        elif args.write:
            patched, msg = sync_dagisel(write=True)
            print(f"  {msg}", file=sys.stderr)
        else:
            patched, msg = sync_dagisel(write=False)
            print(f"  {msg}", file=sys.stderr)
    else:
        if not args.check:
            print(f"  {dag_msg}", file=sys.stderr)

    # ── Exit ─────────────────────────────────────────────────────────────
    if errors:
        for e in errors:
            print(f"ERROR: {e}", file=sys.stderr)
        print(f"\nRun: python3 utils/lint/sort-x86-intrinsics.py --write",
              file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
