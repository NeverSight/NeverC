import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("neverc-protoc.py")


class NeverCProtocTest(unittest.TestCase):
    def run_generator(self, schema: str):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        source = root / "game.proto"
        source.write_text(schema, encoding="utf-8")
        result = subprocess.run(
            [str(SCRIPT), str(source), "--out-dir", str(root / "out")],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        return temporary, root, result

    def test_generates_scalar_and_optional_bindings(self):
        temporary, root, result = self.run_generator(
            'syntax = "proto3"; package game.v1; '
            "message Input { uint64 sequence = 1; optional sint32 dx = 2; "
            "string player = 3; }"
        )
        self.addCleanup(temporary.cleanup)
        self.assertEqual(result.returncode, 0, result.stderr)
        header = (root / "out/game.pb.h").read_text(encoding="utf-8")
        source = (root / "out/game.pb.c").read_text(encoding="utf-8")
        self.assertIn("game_v1_Input_t", header)
        self.assertIn("int has_dx;", header)
        self.assertIn("NEVERC_PROTOBUF_TYPE_SINT32", source)
        self.assertIn("offsetof(game_v1_Input_t, has_dx)", source)

    def test_rejects_unsupported_repeated_fields(self):
        temporary, _root, result = self.run_generator(
            'syntax = "proto3"; message Bad { repeated uint64 value = 1; }'
        )
        self.addCleanup(temporary.cleanup)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("repeated fields are not supported", result.stderr)

    def test_rejects_reserved_and_duplicate_numbers(self):
        for fields, message in [
            ("uint64 value = 19000;", "invalid protobuf field number"),
            ("uint64 a = 1; uint64 b = 1;", "duplicate field number"),
        ]:
            with self.subTest(fields=fields):
                temporary, _root, result = self.run_generator(
                    f'syntax = "proto3"; message Bad {{ {fields} }}'
                )
                self.addCleanup(temporary.cleanup)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)


if __name__ == "__main__":
    unittest.main()
