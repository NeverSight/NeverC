#!/usr/bin/env python3
"""
Sort and reindex DiagnosticGroups.td.h

Sorts diagnostic groups alphabetically by flag name, sorts diag::
entries within each group, rebuilds all array offsets and the name table.

Usage:
    python scripts/sort_diagnostic_groups.py [path/to/DiagnosticGroups.td.h]

With no argument, defaults to
neverc/include/neverc/Foundation/DiagnosticGroups.td.h relative to the repo root.
"""

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

STR_LINE_WIDTH = 70


@dataclass
class DiagGroup:
    cpp_name: str
    flag_name: str
    diag_entries: list = field(default_factory=list)
    sub_group_indices: list = field(default_factory=list)
    doc: str = 'R"()"'


# ── Parsing ──────────────────────────────────────────────────────────────

def parse_c_array(text, prefix):
    """Parse /* PrefixN */ groups from a C int16_t array body.
    Returns {N: [value_strings...]}
    """
    groups = {}
    cur_num = None
    cur_vals = []

    for line in text.split('\n'):
        stripped = line.strip()
        if not stripped or 'static const' in stripped or stripped in ('{', '};', '}'):
            continue
        if '/* Empty */' in stripped:
            continue

        m = re.search(rf'/\*\s*{prefix}(\d+)\b[^*]*\*/', stripped)
        if m:
            num = int(m.group(1))
            after = stripped[m.end():].strip().rstrip(',').strip()
            if not after or after == '-1':
                groups[num] = []
                cur_num = None if after == '-1' else num
                cur_vals = []
            else:
                cur_num = num
                cur_vals = [after]
            continue

        for raw in stripped.split(','):
            v = raw.strip()
            if not v:
                continue
            if v == '-1':
                if cur_num is not None:
                    groups[cur_num] = cur_vals
                    cur_num = None
                    cur_vals = []
            elif cur_num is not None:
                cur_vals.append(v)

    return groups


def parse_entries(text):
    """Parse all DIAG_ENTRY() macros. Returns list of dicts."""
    result = []
    pos = 0
    while True:
        i = text.find('DIAG_ENTRY(', pos)
        if i < 0:
            break
        j = text.find(')")', i)
        if j < 0:
            break
        j += 3
        raw = text[i:j]

        doc_i = raw.find('R"(')
        if doc_i < 0:
            pos = j
            continue

        doc = raw[doc_i:-1]  # exclude trailing ) of DIAG_ENTRY()
        args = raw[len('DIAG_ENTRY('):doc_i]

        nm = re.search(r'(\w+)\s*/\*\s*(.*?)\s*\*/', args)
        if not nm:
            pos = j
            continue

        rest = re.sub(r'/\*.*?\*/', '', args[nm.end():])
        nums = re.findall(r'\d+', rest)
        if len(nums) != 3:
            print(f"WARNING: bad entry: {raw[:60]}...", file=sys.stderr)
            pos = j
            continue

        result.append({
            'cpp_name': nm.group(1),
            'flag_name': nm.group(2),
            'array_offset': int(nums[1]),
            'subgroup_offset': int(nums[2]),
            'doc': doc,
        })
        pos = j
    return result


# ── Rebuilding ───────────────────────────────────────────────────────────

def _len_escape(n):
    """Encode a length byte as a C escape sequence."""
    if n == 9:
        return '\\t'
    if n == 10:
        return '\\n'
    return '\\' + oct(n)[2:].zfill(3)


def _build_name_table(names):
    """Pack names into a length-prefixed string table.
    Returns (escaped_string, {name: byte_offset}).
    """
    offsets = {}
    parts = []
    pos = 0
    for name in names:
        offsets[name] = pos
        parts.append(_len_escape(len(name)) + name)
        pos += 1 + len(name)
    return ''.join(parts), offsets


