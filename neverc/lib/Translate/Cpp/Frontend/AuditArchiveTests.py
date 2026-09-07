#!/usr/bin/env python3
"""Controlled symbol inventories for the builtin frontend link gate."""

import argparse
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest import mock

sys.dont_write_bytecode = True
import AuditArchive


class ArchiveAuditTests(unittest.TestCase):
    def test_standard_entity_not_merely_standard_parameter(self):
        for name in ("_ZNSt3__16vectorIiNS_9allocatorIiEEE5clearEv",
                     "__ZNKSt3__16vectorIiNS_9allocatorIiEEE4sizeEv",
                     "_ZGVZNSt3__112__some_std_fnEvE5value",
                     "_ZTINSt3__19exceptionE", "_Znwm", "__ZdlPvm",
                     "_malloc", "__clang_call_terminate",
                     "DW.ref.__gxx_personality_v0"):
            with self.subTest(name=name):
                self.assertTrue(AuditArchive.standard_shared_symbol(name))
        for name in ("_Z3fooNSt3__112basic_stringIcEE",
                     "_ZN4host3fooENSt3__16vectorIiEE",
                     "_ZN5clang4Decl4kindEv", "LLVMCreateMessage",
                     "malloc_wrong_abi", "__Znwcustom", "DW.ref.host_llvm",
                     "_DW.ref.__gxx_personality_v0"):
            with self.subTest(name=name):
                self.assertFalse(AuditArchive.standard_shared_symbol(name))

    def test_microsoft_std_in_return_argument_or_conversion_is_not_scope(self):
        for declaration in (
                "class std::string __cdecl host_function(void)",
                "void __cdecl host_function(class std::string)",
                "public: __cdecl Host::operator class std::string(void)",
                "void __cdecl Host::method<class std::string>(void)",
                "class std::string (__cdecl *Host::function(void))(void)",
                "const Host::`vftable'{for std::exception}",
                "public: virtual void * __cdecl Host::`scalar deleting destructor'(unsigned int)",
                "class Host<class std::string> `RTTI Type Descriptor'"):
            with self.subTest(declaration=declaration):
                self.assertFalse(AuditArchive.standard_shared_symbol(
                    "?host@@fake", declaration))
        self.assertTrue(AuditArchive.standard_shared_symbol(
            "?member@std@@fake",
            "public: void __cdecl std::vector<int>::clear(void)"))
        self.assertTrue(AuditArchive.standard_shared_symbol(
            "?member@std@@fake",
            "public: __cdecl std::function<void>::operator bool(void)"))
        self.assertTrue(AuditArchive.standard_shared_symbol(
            "??_R0?AVexception@std@@@8",
            "class std::exception `RTTI Type Descriptor'"))
        self.assertTrue(AuditArchive.standard_shared_symbol(
            "??_Gbad_alloc@std@@fake",
            "public: virtual void * __cdecl std::bad_alloc::`scalar deleting destructor'(unsigned int)"))

    def test_scan_excludes_private_dependency_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "nested").mkdir()
            (directory / "_deps").mkdir()
            host = directory / "nested" / "host.lib"
            host.touch()
            (directory / "_deps" / "upstream.a").touch()
            private = directory / "private.a"
            private.touch()
            self.assertEqual(AuditArchive.host_archives(directory, private), [host])

    def check_collision(self, private_kind):
        args = argparse.Namespace(nm="controlled-nm", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"))

        def inventory(_nm, paths, *options):
            if paths == [args.archive]:
                if "--format=posix" in options:
                    return ("neverc_cpp_frontend_main T 0 0\n"
                            f"host_counter {private_kind} 0 0\n")
                return "neverc_cpp_frontend_main\n"
            if "--format=posix" in options:
                return "host_counter D 0 0\n"
            return "host_counter\n"

        with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                mock.patch.object(AuditArchive, "host_archives",
                                  return_value=[Path("host.a")]):
            with self.assertRaisesRegex(ValueError, "intersection: host_counter"):
                AuditArchive.audit(args)

    def test_private_definition_cannot_overlap_host(self):
        self.check_collision("D")

    def test_private_reference_cannot_bind_to_host(self):
        self.check_collision("U")

    def test_private_weak_reference_cannot_bind_to_host(self):
        self.check_collision("w")

    def test_host_reader_is_separate_from_private_reader(self):
        args = argparse.Namespace(nm="private-llvm-nm20", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"),
                                  host_format="nm", host_nm="host-llvm-nm22")

        def inventory(reader, paths, *options):
            if paths == [args.archive]:
                self.assertEqual(reader, "private-llvm-nm20")
                return "neverc_cpp_frontend_main T 0 0\n"
            self.assertEqual(reader, "host-llvm-nm22")
            return "host_only_function T 0 0\n"

        with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                mock.patch.object(AuditArchive, "host_archives",
                                  return_value=[Path("host.a")]):
            AuditArchive.audit(args)

    def test_raw_clang_namespace_matches_without_demangling_ltcg(self):
        for name in ("?foo@Decl@clang@@QAEXXZ", "_ZN5clang4Decl3fooEv"):
            self.assertTrue(AuditArchive.has_raw_clang_name(name))
        for name in ("?foo@neverc@@YAXXZ", "_Z15clangSomethingv"):
            self.assertFalse(AuditArchive.has_raw_clang_name(name))

    def test_fallback_reader_failure_requires_compatible_host_tool(self):
        args = argparse.Namespace(nm="private-llvm-nm20", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"),
                                  host_format="nm", host_nm=None)

        def inventory(reader, paths, *options):
            self.assertEqual(reader, "private-llvm-nm20")
            if paths == [args.archive]:
                return "neverc_cpp_frontend_main T 0 0\n"
            raise OSError("unsupported host object")

        with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                mock.patch.object(AuditArchive, "host_archives",
                                  return_value=[Path("host.a")]):
            with self.assertRaisesRegex(ValueError, "NEVERC_CPP_HOST_NM"):
                AuditArchive.audit(args)

    def test_coff_index_definitions_still_catch_private_references(self):
        args = argparse.Namespace(nm="private-llvm-nm20", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"),
                                  host_format="coff-index", host_nm=None)
        reader = types.ModuleType("HostCoffSymbols")
        reader.read_defined_symbols = mock.Mock(return_value={"host_counter"})

        def inventory(_nm, paths, *options):
            # The proprietary host objects must never reach llvm-nm.
            self.assertEqual(paths, [args.archive])
            return "neverc_cpp_frontend_main T 0 0\nhost_counter U 0 0\n"

        with mock.patch.dict(sys.modules, {"HostCoffSymbols": reader}), \
                mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                mock.patch.object(AuditArchive, "host_archives",
                                  return_value=[Path("host.lib")]):
            with self.assertRaisesRegex(ValueError, "intersection: host_counter"):
                AuditArchive.audit(args)
        reader.read_defined_symbols.assert_called_once_with(Path("host.lib"))

    def test_corrupt_coff_index_is_never_treated_as_no_definitions(self):
        args = argparse.Namespace(nm="private-llvm-nm20", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"),
                                  host_format="coff-index", host_nm=None)
        reader = types.ModuleType("HostCoffSymbols")
        reader.read_defined_symbols = mock.Mock(side_effect=ValueError("bad index"))
        with mock.patch.dict(sys.modules, {"HostCoffSymbols": reader}), \
                mock.patch.object(AuditArchive, "nm_output",
                                  return_value="neverc_cpp_frontend_main T 0 0\n"), \
                mock.patch.object(AuditArchive, "host_archives",
                                  return_value=[Path("host.lib")]):
            with self.assertRaisesRegex(ValueError, "bad index"):
                AuditArchive.audit(args)

    def test_inspection_failure_invalidates_aggregate(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "private.a"
            archive.touch()
            with mock.patch.object(sys, "argv", ["audit", "--nm", "controlled",
                                                 "--archive", str(archive)]), \
                    mock.patch.object(AuditArchive, "audit",
                                      side_effect=OSError("inspection failed")):
                with self.assertRaisesRegex(SystemExit, "inspection failed"):
                    AuditArchive.main()
            self.assertFalse(archive.exists())


if __name__ == "__main__":
    unittest.main()
