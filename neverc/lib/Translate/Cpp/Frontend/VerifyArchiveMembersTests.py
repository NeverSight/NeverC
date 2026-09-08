#!/usr/bin/env python3
"""Controlled archive-reader output; real readers are exercised by CI builds."""

import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock


sys.dont_write_bytecode = True
_source = Path(__file__).resolve().with_name("VerifyArchiveMembers.py")
_spec = importlib.util.spec_from_file_location("neverc_verify_archive_members", _source)
verifier = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(verifier)


class VerifyArchiveMembersTests(unittest.TestCase):
    def reader_result(self, output=b"Frontend.cpp.obj\n", status=0, stderr=b""):
        return subprocess.CompletedProcess([], status, output, stderr)

    def test_object_names_paths_spaces_and_duplicates_are_preserved(self):
        members = ["Frontend.cpp.o", "folder with spaces/Lowering.cpp.obj",
                   "dir\\subdir\\Project.obj", "mixed\\dir/MathSDK.o",
                   "same.o", "same.o"]
        for kind in ("ar", "darwin", "msvc"):
            for newline in ("\n", "\r\n"):
                with self.subTest(kind=kind, newline=repr(newline)):
                    self.assertEqual(verifier.parse_members(newline.join(members) + newline,
                                                            kind), members)

    def test_final_newline_is_optional(self):
        self.assertEqual(verifier.parse_members("member.o", "ar"), ["member.o"])

    def test_c0_and_delete_controls_are_not_record_separators(self):
        for value in (*range(10), *range(11, 32), 127):
            with self.subTest(value=value):
                text = "first.o" + chr(value) + "second.o\n"
                with self.assertRaisesRegex(ValueError, "control character"):
                    verifier.parse_members(text, "ar")

    def test_blank_and_unknown_records_fail(self):
        for text in ("", "\n", "\r\n", "\nmember.o\n", "member.o\n\n",
                     "member.o\n\nother.obj\n", "warning: ignored\nmember.o\n",
                     "Microsoft Library Manager\nmember.obj\n", "source.cpp\n",
                     "directory/\n", ".o\n", ".obj\n", "member.o \n"):
            with self.subTest(text=repr(text)):
                with self.assertRaises(ValueError):
                    verifier.parse_members(text, "ar")

    def test_one_exact_first_darwin_index_is_ignored(self):
        for metadata in ("__.SYMDEF", "__.SYMDEF SORTED",
                         "__.SYMDEF_64", "__.SYMDEF_64 SORTED"):
            with self.subTest(metadata=metadata):
                self.assertEqual(verifier.parse_members(metadata + "\r\na.o\r\na.o\r\n",
                                                        "darwin"), ["a.o", "a.o"])

    def test_darwin_index_cannot_be_repeated_late_or_the_only_member(self):
        for text in ("__.SYMDEF\n", "a.o\n__.SYMDEF\n",
                     "__.SYMDEF\n__.SYMDEF\na.o\n",
                     "__.SYMDEF SORTED\n__.SYMDEF_64\na.o\n",
                     "__.SYMDEF\na.o\n__.SYMDEF SORTED\n"):
            with self.subTest(text=repr(text)):
                with self.assertRaises(ValueError):
                    verifier.parse_members(text, "darwin")

    def test_darwin_metadata_is_not_accepted_in_other_modes(self):
        for kind in ("ar", "msvc"):
            for metadata in ("__.SYMDEF", "__.SYMDEF SORTED",
                             "__.SYMDEF_64", "__.SYMDEF_64 SORTED"):
                with self.subTest(kind=kind, metadata=metadata):
                    with self.assertRaisesRegex(ValueError, "metadata"):
                        verifier.parse_members(metadata + "\na.o\n", kind)

    def test_metadata_near_names_paths_and_unsupported_indices_fail(self):
        for name in ("dir/__.SYMDEF", "__.SYMDEF SORTED ", "__.SYMDEF_64SORTED",
                     "__.SYMDEF_32", "/", "//", "/SYM64/"):
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, "non-object"):
                    verifier.parse_members(name + "\na.o\n", "darwin")

    def test_unknown_mode_fails_before_running_reader(self):
        with self.assertRaisesRegex(ValueError, "unsupported reader kind"):
            verifier.parse_members("a.o\n", "automatic")
        with mock.patch.object(verifier.subprocess, "run") as reader:
            with self.assertRaisesRegex(ValueError, "unsupported reader kind"):
                verifier.verify_archive("bundle.a", "ar", "automatic")
        reader.assert_not_called()

    def test_each_mode_uses_the_exact_read_only_arguments(self):
        archive = Path("directory with spaces") / "bundle ; untouched.a"
        tool = Path("tool directory") / "reader"
        for kind, options in (("ar", ["t"]), ("darwin", ["t"]),
                              ("msvc", ["/NOLOGO", "/LIST"])):
            with self.subTest(kind=kind):
                with mock.patch.object(verifier.subprocess, "run",
                                       return_value=self.reader_result()) as reader:
                    self.assertEqual(verifier.verify_archive(archive, tool, kind),
                                     ["Frontend.cpp.obj"])
                reader.assert_called_once_with(
                    [str(tool), *options, str(archive.absolute())],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120,
                    check=False, shell=False)

    def test_libtool_is_never_treated_as_an_archive_listing_reader(self):
        for tool in ("libtool", "/usr/bin/libtool", "glibtool",
                     "C:\\tools\\LIBTOOL.EXE", "/tools/glibtool.exe"):
            for kind in ("ar", "darwin", "msvc"):
                with self.subTest(tool=tool, kind=kind):
                    with mock.patch.object(verifier.subprocess, "run") as reader:
                        with self.assertRaisesRegex(ValueError, "not an archive listing reader"):
                            verifier.verify_archive("bundle.a", tool, kind)
                    reader.assert_not_called()

    def test_nonzero_reader_status_fails_even_with_valid_stdout(self):
        with mock.patch.object(verifier.subprocess, "run", return_value=
                               self.reader_result(status=7, stderr=b"broken archive")):
            with self.assertRaisesRegex(ValueError, "status 7.*broken archive"):
                verifier.verify_archive("bundle.a", "ar", "ar")

    def test_reader_start_failure_and_timeout_are_explicit(self):
        for error, message in ((OSError("reader missing"), "cannot run reader.*reader missing"),
                               (subprocess.TimeoutExpired(["ar"], 120), "timed out after 120s")):
            with self.subTest(error=type(error).__name__):
                with mock.patch.object(verifier.subprocess, "run", side_effect=error):
                    with self.assertRaisesRegex(ValueError, message):
                        verifier.verify_archive("bundle.a", "ar", "ar")

    def test_empty_output_and_isolated_cr_survive_reader_boundary_as_errors(self):
        for output in (b"", b"\n", b"a.o\rb.o\n", b"a.o\x00b.o\n"):
            with self.subTest(output=output):
                with mock.patch.object(verifier.subprocess, "run",
                                       return_value=self.reader_result(output)):
                    with self.assertRaises(ValueError):
                        verifier.verify_archive("bundle.a", "ar", "ar")

    def test_all_four_excluded_stems_and_suffixes_are_rejected(self):
        for stem in ("AllTUsExecution", "Execution", "StandaloneExecution",
                     "BalancedPartitioning"):
            for suffix in (".cpp.o", ".cpp.obj", ".o", ".obj"):
                member = "objects with spaces\\nested/" + stem + suffix
                with self.subTest(member=member):
                    with mock.patch.object(verifier.subprocess, "run", return_value=
                                           self.reader_result(("safe.o\n" + member + "\n").encode())):
                        with self.assertRaisesRegex(ValueError, "excluded frontend object") as caught:
                            verifier.verify_archive("bundle.a", "ar", "ar")
                    self.assertIn(repr(member), str(caught.exception))

    def test_exclusions_apply_to_all_reader_modes(self):
        for kind in ("ar", "darwin", "msvc"):
            with self.subTest(kind=kind):
                with mock.patch.object(verifier.subprocess, "run", return_value=
                                       self.reader_result(b"Execution.cpp.obj\r\n")):
                    with self.assertRaisesRegex(ValueError, "excluded frontend object"):
                        verifier.verify_archive("bundle.lib", "reader", kind)

    def test_near_names_and_excluded_directory_components_are_accepted(self):
        names = ["MyExecution.cpp.obj", "ExecutionExtra.o", "Execution.cpp.obj.o",
                 "MyAllTUsExecution.o", "StandaloneExecutionExtra.cpp.obj",
                 "MyBalancedPartitioning.o", "Execution.cpp.obj/Safe.o",
                 "BalancedPartitioning\\Safe.obj", "safe.o", "safe.o"]
        with mock.patch.object(verifier.subprocess, "run", return_value=
                               self.reader_result(("\n".join(names) + "\n").encode())):
            self.assertEqual(verifier.verify_archive("bundle.lib", "reader", "msvc"), names)

    def test_non_utf8_member_bytes_are_not_replaced_or_merged(self):
        with mock.patch.object(verifier.subprocess, "run", return_value=
                               self.reader_result(b"dir\xff/a.o\ndir\xfe/a.o\n")):
            members = verifier.verify_archive("bundle.a", "ar", "ar")
        self.assertEqual([name.encode("utf-8", "surrogateescape") for name in members],
                         [b"dir\xff/a.o", b"dir\xfe/a.o"])

    def test_cli_reports_success_count_and_fails_closed(self):
        arguments = ["--archive", "bundle.a", "--archiver", "ar", "--kind", "ar"]
        for result, code, message in ((["a.o", "a.o"], 0, "checked 2 object members"),
                                      (ValueError("controlled failure"), 1, "controlled failure")):
            with self.subTest(code=code):
                options = {"side_effect": result} if isinstance(result, Exception) else {"return_value": result}
                output, errors = io.StringIO(), io.StringIO()
                with mock.patch.object(verifier, "verify_archive", **options), \
                        contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                    self.assertEqual(verifier.main(arguments), code)
                self.assertIn(message, output.getvalue() if code == 0 else errors.getvalue())


if __name__ == "__main__":
    unittest.main()