def _wrap_str(packed, width=STR_LINE_WIDTH):
    """Split a packed C-escaped string into lines of ~width display chars."""
    lines = []
    cur = ''
    i = 0
    while i < len(packed):
        if packed[i] == '\\':
            j = i + 1
            if j < len(packed) and packed[j] in 'tn':
                j += 1
            elif j < len(packed) and packed[j].isdigit():
                while j < len(packed) and j - i <= 3 and packed[j].isdigit():
                    j += 1
            else:
                j += 1
            token = packed[i:j]
        else:
            token = packed[i]
            j = i + 1

        if len(cur) + len(token) > width:
            lines.append(cur)
            cur = ''
        cur += token
        i = j

    if cur:
        lines.append(cur)
    return lines


def emit_diag_arrays(groups):
    """Rebuild DiagArrays[]. Returns (text_lines, {group_idx: offset})."""
    lines = ['static const int16_t DiagArrays[] = {', '    /* Empty */ -1,']
    offsets = {}
    off = 1

    for idx, g in enumerate(groups):
        if not g.diag_entries:
            continue
        offsets[idx] = off
        for i, e in enumerate(g.diag_entries):
            tag = f'/* DiagArray{idx} */ ' if i == 0 else ''
            lines.append(f'    {tag}{e},')
            off += 1
        lines.append('    -1,')
        off += 1

    lines.append('};')
    return lines, offsets


def emit_diag_subgroups(groups):
    """Rebuild DiagSubGroups[]. Returns (text_lines, {group_idx: offset})."""
    lines = ['static const int16_t DiagSubGroups[] = {', '    /* Empty */ -1,']
    offsets = {}
    off = 1

    for idx, g in enumerate(groups):
        if not g.sub_group_indices:
            continue
        offsets[idx] = off
        for i, ref in enumerate(g.sub_group_indices):
            tag = f'/* DiagSubGroup{idx} */ ' if i == 0 else ''
            lines.append(f'    {tag}{ref},')
            off += 1
        lines.append('    -1,')
        off += 1

    lines.append('};')
    return lines, offsets


def emit_name_table(groups):
    """Rebuild DiagGroupNames[]. Returns (text_lines, {flag_name: offset})."""
    names = [g.flag_name for g in groups]
    packed, offsets = _build_name_table(names)
    str_lines = _wrap_str(packed)

    lines = ['static const char DiagGroupNames[] = {']
    for i, s in enumerate(str_lines):
        if i == len(str_lines) - 1:
            lines.append(f'    "{s}"' + '};')
        else:
            lines.append(f'    "{s}"')

    return lines, offsets


def _format_entry(g, idx, name_off, arr_off, sg_off):
    """Format one DIAG_ENTRY(), wrapping if needed."""
    arr = f'/* DiagArray{idx} */ {arr_off}' if arr_off else '0'
    sg = f'/* DiagSubGroup{idx} */ {sg_off}' if sg_off else '0'

    one = f'DIAG_ENTRY({g.cpp_name} /* {g.flag_name} */, {name_off}, {arr}, {sg}, {g.doc})'
    if len(one) <= 80 and '\n' not in g.doc:
        return one

    if '\n' not in g.doc:
        two_first = f'DIAG_ENTRY({g.cpp_name} /* {g.flag_name} */, {name_off},'
        two_rest = f'           {arr}, {sg}, {g.doc})'
        if len(two_rest) <= 80:
            return two_first + '\n' + two_rest

    return (f'DIAG_ENTRY(\n'
            f'    {g.cpp_name} /* {g.flag_name} */, {name_off},\n'
            f'    {arr}, {sg},\n'
            f'    {g.doc})')


def emit_entries(groups, name_offsets, arr_offsets, sg_offsets):
    """Rebuild all DIAG_ENTRY() lines."""
    lines = []
    for idx, g in enumerate(groups):
        lines.append(_format_entry(
            g, idx,
            name_offsets[g.flag_name],
            arr_offsets.get(idx, 0),
            sg_offsets.get(idx, 0),
        ))
    return lines


