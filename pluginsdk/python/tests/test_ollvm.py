from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest


def load_example():
    path = (
        Path(__file__).resolve().parents[1]
        / "examples"
        / "ollvm"
        / "ollvm_plugin.py"
    )
    name = "neverc_test_ollvm_example"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    try:
        spec.loader.exec_module(module)
    except Exception:
        sys.modules.pop(name, None)
        raise
    return module


class FakeContext:
    def __init__(self, values=None):
        self.values = values or {}

    def option_values(self, spelling):
        return tuple(self.values.get(spelling, ()))


class OLLVMConfigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = load_example()

    @classmethod
    def tearDownClass(cls):
        sys.modules.pop(cls.module.__name__, None)

    def test_options_are_independent_and_bounded(self):
        config = self.module._Config.from_context(
            FakeContext(
                {
                    "--ollvm-sub": ("true",),
                    "--ollvm-probability": ("73",),
                    "--ollvm-iterations": ("3",),
                    "--ollvm-seed": ("99",),
                }
            )
        )
        self.assertTrue(config.sub)
        self.assertFalse(config.bcf)
        self.assertFalse(config.fla)
        self.assertEqual((config.probability, config.iterations, config.seed), (73, 3, 99))
        with self.assertRaisesRegex(ValueError, "between 0 and 100"):
            self.module._Config.from_context(
                FakeContext({"--ollvm-probability": ("101",)})
            )
        with self.assertRaisesRegex(ValueError, "between 0 and 8"):
            self.module._Config.from_context(
                FakeContext({"--ollvm-iterations": ("9",)})
            )

    def test_seed_derivation_is_reproducible_and_scoped(self):
        config = self.module._Config(seed=42)
        first_random = config.random("sub", "target")
        second_random = config.random("sub", "target")
        first = [first_random.getrandbits(64) for _ in range(3)]
        second = [second_random.getrandbits(64) for _ in range(3)]
        different = config.random("bcf", "target").getrandbits(64)
        self.assertEqual(first, second)
        self.assertNotEqual(first[0], different)

    def test_filters_skip_host_runtime(self):
        config = self.module._Config(
            include=("ollvm_*",), exclude=("*_helper",)
        )
        self.assertTrue(config.accepts("ollvm_target"))
        self.assertFalse(config.accepts("ollvm_helper"))
        self.assertFalse(config.accepts("other"))
        self.assertFalse(self.module._Config().accepts("__neverc_runtime"))
        self.assertFalse(self.module._Config().accepts("llvm.memcpy"))


if __name__ == "__main__":
    unittest.main()
