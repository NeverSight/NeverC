#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("test-python-plugin-package.py")


def load_module():
    spec = importlib.util.spec_from_file_location("test_python_plugin_package", SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class PythonPluginPackageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = load_module()

    def test_finds_install_and_root_layouts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            install = root / "install"
            (install / "bin").mkdir(parents=True)
            (install / "pluginsdk" / "python").mkdir(parents=True)
            (install / "bin" / "neverc").write_bytes(b"")
            self.assertEqual(self.module.find_prefix(root), install.resolve())
            self.assertEqual(self.module.find_prefix(install), install.resolve())

    def test_sanitized_environment_hides_python_toolcache(self):
        original = {
            "Path": os.pathsep.join(
                ["/opt/hostedtoolcache/Python/3.12/bin", "/usr/bin"]
            ),
            "pythonLocation": "/opt/hostedtoolcache/Python/3.12",
            "PYTHONPATH": "/tmp/source",
            "pythonhome": "/tmp/python",
            "PYTHONUSERBASE": "/tmp/user-python",
            "LD_LIBRARY_PATH": "/tmp/lib",
            "DYLD_LIBRARY_PATH": "/tmp/maclib",
            "DYLD_FALLBACK_LIBRARY_PATH": "/tmp/fallback",
            "DYLD_FRAMEWORK_PATH": "/tmp/frameworks",
            "KEEP_ME": "yes",
        }
        result = self.module.sanitized_environment(original)
        self.assertEqual(result["PATH"], "/usr/bin")
        self.assertEqual(result["KEEP_ME"], "yes")
        for name in (
            "pythonLocation", "PYTHONPATH", "pythonhome", "PYTHONUSERBASE",
            "Python3_ROOT_DIR", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH",
            "DYLD_FALLBACK_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH",
        ):
            self.assertNotIn(name, result)

    def test_linux_loader_contract(self):
        good = """\
 0x0000000000000001 (NEEDED) Shared library: [libpython3.12.so.1.0]
 0x000000000000001d (RUNPATH) Library runpath: [$ORIGIN/../python/lib]
"""
        self.module.check_linux_dynamic(good, Path("/tmp/package"))
        with self.assertRaisesRegex(ValueError, "build-time Python path|relative Python rpath"):
            self.module.check_linux_dynamic(
                good.replace("$ORIGIN/../python/lib", "/opt/hostedtoolcache/Python/lib"),
                Path("/tmp/package"),
            )

    def test_loader_filter_accepts_relocated_prefix_with_spaces(self):
        prefix = Path("/private/tmp/neverc-python-package/test prefix")
        metadata = f"""\
{prefix}/bin/neverc:
\t@rpath/libpython3.12.dylib (compatibility version 3.12.0)
"""
        self.module.reject_build_paths(metadata, prefix)

    def test_probe_imports_cover_native_stdlib_modules(self):
        for module in ("ssl", "hashlib", "ctypes", "bz2", "lzma", "sqlite3"):
            self.assertIn(f"import {module}", self.module.PROBE_PLUGIN)


if __name__ == "__main__":
    unittest.main()
