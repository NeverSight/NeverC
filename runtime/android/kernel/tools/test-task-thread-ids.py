#!/usr/bin/env python3
"""Build and run the host fixture for task thread-group TID snapshots."""

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


def check_profile_evidence():
    expected = {
        510: (4736, 1480, 1584, 2008, 1672, 1120, 16),
        51013: (4736, 1480, 1584, 2008, 1672, 1120, 16),
        515: (4608, 1496, 1600, 2032, 1688, 1120, 16),
        51514: (4736, 1600, 1704, 2136, 1792, 1120, 16),
        601: (4800, 1584, 1688, 2192, 1776, 1128, 16),
        606: (4800, 1560, 1664, 2168, 1752, 1136, 16),
        612: (5184, 1800, 1904, 2392, 1976, 1120, 16),
        618: (4736, 1816, 1920, 2400, 1992, 1176, 16),
    }
    evidence = {
        510: (
            "02fdb433320203a2ea3634225a7d459e7e9c36ec6fd1d450d64a493c80a2402a",
            "e8707e1241541a9bc7faef5aa909c9ee0942e19c",
        ),
        51013: (
            "90b3782a87354db091200624b45aee13e4f41983d1370cb6bde77725c91fb677",
            "d2c6bededf522a14de184c527101226c3e258942",
        ),
        515: (
            "10279f23fd2da25ec4e7308e735a950edbc67e003ee082295c3a4265568881fa",
            "c9d6826901380c3bbf749f0bda901c0759907040",
        ),
        51514: (
            "81d95cea163c171d42b50904e10069bb380c3545eeb426154b1beb7eee874557",
            "04b7e217e5601b15989535cbdad96f6c62b777a7",
        ),
        601: (
            "826879c4782c086fbbb1630f192d4dd6523debf8a3ffcd08ec9fcc8d5118377c",
            "fa5e8475c0a2a08b6f4413e63bd586f8dd23af75",
        ),
        606: (
            "30cc3dae48ed85eea1654188830c1665edf66f06ff6a29aaf999dbb1db7edb92",
            "918625727c3f09f2afc582653695771adc4fef83",
        ),
        612: (
            "422ed6bb6878fbbf3e6d007263347d8598922f0a743d9164d91f6700c34ddbf3",
            "b3b8ca8f204eb51ff7448c2a305bc007ff795dbd",
        ),
        618: (
            "baa888b6d1024b6025b6119fe9e0128c61dda167ee57d21bc3add857cb33ae57",
            "e36c2ac50c3578e730c54bab1ae6fd6da4645c85",
        ),
    }
    for profile in PROFILES:
        manifest = json.loads(
            (RUNTIME_ROOT / f"arm64/gki-manifests/{profile}.json").read_text(
                encoding="utf-8"
            )
        )
        task = manifest["layouts"]["task_struct"]
        signal = manifest["layouts"].get("signal_struct")
        actual = (
            task["size"],
            task["members"]["pid"],
            task["members"]["thread_pid"],
            task["members"]["signal"],
            task["members"]["thread_node"],
            signal["size"] if signal else None,
            signal["members"]["thread_head"] if signal else None,
        )
        if actual != expected[profile]:
            raise RuntimeError(
                f"GKI {profile} task thread layout mismatch: {actual}"
            )
        actual_evidence = (
            manifest["evidence"]["layout_sha256"],
            manifest["evidence"]["vmlinux_build_id"],
        )
        if actual_evidence != evidence[profile]:
            raise RuntimeError(
                f"GKI {profile} task thread evidence mismatch: {actual_evidence}"
            )


