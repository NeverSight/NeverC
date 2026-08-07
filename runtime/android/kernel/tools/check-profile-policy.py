#!/usr/bin/env python3
"""Enforce ownership boundaries for opaque Android kernel profile IDs."""

import argparse
import json
from pathlib import Path
import re
import sys


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent
REPO_ROOT = RUNTIME_ROOT.parents[2]
DEFAULT_CATALOG = RUNTIME_ROOT / "arm64/gki-profiles.json"
PROFILE_MACRO = re.compile(r"\b(?:NVK_KERNEL|NEVERC_KRT_KERNEL)\b")
RAW_RUNTIME_STATE = re.compile(r"\b_neverc_krt_kernel_ver\b")
CHRONOLOGICAL_ABI_NAME = re.compile(
    r"\b_neverc_krt_[A-Za-z0-9_]*(?:legacy|modern)[A-Za-z0-9_]*\b"
)
CAPABILITY_DISPATCH_SOURCES = {
    "nvk_interpose.c",
    "nvk_pid_vis.c",
    "nvk_ksyms.c",
    "nvk_xmem.c",
}


def matching_lines(path, pattern):
    text = path.read_text(encoding="utf-8")
    for line_number, line in enumerate(text.splitlines(), 1):
        if pattern.search(line):
            yield line_number


def load_profile_ids(path):
    document = json.loads(path.read_text(encoding="utf-8"))
    profile_ids = [profile["legacy_id"] for profile in document["profiles"]]
    if not profile_ids or any(
        not isinstance(profile_id, int) or isinstance(profile_id, bool)
        for profile_id in profile_ids
    ):
        raise ValueError(f"{path}: invalid profile IDs")
    return profile_ids


def profile_literal_pattern(profile_ids):
    alternatives = "|".join(re.escape(str(value)) for value in profile_ids)
    return re.compile(rf"\b(?:{alternatives})\b")


def ordered_profile_literal_pattern(profile_ids):
    alternatives = "|".join(re.escape(str(value)) for value in profile_ids)
    name = r"(?:profile(?:_id)?|kernel(?:_ver(?:sion)?)?|kv)"
    return re.compile(
        rf"(?:\b{name}\b[^\n]{{0,80}}(?:>=|<=|>|<)\s*(?:{alternatives})\b|"
        rf"\b(?:{alternatives})\b\s*(?:>=|<=|>|<)[^\n]{{0,80}}\b{name}\b)",
        re.IGNORECASE,
    )


def collect_violations(repo_root, runtime_root, profile_ids):
    violations = []
    legacy_profile_literal = profile_literal_pattern(profile_ids)
    ordered_profile_literal = ordered_profile_literal_pattern(profile_ids)

    compiler_roots = (
        repo_root / "neverc/lib/Compiler",
        repo_root / "neverc/lib/Invoke/ToolChains",
        repo_root / "neverc/lib/Emit",
        repo_root / "neverc/lib/Linker",
        repo_root / "neverc/lib/Plugin/Link",
        repo_root / "neverc/include/neverc/Emit",
    )
    for root in compiler_roots:
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if path.suffix not in {".cpp", ".h"}:
                continue
            for pattern, message in (
                (legacy_profile_literal,
                 "compiler must not enumerate Android kernel profile IDs"),
                (PROFILE_MACRO,
                 "compiler must not parse Android kernel profile macros"),
            ):
                for line_number in matching_lines(path, pattern):
                    violations.append((path, line_number, message))

    compiler_contract_files = (
        repo_root
        / "neverc/include/neverc/Foundation/AndroidKernelProfileContract.h",
        repo_root / "llvm/include/llvm/LTO/Config.h",
        repo_root / "llvm/lib/LTO/LTO.cpp",
    )
    for path in compiler_contract_files:
        if not path.is_file():
            continue
        for pattern, message in (
            (legacy_profile_literal,
             "compiler contract layer must not enumerate profile IDs"),
            (PROFILE_MACRO,
             "compiler contract layer must not parse profile macros"),
        ):
            for line_number in matching_lines(path, pattern):
                violations.append((path, line_number, message))

    runtime_source = runtime_root / "src"
    for path in (sorted(runtime_source.iterdir()) if runtime_source.is_dir() else ()):
        if path.suffix not in {".c", ".h"}:
            continue
        checks = [
            (legacy_profile_literal,
             "runtime source must use the generated profile policy"),
            (PROFILE_MACRO,
             "runtime source must not consume caller-side profile macros"),
            (RAW_RUNTIME_STATE,
             "runtime source must use verified profile accessors"),
        ]
        if path.name in CAPABILITY_DISPATCH_SOURCES:
            checks.append(
                (CHRONOLOGICAL_ABI_NAME,
                 "ABI implementations must be named by signature, not chronology")
            )
        for pattern, message in checks:
            for line_number in matching_lines(path, pattern):
                violations.append((path, line_number, message))

    sdk_include = runtime_root / "arm64/include"
    if sdk_include.is_dir():
        for path in sorted(sdk_include.rglob("*.h")):
            for line_number in matching_lines(path, PROFILE_MACRO):
                violations.append(
                    (
                        path,
                        line_number,
                        "SDK headers must branch on semantic Linux version predicates, "
                        "not opaque profile handles",
                    )
                )

    runtime_tools = runtime_root / "tools"
    if runtime_tools.is_dir():
        for path in sorted(runtime_tools.iterdir()):
            if path.name.startswith("test-") or path.suffix not in {
                ".c", ".py", ".sh"
            }:
                continue
            for line_number in matching_lines(path, ordered_profile_literal):
                violations.append(
                    (
                        path,
                        line_number,
                        "runtime tools must not order opaque profile IDs",
                    )
                )

    return violations


def main():
    parser = argparse.ArgumentParser(
        description="check Android kernel profile ownership boundaries"
    )
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--runtime-root", type=Path, default=RUNTIME_ROOT)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    args = parser.parse_args()

    try:
        profile_ids = load_profile_ids(args.catalog)
        violations = collect_violations(
            args.repo_root, args.runtime_root, profile_ids
        )
    except (json.JSONDecodeError, KeyError, OSError, TypeError, ValueError) as error:
        print(f"check-profile-policy: {error}", file=sys.stderr)
        return 1

    for path, line_number, message in violations:
        try:
            display_path = path.relative_to(args.repo_root)
        except ValueError:
            display_path = path
        print(
            f"{display_path}:{line_number}: {message}",
            file=sys.stderr,
        )
    if violations:
        print(
            f"check-profile-policy: {len(violations)} violation(s)",
            file=sys.stderr,
        )
        return 1
    print("check-profile-policy: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
