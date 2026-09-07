#!/usr/bin/env python3
"""Exercise the frontend ABI audit with real, tiny Microsoft COFF archives."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ENTRY = 'extern "C" int neverc_cpp_frontend_main(int, const char **) { return 0; }\n'


class CppFrontendToolchainTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.audit = (Path(__file__).resolve().parents[2] /
                     "neverc/lib/Translate/Cpp/Frontend/AuditArchive.py")
        cls.clang = cls.llvm_root / "bin/clang++.exe"
        cls.librarian = cls.llvm_root / "bin/llvm-lib.exe"
        cls.nm = cls.llvm_root / "bin/llvm-nm.exe"
        cls.readobj = cls.llvm_root / "bin/llvm-readobj.exe"
        cls.msvc = shutil.which("cl.exe")
        if not cls.msvc:
            raise FileNotFoundError("The MSVC developer environment is required")
        for tool in (cls.clang, cls.librarian, cls.nm, cls.readobj):
            if not tool.is_file():
                raise FileNotFoundError(f"Required CI tool is missing: {tool}")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="neverc-cpp-coff-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def run_command(self, command):
        return subprocess.run(
            [str(argument) for argument in command], cwd=self.root,
            capture_output=True, text=True, encoding="utf-8", errors="replace",
            timeout=120, check=False,
        )

    def require_success(self, command):
        result = self.run_command(command)
        self.assertEqual(result.returncode, 0,
                         f"{command!r}\n{result.stdout}\n{result.stderr}")
        return result.stdout

    def archive(self, directory, name, source, *, msvc=False, assembly=None,
                extra_sources=(), prefix_header=None):
        directory.mkdir(parents=True, exist_ok=True)
        library = directory / (name + ".lib")
        objects = []
        for index, text in enumerate((source, *extra_sources)):
            stem = name if index == 0 else f"{name}-{index}"
            cpp = directory / (stem + ".cpp")
            obj = directory / (stem + ".obj")
            cpp.write_text(text, encoding="utf-8")
            if msvc:
                prefix = ["/FI" + str(prefix_header)] if prefix_header else []
                self.require_success([self.msvc, "/nologo", "/c", "/Od", "/GL-",
                                      "/GR-", "/EHsc", *prefix, "/Fo" + str(obj), cpp])
            else:
                prefix = ["-include", prefix_header] if prefix_header else []
                self.require_success([
                    self.clang, "--target=" + self.target, "-std=c++17", "-O2",
                    "-fms-extensions", "-fmerge-all-constants", "-fno-exceptions",
                    "-fno-rtti", *prefix, "-c", cpp, "-o", obj,
                ])
            objects.append(obj)
        if assembly is not None:
            asm = directory / (name + ".s")
            asm_obj = directory / (name + "-asm.obj")
            asm.write_text(assembly, encoding="utf-8")
            self.require_success([self.clang, "--target=" + self.target, "-c",
                                  "-x", "assembler", "-fno-lto", asm, "-o", asm_obj])
            objects.append(asm_obj)
        self.require_success([self.librarian, "/nologo", "/out:" + str(library), *objects])
        return library

    def check_audit(self, private, host=None, host_format="nm", error=None,
                    *, coff_reader=True, prefix_header=None):
        command = [sys.executable, "-E", "-B", self.audit,
                   "--nm", self.nm, "--archive", private]
        if coff_reader:
            command.extend(["--coff-readobj", self.readobj])
        if prefix_header is not None:
            command.extend(["--prefix-header", prefix_header])
        if host is not None:
            command.extend(["--host-lib-dir", host, "--host-format", host_format])
            if host_format == "nm":
                command.extend(["--host-nm", self.nm])
        result = self.run_command(command)
        output = result.stdout + result.stderr
        if error is None:
            self.assertEqual(result.returncode, 0, output)
            self.assertIn("defined and undefined symbols use the private LLVM ABI",
                          output)
            self.assertTrue(private.is_file())
        else:
            self.assertNotEqual(result.returncode, 0, output)
            self.assertIn(error, output)
            self.assertFalse(private.exists(), "A rejected aggregate must be removed")

    def test_windows_abort_handler_definition_and_references_are_private(self):
        # Like Windows Signals.inc, this C-linkage function is declared inside
        # namespace llvm. A namespace prefix alone cannot isolate its ABI name.
        definition = ('namespace llvm { extern "C" void HandleAbort(int signal) '
                      '{ (void)signal; } }\n')
        reference = (ENTRY + 'namespace llvm { extern "C" void HandleAbort(int); }\n'
                     'extern "C" { void (*neverc_cpp_abort_handler)(int) = '
                     'llvm::HandleAbort; }\n')
        header = self.root / "PrivatePrefix.h"
        header.write_text("#define llvm neverc_cpp_llvm\n"
                          "#define HandleAbort neverc_cpp_HandleAbort\n", encoding="utf-8")
        for msvc in (False, True):
            compiler = "msvc" if msvc else "clang"
            host = self.root / compiler / "host"
            self.archive(host, "host", definition, msvc=msvc)
            for host_format in ("nm", "coff-index"):
                with self.subTest(compiler=compiler, host_format=host_format):
                    directory = self.root / compiler / host_format
                    unisolated = self.archive(directory / "unisolated", "private",
                                              definition, extra_sources=(reference,), msvc=msvc)
                    self.check_audit(
                        unisolated, host, host_format,
                        error="private/host symbol intersection: HandleAbort")
                    isolated = self.archive(directory / "isolated", "private",
                                            definition, extra_sources=(reference,),
                                            msvc=msvc, prefix_header=header)
                    inventory = self.require_success(
                        [self.nm, "--extern-only", "--format=posix", isolated])
                    self.assertRegex(inventory, r"(?m)^neverc_cpp_HandleAbort T\s")
                    self.assertRegex(inventory, r"(?m)^neverc_cpp_HandleAbort U\s")
                    self.assertNotRegex(inventory, r"(?m)^HandleAbort\s")
                    self.check_audit(isolated, host, host_format, prefix_header=header)
                    unresolved = self.archive(directory / "unresolved", "private",
                                              reference, msvc=msvc, prefix_header=header)
                    self.check_audit(
                        unresolved, host, host_format, prefix_header=header,
                        error="unresolved private dependency: neverc_cpp_HandleAbort")

    def test_msvc_vector_destructor_fallback_is_resolved_from_actual_aux_record(self):
        source = ENTRY + """
