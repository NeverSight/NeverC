#!/usr/bin/env python3
"""Unit tests for NeverC-read GKI header fingerprints."""

import importlib.util
from pathlib import Path
import unittest


TOOLS = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "watch_gki_layouts", TOOLS / "watch-gki-layouts.py"
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load watch-gki-layouts.py")
layouts = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(layouts)


MODULE_HEADER = """
struct module {
	enum module_state state;
	struct list_head list;
	char name[MODULE_NAME_LEN];
	struct module_kobject mkobj;
	int (*init)(void);
#ifdef CONFIG_MODULE_UNLOAD
	void (*exit)(void);
#endif
};
"""

TASK_HEADER = """
struct task_struct {
#ifdef CONFIG_THREAD_INFO_IN_TASK
	struct thread_info thread_info;
#endif
	unsigned int __state;
	refcount_t usage;
	unsigned int flags;
	struct mm_struct *mm;
	char comm[TASK_COMM_LEN];
};
"""

TASK_HEADER_INSERTED = """
struct task_struct {
#ifdef CONFIG_THREAD_INFO_IN_TASK
	struct thread_info thread_info;
#endif
	unsigned int __state;
	refcount_t usage;
	unsigned int flags;
	int is_blocked;
	struct mm_struct *mm;
	char comm[TASK_COMM_LEN];
	int unused_tail;
};
"""


class ContractReuseTests(unittest.TestCase):
    def test_watched_fields_match_compat_contract(self):
        expected = layouts.COMPAT.neverc_read_members_by_struct()
        watched = {
            spec["name"]: frozenset(spec["fields"]) for spec in layouts.WATCHED_STRUCTS
        }
        self.assertEqual(watched, expected)
        self.assertIn("init", expected["module"])
        self.assertIn("f_path", expected["file"])
        self.assertIn("preempt_count", expected["thread_info"])
        self.assertIn("tgid", expected["task_struct"])

    def test_every_layout_field_struct_has_a_header_path(self):
        missing = [
            name
            for name in layouts.COMPAT.neverc_read_members_by_struct()
            if name not in layouts.STRUCT_HEADER_PATHS
        ]
        self.assertEqual(missing, [])


