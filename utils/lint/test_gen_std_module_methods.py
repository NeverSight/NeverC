import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("gen_std_module_methods.py")
SPEC = importlib.util.spec_from_file_location("gen_std_module_methods", SCRIPT)
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)


class GenStdModuleMethodsTest(unittest.TestCase):
    def test_render_preserves_exact_targets_and_special_marker(self):
        manifest = {
            "modules": {
                "math/big": {
                    "symbols": {"neverc_bigint_init": "src/big.c"},
                    "dot_methods": {"init": "neverc_bigint_init"},
                },
                "net/http2": {
                    "dot_marker": "h2",
                    "symbols": {"neverc_hpack_decode": "src/h2.c"},
                    "dot_methods": {"hpack_decode": "neverc_hpack_decode"},
                },
            }
        }
        generated = GEN.render(manifest)
        self.assertIn(
            "STD_MODULE_METHOD(big, init, neverc_bigint_init)", generated
        )
        self.assertIn(
            "STD_MODULE_METHOD(h2, hpack_decode, neverc_hpack_decode)",
            generated,
        )

    def test_duplicate_marker_method_is_rejected(self):
        manifest = {
            "modules": {
                "one/shared": {
                    "symbols": {"neverc_one": "src/one.c"},
                    "dot_methods": {"run": "neverc_one"},
                },
                "two": {
                    "dot_marker": "shared",
                    "symbols": {"neverc_two": "src/two.c"},
                    "dot_methods": {"run": "neverc_two"},
                },
            }
        }
        with self.assertRaisesRegex(ValueError, "declared by both"):
            GEN.collect_entries(manifest)

    def test_unregistered_target_and_invalid_method_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "unregistered symbol"):
            GEN.collect_entries(
                {
                    "modules": {
                        "x": {
                            "symbols": {},
                            "dot_methods": {"run": "neverc_x_run"},
                        }
                    }
                }
            )
        with self.assertRaisesRegex(ValueError, "invalid dot method"):
            GEN.collect_entries(
                {
                    "modules": {
                        "x": {
                            "symbols": {"neverc_x_run": "src/x.c"},
                            "dot_methods": {"1run": "neverc_x_run"},
                        }
                    }
                }
            )

    def test_core_collection_modules_map_every_registered_symbol(self):
        with GEN.DEFAULT_MANIFEST.open(encoding="utf-8") as source:
            modules = json.load(source)["modules"]

        for module_key in ("sort", "slices", "container/vector"):
            with self.subTest(module=module_key):
                module = modules[module_key]
                registered = set(module["symbols"])
                mapped = set(module.get("dot_methods", {}).values())
                self.assertEqual(
                    registered - mapped,
                    set(),
                    f"{module_key} has registered symbols without dot methods",
                )

    def test_service_modules_map_every_registered_symbol(self):
        with GEN.DEFAULT_MANIFEST.open(encoding="utf-8") as source:
            modules = json.load(source)["modules"]

        for module_key in ("unicode", "log", "time"):
            with self.subTest(module=module_key):
                module = modules[module_key]
                registered = set(module["symbols"])
                mapped = set(module.get("dot_methods", {}).values())
                self.assertEqual(
                    registered - mapped,
                    set(),
                    f"{module_key} has registered symbols without dot methods",
                )

    def test_io_os_modules_map_every_registered_symbol(self):
        with GEN.DEFAULT_MANIFEST.open(encoding="utf-8") as source:
            modules = json.load(source)["modules"]

        for module_key in ("io", "os"):
            with self.subTest(module=module_key):
                module = modules[module_key]
                registered = set(module["symbols"])
                mapped = set(module.get("dot_methods", {}).values())
                self.assertEqual(
                    registered - mapped,
                    set(),
                    f"{module_key} has registered symbols without dot methods",
                )

    def test_image_module_maps_every_registered_symbol(self):
        with GEN.DEFAULT_MANIFEST.open(encoding="utf-8") as source:
            module = json.load(source)["modules"]["image"]

        registered = set(module["symbols"])
        mapped = set(module.get("dot_methods", {}).values())
        self.assertEqual(
            registered - mapped,
            set(),
            "image has registered symbols without dot methods",
        )

    def test_check_mode_detects_a_stale_file(self):
        manifest = {
            "modules": {
                "x": {
                    "symbols": {"neverc_x_run": "src/x.c"},
                    "dot_methods": {"run": "neverc_x_run"},
                }
            }
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest_path = root / "manifest.json"
            output_path = root / "StdModuleMethods.def"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            output_path.write_text("stale\n", encoding="utf-8")
            self.assertEqual(
                GEN.main(
                    [
                        "--manifest",
                        str(manifest_path),
                        "--output",
                        str(output_path),
                        "--check",
                    ]
                ),
                1,
            )
            self.assertEqual(
                GEN.main(
                    [
                        "--manifest",
                        str(manifest_path),
                        "--output",
                        str(output_path),
                        "--write",
                    ]
                ),
                0,
            )
            self.assertEqual(
                GEN.main(
                    [
                        "--manifest",
                        str(manifest_path),
                        "--output",
                        str(output_path),
                        "--check",
                    ]
                ),
                0,
            )


if __name__ == "__main__":
    unittest.main()
