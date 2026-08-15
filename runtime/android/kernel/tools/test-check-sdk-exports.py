#!/usr/bin/env python3
"""Tests for the streaming Clang AST declaration extractor."""

import importlib.util
import json
from pathlib import Path
import shutil
import unittest


PATH = Path(__file__).with_name("check-sdk-exports.py")
SPEC = importlib.util.spec_from_file_location("check_sdk_exports", PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load check-sdk-exports.py")
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class AstTextDeclarationTests(unittest.TestCase):
    def test_only_external_top_level_declarations_are_returned(self):
        lines = [
            "|-FunctionDecl 0x1 <a.h:1:1, col:20> col:5 keep_fn 'int (void)'",
            "|-FunctionDecl 0x2 <a.h:2:1, col:20> col:5 static_fn 'int (void)' static",
            "|-FunctionDecl 0x3 <a.h:3:1, line:4:1> line:3:5 defined_fn 'int (void)'",
            "| `-CompoundStmt 0x4 <col:1, col:2>",
            "|-VarDecl 0x5 <a.h:5:1, col:12> col:12 keep_var 'int' extern",
            "|-VarDecl 0x6 <a.h:6:1, col:12> col:12 local_var 'int' static",
            "| `-FunctionDecl 0x7 <nested.h:1:1> nested_fn 'int (void)'",
            "`-FunctionDecl 0x8 <a.h:8:1, col:20> col:5 final_fn 'int (void)'",
        ]
        self.assertEqual(
            checker.ast_text_declarations(lines),
            {"keep_fn", "keep_var", "final_fn"},
        )

    def test_attributes_before_name_do_not_confuse_parser(self):
        lines = [
            "|-FunctionDecl 0x1 prev 0x0 <a.h:1:1> col:5 used register_kprobe 'int (struct kprobe *)' extern",
            "`-VarDecl 0x2 <a.h:2:1> col:8 used system_wq 'struct workqueue_struct *' extern",
        ]
        self.assertEqual(
            checker.ast_text_declarations(lines),
            {"register_kprobe", "system_wq"},
        )


class CurrentSdkManifestTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("clang"), "clang is required")
    def test_current_declarations_match_every_checked_manifest(self):
        manifest_root = PATH.parents[1] / "arm64/gki-manifests"
        for profile in (510, 51013, 515, 51514, 601, 606, 612, 618):
            with self.subTest(profile=profile):
                manifest = json.loads(
                    (manifest_root / f"{profile}.json").read_text(encoding="utf-8")
                )
                self.assertEqual(
                    checker.sdk_declarations("clang", profile),
                    set(manifest["sdk_exports"]),
                )


if __name__ == "__main__":
    unittest.main()