class ParserTests(unittest.TestCase):
    def test_module_used_fields(self):
        spec = next(item for item in layouts.WATCHED_STRUCTS if item["name"] == "module")
        fingerprint = layouts.fingerprint_struct(MODULE_HEADER, spec)
        self.assertEqual(fingerprint["missing"], [])
        self.assertEqual(fingerprint["used"]["name"]["index"], 2)
        self.assertEqual(fingerprint["used"]["init"]["decl"], "int (*init)(void)")
        self.assertEqual(fingerprint["used"]["exit"]["decl"], "void (*exit)(void)")

    def test_task_keeps_dunder_state_and_detects_insert(self):
        spec = next(
            item for item in layouts.WATCHED_STRUCTS if item["name"] == "task_struct"
        )
        before = layouts.fingerprint_struct(TASK_HEADER, spec)
        after = layouts.fingerprint_struct(TASK_HEADER_INSERTED, spec)
        self.assertIn("__state", before["members"])
        self.assertEqual(before["used"]["comm"]["index"], 5)
        self.assertEqual(after["used"]["comm"]["index"], 6)
        diff = layouts.diff_compact(
            layouts.compact_fingerprint({"task_struct": before}),
            layouts.compact_fingerprint({"task_struct": after}),
            against="kminext",
        )
        comm = next(item for item in diff if item["field"] == "comm")
        self.assertEqual(comm["severity"], "offset_risk")
        self.assertIn("index 5 -> 6", comm["detail"])

    def test_incomplete_probe_does_not_invent_field_changes(self):
        full = layouts.compact_fingerprint(
            {
                "task_struct": layouts.fingerprint_struct(TASK_HEADER, next(
                    item for item in layouts.WATCHED_STRUCTS if item["name"] == "task_struct"
                )),
                "path": layouts.fingerprint_struct(
                    "struct path { struct vfsmount *mnt; struct dentry *dentry; };",
                    {"name": "path", "fields": ("dentry",)},
                ),
            }
        )
        partial = layouts.compact_fingerprint(
            {
                "task_struct": layouts.fingerprint_struct(TASK_HEADER, next(
                    item for item in layouts.WATCHED_STRUCTS if item["name"] == "task_struct"
                )),
            }
        )
        self.assertEqual(layouts.diff_compact(full, partial, against="snapshot"), [])
        self.assertEqual(layouts.diff_compact(partial, full, against="snapshot"), [])

    def test_merge_keeps_structs_missing_from_a_partial_probe(self):
        full = layouts.compact_fingerprint(
            {
                "task_struct": layouts.fingerprint_struct(TASK_HEADER, next(
                    item for item in layouts.WATCHED_STRUCTS if item["name"] == "task_struct"
                )),
                "path": layouts.fingerprint_struct(
                    "struct path { struct vfsmount *mnt; struct dentry *dentry; };",
                    {"name": "path", "fields": ("dentry",)},
                ),
            }
        )
        partial = layouts.compact_fingerprint(
            {
                "task_struct": layouts.fingerprint_struct(TASK_HEADER, next(
                    item for item in layouts.WATCHED_STRUCTS if item["name"] == "task_struct"
                )),
            }
        )
        merged = layouts.merge_compact(full, partial)
        self.assertIn("path", merged["member_counts"])
        self.assertIn("path.dentry", merged["used"])
        self.assertEqual(layouts.merge_compact(full, None), full)
        self.assertEqual(layouts.merge_compact(None, partial), partial)

    def test_trailing_unused_member_is_sizeof_risk_only(self):
        spec = {
            "name": "path",
            "fields": ("dentry",),
        }
        before = layouts.fingerprint_struct(
            "struct path { struct vfsmount *mnt; struct dentry *dentry; };",
            spec,
        )
        after = layouts.fingerprint_struct(
            "struct path { struct vfsmount *mnt; struct dentry *dentry; void *extra; };",
            spec,
        )
        diff = layouts.diff_compact(
            layouts.compact_fingerprint({"path": before}),
            layouts.compact_fingerprint({"path": after}),
            against="snapshot",
        )
        self.assertEqual(len(diff), 1)
        self.assertEqual(diff[0]["severity"], "sizeof_risk")
        self.assertEqual(diff[0]["field"], "*")

    def test_disappeared_field_is_reported_once(self):
        before = {
            "digest": "old",
            "member_counts": {"module": 4},
            "missing": [],
            "used": {"module.exit": {"index": 3, "decl": "void (*exit)(void)"}},
        }
        after = {
            "digest": "new",
            "member_counts": {"module": 3},
            "missing": ["module.exit"],
            "used": {},
        }
        diff = layouts.diff_compact(before, after, against="snapshot")
        disappeared = [item for item in diff if item["field"] == "exit"]
        self.assertEqual(len(disappeared), 1)

    def test_file_prefix_does_not_hide_file_operations_sizeof(self):
        before = {
            "digest": "old",
            "member_counts": {"file": 10, "file_operations": 20},
            "missing": [],
            "used": {"file.f_path": {"index": 4, "decl": "struct path f_path"}},
        }
        after = {
            "digest": "new",
            "member_counts": {"file": 10, "file_operations": 21},
            "missing": [],
            "used": {"file.f_path": {"index": 4, "decl": "struct path f_path"}},
        }
        diff = layouts.diff_compact(before, after, against="snapshot")
        self.assertEqual(len(diff), 1)
        self.assertEqual(diff[0]["struct"], "file_operations")
        self.assertEqual(diff[0]["severity"], "sizeof_risk")


if __name__ == "__main__":
    unittest.main()
