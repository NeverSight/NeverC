#!/usr/bin/env python3
"""Check that offset probes preserve the selected kernel configuration."""

import contextlib
import importlib.util
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


TOOLS = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("gki_gen_offsets_test", TOOLS / "gen-offsets.py")
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


def prepared(root):
    config = root / "include/generated/autoconf.h"
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text("/* measured configuration */\n")


class PreparedTreeTests(unittest.TestCase):
    def test_explicit_prepared_output_is_not_reconfigured(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "source", Path(directory) / "out"
            prepared(output)
            with (
                mock.patch.object(sys, "argv", ["gen-offsets.py", str(source), str(output)]),
                mock.patch.object(probe, "find_neverc", return_value="clang"),
                mock.patch.object(probe, "prepare_tree") as prepare,
                mock.patch.object(probe, "compile_probe", return_value={"OFF_NAME": 24}) as compile,
                contextlib.redirect_stdout(io.StringIO()),
            ):
                probe.main()
            prepare.assert_not_called()
            compile.assert_called_once_with("clang", str(source), str(output))

    def test_explicit_unprepared_output_is_prepared_even_if_source_is_prepared(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "source", Path(directory) / "out"
            prepared(source)
            with (
                mock.patch.object(sys, "argv", ["gen-offsets.py", str(source), str(output)]),
                mock.patch.object(probe, "find_neverc", return_value="clang"),
                mock.patch.object(probe, "prepare_tree") as prepare,
                mock.patch.object(probe, "compile_probe", return_value={"OFF_NAME": 24}),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                probe.main()
            prepare.assert_called_once_with(str(source), str(output))

    def test_compiler_removing_failed_output_keeps_original_error(self):
        def failure(command, **kwargs):
            Path(command[command.index("-o") + 1]).unlink()
            return subprocess.CompletedProcess(command, 1, "", "invalid kernel header")

        with (
            mock.patch.object(probe.subprocess, "run", side_effect=failure),
            contextlib.redirect_stderr(io.StringIO()) as errors,
        ):
            self.assertIsNone(probe.compile_probe("clang", "/source", "/out"))
        self.assertIn("invalid kernel header", errors.getvalue())

    @unittest.skipUnless(shutil.which("clang"), "Clang is required for the header probe")
    def test_named_anonymous_member_used_by_android18_headers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepared(root)
            kconfig = root / "include/linux/kconfig.h"
            kconfig.parent.mkdir(parents=True)
            kconfig.write_text("")
            source = root / "probe.c"
            source.write_text(
                'struct head { long name; };\n'
                'struct filename { struct head; long tail; };\n'
                'void probe(void) {\n'
                '  __asm__ __volatile__("\\n.ascii \\"==NVK== OFFSET %0 ==\\"\\n"\n'
                '    : : "i"(__builtin_offsetof(struct filename, tail)));\n'
                '}\n'
            )
            with mock.patch.object(probe, "PROBE_C", str(source)):
                self.assertEqual(probe.compile_probe(shutil.which("clang"), str(root), str(root)), {"OFFSET": 8})


if __name__ == "__main__":
    unittest.main()
