#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).with_name("bundle-python-runtime.py")


def load_module():
    spec = importlib.util.spec_from_file_location("bundle_python_runtime", SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class BundlePythonRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = load_module()

    def make_posix_layout(self, root: Path):
        base = root / "cpython"
        stdlib = base / "lib" / "python3.12"
        (stdlib / "lib-dynload").mkdir(parents=True)
        (stdlib / "test").mkdir()
        (stdlib / "site-packages").mkdir()
        (stdlib / "idlelib").mkdir()
        (stdlib / "tkinter").mkdir()
        (stdlib / "ensurepip").mkdir()
        (stdlib / "venv").mkdir()
        (stdlib / "pkg" / "__pycache__").mkdir(parents=True)
        (stdlib / "os.py").write_text("# os\n", encoding="utf-8")
        (stdlib / "lib-dynload" / "_ssl.so").write_bytes(b"extension")
        (stdlib / "test" / "test_os.py").write_text("", encoding="utf-8")
        (stdlib / "site-packages" / "foreign.py").write_text("", encoding="utf-8")
        (stdlib / "idlelib" / "idle.py").write_text("", encoding="utf-8")
        (stdlib / "tkinter" / "__init__.py").write_text("", encoding="utf-8")
        (stdlib / "ensurepip" / "__init__.py").write_text("", encoding="utf-8")
        (stdlib / "venv" / "__init__.py").write_text("", encoding="utf-8")
        (stdlib / "pkg" / "__pycache__" / "x.pyc").write_bytes(b"bytecode")
        (stdlib / "LICENSE.txt").write_text("PSF license\n", encoding="utf-8")
        (base / "lib" / "libpython3.12.so.1.0").write_bytes(b"runtime")
        return self.module.RuntimeLayout(
            platform="linux",
            base_prefix=base,
            stdlib=stdlib,
            libdir=base / "lib",
            runtime_name="libpython3.12.so.1.0",
            version="3.12.10",
            abi="cpython-312",
        )

    def test_posix_layout_is_trimmed_and_manifested(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            layout = self.make_posix_layout(root)
            prefix = root / "install"
            neverc = prefix / "bin" / "neverc"
            neverc.parent.mkdir(parents=True)
            neverc.write_bytes(b"not-an-elf")

            manifest = self.module.bundle_runtime(
                prefix, neverc, layout=layout, repair=False
            )

            python = prefix / "python"
            stdlib = python / "lib" / "python3.12"
            self.assertTrue((stdlib / "os.py").is_file())
            self.assertTrue((stdlib / "lib-dynload" / "_ssl.so").is_file())
            for excluded in (
                "test", "site-packages", "idlelib", "tkinter", "ensurepip", "venv"
            ):
                self.assertFalse((stdlib / excluded).exists())
            self.assertFalse((stdlib / "pkg" / "__pycache__").exists())
            self.assertTrue((python / "lib" / "libpython3.12.so.1.0").is_file())
            self.assertEqual(manifest["runtime"], "libpython3.12.so.1.0")
            self.assertNotIn(str(layout.base_prefix), (python / "runtime.json").read_text())
            self.assertTrue((python / "licenses" / "CPython-LICENSE.txt").is_file())

    def test_source_installed_cpython_uses_bundler_license_fallback(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            layout = self.make_posix_layout(root)
            (layout.stdlib / "LICENSE.txt").unlink()
            fallback = root / "bundler" / "licenses" / "CPython-LICENSE.txt"
            fallback.parent.mkdir(parents=True)
            fallback.write_text("PSF license fallback\n", encoding="utf-8")

            with mock.patch.object(
                self.module, "BUNDLED_CPYTHON_LICENSE", fallback, create=True
            ):
                selected = self.module.find_license(layout)

            self.assertEqual(selected, fallback)

    def test_windows_layout_places_runtime_beside_executable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            base = root / "cpython"
            stdlib = base / "Lib"
            dlls = base / "DLLs"
            stdlib.mkdir(parents=True)
            dlls.mkdir()
            (stdlib / "os.py").write_text("# os\n", encoding="utf-8")
            (base / "LICENSE.txt").write_text("PSF license\n", encoding="utf-8")
            (base / "python312.dll").write_bytes(b"runtime")
            (dlls / "_ssl.pyd").write_bytes(b"extension")
            layout = self.module.RuntimeLayout(
                platform="windows",
                base_prefix=base,
                stdlib=stdlib,
                libdir=base,
                runtime_name="python312.dll",
                version="3.12.10",
                abi="cp312-win_arm64",
            )
            prefix = root / "install"
            neverc = prefix / "bin" / "neverc.exe"
            neverc.parent.mkdir(parents=True)
            neverc.write_bytes(b"not-a-pe")

            self.module.bundle_runtime(prefix, neverc, layout=layout, repair=False)

            self.assertTrue((prefix / "bin" / "python312.dll").is_file())
            self.assertTrue((prefix / "python" / "Lib" / "os.py").is_file())
            self.assertTrue((prefix / "python" / "DLLs" / "_ssl.pyd").is_file())

    def test_dependency_parsers(self):
        linux = """\
libssl.so.3 => /opt/runtime/lib/libssl.so.3 (0x1)
libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x2)
libmissing.so => not found
"""
        dependencies, missing = self.module.parse_ldd(linux)
        self.assertEqual(dependencies["libssl.so.3"], Path("/opt/runtime/lib/libssl.so.3"))
        self.assertIn("libmissing.so", missing)

        mac = """\
/tmp/_ssl.so:
\t/opt/runtime/lib/libssl.3.dylib (compatibility version 3.0.0)
\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)
"""
        self.assertEqual(
            self.module.parse_otool(mac),
            ["/opt/runtime/lib/libssl.3.dylib", "/usr/lib/libSystem.B.dylib"],
        )

        pe = """\
    python312.dll
    KERNEL32.dll
"""
        self.assertEqual(
            self.module.parse_pe_imports(pe), ["python312.dll", "KERNEL32.dll"]
        )

    def test_system_dependency_allowlists(self):
        self.assertTrue(self.module.is_system_dependency("linux", "/usr/lib/libc.so.6"))
        self.assertTrue(
            self.module.is_system_dependency(
                "macos", "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"
            )
        )
        self.assertTrue(self.module.is_system_dependency("windows", "KERNEL32.dll"))
        self.assertFalse(
            self.module.is_system_dependency("linux", "/opt/vendor/libcrypto.so.3")
        )
        self.assertFalse(self.module.is_system_dependency("linux", "/usr/lib/libz.so.1"))

    def test_native_file_magic_detection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            elf = root / "runtime.so"
            macho = root / "runtime.dylib"
            fat64_macho = root / "universal-runtime"
            text = root / "module.py"
            elf.write_bytes(b"\x7fELFpayload")
            macho.write_bytes(b"\xcf\xfa\xed\xfepayload")
            fat64_macho.write_bytes(b"\xca\xfe\xba\xbfpayload")
            text.write_text("pass\n", encoding="utf-8")

            self.assertTrue(self.module.is_elf(elf))
            self.assertTrue(self.module.is_macho(macho))
            self.assertTrue(self.module.is_macho(fat64_macho))
            self.assertFalse(self.module.is_elf(text))
            self.assertFalse(self.module.is_macho(text))

    def test_windows_distribution_dependency_precedes_system_fallback(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            base = root / "cpython"
            base.mkdir()
            dependency = base / "vcruntime140.dll"
            dependency.write_bytes(b"runtime dependency")
            prefix = root / "install"
            neverc = prefix / "bin" / "neverc.exe"
            neverc.parent.mkdir(parents=True)
            neverc.write_bytes(b"compiler")
            licenses = prefix / "python" / "licenses"
            licenses.mkdir(parents=True)
            layout = self.module.RuntimeLayout(
                platform="windows",
                base_prefix=base,
                stdlib=base / "Lib",
                libdir=base,
                runtime_name="python312.dll",
                version="3.12.10",
                abi="cp312-win_amd64",
            )

            def inspect(path, _system):
                if path == neverc.resolve():
                    return {"VCRUNTIME140.dll": None}, []
                return {}, []

            with mock.patch.object(
                self.module, "native_files", return_value=[neverc.resolve()]
            ), mock.patch.object(
                self.module, "inspect_dependencies", side_effect=inspect
            ), mock.patch.object(
                self.module,
                "dependency_license",
                return_value="licenses/runtime.txt",
            ):
                copied, system = self.module.collect_dependency_closure(
                    prefix, neverc, layout, licenses
                )

            self.assertTrue((prefix / "bin" / "vcruntime140.dll").is_file())
            self.assertEqual(copied[0]["name"], "vcruntime140.dll")
            self.assertEqual(system, [])

    def test_debian_shared_documentation_directory_attributes_license(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            layout = self.make_posix_layout(root)
            source = root / "vendor" / "lib" / "libncursesw.so.6"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"ncurses runtime")
            licenses = root / "install" / "python" / "licenses"
            licenses.mkdir(parents=True)

            doc_root = root / "usr" / "share" / "doc"
            shared_doc = doc_root / "libtinfo6"
            shared_doc.mkdir(parents=True)
            copyright_file = shared_doc / "copyright"
            copyright_file.write_text("ncurses license\n", encoding="utf-8")
            (doc_root / "libncursesw6").symlink_to(
                shared_doc.name, target_is_directory=True
            )

            def dpkg_query(command, *, check=True):
                if command[1] == "-S":
                    return subprocess.CompletedProcess(
                        command,
                        0,
                        stdout=f"libncursesw6:arm64: {source}\n",
                        stderr="",
                    )
                if command[1] == "-L":
                    return subprocess.CompletedProcess(
                        command,
                        0,
                        stdout="/usr/share/doc/libncursesw6\n",
                        stderr="",
                    )
                raise AssertionError(command)

            with mock.patch.object(
                self.module.shutil, "which", return_value="/usr/bin/dpkg-query"
            ), mock.patch.object(
                self.module, "run_tool", side_effect=dpkg_query
            ), mock.patch.object(
                self.module, "DEBIAN_DOC_ROOT", doc_root, create=True
            ):
                attributed = self.module.dependency_license(
                    source, layout, licenses
                )

            copied = root / "install" / "python" / attributed
            self.assertEqual(copied.read_text(encoding="utf-8"), "ncurses license\n")


if __name__ == "__main__":
    unittest.main()