# ── Main ─────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) > 1:
        path = Path(sys.argv[1])
    else:
        # Default: relative to repo root (this script lives in <repo>/scripts/).
        repo_root = Path(__file__).resolve().parent.parent
        path = (repo_root / 'neverc' / 'include' / 'neverc' / 'Foundation'
                / 'DiagnosticGroups.td.h')

    if not path.exists():
        print(f"Error: {path} not found", file=sys.stderr)
        sys.exit(1)

    content = path.read_text()

    # ── Locate sections ──
    arrays_hdr = '#ifdef GET_DIAG_ARRAYS'
    arrays_end = '#endif // GET_DIAG_ARRAYS'
    a_s = content.index(arrays_hdr)
    a_e = content.index(arrays_end, a_s) + len(arrays_end)
    arrays_text = content[a_s + len(arrays_hdr) + 1:a_e - len(arrays_end)]

    entry_hdr = '#ifdef DIAG_ENTRY'
    entry_end_mark = '#endif // DIAG_ENTRY'
    e_s = content.index(entry_hdr)
    e_e = content.index(entry_end_mark, e_s) + len(entry_end_mark)
    entries_text = content[e_s + len(entry_hdr) + 1:e_e - len(entry_end_mark)]

    cat_hdr = '#ifdef GET_CATEGORY_TABLE'
    cat_end_mark = '#endif // GET_CATEGORY_TABLE'
    c_s = content.index(cat_hdr)
    c_e = content.index(cat_end_mark, c_s) + len(cat_end_mark)
    categories = content[c_s:c_e]

    # ── Parse sub-sections of GET_DIAG_ARRAYS ──
    da_i = arrays_text.index('static const int16_t DiagArrays[]')
    da_j = arrays_text.index('};', da_i) + 2
    dsg_i = arrays_text.index('static const int16_t DiagSubGroups[]')
    dsg_j = arrays_text.index('};', dsg_i) + 2

    diag_arrays = parse_c_array(arrays_text[da_i:da_j], 'DiagArray')
    sub_groups = parse_c_array(arrays_text[dsg_i:dsg_j], 'DiagSubGroup')
    entries = parse_entries(entries_text)

    print(f"Parsed {len(entries)} groups, "
          f"{len(diag_arrays)} DiagArrays, "
          f"{len(sub_groups)} DiagSubGroups", file=sys.stderr)

    # ── Build group model ──
    groups = []
    for i, e in enumerate(entries):
        groups.append(DiagGroup(
            cpp_name=e['cpp_name'],
            flag_name=e['flag_name'],
            diag_entries=sorted(diag_arrays.get(i, [])),
            sub_group_indices=[int(v) for v in sub_groups.get(i, [])],
            doc=e['doc'],
        ))

    # ── Sort by flag_name ──
    indexed = list(enumerate(groups))
    indexed.sort(key=lambda x: x[1].flag_name)

    old_to_new = {old: new for new, (old, _) in enumerate(indexed)}
    sorted_groups = [g for _, g in indexed]

    for g in sorted_groups:
        g.sub_group_indices = sorted(old_to_new[oi] for oi in g.sub_group_indices)

    # ── Rebuild all sections ──
    da_lines, da_off = emit_diag_arrays(sorted_groups)
    dsg_lines, dsg_off = emit_diag_subgroups(sorted_groups)
    name_lines, name_off = emit_name_table(sorted_groups)
    entry_lines = emit_entries(sorted_groups, name_off, da_off, dsg_off)

    # ── Assemble output ──
    out = []
    out.append('')
    out.append('#ifdef GET_DIAG_ARRAYS')
    out.extend(da_lines)
    out.append('')
    out.extend(dsg_lines)
    out.append('')
    out.extend(name_lines)
    out.append('')
    out.append('#endif // GET_DIAG_ARRAYS')
    out.append('')
    out.append('#ifdef DIAG_ENTRY')
    out.extend(entry_lines)
    out.append('#endif // DIAG_ENTRY')
    out.append('')
    out.append(categories)
    out.append('')

    path.write_text('\n'.join(out))
    print(f"Wrote {len(sorted_groups)} sorted groups to {path}", file=sys.stderr)


if __name__ == '__main__':
    main()
