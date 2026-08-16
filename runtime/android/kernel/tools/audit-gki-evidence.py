#!/usr/bin/env python3
"""Audit local GKI source/binary evidence without embedding host paths."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import sys
from typing import Any


ARTIFACT_KEYS = ("config", "image", "symvers", "system_map", "vmlinux")
MANIFEST_BOUND_ARTIFACTS = ("config", "symvers", "vmlinux")
BACKEND_SYMBOLS = {
    ("binder_filter_backend", "transaction"): "binder_transaction",
    ("vmalloc_visibility_backend", "seq_operations"): "vmalloc_op",
    ("vmalloc_visibility_backend", "named_show"): "vmalloc_info_show",
}
REQUIRED_SYMBOLS = (
    "kobject_del",
    "module_mutex",
    "modules",
    "modules_op",
    "mutex_lock",
    "mutex_unlock",
    "filp_open",
    "filp_close",
    "kernel_read",
    "kernel_write",
    "vfs_llseek",
    "task_active_pid_ns",
    "pid_nr_ns",
    "register_pm_notifier",
    "unregister_pm_notifier",
    "register_reboot_notifier",
    "unregister_reboot_notifier",
)
ALTERNATIVE_SYMBOLS = (
    ("vfs_fstatat", "vfs_stat"),
)


class AuditError(RuntimeError):
    """An audit input is malformed."""


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AuditError(f"cannot read JSON input {path.name}: {error}") from error
    if not isinstance(value, dict):
        raise AuditError(f"{path.name} must contain a JSON object")
    return value


def checked_relative_path(value: object, context: str) -> str | None:
    if value is None:
        return None
    if not isinstance(value, str) or not value or "\\" in value:
        raise AuditError(f"{context} must be a normalized relative path or null")
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or str(path) != value:
        raise AuditError(f"{context} must be a normalized relative path")
    return value


def load_catalog(path: Path) -> dict[str, dict[str, Any]]:
    document = load_json(path)
    if document.get("schema") != 1:
        raise AuditError("evidence catalog schema must be 1")
    profiles = document.get("profiles")
    if not isinstance(profiles, dict) or not profiles:
        raise AuditError("evidence catalog profiles must be a non-empty object")

    checked: dict[str, dict[str, Any]] = {}
    for profile_id, entry in profiles.items():
        context = f"profile {profile_id}"
        if not isinstance(profile_id, str) or not profile_id.isdigit():
            raise AuditError("evidence catalog profile IDs must be decimal strings")
        if not isinstance(entry, dict) or set(entry) != {
            "artifacts",
            "manifest_bound_artifacts",
            "source_roots",
        }:
            raise AuditError(
                f"{context} fields must be artifacts, "
                "manifest_bound_artifacts, and source_roots"
            )
        manifest_bound = entry["manifest_bound_artifacts"]
        if (
            not isinstance(manifest_bound, list)
            or len(set(manifest_bound)) != len(manifest_bound)
            or any(value not in MANIFEST_BOUND_ARTIFACTS for value in manifest_bound)
        ):
            raise AuditError(
                f"{context} manifest_bound_artifacts contains an invalid value"
            )
        source_roots = entry["source_roots"]
        if not isinstance(source_roots, list):
            raise AuditError(f"{context} source_roots must be an array")
        checked_roots = [
            checked_relative_path(value, f"{context} source_roots")
            for value in source_roots
        ]
        artifacts = entry["artifacts"]
        if not isinstance(artifacts, dict) or set(artifacts) != set(ARTIFACT_KEYS):
            raise AuditError(
                f"{context} artifacts must be exactly {', '.join(ARTIFACT_KEYS)}"
            )
        checked[profile_id] = {
            "source_roots": checked_roots,
            "manifest_bound_artifacts": manifest_bound,
            "artifacts": {
                key: checked_relative_path(
                    artifacts[key], f"{context} artifact {key}"
                )
                for key in ARTIFACT_KEYS
            },
        }
    return checked


def load_profiles(path: Path) -> dict[str, dict[str, Any]]:
    document = load_json(path)
    values = document.get("profiles")
    if not isinstance(values, list):
        raise AuditError("profile catalog profiles must be an array")
    profiles: dict[str, dict[str, Any]] = {}
    for entry in values:
        if not isinstance(entry, dict) or not isinstance(entry.get("legacy_id"), int):
            raise AuditError("profile catalog contains an invalid profile")
        profile_id = str(entry["legacy_id"])
        capabilities = entry.get("capabilities")
        if not isinstance(capabilities, dict):
            raise AuditError(f"profile {profile_id} capabilities must be an object")
        profiles[profile_id] = entry
    return profiles


def system_map_symbols(path: Path) -> set[str]:
    symbols: set[str] = set()
    try:
        with path.open(encoding="utf-8", errors="replace") as stream:
            for line in stream:
                fields = line.split()
                if len(fields) >= 3:
                    symbols.add(fields[-1])
    except OSError as error:
        raise AuditError(f"cannot read System.map evidence: {error}") from error
    return symbols


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def vmlinux_evidence(path: Path) -> dict[str, str]:
    tool_path = Path(__file__).resolve().parent / "generate-gki-manifest.py"
    spec = importlib.util.spec_from_file_location("nvk_generate_gki_manifest", tool_path)
    if spec is None or spec.loader is None:
        raise AuditError("cannot load the GKI manifest evidence reader")
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
        value = module.elf_evidence(path)
    except (ImportError, OSError, ValueError) as error:
        raise AuditError(f"cannot read vmlinux identity evidence: {error}") from error
    return value


def audit_profile(
    profile_id: str,
    profile: dict[str, Any],
    evidence: dict[str, Any],
    local_docs: Path,
    manifest_root: Path,
) -> dict[str, Any]:
    issues: list[str] = []
    artifacts = evidence["artifacts"]
    present: dict[str, bool] = {}

    for key in ARTIFACT_KEYS:
        relative = artifacts[key]
        if relative is None:
            present[key] = False
            issues.append(f"unavailable-artifact:{key}")
            continue
        exists = (local_docs / relative).is_file()
        present[key] = exists
        if not exists:
            issues.append(f"missing-artifact:{key}")

    source_roots = evidence["source_roots"]
    source_present = [
        relative
        for relative in source_roots
        if relative is not None and (local_docs / relative).is_dir()
    ]
    if not source_roots:
        issues.append("unavailable-source-tree")
    elif len(source_present) != len(source_roots):
        issues.append("missing-source-tree")

    symbols: set[str] = set()
    if present["system_map"] and artifacts["system_map"] is not None:
        symbols = system_map_symbols(local_docs / artifacts["system_map"])
    capabilities = profile["capabilities"]
    expected_symbols: list[str] = list(REQUIRED_SYMBOLS)
    for symbol in REQUIRED_SYMBOLS:
        if symbol not in symbols:
            issues.append(f"missing-symbol:{symbol}")
    for primary, fallback in ALTERNATIVE_SYMBOLS:
        if primary in symbols:
            expected_symbols.append(primary)
        elif fallback in symbols:
            expected_symbols.append(fallback)
        else:
            expected_symbols.append(primary)
            issues.append(f"missing-symbol:{primary}|{fallback}")
    for (capability, backend), symbol in BACKEND_SYMBOLS.items():
        if capabilities.get(capability) == backend:
            expected_symbols.append(symbol)
            if symbol not in symbols:
                issues.append(f"missing-symbol:{symbol}")

    manifest_evidence: dict[str, Any] = {}
    manifest_bound = evidence["manifest_bound_artifacts"]
    for artifact_key in MANIFEST_BOUND_ARTIFACTS:
        if artifact_key not in manifest_bound:
            issues.append(f"unbound-manifest-artifact:{artifact_key}")
    manifest_path = manifest_root / f"{profile_id}.json"
    if not manifest_path.is_file():
        issues.append("missing-profile-manifest")
    else:
        manifest = load_json(manifest_path)
        if manifest.get("profile") != int(profile_id):
            issues.append("profile-manifest-id-mismatch")
        raw_evidence = manifest.get("evidence")
        if not isinstance(raw_evidence, dict):
            issues.append("profile-manifest-evidence-invalid")
        else:
            manifest_evidence = raw_evidence
            for artifact_key, evidence_key in (
                ("config", "config_sha256"),
                ("symvers", "symvers_sha256"),
            ):
                if artifact_key not in manifest_bound:
                    continue
                relative = artifacts[artifact_key]
                if not present[artifact_key] or relative is None:
                    continue
                expected = raw_evidence.get(evidence_key)
                if not isinstance(expected, str):
                    issues.append(f"manifest-missing-evidence:{evidence_key}")
                    continue
                try:
                    actual = sha256_file(local_docs / relative)
                except OSError as error:
                    raise AuditError(
                        f"cannot hash {artifact_key} evidence: {error}"
                    ) from error
                if actual != expected:
                    issues.append(f"evidence-drift:{evidence_key}")
            if "vmlinux" in manifest_bound:
                relative = artifacts["vmlinux"]
                if present["vmlinux"] and relative is not None:
                    actual = vmlinux_evidence(local_docs / relative)
                    for evidence_key in ("layout_sha256", "vmlinux_build_id"):
                        expected = raw_evidence.get(evidence_key)
                        if not isinstance(expected, str):
                            issues.append(
                                f"manifest-missing-evidence:{evidence_key}"
                            )
                        elif actual.get(evidence_key) != expected:
                            issues.append(f"evidence-drift:{evidence_key}")

    return {
        "kernel_name": profile.get("kernel_name", ""),
        "status": "pass" if not issues else "fail",
        "source_roots": source_roots,
        "artifacts": artifacts,
        "artifact_present": present,
        "expected_symbols": sorted(expected_symbols),
        "manifest_bound_artifacts": manifest_bound,
        "manifest_evidence": manifest_evidence,
        "issues": sorted(issues),
    }


def audit(
    catalog: dict[str, dict[str, Any]],
    profiles: dict[str, dict[str, Any]],
    local_docs: Path,
    manifest_root: Path,
) -> dict[str, Any]:
    profile_reports: dict[str, Any] = {}
    global_issues: list[str] = []
    catalog_ids = set(catalog)
    profile_ids = set(profiles)
    for missing in sorted(profile_ids - catalog_ids, key=int):
        global_issues.append(f"missing-catalog-profile:{missing}")
    for unknown in sorted(catalog_ids - profile_ids, key=int):
        global_issues.append(f"unknown-catalog-profile:{unknown}")

    for profile_id in sorted(catalog_ids & profile_ids, key=int):
        profile_reports[profile_id] = audit_profile(
            profile_id,
            profiles[profile_id],
            catalog[profile_id],
            local_docs,
            manifest_root,
        )
    failed = sum(report["status"] == "fail" for report in profile_reports.values())
    return {
        "schema": 1,
        "status": "pass" if not global_issues and failed == 0 else "fail",
        "summary": {
            "profiles": len(profile_reports),
            "failed_profiles": failed,
            "global_issues": len(global_issues),
        },
        "global_issues": global_issues,
        "profiles": profile_reports,
    }


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# GKI evidence audit",
        "",
        f"Status: **{report['status']}**",
        (
            f"Profiles: {report['summary']['profiles']}; "
            f"failed: {report['summary']['failed_profiles']}."
        ),
    ]
    if report["global_issues"]:
        lines.extend(["", "Global issues:"])
        lines.extend(f"- `{issue}`" for issue in report["global_issues"])
    for profile_id, profile in report["profiles"].items():
        lines.extend(
            [
                "",
                f"## {profile_id} — {profile['kernel_name']}",
                f"Status: **{profile['status']}**",
            ]
        )
        if profile["issues"]:
            lines.extend(f"- `{issue}`" for issue in profile["issues"])
        else:
            lines.append("- Evidence and backend symbols present.")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="audit local GKI source, binary, and backend-symbol evidence"
    )
    runtime_root = Path(__file__).resolve().parents[1]
    parser.add_argument(
        "--catalog",
        type=Path,
        default=runtime_root / "arm64/gki-local-evidence.json",
    )
    parser.add_argument(
        "--profiles",
        type=Path,
        default=runtime_root / "arm64/gki-profiles.json",
    )
    parser.add_argument(
        "--manifest-root",
        type=Path,
        default=runtime_root / "arm64/gki-manifests",
    )
    parser.add_argument("--local-docs", required=True, type=Path)
    parser.add_argument("--format", choices=("json", "markdown"), default="json")
    args = parser.parse_args()

    try:
        report = audit(
            load_catalog(args.catalog),
            load_profiles(args.profiles),
            args.local_docs,
            args.manifest_root,
        )
    except AuditError as error:
        print(f"audit-gki-evidence: {error}", file=sys.stderr)
        return 2

    if args.format == "json":
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(render_markdown(report), end="")
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
