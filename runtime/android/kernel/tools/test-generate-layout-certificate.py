#!/usr/bin/env python3
"""Unit tests for compatible-identity layout certificate assembly."""

import importlib.util
from pathlib import Path
import unittest


TOOLS_ROOT = Path(__file__).resolve().parent


def load_tool(name, filename):
    spec = importlib.util.spec_from_file_location(name, TOOLS_ROOT / filename)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {filename}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CERT = load_tool(
    "nvk_generate_layout_certificate_test", "generate-layout-certificate.py"
)
COMPAT = load_tool("nvk_generate_compat_table_cert_test", "generate-compat-table.py")


def fixture_layouts():
    return {
        "dir_context": {
            "size": 16,
            "members": {"actor": 0, "pos": 8},
            "member_sizes": {"actor": 8, "pos": 8},
        },
        "filename": {
            "size": 32,
            "members": {"name": 0},
            "member_sizes": {"name": 8},
        },
        "inode": {
            "size": 752,
            "members": {"i_atime": 88, "i_mtime": 104},
            "member_sizes": {"i_atime": 16, "i_mtime": 16},
        },
        "timespec64": {
            "size": 16,
            "members": {"tv_sec": 0, "tv_nsec": 8},
            "member_sizes": {"tv_sec": 8, "tv_nsec": 8},
        },
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
        "file": {
            "size": 256,
            "members": {"f_path": 64},
            "member_sizes": {"f_path": 16},
        },
        "task_struct": {
            "size": 4608,
            "members": {
                "comm": 1960,
                "flags": 60,
                "group_leader": 1560,
                "mm": 1312,
                "parent": 1520,
                "pid": 1496,
                "real_cred": 1936,
                "real_parent": 1512,
                "signal": 2032,
                "stack": 48,
                "stack_refcount": 2848,
                "tasks": 1232,
                "thread_node": 1688,
                "thread_pid": 1600,
                "usage": 56,
            },
            "member_sizes": {
                "comm": 16,
                "flags": 4,
                "group_leader": 8,
                "mm": 8,
                "parent": 8,
                "pid": 4,
                "real_cred": 8,
                "real_parent": 8,
                "signal": 8,
                "stack": 8,
                "stack_refcount": 4,
                "tasks": 16,
                "thread_node": 16,
                "thread_pid": 8,
                "usage": 4,
            },
        },
        "cred": {
            "size": 176,
            "members": {
                "egid": 24,
                "euid": 20,
                "fsgid": 32,
                "fsuid": 28,
                "gid": 8,
                "sgid": 16,
                "suid": 12,
                "uid": 4,
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
        "signal_struct": {
            "size": 1120,
            "members": {"thread_head": 16},
            "member_sizes": {"thread_head": 16},
        },
        "pt_regs": {
            "size": 336,
            "members": {"pc": 256, "pstate": 264, "regs": 0, "sp": 248},
            "member_sizes": {"pc": 8, "pstate": 8, "regs": 248, "sp": 8},
        },
        "mm_struct": {
            "size": 992,
            "members": {
                "mm_count": 80,
                "mmap_lock": 104,
                "page_table_lock": 100,
                "pgd": 64,
            },
            "member_sizes": {
                "mm_count": 4,
                "mmap_lock": 64,
                "page_table_lock": 4,
                "pgd": 8,
            },
        },
        "vm_area_struct": {
            "size": 232,
            "members": {
                "vm_end": 8,
                "vm_flags": 80,
                "vm_mm": 64,
                "vm_pgoff": 152,
                "vm_start": 0,
            },
            "member_sizes": {
                "vm_end": 8,
                "vm_flags": 8,
                "vm_mm": 8,
                "vm_pgoff": 8,
                "vm_start": 8,
            },
        },
    }


def fixture_profile():
    return {
        "legacy_id": 515,
        "linux_major": 5,
        "linux_minor": 15,
        "android_release": 13,
        "page_shift": 12,
        "capabilities": {"filldir_abi": "returns_int"},
    }


def fixture_manifest():
    return {
        "profile": 515,
        "config": {
            "PAGE_SHIFT": 12,
            "CONFIG_ARM64_VA_BITS": 39,
            "CONFIG_ARM64_PA_BITS": 48,
            "CONFIG_PGTABLE_LEVELS": 3,
        },
        "layouts": {
            "mm_struct": fixture_layouts()["mm_struct"],
            "vm_area_struct": {
                "size": 232,
                "members": {"vm_end": 8, "vm_start": 0},
                "member_sizes": {"vm_end": 8, "vm_start": 8},
            },
            "pt_regs": fixture_layouts()["pt_regs"],
        },
    }


class GenerateLayoutCertificateTests(unittest.TestCase):
    def test_build_certificate_includes_public_vma_fields(self):
        certificate = CERT.build_certificate(
            fixture_profile(),
            fixture_manifest(),
            "5.15.153-android13-8-fixture",
            fixture_layouts(),
            "raw_btf",
            {"sha256": "a" * 64, "size": 12},
        )

        self.assertEqual(certificate["profile_id"], 515)
        self.assertEqual(certificate["filldir_abi"], "returns_int")
        self.assertEqual(certificate["file_dentry"], 72)
        self.assertEqual(
            certificate["inode_times"]["members"],
            {
                "atime_sec": 88,
                "atime_nsec": 96,
                "mtime_sec": 104,
                "mtime_nsec": 112,
            },
        )
        vma = certificate["user_ptmap"]["vm_area_struct"]
        self.assertEqual(
            vma["members"],
            {
                "vm_end": 8,
                "vm_flags": 80,
                "vm_mm": 64,
                "vm_pgoff": 152,
                "vm_start": 0,
            },
        )
        COMPAT.validate_user_ptmap_layout(
            certificate["user_ptmap"],
            "fixture",
            certificate["user_ptmap"]["geometry"],
        )

    def test_build_certificate_rejects_foreign_family(self):
        with self.assertRaisesRegex(ValueError, "outside profile family"):
            CERT.build_certificate(
                fixture_profile(),
                fixture_manifest(),
                "6.12.45-android16-5-fixture",
                fixture_layouts(),
                "raw_dwarf",
                {"sha256": "b" * 64, "size": 8},
            )

    def test_build_certificate_accepts_same_series_android_variant(self):
        certificate = CERT.build_certificate(
            fixture_profile(),
            fixture_manifest(),
            "5.15.164-android14-11-maybe-dirty",
            fixture_layouts(),
            "raw_btf",
            {"sha256": "e" * 64, "size": 20},
        )
        self.assertEqual(certificate["profile_id"], 515)
        self.assertEqual(certificate["identity"]["android_release"], 14)
        self.assertEqual(certificate["identity"]["kmi_generation"], 11)

    def test_build_certificate_rejects_displaced_compile_family(self):
        catalog = [
            fixture_profile(),
            {
                "legacy_id": 51514,
                "linux_major": 5,
                "linux_minor": 15,
                "android_release": 14,
                "kmi_generation": 11,
                "page_shift": 12,
            },
        ]
        with self.assertRaisesRegex(ValueError, "compile family 51514"):
            CERT.build_certificate(
                fixture_profile(),
                fixture_manifest(),
                "5.15.164-android14-11-maybe-dirty",
                fixture_layouts(),
                "raw_btf",
                {"sha256": "e" * 64, "size": 20},
                catalog,
            )

    def test_merge_replaces_same_identity(self):
        first = CERT.build_certificate(
            fixture_profile(),
            fixture_manifest(),
            "5.15.153-android13-8-fixture",
            fixture_layouts(),
            "raw_btf",
            {"sha256": "a" * 64, "size": 12},
        )
        second = dict(first)
        second["raw_btf"] = {"sha256": "c" * 64, "size": 24}
        document = CERT.merge_certificate(
            {"schema": 1, "certificates": [first]}, second
        )
        self.assertEqual(len(document["certificates"]), 1)
        self.assertEqual(
            document["certificates"][0]["raw_btf"]["sha256"], "c" * 64
        )

    def test_identity_from_token_parses_dirty_local_release(self):
        identity = CERT.identity_from_token(
            "5.10.205-android12-9-dirty", 12
        )
        self.assertEqual(
            identity,
            {
                "linux_major": 5,
                "linux_minor": 10,
                "linux_patch": 205,
                "android_release": 12,
                "kmi_generation": 9,
                "page_shift": 12,
            },
        )

    def test_build_certificate_narrows_dir_context_to_certified_fields(self):
        layouts = fixture_layouts()
        layouts["dir_context"] = {
            "size": 24,
            "members": {
                "actor": 0,
                "pos": 8,
                "count": 16,
                "dt_flags_mask": 20,
            },
            "member_sizes": {
                "actor": 8,
                "pos": 8,
                "count": 4,
                "dt_flags_mask": 4,
            },
        }
        profile = fixture_profile()
        profile["legacy_id"] = 618
        profile["linux_major"] = 6
        profile["linux_minor"] = 18
        profile["android_release"] = 17
        certificate = CERT.build_certificate(
            profile,
            {
                "profile": 618,
                "config": fixture_manifest()["config"],
                "layouts": fixture_manifest()["layouts"],
            },
            "6.18.24-android17-5-maybe-dirty-4k",
            layouts,
            "raw_btf",
            {"sha256": "d" * 64, "size": 16},
        )

        self.assertEqual(
            certificate["dir_context"],
            {
                "size": 24,
                "members": {"actor": 0, "pos": 8},
                "member_sizes": {"actor": 8, "pos": 8},
            },
        )


if __name__ == "__main__":
    unittest.main()
