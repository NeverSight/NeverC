#!/usr/bin/env python3
"""Compile SDK layout contracts against deterministic GKI manifests."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent
REPO_ROOT = TOOLS_ROOT.parents[3]
DEFAULT_MANIFEST_ROOT = (
    REPO_ROOT / "runtime/android/kernel/arm64/gki-manifests"
)
PROFILES = (510, 515, 601, 606, 612, 618)
ARM64_INCLUDE_ROOT = RUNTIME_ROOT / "arm64" / "include"
PUBLIC_INCLUDE_ROOT = RUNTIME_ROOT / "include"
SDK_INCLUDES = tuple(
    path.relative_to(ARM64_INCLUDE_ROOT).as_posix()
    for path in sorted(ARM64_INCLUDE_ROOT.rglob("*.h"))
) + tuple(path.name for path in sorted(PUBLIC_INCLUDE_ROOT.glob("*.h")))

# Each concrete SDK type that is passed to the kernel by value or embeds
# kernel-owned fields must match the selected configured GKI layout exactly.
# Opaque pointer-only types intentionally do not appear here.
CONTRACTS = {
    "attribute": (
        "struct attribute",
        {"name": "name", "mode": "mode"},
    ),
    "attribute_group": (
        "struct attribute_group",
        {"attrs": "attrs", "bin_attrs": "bin_attrs"},
    ),
    "callback_head": (
        "struct callback_head",
        {"next": "next", "func": "func"},
    ),
    "completion": (
        "struct completion",
        {"done": "done", "wait": "wait"},
    ),
    "cpumask": ("struct cpumask", {"bits": "bits"}),
    "delayed_work": (
        "struct delayed_work",
        {"work": "work", "timer": "timer"},
    ),
    "dev_pm_ops": (
        "struct dev_pm_ops",
        {
            "suspend": "suspend",
            "resume": "resume",
            "runtime_suspend": "runtime_suspend",
            "runtime_resume": "runtime_resume",
        },
    ),
    "file_operations": (
        "struct file_operations",
        {
            "owner": "owner",
            "read": "read",
            "write": "write",
            "unlocked_ioctl": "unlocked_ioctl",
            "open": "open",
            "release": "release",
            "mmap_prepare": "mmap_prepare",
        },
    ),
    "firmware": (
        "struct firmware",
        {"data": "data", "size": "size"},
    ),
    "hrtimer": ("struct hrtimer", {"function": "function"}),
    "idr": (
        "struct idr",
        {"idr_base": "__idr_base", "idr_next": "__idr_next"},
    ),
    "kprobe": (
        "struct kprobe",
        {
            "addr": "addr",
            "symbol_name": "symbol_name",
            "offset": "offset",
            "pre_handler": "pre_handler",
        },
    ),
    "kref": ("struct kref", {"refcount": "refcount"}),
    "miscdevice": (
        "struct miscdevice",
        {
            "minor": "minor",
            "name": "name",
            "fops": "fops",
            "mode": "mode",
        },
    ),
    "mutex": (
        "struct mutex",
        {
            "owner": "owner",
            "wait_lock": "wait_lock",
            "osq": "osq",
            "wait_list": "wait_list",
        },
    ),
    "netlink_kernel_cfg": (
        "struct netlink_kernel_cfg",
        {"groups": "groups", "flags": "flags", "input": "input"},
    ),
    "nf_hook_ops": (
        "struct nf_hook_ops",
        {
            "hook": "hook",
            "dev": "dev",
            "priv": "priv",
            "pf": "pf",
            "hooknum": "hooknum",
            "priority": "priority",
        },
    ),
    "nf_hook_state": (
        "struct nf_hook_state",
        {
            "hook": "hook",
            "pf": "pf",
            "in": "in",
            "out": "out",
            "sk": "sk",
            "net": "net",
            "okfn": "okfn",
        },
    ),
    "nlmsghdr": (
        "struct nlmsghdr",
        {
            "nlmsg_len": "nlmsg_len",
            "nlmsg_type": "nlmsg_type",
            "nlmsg_flags": "nlmsg_flags",
            "nlmsg_seq": "nlmsg_seq",
            "nlmsg_pid": "nlmsg_pid",
        },
    ),
    "notifier_block": (
        "struct notifier_block",
        {
            "notifier_call": "notifier_call",
            "next": "next",
            "priority": "priority",
        },
    ),
    "optimistic_spin_queue": (
        "struct optimistic_spin_queue",
        {"tail": "tail"},
    ),
    "proc_ops": (
        "struct proc_ops",
        {
            "proc_flags": "proc_flags",
            "proc_open": "proc_open",
            "proc_read": "proc_read",
            "proc_write": "proc_write",
            "proc_lseek": "proc_lseek",
            "proc_release": "proc_release",
        },
    ),
    "pt_regs": (
        "struct pt_regs",
        {
            "regs": "regs",
            "sp": "sp",
            "pc": "pc",
            "pstate": "pstate",
            "orig_x0": "orig_x0",
            "syscallno": "syscallno",
        },
    ),
    "rb_node": (
        "struct rb_node",
        {
            "__rb_parent_color": "__rb_parent_color",
            "rb_right": "rb_right",
            "rb_left": "rb_left",
        },
    ),
    "rb_root": ("struct rb_root", {"rb_node": "rb_node"}),
    "regmap_config": (
        "struct regmap_config",
        {
            "name": "name",
            "reg_bits": "reg_bits",
            "reg_stride": "reg_stride",
            "val_bits": "val_bits",
            "fast_io": "fast_io",
            "max_register": "max_register",
            "reg_defaults": "reg_defaults",
            "num_reg_defaults": "num_reg_defaults",
        },
    ),
    "resource": (
        "struct resource",
        {"start": "start", "end": "end", "flags": "flags"},
    ),
    "rw_semaphore": ("struct rw_semaphore", {}),
    "scatterlist": (
        "struct scatterlist",
        {
            "page_link": "page_link",
            "offset": "offset",
            "length": "length",
            "dma_address": "dma_address",
            "dma_length": "dma_length",
        },
    ),
    "semaphore": (
        "struct semaphore",
        {"lock": "lock", "count": "count", "wait_list": "wait_list"},
    ),
    "seq_operations": (
        "struct seq_operations",
        {"start": "start", "stop": "stop", "next": "next", "show": "show"},
    ),
    "sg_table": (
        "struct sg_table",
        {"sgl": "sgl", "nents": "nents", "orig_nents": "orig_nents"},
    ),
    "tasklet_struct": ("struct tasklet_struct", {}),
    "timer_list": (
        "struct timer_list",
        {
            "entry": "entry",
            "expires": "expires",
            "function": "function",
            "flags": "flags",
        },
    ),
    "timespec64": (
        "struct timespec64",
        {"tv_sec": "tv_sec", "tv_nsec": "tv_nsec"},
    ),
    "wait_queue_entry": (
        "struct wait_queue_entry",
        {
            "flags": "flags",
            "private": "private",
            "func": "func",
            "entry": "entry",
        },
    ),
    "wait_queue_head": (
        "struct wait_queue_head",
        {"lock": "__lock", "head": "head"},
    ),
    "work_struct": (
        "struct work_struct",
        {"data": "data", "entry": "entry", "func": "func"},
    ),
}


def c_assert(expression, expected, label):
    message = label.replace('"', "'")
    return (
        f'_Static_assert(({expression}) == {expected}, '
        f'"GKI manifest mismatch: {message}");'
    )


def generate_source(manifest):
    layouts = manifest["layouts"]
    lines = [f"#include <{name}>" for name in SDK_INCLUDES]
    lines.append("")

    for layout_name, (c_type, fields) in CONTRACTS.items():
        layout = layouts[layout_name]
        lines.append(
            c_assert(
                f"sizeof({c_type})",
                layout["size"],
                f"{layout_name}.sizeof",
            )
        )
        for manifest_field, c_field in fields.items():
            if manifest_field not in layout["members"]:
                continue
            lines.append(
                c_assert(
                    f"__builtin_offsetof({c_type}, {c_field})",
                    layout["members"][manifest_field],
                    f"{layout_name}.{manifest_field}",
                )
            )

    module = layouts["module"]
    lines.extend(
        [
            c_assert(
                "sizeof(struct neverc_krt_this_module)",
                module["size"],
                "module.sizeof",
            ),
            c_assert(
                "__builtin_offsetof(struct neverc_krt_this_module, name)",
                module["members"]["name"],
                "module.name",
            ),
            c_assert(
                "__builtin_offsetof(struct neverc_krt_this_module, init)",
                module["members"]["init"],
                "module.init",
            ),
            c_assert(
                "__builtin_offsetof(struct neverc_krt_this_module, exit)",
                module["members"]["exit"],
                "module.exit",
            ),
        ]
    )

    file_path_dentry = (
        layouts["file"]["members"]["f_path"]
        + layouts["path"]["members"]["dentry"]
    )
    dentry_name = (
        layouts["dentry"]["members"]["d_name"]
        + layouts["qstr"]["members"]["name"]
    )
    task_preempt_count = (
        layouts["task_struct"]["members"]["thread_info"]
        + layouts["thread_info"]["members"]["preempt_count"]
    )
    lines.extend(
        [
            c_assert(
                "NEVERC_KRT_FILE_DENTRY_OFF",
                file_path_dentry,
                "NEVERC_KRT_FILE_DENTRY_OFF",
            ),
            c_assert(
                "NEVERC_KRT_DENTRY_DNAME_OFF",
                dentry_name,
                "NEVERC_KRT_DENTRY_DNAME_OFF",
            ),
            c_assert(
                "NEVERC_KRT_NR_CPUS",
                manifest["config"]["CONFIG_NR_CPUS"],
                "NEVERC_KRT_NR_CPUS",
            ),
            c_assert(
                "NEVERC_KRT_PAGE_SHIFT",
                manifest["config"]["PAGE_SHIFT"],
                "NEVERC_KRT_PAGE_SHIFT",
            ),
            c_assert(
                "NEVERC_KRT_VA_BITS",
                manifest["config"]["CONFIG_ARM64_VA_BITS"],
                "NEVERC_KRT_VA_BITS",
            ),
            c_assert(
                "NEVERC_KRT_PA_BITS",
                manifest["config"]["CONFIG_ARM64_PA_BITS"],
                "NEVERC_KRT_PA_BITS",
            ),
            c_assert(
                "NEVERC_KRT_PGTABLE_LEVELS",
                manifest["config"]["CONFIG_PGTABLE_LEVELS"],
                "NEVERC_KRT_PGTABLE_LEVELS",
            ),
            c_assert(
                "NEVERC_KRT_TASK_PREEMPT_COUNT",
                task_preempt_count,
                "NEVERC_KRT_TASK_PREEMPT_COUNT",
            ),
        ]
    )

    if manifest["profile"] >= 612:
        task_cpu = (
            layouts["task_struct"]["members"]["thread_info"]
            + layouts["thread_info"]["members"]["cpu"]
        )
        lines.append(
            c_assert(
                "NEVERC_KRT_TASK_CPU",
                task_cpu,
                "NEVERC_KRT_TASK_CPU",
            )
        )

    lines.append("int nvk_manifest_layout_contracts(void) { return 0; }")
    return "\n".join(lines) + "\n"


def verify_manifest_evidence(manifest_path, manifest):
    evidence = manifest.get("evidence")
    if not isinstance(evidence, dict):
        raise ValueError(f"{manifest_path}: missing evidence object")

    for name in ("config_sha256", "layout_sha256", "symvers_sha256"):
        value = evidence.get(name)
        if (
            not isinstance(value, str)
            or len(value) != 64
            or any(char not in "0123456789abcdef" for char in value)
        ):
            raise ValueError(f"{manifest_path}: invalid {name}")

    build_id = evidence.get("vmlinux_build_id")
    if (
        not isinstance(build_id, str)
        or not build_id
        or len(build_id) % 2
        or any(char not in "0123456789abcdef" for char in build_id)
    ):
        raise ValueError(f"{manifest_path}: invalid vmlinux_build_id")


def verify_profile(compiler, manifest_path):
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    profile = manifest["profile"]
    if profile not in PROFILES:
        raise ValueError(f"{manifest_path}: unsupported profile {profile}")
    verify_manifest_evidence(manifest_path, manifest)

    source = generate_source(manifest)
    with tempfile.TemporaryDirectory(prefix=f"nvk-layout-{profile}-") as temp:
        source_path = Path(temp) / "manifest-layouts.c"
        source_path.write_text(source, encoding="utf-8")
        command = [
            str(compiler),
            "--target=aarch64-linux-android",
            "-fandroid-kernel-driver-mode",
            f"-DNVK_KERNEL={profile}",
            f"-I{ARM64_INCLUDE_ROOT}",
            f"-I{PUBLIC_INCLUDE_ROOT}",
            "-std=gnu11",
            "-Wall",
            "-Werror",
            "-fsyntax-only",
            str(source_path),
        ]
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
        )
    if result.returncode:
        detail = result.stderr or result.stdout
        raise RuntimeError(f"GKI {profile} SDK layout check failed:\n{detail}")


def main():
    parser = argparse.ArgumentParser(
        description="verify SDK structure layouts against GKI manifests"
    )
    parser.add_argument(
        "--compiler",
        type=Path,
        default=REPO_ROOT / "build-neverc/bin/neverc",
    )
    parser.add_argument(
        "--manifest-root",
        type=Path,
        default=DEFAULT_MANIFEST_ROOT,
    )
    args = parser.parse_args()

    try:
        for profile in PROFILES:
            manifest = args.manifest_root / f"{profile}.json"
            verify_profile(args.compiler, manifest)
            print(f"GKI {profile}: SDK layouts match manifest")
    except (
        json.JSONDecodeError,
        OSError,
        RuntimeError,
        ValueError,
    ) as error:
        print(f"verify-sdk-layouts: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
