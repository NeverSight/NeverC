#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from unittest import mock
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

    def run_archive_verifier_with_windows_cleanup_error(
        self,
        cleanup_error: OSError,
        *,
        verification_error: Exception | None = None,
    ) -> tuple[int, str]:
        stderr = io.StringIO()
        windows_name = mock.patch.object(self.module.os, "name", "nt")

        def verify(_root):
            windows_name.start()
            if verification_error is not None:
                raise verification_error

        try:
            with (
                mock.patch.object(
                    sys,
                    "argv",
                    ["test-python-plugin-package.py", "--archive", "package.zip"],
                ),
                mock.patch.object(
                    self.module.tempfile,
                    "mkdtemp",
                    return_value="package-root",
                ),
                mock.patch.object(self.module, "safe_extract"),
                mock.patch.object(self.module, "verify_prefix", side_effect=verify),
                mock.patch.object(
                    self.module.shutil,
                    "rmtree",
                    side_effect=cleanup_error,
                ),
                mock.patch.object(self.module.time, "sleep"),
                redirect_stderr(stderr),
            ):
                result = self.module.main()
        finally:
            windows_name.stop()
        return result, stderr.getvalue()

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

    def test_python_sdk_contract_requires_full_ffi_and_ollvm_example(self):
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary)
            for relative in self.module.REQUIRED_PYTHON_SDK_FILES:
                path = prefix / "pluginsdk" / "python" / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")
            sdk = self.module.check_python_sdk(prefix)
            self.assertEqual(sdk, prefix / "pluginsdk" / "python")
            (sdk / "neverc_plugin" / "ffi.py").unlink()
            with self.assertRaisesRegex(ValueError, "ffi.py"):
                self.module.check_python_sdk(prefix)

    def test_ollvm_ir_contract_requires_all_three_transforms(self):
        complete = "ollvm.sub\nollvm.bcf.gate\nollvm.fla.dispatch\n"
        self.module.check_ollvm_ir(complete, "x86_64")
        with self.assertRaisesRegex(ValueError, "ollvm.bcf.gate"):
            self.module.check_ollvm_ir(
                complete.replace("ollvm.bcf.gate", "missing"), "x86_64"
            )

    def test_ollvm_ir_probe_preserves_names_in_release_builds(self):
        self.assertIn(
            "-fno-discard-value-names", self.module.OLLVM_IR_FLAGS
        )

    def test_remove_tree_retries_transient_windows_lock(self):
        path = Path("package")
        sharing_violation = OSError("file is in use")
        sharing_violation.winerror = 32
        with (
            mock.patch.object(self.module.os, "name", "nt"),
            mock.patch.object(
                self.module.shutil,
                "rmtree",
                side_effect=[sharing_violation, None],
            ) as remove,
            mock.patch.object(self.module.time, "sleep") as sleep,
        ):
            self.module.remove_tree_with_retries(path, attempts=2)
        self.assertEqual(remove.call_args_list, [mock.call(path), mock.call(path)])
        sleep.assert_called_once_with(0.25)

    def test_successful_verification_is_not_failed_by_persistent_windows_cleanup_lock(self):
        sharing_violation = OSError("file is in use")
        sharing_violation.winerror = 32
        result, stderr = self.run_archive_verifier_with_windows_cleanup_error(
            sharing_violation
        )

        self.assertEqual(result, 0)
        self.assertIn("warning", stderr.casefold())
        self.assertIn("package-root", stderr)

    def test_verification_failure_is_preserved_when_windows_cleanup_also_fails(self):
        sharing_violation = OSError("file is in use")
        sharing_violation.winerror = 32
        result, stderr = self.run_archive_verifier_with_windows_cleanup_error(
            sharing_violation,
            verification_error=RuntimeError("probe failed"),
        )

        self.assertEqual(result, 1)
        self.assertIn("probe failed", stderr)
        self.assertIn("preserving the original error", stderr)

    def test_successful_verification_still_fails_on_nontransient_cleanup_error(self):
        invalid_name = OSError("invalid name")
        invalid_name.winerror = 123
        result, stderr = self.run_archive_verifier_with_windows_cleanup_error(
            invalid_name
        )

        self.assertEqual(result, 1)
        self.assertIn("invalid name", stderr)
        self.assertNotIn("warning", stderr.casefold())

    def test_probe_failure_is_not_masked_by_nontransient_cleanup_failure(self):
        cleanup_failure = OSError("cleanup failed")
        cleanup_failure.winerror = 123
        stderr = io.StringIO()

        with (
            mock.patch.object(
                self.module.tempfile,
                "mkdtemp",
                return_value="probe-root",
            ),
            mock.patch.object(
                self.module,
                "remove_tree_with_retries",
                side_effect=cleanup_failure,
            ),
            redirect_stderr(stderr),
            self.assertRaisesRegex(RuntimeError, "probe failed"),
        ):
            with self.module.managed_temporary_directory(
                prefix="neverc-probe-"
            ):
                raise RuntimeError("probe failed")

        self.assertIn("cleanup failed", stderr.getvalue())
        self.assertIn("probe-root", stderr.getvalue())

    def test_remove_tree_reraises_nontransient_error_without_sleeping(self):
        path = Path("package")
        invalid_name = OSError("invalid name")
        invalid_name.winerror = 123
        with (
            mock.patch.object(self.module.os, "name", "nt"),
            mock.patch.object(
                self.module.shutil, "rmtree", side_effect=invalid_name
            ),
            mock.patch.object(self.module.time, "sleep") as sleep,
            self.assertRaises(OSError) as raised,
        ):
            self.module.remove_tree_with_retries(path)
        self.assertIs(raised.exception, invalid_name)
        sleep.assert_not_called()

    def test_remove_tree_does_not_retry_windows_code_on_other_platforms(self):
        path = Path("package")
        sharing_violation = OSError("file is in use")
        sharing_violation.winerror = 32
        with (
            mock.patch.object(self.module.os, "name", "posix"),
            mock.patch.object(
                self.module.shutil, "rmtree", side_effect=sharing_violation
            ) as remove,
            mock.patch.object(self.module.time, "sleep") as sleep,
            self.assertRaises(OSError) as raised,
        ):
            self.module.remove_tree_with_retries(path)
        self.assertIs(raised.exception, sharing_violation)
        remove.assert_called_once_with(path)
        sleep.assert_not_called()

    def test_remove_tree_reraises_after_retry_budget(self):
        path = Path("package")
        access_denied = OSError("access denied")
        access_denied.winerror = 5
        with (
            mock.patch.object(self.module.os, "name", "nt"),
            mock.patch.object(
                self.module.shutil, "rmtree", side_effect=access_denied
            ) as remove,
            mock.patch.object(self.module.time, "sleep") as sleep,
            self.assertRaises(OSError) as raised,
        ):
            self.module.remove_tree_with_retries(path, attempts=3)
        self.assertIs(raised.exception, access_denied)
        self.assertEqual(remove.call_count, 3)
        self.assertEqual(
            sleep.call_args_list,
            [mock.call(0.25), mock.call(0.5)],
        )

    def test_remove_tree_rejects_empty_retry_budget(self):
        with self.assertRaisesRegex(ValueError, "attempts must be positive"):
            self.module.remove_tree_with_retries(Path("package"), attempts=0)

    def test_managed_temporary_directory_cleans_up_after_body_error(self):
        path = Path("package")
        with (
            mock.patch.object(
                self.module.tempfile, "mkdtemp", return_value=str(path)
            ),
            mock.patch.object(
                self.module, "remove_tree_with_retries"
            ) as remove,
            self.assertRaisesRegex(RuntimeError, "probe failed"),
        ):
            with self.module.managed_temporary_directory(
                prefix="neverc-probe-"
            ) as temporary:
                self.assertEqual(temporary, path)
                raise RuntimeError("probe failed")
        remove.assert_called_once_with(path)


if __name__ == "__main__":
    unittest.main()