def check_compatibility_certificate():
    document = json.loads(
        (RUNTIME_ROOT / "arm64/gki-layout-certificates.json").read_text(
            encoding="utf-8"
        )
    )
    certificates = document.get("certificates", [])
    if document.get("schema") != 1 or not certificates:
        raise RuntimeError("unexpected task-thread certificate document")
    certificate = None
    for candidate in certificates:
        if "task_threads" not in candidate:
            raise RuntimeError(
                "layout certificate is missing task-thread evidence"
            )
        if candidate.get("raw_btf") == {
            "sha256": "ae81b5a86e938c2d2db08e4c78c712143a61c1d823f96383a019b86e9e8b2e79",
            "size": 6909037,
        }:
            certificate = candidate
    if certificate is None:
        raise RuntimeError("task-thread certificate raw BTF identity mismatch")
    if certificate.get("task_threads") != {
        "task_struct": {
            "size": 5184,
            "members": {
                "pid": 1800,
                "signal": 2392,
                "thread_node": 1976,
                "thread_pid": 1904,
            },
            "member_sizes": {
                "pid": 4,
                "signal": 8,
                "thread_node": 16,
                "thread_pid": 8,
            },
        },
        "signal_struct": {
            "size": 1120,
            "members": {"thread_head": 16},
            "member_sizes": {"thread_head": 16},
        },
    }:
        raise RuntimeError("task-thread compatibility certificate mismatch")
    android14_515 = None
    for candidate in certificates:
        if candidate.get("profile_id") == 51514 and candidate.get(
            "release_token"
        ) == "5.15.164-android14-11-maybe-dirty":
            android14_515 = candidate
    if android14_515 is None:
        raise RuntimeError("missing android14-5.15 certificate on 51514")
    if android14_515["identity"]["android_release"] != 14:
        raise RuntimeError("android14-5.15 certificate identity mismatch")
    if android14_515["task_threads"]["task_struct"] != {
        "size": 4736,
        "members": {
            "pid": 1600,
            "signal": 2136,
            "thread_node": 1792,
            "thread_pid": 1704,
        },
        "member_sizes": {
            "pid": 4,
            "signal": 8,
            "thread_node": 16,
            "thread_pid": 8,
        },
    }:
        raise RuntimeError(
            "android14-5.15 task-thread certificate mismatch: "
            + str(android14_515["task_threads"]["task_struct"])
        )
    if android14_515["task_walk"]["task_struct"]["members"]["comm"] != 2064:
        raise RuntimeError("android14-5.15 comm overlay mismatch")

    if certificate.get("task_walk") != {
        "task_struct": {
            "size": 5184,
            "members": {
                "comm": 2320,
                "group_leader": 1864,
                "mm": 1672,
                "parent": 1824,
                "real_cred": 2296,
                "real_parent": 1816,
                "tasks": 1592,
            },
            "member_sizes": {
                "comm": 16,
                "group_leader": 8,
                "mm": 8,
                "parent": 8,
                "real_cred": 8,
                "real_parent": 8,
                "tasks": 16,
            },
        },
        "cred": {
            "size": 184,
            "members": {
                "egid": 28,
                "euid": 24,
                "fsgid": 36,
                "fsuid": 32,
                "gid": 12,
                "sgid": 20,
                "suid": 16,
                "uid": 8,
            },
            "member_sizes": {
                "egid": 4,
                "euid": 4,
                "fsgid": 4,
                "fsuid": 4,
                "gid": 4,
                "sgid": 4,
                "suid": 4,
                "uid": 4,
            },
        },
    }:
        raise RuntimeError("task-walk compatibility certificate mismatch")
    if certificate.get("task_ref") != {
        "task_struct": {
            "size": 5184,
            "members": {"usage": 64},
            "member_sizes": {"usage": 4},
        },
    }:
        raise RuntimeError("task-ref compatibility certificate mismatch")
    if certificate.get("task_user_state") != {
        "task_struct": {
            "size": 5184,
            "members": {
                "flags": 68,
                "stack": 56,
                "stack_refcount": 3280,
            },
            "member_sizes": {
                "flags": 4,
                "stack": 8,
                "stack_refcount": 4,
            },
        },
        "pt_regs": {
            "size": 336,
            "members": {"pc": 256, "pstate": 264},
            "member_sizes": {"pc": 8, "pstate": 8},
        },
    }:
        raise RuntimeError("task-user-state compatibility certificate mismatch")
    rendered = (RUNTIME_ROOT / "src/nvk_compat_table.inc").read_text(
        encoding="utf-8"
    )
    for fact in (
        "NEVERC_KRT_LAYOUT_CERT_TASK_THREADS",
        "NEVERC_KRT_LAYOUT_CERT_TASK_WALK",
        "NEVERC_KRT_LAYOUT_CERT_TASK_REF",
        "NEVERC_KRT_LAYOUT_CERT_TASK_USER_STATE",
        "\t\t.task_walk_task_size = 5184,",
        "\t\t.task_tasks = 1592,",
        "\t\t.task_mm = 1672,",
        "\t\t.task_parent = 1824,",
        "\t\t.task_real_parent = 1816,",
        "\t\t.task_group_leader = 1864,",
        "\t\t.task_real_cred = 2296,",
        "\t\t.task_comm = 2320,",
        "\t\t.task_ref_task_size = 5184,",
        "\t\t.task_usage = 64,",
        "\t\t.task_user_state_task_size = 5184,",
        "\t\t.task_stack = 56,",
        "\t\t.task_stack_refcount = 3280,",
        "\t\t.task_flags = 68,",
        "\t\t.task_size = 5184,",
        "\t\t.task_pid = 1800,",
        "\t\t.task_thread_pid = 1904,",
        "\t\t.task_signal = 2392,",
        "\t\t.task_thread_node = 1976,",
        "\t\t.signal_size = 1120,",
        "\t\t.signal_thread_head = 16,",
    ):
        if fact not in rendered:
            raise RuntimeError(f"generated task-thread certificate lacks {fact}")


def main():
    subprocess.run(
        [sys.executable, str(TOOLS_ROOT / "generate-compat-table.py"), "--check"],
        check=True,
    )
    check_profile_evidence()
    check_compatibility_certificate()
    compiler = shlex.split(os.environ.get("CC", "cc"))
    if not compiler:
        raise RuntimeError("CC does not name a compiler")

    with tempfile.TemporaryDirectory(prefix="neverc-task-thread-ids-") as tmp:
        fixtures = (
            (
                "test-task-thread-ids",
                "NEVERC_KRT_THREAD_IDS_HOST_TEST",
                "test-task-thread-ids.c",
                "nvk_thread_ids.c",
            ),
            (
                "test-task-walk",
                "NEVERC_KRT_TASK_WALK_HOST_TEST",
                "test-task-walk.c",
                "nvk_task_walk.c",
            ),
        )
        for name, host_define, fixture, source in fixtures:
            output = Path(tmp) / name
            command = compiler + [
                "-std=gnu11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-U__weak",
                f"-D{host_define}=1",
                f"-I{RUNTIME_ROOT / 'arm64/include'}",
                f"-I{RUNTIME_ROOT / 'include'}",
                f"-I{TOOLS_ROOT}",
                str(TOOLS_ROOT / fixture),
                str(RUNTIME_ROOT / f"src/{source}"),
                "-o",
                str(output),
            ]
            subprocess.run(command, check=True)
            subprocess.run([str(output)], check=True)

    print("test-task-thread-ids: OK")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"test-task-thread-ids: {error}", file=sys.stderr)
        sys.exit(1)
