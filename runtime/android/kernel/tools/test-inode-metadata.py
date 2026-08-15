#!/usr/bin/env python3
"""Build and run the host fixture for opaque filename/path/inode access."""

import os
from pathlib import Path
import json
import shlex
import subprocess
import sys
import tempfile


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent


def check_profile_evidence():
    profile_catalog = {
        entry["legacy_id"]: entry
        for entry in json.loads(
            (RUNTIME_ROOT / "arm64/gki-profiles.json").read_text(
                encoding="utf-8"
            )
        )["profiles"]
    }
    # Each tag's include/linux/fs.h defines name as the first member of
    # struct filename.  The hashes/build-ids bind the generated facts to the
    # exact checked profile artifacts rather than to a version-range guess.
    filename_evidence = {
        510: (
            "refs/tags/android12-5.10.257_r00",
            "02fdb433320203a2ea3634225a7d459e7e9c36ec6fd1d450d64a493c80a2402a",
            "e8707e1241541a9bc7faef5aa909c9ee0942e19c",
        ),
        515: (
            "refs/tags/android13-5.15.208_r00",
            "10279f23fd2da25ec4e7308e735a950edbc67e003ee082295c3a4265568881fa",
            "c9d6826901380c3bbf749f0bda901c0759907040",
        ),
        601: (
            "refs/tags/android14-6.1.174_r00",
            "826879c4782c086fbbb1630f192d4dd6523debf8a3ffcd08ec9fcc8d5118377c",
            "fa5e8475c0a2a08b6f4413e63bd586f8dd23af75",
        ),
        606: (
            "refs/tags/android15-6.6.139_r00",
            "30cc3dae48ed85eea1654188830c1665edf66f06ff6a29aaf999dbb1db7edb92",
            "918625727c3f09f2afc582653695771adc4fef83",
        ),
        612: (
            "refs/tags/android16-6.12.89_r00",
            "422ed6bb6878fbbf3e6d007263347d8598922f0a743d9164d91f6700c34ddbf3",
            "b3b8ca8f204eb51ff7448c2a305bc007ff795dbd",
        ),
        618: (
            "refs/tags/android17-6.18.24_r00",
            "baa888b6d1024b6025b6119fe9e0128c61dda167ee57d21bc3add857cb33ae57",
            "e36c2ac50c3578e730c54bab1ae6fd6da4645c85",
        ),
    }
    expected = {
        510: (704, 88, 8, 96, 8, 104, 8, 112, 8),
        51013: (704, 88, 8, 96, 8, 104, 8, 112, 8),
        515: (752, 88, 8, 96, 8, 104, 8, 112, 8),
        51514: (752, 88, 8, 96, 8, 104, 8, 112, 8),
        601: (704, 88, 8, 96, 8, 104, 8, 112, 8),
        606: (704, 88, 8, 96, 8, 104, 8, 112, 8),
        612: (704, 88, 8, 112, 4, 96, 8, 116, 4),
        618: (688, 88, 8, 112, 4, 96, 8, 116, 4),
    }
    for profile, expected_scalars in expected.items():
        manifest = json.loads(
            (RUNTIME_ROOT / f"arm64/gki-manifests/{profile}.json").read_text(
                encoding="utf-8"
            )
        )
        layouts = manifest["layouts"]
        if profile in filename_evidence:
            source_ref, layout_sha256, build_id = filename_evidence[profile]
            expected_source_ref = (
                f"refs/tags/{manifest['kernel_name']}."
                f"{profile_catalog[profile]['linux_patch']}_r00"
            )
            if source_ref != expected_source_ref:
                raise RuntimeError(
                    f"GKI {profile} filename source ref mismatch: {source_ref}"
                )
            if manifest["evidence"]["layout_sha256"] != layout_sha256:
                raise RuntimeError(
                    f"GKI {profile} filename layout hash mismatch"
                )
            if manifest["evidence"]["vmlinux_build_id"] != build_id:
                raise RuntimeError(
                    f"GKI {profile} filename build-id mismatch"
                )
        else:
            local_evidence = {
                51013: (
                    "90b3782a87354db091200624b45aee13e4f41983d1370cb6bde77725c91fb677",
                    "d2c6bededf522a14de184c527101226c3e258942",
                ),
                51514: (
                    "81d95cea163c171d42b50904e10069bb380c3545eeb426154b1beb7eee874557",
                    "04b7e217e5601b15989535cbdad96f6c62b777a7",
                ),
            }
            if profile not in local_evidence:
                raise RuntimeError(f"GKI {profile} lacks local layout evidence")
            layout_sha256, build_id = local_evidence[profile]
            if manifest["evidence"]["layout_sha256"] != layout_sha256:
                raise RuntimeError(
                    f"GKI {profile} filename layout hash mismatch"
                )
            if manifest["evidence"]["vmlinux_build_id"] != build_id:
                raise RuntimeError(
                    f"GKI {profile} filename build-id mismatch"
                )
        if layouts.get("filename") != {
            "size": 32,
            "members": {"name": 0},
            "member_sizes": {"name": 8},
        }:
            raise RuntimeError(f"GKI {profile} filename.name evidence mismatch")
        inode = layouts["inode"]
        if "i_atime" in inode["members"]:
            timespec = layouts["timespec64"]
            actual = (
                inode["size"],
                inode["members"]["i_atime"] + timespec["members"]["tv_sec"],
                timespec["member_sizes"]["tv_sec"],
                inode["members"]["i_atime"] + timespec["members"]["tv_nsec"],
                timespec["member_sizes"]["tv_nsec"],
                inode["members"]["i_mtime"] + timespec["members"]["tv_sec"],
                timespec["member_sizes"]["tv_sec"],
                inode["members"]["i_mtime"] + timespec["members"]["tv_nsec"],
                timespec["member_sizes"]["tv_nsec"],
            )
        else:
            actual = (
                inode["size"],
                inode["members"]["i_atime_sec"],
                inode["member_sizes"]["i_atime_sec"],
                inode["members"]["i_atime_nsec"],
                inode["member_sizes"]["i_atime_nsec"],
                inode["members"]["i_mtime_sec"],
                inode["member_sizes"]["i_mtime_sec"],
                inode["members"]["i_mtime_nsec"],
                inode["member_sizes"]["i_mtime_nsec"],
            )
        if actual != expected_scalars:
            raise RuntimeError(
                f"GKI {profile} inode scalar evidence mismatch: {actual}"
            )
        if (
            layouts["path"]["size"],
            layouts["path"]["members"]["dentry"],
            layouts["path"]["member_sizes"]["dentry"],
            layouts["dentry"]["size"],
            layouts["dentry"]["members"]["d_inode"],
            layouts["dentry"]["member_sizes"]["d_inode"],
        ) != (16, 8, 8, 208, 48, 8):
            raise RuntimeError(f"GKI {profile} path_inode evidence mismatch")

    certificates = json.loads(
        (RUNTIME_ROOT / "arm64/gki-layout-certificates.json").read_text(
            encoding="utf-8"
        )
    )["certificates"]
    if not certificates:
        raise RuntimeError("unexpected filename/path/inode certificate count")
    certificate = None
    for candidate in certificates:
        if "inode_times" not in candidate or "filename_name" not in candidate:
            raise RuntimeError(
                "layout certificate is missing filename/path/inode evidence"
            )
        if candidate.get("raw_btf", {}).get("sha256") == (
            "ae81b5a86e938c2d2db08e4c78c712143a61c1d823f96383a019b86e9e8b2e79"
        ):
            certificate = candidate
    if certificate is None:
        raise RuntimeError(
            "unexpected filename/path/inode certificate BTF identity"
        )
    if certificate.get("filename_name") != {
        "size": 32,
        "members": {"name": 0},
        "member_sizes": {"name": 8},
    }:
        raise RuntimeError("unexpected filename_name compatibility certificate")
    if certificate["inode_times"] != {
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
    }:
        raise RuntimeError("unexpected inode_times compatibility certificate")
    if certificate["path_inode"] != {
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
    }:
        raise RuntimeError("unexpected path_inode compatibility certificate")


def main():
    subprocess.run(
        [sys.executable, str(TOOLS_ROOT / "generate-compat-table.py"), "--check"],
        check=True,
    )
    check_profile_evidence()
    compiler = shlex.split(os.environ.get("CC", "cc"))
    if not compiler:
        raise RuntimeError("CC does not name a compiler")

    with tempfile.TemporaryDirectory(prefix="neverc-inode-metadata-") as tmp:
        output = Path(tmp) / "test-inode-metadata"
        command = compiler + [
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DNEVERC_KRT_INODE_METADATA_HOST_TEST=1",
            f"-I{RUNTIME_ROOT / 'arm64/include'}",
            f"-I{RUNTIME_ROOT / 'include'}",
            f"-I{TOOLS_ROOT}",
            str(TOOLS_ROOT / "test-inode-metadata.c"),
            str(RUNTIME_ROOT / "src/nvk_inode.c"),
            "-o",
            str(output),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(output)], check=True)

    print("test-inode-metadata: OK")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"test-inode-metadata: {error}", file=sys.stderr)
        sys.exit(1)
