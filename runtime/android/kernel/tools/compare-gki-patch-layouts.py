#!/usr/bin/env python3
"""Compare NeverC-read kernel layouts across GKI patch identities.

Answers whether linux_patch (the 45 in 6.12.45) moves the fields the runtime
actually reads, versus android_release / KMI generation / page size.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent
DEFAULT_MANIFEST_ROOT = RUNTIME_ROOT / "arm64/gki-manifests"
DEFAULT_CERTIFICATES = RUNTIME_ROOT / "arm64/gki-layout-certificates.json"

COMPARE_FIELDS = (
    ("dir_context", "dir_context", ("actor", "pos")),
    ("filename", "filename", ("name",)),
    ("inode", "inode", None),
    ("path", "path", ("dentry",)),
    ("dentry", "dentry", ("d_inode",)),
    ("cred", "cred", ("uid", "gid", "suid", "sgid", "euid", "egid", "fsuid", "fsgid")),
    ("task_struct", "task_struct", (
        "comm", "flags", "group_leader", "mm", "parent", "pid", "real_cred",
        "real_parent", "signal", "stack", "stack_refcount", "tasks",
        "thread_node", "thread_pid", "usage",
    )),
    ("signal_struct", "signal_struct", ("thread_head",)),
    ("mm_struct", "mm_struct", ("mm_count", "mmap_lock", "page_table_lock", "pgd")),
    ("vm_area_struct", "vm_area_struct", (
        "vm_start", "vm_end", "vm_mm", "vm_flags", "vm_pgoff",
    )),
    ("pt_regs", "pt_regs", ("regs", "sp", "pc", "pstate")),
)


def load_tool(name, filename):
    import importlib.util

    spec = importlib.util.spec_from_file_location(name, TOOLS_ROOT / filename)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {filename}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


COMPAT = load_tool("nvk_compare_compat", "generate-compat-table.py")
EXTRACT = load_tool("nvk_compare_extract", "extract-btf-layouts.py")
CERT = load_tool("nvk_compare_cert", "generate-layout-certificate.py")


def project_layout(layouts, structure, members):
    if structure not in layouts:
        return None
    layout = layouts[structure]
    if members is None:
        if structure == "inode":
            try:
                normalized = COMPAT.normalize_inode_times_layout(
                    {"inode": layout, "timespec64": layouts.get("timespec64", {})},
                    structure,
                )
            except ValueError:
                normalized = None
            if normalized is not None:
                return {
                    "size": normalized["size"],
                    "members": dict(sorted(normalized["members"].items())),
                    "member_sizes": dict(sorted(normalized["member_sizes"].items())),
                }
        members = tuple(sorted(layout.get("members", {})))
    present = [name for name in members if name in layout.get("members", {})]
    if not present:
        return {"size": layout.get("size"), "members": {}, "member_sizes": {}}
    return {
        "size": layout.get("size"),
        "members": {name: layout["members"][name] for name in present},
        "member_sizes": {
            name: layout.get("member_sizes", {}).get(name)
            for name in present
        },
    }


def project_all(layouts):
    return {
        label: project_layout(layouts, structure, members)
        for label, structure, members in COMPARE_FIELDS
    }


def identity_key(identity):
    return (
        identity.get("linux_major"),
        identity.get("linux_minor"),
        identity.get("android_release"),
        identity.get("kmi_generation"),
        identity.get("page_shift"),
    )


def series_key(identity):
    return (identity.get("linux_major"), identity.get("linux_minor"))


def load_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def record_from_extract(path, release_token, layouts=None):
    if layouts is None:
        layouts = EXTRACT.extract_layouts(str(path), CERT.CERTIFICATE_STRUCTURES)
    identity = CERT.identity_from_token(release_token, 12)
    return {
        "source": str(path),
        "release_token": release_token,
        "identity": identity,
        "fields": project_all(layouts),
    }


def record_from_manifest(path):
    manifest = load_json(path)
    profile_id = manifest["profile"]
    catalog = load_json(RUNTIME_ROOT / "arm64/gki-profiles.json")
    profile = next(
        item for item in catalog["profiles"] if item["legacy_id"] == profile_id
    )
    release_token = (
        f"{profile['linux_major']}.{profile['linux_minor']}."
        f"{profile['linux_patch']}-android{profile['android_release']}-"
        f"{profile['kmi_generation']}"
    )
    return {
        "source": str(path),
        "release_token": release_token,
        "identity": {
            "linux_major": profile["linux_major"],
            "linux_minor": profile["linux_minor"],
            "linux_patch": profile["linux_patch"],
            "android_release": profile["android_release"],
            "kmi_generation": profile["kmi_generation"],
            "page_shift": profile["page_shift"],
        },
        "fields": project_all(manifest["layouts"]),
    }


def record_from_certificate(certificate):
    layouts = {
        "dir_context": certificate.get("dir_context"),
        "filename": certificate.get("filename_name"),
        "inode": None,
        "path": certificate.get("path_inode", {}).get("path"),
        "dentry": certificate.get("path_inode", {}).get("dentry"),
        "cred": certificate.get("task_walk", {}).get("cred"),
        "task_struct": {},
        "signal_struct": certificate.get("task_threads", {}).get("signal_struct"),
        "mm_struct": certificate.get("user_ptmap", {}).get("mm_struct"),
        "vm_area_struct": certificate.get("user_ptmap", {}).get("vm_area_struct"),
        "pt_regs": certificate.get("user_ptmap", {}).get("pt_regs")
        or certificate.get("task_user_state", {}).get("pt_regs"),
    }
    merged_task = {}
    merged_sizes = {}
    for group in (
        certificate.get("task_walk", {}).get("task_struct"),
        certificate.get("task_ref", {}).get("task_struct"),
        certificate.get("task_user_state", {}).get("task_struct"),
        certificate.get("task_threads", {}).get("task_struct"),
    ):
        if not group:
            continue
        merged_task.update(group.get("members", {}))
        merged_sizes.update(group.get("member_sizes", {}))
        layouts["task_struct"] = {
            "size": group.get("size"),
            "members": merged_task,
            "member_sizes": merged_sizes,
        }
    if certificate.get("inode_times"):
        layouts["inode"] = {
            "size": certificate["inode_times"]["size"],
            "members": {
                "i_atime_sec": certificate["inode_times"]["members"]["atime_sec"],
                "i_mtime_sec": certificate["inode_times"]["members"]["mtime_sec"],
                "i_atime_nsec": certificate["inode_times"]["members"]["atime_nsec"],
                "i_mtime_nsec": certificate["inode_times"]["members"]["mtime_nsec"],
            },
            "member_sizes": {
                "i_atime_sec": certificate["inode_times"]["member_sizes"]["atime_sec"],
                "i_mtime_sec": certificate["inode_times"]["member_sizes"]["mtime_sec"],
                "i_atime_nsec": certificate["inode_times"]["member_sizes"]["atime_nsec"],
                "i_mtime_nsec": certificate["inode_times"]["member_sizes"]["mtime_nsec"],
            },
        }
    layouts = {key: value for key, value in layouts.items() if value}
    return {
        "source": f"certificate:{certificate['release_token']}",
        "release_token": certificate["release_token"],
        "identity": certificate["identity"],
        "fields": project_all(layouts),
    }


def diff_fields(left, right):
    diffs = []
    labels = sorted(set(left) | set(right))
    for label in labels:
        lval = left.get(label)
        rval = right.get(label)
        if lval == rval:
            continue
        if lval is None or rval is None:
            diffs.append(f"{label}: missing on one side")
            continue
        if lval.get("size") != rval.get("size"):
            diffs.append(f"{label}.size {lval.get('size')} != {rval.get('size')}")
        names = sorted(set(lval.get("members", {})) | set(rval.get("members", {})))
        for name in names:
            lo = lval.get("members", {}).get(name)
            ro = rval.get("members", {}).get(name)
            if lo is None or ro is None:
                continue
            if lo != ro:
                diffs.append(f"{label}.{name} offset {lo} != {ro}")
            lw = lval.get("member_sizes", {}).get(name)
            rw = rval.get("member_sizes", {}).get(name)
            if lw is not None and rw is not None and lw != rw:
                diffs.append(f"{label}.{name} width {lw} != {rw}")
    return diffs


def compare_groups(records, key_fn, title):
    groups = {}
    for record in records:
        groups.setdefault(key_fn(record["identity"]), []).append(record)
    print(f"## {title}")
    any_diff = False
    for key, items in sorted(groups.items()):
        if len(items) < 2:
            continue
        baseline = items[0]
        print(f"group {key}")
        for item in items:
            print(f"  {item['release_token']}  ({item['source']})")
        for item in items[1:]:
            diffs = diff_fields(baseline["fields"], item["fields"])
            if diffs:
                any_diff = True
                print(f"  DIFF {baseline['release_token']} vs {item['release_token']}")
                for line in diffs:
                    print(f"    {line}")
            else:
                print(
                    f"  MATCH {baseline['release_token']} vs {item['release_token']}"
                )
        print()
    if not any_diff:
        print("no field diffs inside any group\n")
    return any_diff


def main():
    parser = argparse.ArgumentParser(
        description="compare NeverC-read layouts across GKI patch identities"
    )
    parser.add_argument(
        "--extract-json",
        action="append",
        default=[],
        metavar="TOKEN=PATH",
        help="release_token=/path/to/extract.json",
    )
    parser.add_argument(
        "--vmlinux",
        action="append",
        default=[],
        metavar="TOKEN=PATH",
        help="release_token=/path/to/vmlinux",
    )
    parser.add_argument("--include-manifests", action="store_true")
    parser.add_argument("--include-certificates", action="store_true")
    args = parser.parse_args()

    records = []
    for spec in args.extract_json:
        token, path = spec.split("=", 1)
        records.append(record_from_extract(path, token, load_json(path)))
    for spec in args.vmlinux:
        token, path = spec.split("=", 1)
        print(f"extract {token} from {path}", file=sys.stderr)
        records.append(record_from_extract(path, token))
    if args.include_manifests:
        for path in sorted(DEFAULT_MANIFEST_ROOT.glob("*.json")):
            records.append(record_from_manifest(path))
    if args.include_certificates:
        document = load_json(DEFAULT_CERTIFICATES)
        for certificate in document.get("certificates", []):
            records.append(record_from_certificate(certificate))

    if len(records) < 2:
        print("compare-gki-patch-layouts: need at least two records", file=sys.stderr)
        return 1

    kmi_diff = compare_groups(
        records,
        identity_key,
        "same android_release + KMI + page_shift (true same-family)",
    )
    series_diff = compare_groups(
        records,
        series_key,
        "same linux major.minor only (wide 5.10-style match)",
    )
    print("## summary")
    print(
        "same-KMI patch versions moved NeverC fields"
        if kmi_diff else
        "same-KMI patch versions did not move NeverC fields"
    )
    print(
        "wide major.minor matching would mix different layouts"
        if series_diff else
        "wide major.minor matching saw no extra diffs in this sample"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