namespace neverc_cpp_llvm {
struct Probe { virtual ~Probe(); virtual int value() const; };
Probe::~Probe() {}
int Probe::value() const { return 7; }
Probe instance;
}
extern "C" void destroy_probe(neverc_cpp_llvm::Probe *p) { delete p; }
"""
        private = self.archive(self.root / "private", "private", source, msvc=True)
        records = self.require_success([self.readobj, "--symbols", private])
        blocks = re.findall(r"  Symbol \{\n(.*?)\n  \}", records, re.S)
        aliases = [block for block in blocks
                   if re.search(r"Name: \?\?_E[^\n]*@neverc_cpp_llvm@@", block)]
        self.assertTrue(aliases, "MSVC must emit the vector destructor fixture")
        self.assertTrue(any("StorageClass: WeakExternal" in block and
                            "AuxWeakExternal {" in block for block in aliases),
                        records)
        # This is a real-tool RED check: nm alone cannot prove this fallback.
        without_reader = self.root / "without-reader.lib"
        shutil.copyfile(private, without_reader)
        self.check_audit(without_reader, coff_reader=False,
                         error="unresolved private dependency: ??_E")
        self.check_audit(private)

    @staticmethod
    def weak_alias_assembly(target):
        return (".data\n.globl neverc_cpp_llvm_target\nneverc_cpp_llvm_target:\n"
                ".long 7\n.weak neverc_cpp_llvm_alias\n"
                f"neverc_cpp_llvm_alias = {target}\n"
                ".globl alias_anchor\nalias_anchor:\n.quad neverc_cpp_llvm_alias\n")

    def test_actual_coff_alias_requires_a_private_fallback_definition(self):
        private = self.archive(
            self.root / "private", "private", ENTRY,
            assembly=self.weak_alias_assembly("neverc_cpp_llvm_target"))
        records = self.require_success([self.readobj, "--symbols", private])
        self.assertIn("AuxWeakExternal {", records)
        self.assertIn("Linked: neverc_cpp_llvm_target (", records)
        self.assertIn("Search: Alias (0x3)", records)
        self.check_audit(private)

        missing = self.archive(
            self.root / "missing", "private", ENTRY,
            assembly=self.weak_alias_assembly("missing_fallback_target"))
        # The missing target has no private namespace marker: the alias edge
        # itself must be checked even if nm calls the alias a definition.
        self.check_audit(missing, error="COFF")

    @staticmethod
    def literals(prefix):
        return (
            f'extern "C" const char *{prefix}_llvm() {{ return "llvm::"; }}\n'
            f'extern "C" const char *{prefix}_clang() {{ return "clang::"; }}\n'
        )

    def test_pooled_literal_contents_are_not_namespace_entities(self):
        private = self.archive(self.root / "private", "private",
                               ENTRY + self.literals("neverc_cpp_literal"))
        host = self.root / "host"
        host_archive = self.archive(host, "host", self.literals("host_literal"))
        inventories = []
        for archive in (private, host_archive):
            raw = self.require_success(
                [self.nm, "--extern-only", "--format=just-symbols", archive])
            inventories.append({line for line in raw.splitlines()
                                if line.startswith("??_C@_")})
            decoded = self.require_success(
                [self.nm, "--extern-only", "--demangle", archive])
            self.assertIn('"llvm::"', decoded)
            self.assertIn('"clang::"', decoded)
        self.assertGreaterEqual(len(inventories[0] & inventories[1]), 2,
                                "The fixture must exercise shared literal COMDATs")
        self.check_audit(private)
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                self.check_audit(private, host, host_format)

    def test_actual_private_llvm_entity_is_rejected(self):
        private = self.archive(
            self.root / "private", "private",
            ENTRY + "namespace llvm { int unisolated_entity() { return 7; } }\n")
        self.check_audit(private, error="llvm::unisolated_entity")

    def test_actual_host_clang_entity_is_rejected(self):
        host = self.root / "host"
        self.archive(host, "host",
                     "namespace clang { int unisolated_entity() { return 7; } }\n")
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                private = self.archive(self.root / host_format, "private", ENTRY)
                self.check_audit(private, host, host_format,
                                 error="unexpected host Clang definition:")

    def test_actual_nonstandard_shared_definition_is_rejected(self):
        shared = 'extern "C" int forbidden_shared_function() { return 7; }\n'
        host = self.root / "host"
        self.archive(host, "host", shared)
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                private = self.archive(self.root / host_format, "private",
                                       ENTRY + shared)
                self.check_audit(
                    private, host, host_format,
                    error="private/host symbol intersection: forbidden_shared_function")

    def test_actual_strong_strdup_collision_is_rejected(self):
        # A familiar CRT spelling must not exempt two competing definitions.
        # This fixture deliberately defines its own function in both archives.
        for name in ("strdup", "_strdup"):
            shared = f'extern "C" char *{name}(const char *) {{ return nullptr; }}\n'
            host = self.root / name / "host"
            self.archive(host, "host", shared)
            for host_format in ("nm", "coff-index"):
                with self.subTest(name=name, host_format=host_format):
                    private = self.archive(
                        self.root / name / host_format, "private", ENTRY + shared)
                    self.check_audit(
                        private, host, host_format,
                        error="private/host symbol intersection: " + name)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-root", type=Path, required=True)
    parser.add_argument("--target", required=True, choices=(
        "x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc"))
    arguments = parser.parse_args()
    CppFrontendToolchainTests.llvm_root = arguments.llvm_root.resolve()
    CppFrontendToolchainTests.target = arguments.target
    unittest.main(argv=[sys.argv[0]], verbosity=2)
