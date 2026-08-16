#!/usr/bin/env python3
"""Regression tests for canonical GKI manifest config fields."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


TOOLS_ROOT = Path(__file__).resolve().parent
GENERATOR = TOOLS_ROOT / "generate-gki-manifest.py"


def load_generator():
    spec = importlib.util.spec_from_file_location(
        "nvk_generate_gki_manifest_test", GENERATOR
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load GKI manifest generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ManifestConfigTests(unittest.TestCase):
    def read(self, text: str):
        generator = load_generator()
        with tempfile.TemporaryDirectory(prefix="neverc-manifest-config-") as raw:
            config = Path(raw) / ".config"
            config.write_text(
                "CONFIG_ARM64_4K_PAGES=y\n" + text,
                encoding="utf-8",
            )
            return generator.read_config(config)

    def test_legacy_cfi_spelling_is_preserved(self):
        values = self.read("CONFIG_CFI_CLANG=y\n")
        self.assertIs(values["CONFIG_CFI_CLANG"], True)
        self.assertNotIn("CONFIG_CFI", values)

    def test_modern_cfi_spelling_maps_to_canonical_field(self):
        values = self.read("CONFIG_CFI=y\n")
        self.assertIs(values["CONFIG_CFI_CLANG"], True)
        self.assertNotIn("CONFIG_CFI", values)

    def test_absent_cfi_stays_disabled(self):
        values = self.read("")
        self.assertIs(values["CONFIG_CFI_CLANG"], False)


if __name__ == "__main__":
    unittest.main()
