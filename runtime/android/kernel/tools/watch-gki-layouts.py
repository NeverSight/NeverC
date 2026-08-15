#!/usr/bin/env python3
"""Fingerprint NeverC-read kernel struct fields from official AOSP headers.

Daily GKI watch cannot rebuild vmlinux. It can still fetch the headers that
define the structs the runtime reads and compare member order / declarations.
A used field whose index or type changes is an offset risk; a change that
only adds unused trailing members is a sizeof risk.
"""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import re


TOOLS_ROOT = Path(__file__).resolve().parent


def _load_compat():
    spec = importlib.util.spec_from_file_location(
        "nvk_watch_compat", TOOLS_ROOT / "generate-compat-table.py"
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load generate-compat-table.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


COMPAT = _load_compat()


COMMENT_RE = re.compile(r"/\*.*?\*/|//.*?$", re.DOTALL | re.MULTILINE)
IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
FUNC_PTR_RE = re.compile(r"\(\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)")
ATTR_RE = re.compile(
    r"__attribute__\s*\(\([^;]*\)\)|"
    r"__aligned\s*\([^)]*\)|"
    r"__packed\b|"
    r"__randomize_layout\b|"
    r"__randomize_none\b|"
    r"__no_randomize_layout\b"
)
STRUCT_HEAD_RE = re.compile(
    r"""
    \bstruct\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)
    (?P<attrs>(?:\s+(?:__\w+|\w+_layout)(?:\s*\([^;{]*\))?)*)
    \s*\{
    """,
    re.VERBOSE,
)
TYPE_KEYWORDS = frozenset(
    {
        "auto",
        "bool",
        "char",
        "const",
        "enum",
        "extern",
        "inline",
        "int",
        "long",
        "restrict",
        "short",
        "signed",
        "static",
        "struct",
        "typedef",
        "union",
        "unsigned",
        "void",
        "volatile",
        "_Bool",
        "__attribute__",
        "__extension__",
        "__restrict",
        "__user",
        "__kernel",
        "__rcu",
        "__private",
        "__bitwise",
        "__packed",
        "__aligned",
        "__randomize_layout",
        "__randomize_none",
        "__no_randomize_layout",
    }
)

# Header search only. Member names come from generate-compat-table.py.
STRUCT_HEADER_PATHS = {
    "cred": ("include/linux/cred.h",),
    "dentry": ("include/linux/dcache.h",),
    "dir_context": ("include/linux/fs.h",),
    "file": ("include/linux/fs.h",),
    "file_operations": ("include/linux/fs.h",),
    "filename": ("include/linux/fs.h", "include/linux/filename.h"),
    "inode": ("include/linux/fs.h",),
    "kobject": ("include/linux/kobject.h",),
    "kstat": ("include/linux/stat.h",),
    "mm_struct": ("include/linux/mm_types.h",),
    "module": ("include/linux/module.h",),
    "module_kobject": ("include/linux/module.h",),
    "nsproxy": ("include/linux/nsproxy.h",),
    "path": ("include/linux/path.h",),
    "pt_regs": ("arch/arm64/include/asm/ptrace.h",),
    "qstr": ("include/linux/dcache.h", "include/linux/qstr.h"),
    "seccomp": ("include/linux/seccomp.h",),
    "signal_struct": ("include/linux/sched/signal.h",),
    "sk_buff": ("include/linux/skbuff.h",),
    "sock_common": ("include/net/sock.h",),
    "task_struct": ("include/linux/sched.h",),
    "thread_info": (
        "arch/arm64/include/asm/thread_info.h",
        "include/linux/thread_info.h",
    ),
    "timespec64": ("include/linux/time64.h", "include/vdso/time64.h"),
    "vm_area_struct": ("include/linux/mm_types.h",),
    "vmap_area": ("include/linux/vmalloc.h",),
}
OPTIONAL_STRUCTS = frozenset({"inode", "thread_info"})


def watched_structs_from_compat(compat=COMPAT):
    specs = []
    for name, fields in compat.neverc_read_members_by_struct().items():
        paths = STRUCT_HEADER_PATHS.get(name)
        if not paths:
            raise RuntimeError(
                f"NeverC-read struct {name} has no AOSP header path in watch"
            )
        specs.append(
            {
                "name": name,
                "paths": paths,
                "fields": tuple(sorted(fields)),
                "optional": name in OPTIONAL_STRUCTS,
            }
        )
    return tuple(specs)


WATCHED_STRUCTS = watched_structs_from_compat()


def strip_c_comments(text):
    return COMMENT_RE.sub("", text)


def _matching_brace_end(text, open_index):
    depth = 0
    for index in range(open_index, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def extract_struct_body(text, name):
    cleaned = strip_c_comments(text)
    for match in STRUCT_HEAD_RE.finditer(cleaned):
        if match.group("name") != name:
            continue
        open_index = match.end() - 1
        close_index = _matching_brace_end(cleaned, open_index)
        if close_index < 0:
            continue
        return cleaned[open_index + 1 : close_index]
    return None


def _skip_preprocessor(body, index):
    end = body.find("\n", index)
    if end < 0:
        return len(body)
    return end + 1


def iter_top_level_statements(body):
    index = 0
    start = 0
    depth = 0
    length = len(body)
    while index < length:
        char = body[index]
        if char == "#" and depth == 0 and (index == 0 or body[index - 1] == "\n"):
            index = _skip_preprocessor(body, index)
            start = index
            continue
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        elif char == ";" and depth == 0:
            statement = body[start:index].strip()
            if statement:
                yield statement
            start = index + 1
        index += 1


def _normalize_decl(statement):
    return " ".join(statement.split())


def _inner_anonymous_body(statement):
    stripped = statement.lstrip()
    if not (stripped.startswith("struct") or stripped.startswith("union")):
        return None, None
    brace = statement.find("{")
    if brace < 0:
        return None, None
    close = _matching_brace_end(statement, brace)
    if close < 0:
        return None, None
    after = statement[close + 1 :].strip()
    after = re.sub(r"__\w+(?:\s*\([^)]*\))?", "", after).strip()
    return statement[brace + 1 : close], after


def _field_name_from_declarator(statement):
    func_ptr = FUNC_PTR_RE.search(statement)
    if func_ptr is not None:
        return func_ptr.group(1), _normalize_decl(statement)
    cleaned = re.sub(r":\s*\d+\s*$", "", statement)
    cleaned = re.sub(r"\[(?:[^\[\]]|\[(?:[^\[\]])*\])*\]", "", cleaned)
    cleaned = ATTR_RE.sub("", cleaned)
    tokens = IDENT_RE.findall(cleaned)
    name = None
    for token in reversed(tokens):
        if token not in TYPE_KEYWORDS:
            name = token
            break
    if name is None:
        return None, None
    return name, _normalize_decl(statement)


def parse_struct_members(body):
    members = []
    for statement in iter_top_level_statements(body):
        inner, after = _inner_anonymous_body(statement)
        if inner is not None:
            if after:
                name = IDENT_RE.match(after.split("[", 1)[0].strip())
                if name is not None:
                    members.append(
                        {"name": name.group(0), "decl": _normalize_decl(statement)}
                    )
                    continue
            members.extend(parse_struct_members(inner))
            continue
        name, decl = _field_name_from_declarator(statement)
        if name is not None:
            members.append({"name": name, "decl": decl})
    return members


def fingerprint_struct(text, spec):
    body = extract_struct_body(text, spec["name"])
    if body is None:
        return None
    members = parse_struct_members(body)
    names = [member["name"] for member in members]
    index_by_name = {}
    for index, member in enumerate(members):
        index_by_name.setdefault(member["name"], index)
    used = {}
    missing = []
    optional = bool(spec.get("optional"))
    for field in spec["fields"]:
        if field not in index_by_name:
            if not optional:
                missing.append(field)
            continue
        index = index_by_name[field]
        used[field] = {
            "index": index,
            "decl": members[index]["decl"],
            "predecessors": names[:index],
        }
    return {
        "name": spec["name"],
        "members": names,
        "used": used,
        "missing": missing,
    }


def compact_fingerprint(structs):
    used = {}
    missing = []
    member_counts = {}
    for name, fingerprint in sorted(structs.items()):
        member_counts[name] = len(fingerprint["members"])
        for field, meta in sorted(fingerprint["used"].items()):
            used[f"{name}.{field}"] = {
                "index": meta["index"],
                "decl": meta["decl"],
            }
        for field in fingerprint["missing"]:
            missing.append(f"{name}.{field}")
    payload = {
        "member_counts": member_counts,
        "missing": missing,
        "used": used,
    }
    digest = hashlib.sha256(
        json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()
    payload["digest"] = digest
    return payload


def diff_compact(old, new, *, against):
    if old is None or new is None:
        return []
    if old.get("digest") == new.get("digest"):
        return []
    changes = []
    old_used = old.get("used") or {}
    new_used = new.get("used") or {}
    for key in sorted(set(old_used) | set(new_used)):
        previous = old_used.get(key)
        current = new_used.get(key)
        if previous == current:
            continue
        struct_name, field = key.split(".", 1)
        if previous is None:
            changes.append(
                {
                    "kind": "layout",
                    "severity": "offset_risk",
                    "against": against,
                    "struct": struct_name,
                    "field": field,
                    "detail": "used field appeared",
                }
            )
            continue
        if current is None:
            changes.append(
                {
                    "kind": "layout",
                    "severity": "offset_risk",
                    "against": against,
                    "struct": struct_name,
                    "field": field,
                    "detail": "used field disappeared",
                }
            )
            continue
        parts = []
        if previous["index"] != current["index"]:
            parts.append(f"index {previous['index']} -> {current['index']}")
        if previous["decl"] != current["decl"]:
            parts.append("declaration changed")
        changes.append(
            {
                "kind": "layout",
                "severity": "offset_risk",
                "against": against,
                "struct": struct_name,
                "field": field,
                "detail": "; ".join(parts) or "changed",
            }
        )
    reported = {(item["struct"], item["field"]) for item in changes}
    old_missing = set(old.get("missing") or [])
    new_missing = set(new.get("missing") or [])
    for key in sorted(new_missing - old_missing):
        struct_name, field = key.split(".", 1)
        if (struct_name, field) in reported:
            continue
        changes.append(
            {
                "kind": "layout",
                "severity": "offset_risk",
                "against": against,
                "struct": struct_name,
                "field": field,
                "detail": "used field disappeared",
            }
        )
        reported.add((struct_name, field))
    used_structs = {item["struct"] for item in changes}
    old_counts = old.get("member_counts") or {}
    new_counts = new.get("member_counts") or {}
    for struct_name in sorted(set(old_counts) | set(new_counts)):
        if old_counts.get(struct_name) == new_counts.get(struct_name):
            continue
        if struct_name in used_structs:
            continue
        changes.append(
            {
                "kind": "layout",
                "severity": "sizeof_risk",
                "against": against,
                "struct": struct_name,
                "field": "*",
                "detail": (
                    f"member count {old_counts.get(struct_name, 0)} -> "
                    f"{new_counts.get(struct_name, 0)}; "
                    "NeverC-read field indices unchanged"
                ),
            }
        )
    return changes


def fingerprint_headers(texts_by_path):
    structs = {}
    for spec in WATCHED_STRUCTS:
        for path in spec["paths"]:
            text = texts_by_path.get(path)
            if not text:
                continue
            fingerprint = fingerprint_struct(text, spec)
            if fingerprint is None:
                continue
            structs[spec["name"]] = fingerprint
            break
    return structs


def unique_header_paths():
    paths = []
    seen = set()
    for spec in WATCHED_STRUCTS:
        for path in spec["paths"]:
            if path in seen:
                continue
            seen.add(path)
            paths.append(path)
    return paths
