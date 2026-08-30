#!/usr/bin/env python3

import json
import os
import re
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = (
    ROOT / "neverc/lib/Foundation/Std/build_std_bitcode.py"
)
TARGET = "x86_64-apple-darwin"
SENTINEL = b"\x00existing generated header\xff"


def canonical_path(path):
    return os.path.realpath(os.path.abspath(path))


class BuildStdBitcodeTests(unittest.TestCase):
    def setUp(self):
        temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(temporary_directory.cleanup)

        self.temporary_directory = (
            Path(temporary_directory.name) / "fixture with spaces"
        )
        self.temporary_directory.mkdir()
        self.source_directory = self.temporary_directory / "std/src"
        self.include_directory = self.temporary_directory / "std/include"
        self.output = self.temporary_directory / "generated/std_bitcode.h"
        self.compiler = self.temporary_directory / "fake-neverc"
        self.compiler_log = self.temporary_directory / "compiler-calls.jsonl"

        self.source_directory.mkdir(parents=True)
        self.include_directory.mkdir(parents=True)
        self.compiler.write_text(
            "#!{}\n".format(sys.executable)
            + r'''import json
import os
import sys
from pathlib import Path


arguments = sys.argv[1:]
log_path = Path(os.environ["FAKE_NEVERC_LOG"])
with log_path.open("a", encoding="utf-8") as stream:
    json.dump(arguments, stream)
    stream.write("\n")

source = next(
    (
        Path(argument)
        for argument in arguments
        if argument.endswith(".c") and Path(argument).is_file()
    ),
    None,
)
if source is None:
    print("fake-neverc: source argument missing", file=sys.stderr)
    sys.exit(97)

if os.environ.get("FAKE_NEVERC_FAIL_SOURCE") == source.name:
    print(f"{source}: error: requested fake compiler failure", file=sys.stderr)
    sys.exit(23)

try:
    output_index = arguments.index("-o")
    output = Path(arguments[output_index + 1])
except (ValueError, IndexError):
    print("fake-neverc: output argument missing", file=sys.stderr)
    sys.exit(98)

output.parent.mkdir(parents=True, exist_ok=True)
if os.environ.get("FAKE_NEVERC_EMPTY_OUTPUT") == "1":
    output.write_bytes(b"")
else:
    output.write_bytes(
        b"BC\xc0\xde" + source.as_posix().encode("utf-8")
    )
''',
            encoding="utf-8",
        )
        self.compiler.chmod(
            self.compiler.stat().st_mode
            | stat.S_IXUSR
            | stat.S_IXGRP
            | stat.S_IXOTH
        )

    def write_sources(self, *relative_paths):
        sources = []
        for relative_path in relative_paths:
            source = self.source_directory / relative_path
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text("int neverc_std_test_source;\n", encoding="utf-8")
            sources.append(source)
        return sources

    def run_generator(self, *, tag="darwin_x64", extra_arguments=(), env=None):
        process_environment = os.environ.copy()
        process_environment["FAKE_NEVERC_LOG"] = str(self.compiler_log)
        if env:
            process_environment.update(env)

        command = [
            sys.executable,
            str(GENERATOR),
            str(self.compiler),
            str(self.source_directory),
            str(self.include_directory),
            *extra_arguments,
            "--target",
            TARGET,
            "--tag",
            tag,
            "-o",
            str(self.output),
        ]
        return subprocess.run(
            command,
            capture_output=True,
            text=True,
            env=process_environment,
            timeout=180,
            check=False,
        )

    def compiler_calls(self):
        if not self.compiler_log.exists():
            return []
        return [
            json.loads(line)
            for line in self.compiler_log.read_text(encoding="utf-8").splitlines()
            if line
        ]

    def process_diagnostics(self, result):
        return (
            f"exit status: {result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    def assert_failed_without_replacing_output(self, result):
        self.assertNotEqual(0, result.returncode, self.process_diagnostics(result))
        self.assertEqual(SENTINEL, self.output.read_bytes())

    def test_forwards_sysroot_as_adjacent_compiler_arguments(self):
        self.write_sources("time/time.c")
        sysroot = self.temporary_directory / "MacOS SDK"
        sysroot.mkdir()

        result = self.run_generator(
            extra_arguments=("--sysroot", str(sysroot)),
        )

        self.assertEqual(0, result.returncode, self.process_diagnostics(result))
        calls = self.compiler_calls()
        self.assertEqual(1, len(calls))
        self.assertEqual(1, calls[0].count("-isysroot"))
        sysroot_index = calls[0].index("-isysroot")
        self.assertEqual(str(sysroot), calls[0][sysroot_index + 1])
        self.assertNotIn(f"-isysroot={sysroot}", calls[0])

    def test_last_translation_unit_failure_preserves_existing_output(self):
        sources = self.write_sources("00_first.c", "99_last.c")
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(SENTINEL)

        result = self.run_generator(
            env={"FAKE_NEVERC_FAIL_SOURCE": sources[-1].name},
        )

        self.assert_failed_without_replacing_output(result)
        calls = self.compiler_calls()
        self.assertEqual(2, len(calls))
        expected_sources = {canonical_path(source) for source in sources}
        compiled_sources = {
            canonical_path(argument)
            for call in calls
            for argument in call
            if canonical_path(argument) in expected_sources
        }
        self.assertEqual(expected_sources, compiled_sources)

    def test_empty_bitcode_preserves_existing_output_and_fails(self):
        self.write_sources("empty.c")
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(SENTINEL)

        result = self.run_generator(
            env={"FAKE_NEVERC_EMPTY_OUTPUT": "1"},
        )

        self.assert_failed_without_replacing_output(result)
        self.assertEqual(1, len(self.compiler_calls()))

    def test_sanitized_name_collision_fails_before_invoking_compiler(self):
        self.write_sources("a/b.c", "a_b.c")
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(SENTINEL)

        result = self.run_generator()

        self.assert_failed_without_replacing_output(result)
        self.assertEqual([], self.compiler_calls())

    def test_invalid_tag_fails_before_invoking_compiler(self):
        self.write_sources("valid.c")
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(SENTINEL)

        result = self.run_generator(tag="darwin-x64")

        self.assert_failed_without_replacing_output(result)
        self.assertEqual([], self.compiler_calls())

    def test_emits_exact_entry_for_each_of_246_sources(self):
        sources = self.write_sources(
            *(f"module_{index:03d}.c" for index in range(246))
        )

        result = self.run_generator()

        self.assertEqual(0, result.returncode, self.process_diagnostics(result))
        calls = self.compiler_calls()
        self.assertEqual(246, len(calls))

        expected_source_paths = {canonical_path(source) for source in sources}
        compiled_source_paths = []
        for call in calls:
            matching_sources = expected_source_paths.intersection(
                canonical_path(argument) for argument in call
            )
            self.assertEqual(1, len(matching_sources), call)
            compiled_source_paths.extend(matching_sources)
        self.assertEqual(
            expected_source_paths,
            set(compiled_source_paths),
        )
        self.assertEqual(246, len(set(compiled_source_paths)))

        header = self.output.read_text(encoding="utf-8")
        expected_names = {f"module_{index:03d}" for index in range(246)}
        array_names = re.findall(
            r"^static const unsigned char "
            r"kStdBC_darwin_x64_([A-Za-z_][A-Za-z0-9_]*)\[\]\s*=\s*\{$",
            header,
            flags=re.MULTILINE,
        )
        self.assertEqual(246, len(array_names))
        self.assertEqual(expected_names, set(array_names))

        table_match = re.search(
            r"^static const StdBitcodeEntry "
            r"kStdBitcodeEntries_darwin_x64\[\]\s*=\s*\{\s*$"
            r"(?P<body>.*?)"
            r"^\};\s*$",
            header,
            flags=re.MULTILINE | re.DOTALL,
        )
        self.assertIsNotNone(table_match)
        table_entries = re.findall(
            r'^\s*\{\s*"([^"]+)"\s*,\s*'
            r"([A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
            r"sizeof\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*\}\s*,\s*$",
            table_match.group("body"),
            flags=re.MULTILINE,
        )
        self.assertEqual(246, len(table_entries))
        self.assertEqual(expected_names, {entry[0] for entry in table_entries})
        for name, symbol, sizeof_symbol in table_entries:
            self.assertEqual(f"kStdBC_darwin_x64_{name}", symbol)
            self.assertEqual(symbol, sizeof_symbol)

        entry_counts = re.findall(
            r"\bkStdBitcodeEntryCount_darwin_x64\s*=\s*(\d+)\s*;",
            header,
        )
        self.assertEqual(["246"], entry_counts)


if __name__ == "__main__":
    unittest.main()
