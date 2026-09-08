import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("neverc-protoc.py")
REPO = SCRIPT.resolve().parents[2]


class NeverCProtocTest(unittest.TestCase):
    def run_generator(
        self, schema: str, *, existing_outputs=False, filename="game.proto"
    ):
        temporary = tempfile.TemporaryDirectory(prefix="neverc protoc ")
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        source = root / filename
        source.write_text(schema, encoding="utf-8")
        if existing_outputs:
            (root / "out").mkdir()
            for suffix in ("h", "c"):
                (root / f"out/{source.stem}.pb.{suffix}").write_bytes(
                    f"previous {suffix}\r\n".encode() + b"\x00\xff"
                )
        result = subprocess.run(
            [
                sys.executable, str(SCRIPT), str(source),
                "--out-dir", str(root / "out"),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        return root, result

    def assert_rejected_without_output_changes(
        self, fields, diagnostic, *, filename="game.proto"
    ):
        for existing_outputs in (False, True):
            with self.subTest(existing_outputs=existing_outputs):
                root, result = self.run_generator(
                    'syntax = "proto3"; message Earlier { int32 value = 1; } '
                    f"message Example {{ {fields} }}",
                    existing_outputs=existing_outputs,
                    filename=filename,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr)
                stem = Path(filename).stem
                if existing_outputs:
                    self.assertEqual(
                        sorted(path.name for path in (root / "out").iterdir()),
                        [f"{stem}.pb.c", f"{stem}.pb.h"],
                    )
                    for suffix in ("h", "c"):
                        self.assertEqual(
                            (root / f"out/{stem}.pb.{suffix}").read_bytes(),
                            f"previous {suffix}\r\n".encode() + b"\x00\xff",
                        )
                else:
                    self.assertFalse((root / "out").exists())

    def test_rejects_presence_member_collisions_without_changing_outputs(self):
        for fields in (
            "optional int32 x = 1; int32 has_x = 2;",
            "int32 has_x = 2; optional int32 x = 1;",
            "optional int32 x = 1; optional int32 has_x = 2;",
            "optional int32 has_x = 2; optional int32 x = 1;",
        ):
            with self.subTest(fields=fields):
                self.assert_rejected_without_output_changes(
                    fields, "generated C member 'has_x' conflicts"
                )

    def test_rejects_c_and_cpp_keywords_without_changing_outputs(self):
        for name in (
            "int", "restrict", "_Atomic", "class", "and", "requires",
            "constexpr", "nullptr", "typeof_unqual",
        ):
            with self.subTest(name=name):
                self.assert_rejected_without_output_changes(
                    f"int32 {name} = 1;",
                    f"field name {name!r} is a C/C++ keyword",
                )

    def test_rejects_header_macros_without_changing_outputs(self):
        for name in (
            "NULL", "SIZE_MAX", "INT32_MAX", "UINT_FAST16_MAX", "INTPTR_MIN",
            "INTMAX_MAX", "PTRDIFF_MAX", "SIG_ATOMIC_MAX", "WINT_MIN",
            "INT_LEAST64_WIDTH", "NEVERC_ENCODING_PROTOBUF_H",
            "NEVERC_PROTOBUF_MAX_FIELD_NUMBER",
            "NEVERC_PROTOBUF_DEFAULT_MAX_FIELD_SIZE",
        ):
            with self.subTest(name=name):
                self.assert_rejected_without_output_changes(
                    f"int32 {name} = 1;",
                    f"generated C member {name!r} conflicts with a header macro",
                )

    def test_rejects_actual_header_guard_without_changing_outputs(self):
        for filename, guard in (
            ("game.proto", "GAME_PB_H"),
            ("game-v1.proto", "GAME_V1_PB_H"),
        ):
            with self.subTest(filename=filename):
                self.assert_rejected_without_output_changes(
                    f"int32 {guard} = 1;",
                    f"generated C member {guard!r} conflicts with a header macro",
                    filename=filename,
                )

    def test_rejects_implementation_reserved_members(self):
        for name in ("__LINE__", "__cplusplus", "_Pragma", "_Value", "value__x"):
            with self.subTest(name=name):
                self.assert_rejected_without_output_changes(
                    f"int32 {name} = 1;",
                    f"generated C member {name!r} is reserved "
                    "to the C/C++ implementation",
                )
        self.assert_rejected_without_output_changes(
            "optional int32 _value = 1;",
            "generated C member 'has__value' is reserved "
            "to the C/C++ implementation",
        )

    def test_rejects_shadowed_field_types_without_changing_outputs(self):
        for name, scalar in (
            ("int32_t", "int32"),
            ("uint64_t", "uint64"),
            ("neverc_protobuf_bytes_t", "bytes"),
        ):
            with self.subTest(name=name):
                self.assert_rejected_without_output_changes(
                    f"int32 {name} = 1; {scalar} value = 2;",
                    f"generated C member {name!r} shadows a generated field type",
                )

    def compiler_command(self, language, source):
        # CTest supplies the toolchain that configured the build. Missing tools
        # must fail this check rather than silently dropping language coverage.
        prefix = "NEVERC_PROTOC_" + ("C" if language == "c" else "CXX")
        compiler = os.environ.get(prefix + "_COMPILER", "")
        compiler_id = os.environ.get(prefix + "_ID", "")
        frontend = os.environ.get(prefix + "_FRONTEND", "")
        target = os.environ.get(prefix + "_TARGET", "")
        self.assertTrue(
            compiler and shutil.which(compiler),
            f"{prefix}_COMPILER must name an available compiler; "
            "run ProtobufGeneratorTests through CTest "
            "for configured toolchain inputs",
        )
        self.assertIn(
            compiler_id, ("GNU", "Clang", "AppleClang", "MSVC"),
            f"unsupported {prefix}_ID: {compiler_id!r}",
        )
        self.assertIn(
            frontend, ("", "GNU", "MSVC"),
            f"unsupported {prefix}_FRONTEND: {frontend!r}",
        )
        command = [compiler]
        if compiler_id in ("Clang", "AppleClang") and target:
            command.append(f"--target={target}")
        if compiler_id == "MSVC" or frontend == "MSVC":
            command += ["/nologo", "/Zs", "/WX"]
            command += (
                ["/TC", "/std:c11"] if language == "c"
                else ["/TP", "/std:c++14"]
            )
            command.append(f"/I{REPO / 'std/include'}")
        else:
            standard = "c11" if language == "c" else "c++11"
            command += [
                "-fsyntax-only", "-Werror", "-x", language,
                f"-std={standard}", f"-I{REPO / 'std/include'}",
            ]
        return command + [str(source)]

    def test_generated_bindings_compile_in_c_and_cpp(self):
        root, result = self.run_generator(
            'syntax = "proto3"; package game.v1; '
            "message Input { uint64 sequence = 1; optional sint32 dx = 2; "
            "string player = 3; int32 class_name = 4; int32 has_value = 5; "
            "int32 data = 6; int32 length = 7; int32 value = 8; "
            "int32 USER_VALUE = 9; int32 INT32_C = 10; int32 UINT32_MIN = 11; "
            "optional int32 NULL_count = 12; int32 _value = 13; "
            "int32 OTHER_PB_H = 14; } "
            "message Output { optional sint32 dx = 1; }"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        consumer = root / "out/consumer.cpp"
        consumer.write_text(
            '#include "game.pb.h"\n'
            "int main() {\n"
            "    game_v1_Input_t input = {};\n"
            "    input.has_dx = 1;\n"
            "    input.dx = -7;\n"
            "    input.class_name = 3;\n"
            "    input.has_value = 4;\n"
            "    input.INT32_C = input.USER_VALUE;\n"
            "    input.has_NULL_count = 1;\n"
            "    input._value = input.OTHER_PB_H;\n"
            "    return input.dx + input.class_name + input.has_value;\n"
            "}\n",
            encoding="utf-8",
        )
        for language, source in (
            ("c", root / "out/game.pb.c"),
            ("c++", consumer),
        ):
            with self.subTest(language=language):
                command = self.compiler_command(language, source)
                compiled = subprocess.run(
                    command, text=True, capture_output=True, check=False,
                )
                self.assertEqual(
                    compiled.returncode, 0,
                    f"compiler command: {command!r}\n"
                    f"{compiled.stdout}{compiled.stderr}",
                )

    def test_generates_scalar_and_optional_bindings(self):
        root, result = self.run_generator(
            'syntax = "proto3"; package game.v1; '
            "message Input { uint64 sequence = 1; optional sint32 dx = 2; "
            "string player = 3; }"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        header = (root / "out/game.pb.h").read_text(encoding="utf-8")
        source = (root / "out/game.pb.c").read_text(encoding="utf-8")
        self.assertIn("game_v1_Input_t", header)
        self.assertIn("int has_dx;", header)
        self.assertIn("NEVERC_PROTOBUF_TYPE_SINT32", source)
        self.assertIn("offsetof(game_v1_Input_t, has_dx)", source)

    def test_rejects_unsupported_repeated_fields(self):
        _root, result = self.run_generator(
            'syntax = "proto3"; message Bad { repeated uint64 value = 1; }'
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("repeated fields are not supported", result.stderr)

    def test_rejects_reserved_and_duplicate_numbers(self):
        for fields, message in [
            ("uint64 value = 19000;", "invalid protobuf field number"),
            ("uint64 a = 1; uint64 b = 1;", "duplicate field number"),
        ]:
            with self.subTest(fields=fields):
                _root, result = self.run_generator(
                    f'syntax = "proto3"; message Bad {{ {fields} }}'
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)


if __name__ == "__main__":
    unittest.main()
