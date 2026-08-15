#!/usr/bin/env python3
"""Build and run the host fixture for the opaque dir_context filter."""

import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent
PROFILES = (510, 51013, 515, 51514, 601, 606, 612, 618)


def certificate_dir_context(certificate):
    if "dir_context" in certificate:
        return certificate["dir_context"]
    fields = certificate.get("runtime_layout", {}).get("fields")
    if fields is None:
        return None
    return {
        "member_sizes": {
            "actor": fields["dir_context_actor_size"],
            "pos": fields["dir_context_pos_size"],
        },
        "members": {
            "actor": fields["dir_context_actor"],
            "pos": fields["dir_context_pos"],
        },
        "size": fields["dir_context_size"],
    }


def check_profile_evidence():
    expected_abis = {
        510: "returns_int",
        51013: "returns_int",
        515: "returns_int",
        51514: "returns_int",
        601: "returns_bool",
        606: "returns_bool",
        612: "returns_bool",
        618: "returns_bool",
    }
    catalog = json.loads(
        (RUNTIME_ROOT / "arm64/gki-profiles.json").read_text(encoding="utf-8")
    )
    actual_abis = {
        profile["legacy_id"]: profile["capabilities"]["filldir_abi"]
        for profile in catalog["profiles"]
    }
    if actual_abis != expected_abis:
        raise RuntimeError(f"unexpected filldir ABI evidence: {actual_abis}")

    expected_layout = {
        "member_sizes": {"actor": 8, "pos": 8},
        "members": {"actor": 0, "pos": 8},
        "size": 16,
    }
    for profile in PROFILES:
        manifest_path = (
            RUNTIME_ROOT / f"arm64/gki-manifests/{profile}.json"
        )
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        actual = manifest["layouts"].get("dir_context")
        expected_profile_layout = expected_layout
        if profile == 618:
            expected_profile_layout = {
                "member_sizes": {
                    "actor": 8,
                    "count": 4,
                    "dt_flags_mask": 4,
                    "pos": 8,
                },
                "members": {
                    "actor": 0,
                    "count": 16,
                    "dt_flags_mask": 20,
                    "pos": 8,
                },
                "size": 24,
            }
        if actual != expected_profile_layout:
            raise RuntimeError(
                f"GKI {profile} dir_context evidence mismatch: {actual}"
            )

    expected_certificate = {
        "profile_id": 612,
        "release_token": (
            "6.12.38-android16-5-g8c67d4274c0a-ab14275539-4k"
        ),
        "identity": {
            "linux_major": 6,
            "linux_minor": 12,
            "linux_patch": 38,
            "android_release": 16,
            "kmi_generation": 5,
            "page_shift": 12,
        },
        "raw_btf": {
            "sha256": (
                "ae81b5a86e938c2d2db08e4c78c712143a61c1d823f96383a019b86e9e8b2e79"
            ),
            "size": 6909037,
        },
        "dir_context": expected_layout,
        "filldir_abi": "returns_bool",
        "inode_times": {
            "size": 704,
            "members": {
                "atime_nsec": 112,
                "atime_sec": 88,
                "mtime_nsec": 116,
                "mtime_sec": 96,
            },
            "member_sizes": {
                "atime_nsec": 4,
                "atime_sec": 8,
                "mtime_nsec": 4,
                "mtime_sec": 8,
            },
        },
        "path_inode": {
            "path": {
                "size": 16,
                "members": {"dentry": 8},
                "member_sizes": {"dentry": 8},
            },
            "dentry": {
                "size": 208,
                "members": {"d_inode": 48},
                "member_sizes": {"d_inode": 8},
            },
        },
    }
    certificate_document = json.loads(
        (
            RUNTIME_ROOT / "arm64/gki-layout-certificates.json"
        ).read_text(encoding="utf-8")
    )
    certificates = certificate_document.get("certificates", [])
    if certificate_document.get("schema") != 2 or not certificates:
        raise RuntimeError(
            f"unexpected dir_context certificate: {certificate_document}"
        )
    original_612 = None
    for certificate in certificates:
        actual_dir_context = certificate_dir_context(certificate)
        if actual_dir_context != expected_layout and not (
            certificate.get("profile_id") == 618
            and actual_dir_context is not None
            and actual_dir_context.get("size") == 24
        ):
            raise RuntimeError(
                f"dir_context certificate fields mismatch: {certificate}"
            )
        if certificate.get("raw_btf", {}).get("sha256") == (
            "ae81b5a86e938c2d2db08e4c78c712143a61c1d823f96383a019b86e9e8b2e79"
        ):
            original_612 = certificate
    if original_612 is None:
        raise RuntimeError("missing original 6.12 layout certificate")
    if any(
        original_612.get(key) != value
        for key, value in expected_certificate.items()
    ):
        raise RuntimeError(
            f"dir_context certificate fields mismatch: {certificate_document}"
        )


def main():
    check_profile_evidence()
    compiler = shlex.split(os.environ.get("CC", "cc"))
    if not compiler:
        raise RuntimeError("CC does not name a compiler")

    with tempfile.TemporaryDirectory(prefix="neverc-dir-filter-") as tmp:
        output = Path(tmp) / "test-dir-filter"
        command = compiler + [
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DNEVERC_KRT_DIR_FILTER_HOST_TEST=1",
            f"-I{RUNTIME_ROOT / 'arm64/include'}",
            f"-I{RUNTIME_ROOT / 'include'}",
            f"-I{TOOLS_ROOT}",
            str(TOOLS_ROOT / "test-dir-filter.c"),
            str(RUNTIME_ROOT / "src/nvk_dir.c"),
            "-o",
            str(output),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(output)], check=True)

    print("test-dir-filter: OK")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"test-dir-filter: {error}", file=sys.stderr)
        sys.exit(1)
