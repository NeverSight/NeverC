#!/usr/bin/env python3
"""Controlled symbol inventories for the builtin frontend link gate."""

import argparse
import io
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest import mock

sys.dont_write_bytecode = True
import AuditArchive


class ArchiveAuditTests(unittest.TestCase):
    # Controlled ABI spellings use the documented byte length / CRC / encoded
    # byte grammar. Real compiler+nm coverage lives in the CI toolchain test.
    llvm_literal = ('??_C@_06BCDEFGHI@llvm?3?3?$AA@', 'R', '"llvm::"')
    clang_literal = ('??_C@_07CDEFGHIJ@clang?3?3?$AA@', 'R', '"clang::"')

    def audit_inventory(self, private, host=None, host_format="nm", coff_readobj=None):
        private = [("neverc_cpp_frontend_main", "T", "neverc_cpp_frontend_main"),
                   *private]
        args = argparse.Namespace(nm="controlled-nm", nm_file=None,
                                  archive=Path("private.lib"), prefix_header=None,
                                  host_lib_dir=Path("host-libs") if host is not None else None,
                                  host_format=host_format, host_nm=None,
                                  coff_readobj=coff_readobj, coff_readobj_file=None)

        def inventory(_nm, paths, *options):
            rows = private if paths == [args.archive] else host
            self.assertIsNotNone(rows)
            if "--defined-only" in options:
                rows = [row for row in rows if not AuditArchive.is_undefined(row[1])]
            if "--format=posix" in options:
                return (f"\n{paths[0].stem}.cpp.obj:\n" +
                        "".join(f"{raw} {kind} 0 0\n" for raw, kind, _ in rows))
            self.assertIn("--format=just-symbols", options)
            self.assertIn("--no-sort", options)
            return "".join((decoded if "--demangle" in options else raw) + "\n"
                           for raw, _, decoded in rows)

        reader = types.ModuleType("HostCoffSymbols")
        reader.read_defined_symbols = mock.Mock(return_value={
            raw for raw, kind, _ in host or [] if not AuditArchive.is_undefined(kind)})
        with mock.patch.dict(sys.modules, {"HostCoffSymbols": reader}), \
                mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                mock.patch.object(AuditArchive, "host_archives",
                                  return_value=[Path("host.lib")]):
            AuditArchive.audit(args)

    def test_microsoft_literal_requires_raw_identity_and_literal_decoding(self):
        # Wide and truncated spellings are from LLVM's ms-string-literals.test.
        for raw, decoded in (
                (self.llvm_literal[0], self.llvm_literal[2]),
                (self.clang_literal[0], self.clang_literal[2]),
                ('??_C@_01CNACBAHC@?$PP?$AA@', '"\\xFF"'),
                ('??_C@_13IIHIAFKH@?W?$PP?$AA?$AA@', 'L"\\xD7FF"'),
                ('??_C@_05OMLEGLOC@h?$AAi?$AA?$AA?$AA@', 'u"hi"'),
                ('??_C@_0M@GFNAJIPG@h?$AA?$AA?$AAi?$AA?$AA?$AA?$AA?$AA?$AA?$AA@',
                 'U"hi"'),
                ('??_C@_0CF@LABBIIMO@012345678901234567890123456789AB@',
                 '"012345678901234567890123456789AB"...')):
            with self.subTest(raw=raw):
                self.assertTrue(AuditArchive.microsoft_string_literal(raw, decoded))
                self.assertTrue(AuditArchive.standard_shared_symbol(raw, decoded))

    def test_quotes_or_literal_prefix_do_not_exempt_an_entity(self):
        for raw, decoded in (
                ("?call@llvm@@YAXXZ", '"llvm::"'),
                ("host_function", '"clang::"'),
                ("??_C@_host_function", '"llvm::"'),
                ("??_C@_06Q@llvm?3?3?$AA@", '"llvm::"'),
                (self.llvm_literal[0] + "suffix", self.llvm_literal[2]),
                (self.llvm_literal[0], "void __cdecl llvm::call(void)"),
                (self.llvm_literal[0], '"llvm::" unparsed'),
                (self.llvm_literal[0], '"llvm::unterminated'),
                (self.llvm_literal[0], '"llvm::\n"')):
            with self.subTest(raw=raw, decoded=decoded):
                self.assertFalse(AuditArchive.microsoft_string_literal(raw, decoded))
                self.assertFalse(AuditArchive.standard_shared_symbol(raw, decoded))

    def test_private_namespace_text_inside_literal_is_not_an_abi_entity(self):
        self.audit_inventory([self.llvm_literal, self.clang_literal])

    def test_host_namespace_text_and_identical_literal_comdats_are_accepted(self):
        self.audit_inventory([self.llvm_literal, self.clang_literal],
                             [self.llvm_literal, self.clang_literal])

    def test_coff_index_identical_literal_identity_is_accepted(self):
        self.audit_inventory([self.llvm_literal, self.clang_literal],
                             [self.llvm_literal, self.clang_literal], "coff-index")

    def test_real_private_llvm_entity_is_not_hidden_by_literal(self):
        for kind in ("T", "U"):
            with self.subTest(kind=kind):
                with self.assertRaisesRegex(ValueError, "llvm::call"):
                    self.audit_inventory([
                        self.llvm_literal,
                        ("?call@llvm@@YAXXZ", kind, "void __cdecl llvm::call(void)")])

    def test_real_host_clang_entity_is_not_hidden_by_literal(self):
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                with self.assertRaisesRegex(ValueError, "unexpected host Clang"):
                    self.audit_inventory([self.clang_literal], [
                        self.clang_literal,
                        ("?call@clang@@YAXXZ", "T", "void __cdecl clang::call(void)")],
                        host_format)

    def test_quoted_nonliteral_shared_symbol_still_fails_intersection(self):
        with self.assertRaisesRegex(ValueError, "intersection: host_function"):
            self.audit_inventory([("host_function", "T", '"ordinary text"')],
                                 [("host_function", "T", '"ordinary text"')])

    def test_raw_decoded_inventory_length_mismatch_fails_closed(self):
        with mock.patch.object(AuditArchive, "nm_output", side_effect=[
                "raw_one\nraw_two\n", "decoded_one\n"]):
            with self.assertRaisesRegex(ValueError, "Inconsistent llvm-nm"):
                AuditArchive.decoded_symbols("controlled-nm", [Path("private.lib")])

    def test_proven_coff_alias_resolves_private_dependency(self):
        alias = "neverc_cpp_llvm_alias"
        reader = types.ModuleType("CoffWeakAliases")
        reader.read_resolved_aliases = mock.Mock(return_value={alias})
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            self.audit_inventory([(alias, "w", alias)], coff_readobj="controlled-readobj")
        reader.read_resolved_aliases.assert_called_once_with(
            "controlled-readobj", Path("private.lib"), {"neverc_cpp_frontend_main"}, {alias})

    def test_defined_coff_alias_still_requires_fallback_validation(self):
        alias = "neverc_cpp_llvm_alias"
        reader = types.ModuleType("CoffWeakAliases")
        reader.read_resolved_aliases = mock.Mock(side_effect=ValueError("unresolved COFF alias"))
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            with self.assertRaisesRegex(ValueError, "unresolved COFF alias"):
                self.audit_inventory([(alias, "W", alias)], coff_readobj="controlled-readobj")
        self.assertIn(alias, reader.read_resolved_aliases.call_args.args[3])

    def test_resolved_coff_alias_still_participates_in_host_collision_gate(self):
        alias = "neverc_cpp_llvm_alias"
        reader = types.ModuleType("CoffWeakAliases")
        reader.read_resolved_aliases = mock.Mock(return_value={alias})
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            with self.assertRaisesRegex(ValueError, "intersection: " + alias):
                self.audit_inventory([(alias, "w", alias)], [(alias, "T", alias)],
                                     coff_readobj="controlled-readobj")

    def test_posix_records_preserve_original_member_headers_and_rows(self):
        output = "\nSignals.cpp.o:\n_strdup U 0 0\n\nprivate.a(copy.o):\n_strdup W 0 1\n"
        self.assertEqual(list(AuditArchive.symbol_records(output)), [
            ("_strdup", "U", "Signals.cpp.o:", "_strdup U 0 0"),
            ("_strdup", "W", "private.a(copy.o):", "_strdup W 0 1")])
        self.assertEqual(list(AuditArchive.symbol_rows(output)),
                         [("_strdup", "U"), ("_strdup", "W")])

    def test_strdup_reference_only_reports_actual_nm_provenance(self):
        for name in ("strdup", "_strdup"):
            for kind in ("U", "w", "v"):
                with self.subTest(name=name, kind=kind), \
                        mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
                    self.audit_inventory([(name, kind, name)], [(name, "T", name)])
                    text = output.getvalue()
                    self.assertIn("private nm archive='private.lib' member_header='private.cpp.obj:'", text)
                    self.assertIn(f"kind={kind!r} raw='{name} {kind} 0 0'", text)
                    self.assertIn("host nm archive='host.lib' member_header='host.cpp.obj:'", text)
                    self.assertIn(f"kind='T' raw='{name} T 0 0'", text)
                    self.assertNotIn("neverc_cpp_frontend_main T", text)

    def test_strdup_private_strong_or_weak_definition_is_never_exempted(self):
        for name in ("strdup", "_strdup"):
            for kind in ("T", "D", "W", "V"):
                with self.subTest(name=name, kind=kind), \
                        mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
                    # A reference in another member cannot hide a definition.
                    with self.assertRaisesRegex(ValueError, "intersection: " + name):
                        self.audit_inventory([(name, "U", name), (name, kind, name)],
                                             [(name, "T", name)])
                    text = output.getvalue()
                    self.assertIn(
                        "private nm archive='private.lib' member_header='private.cpp.obj:' "
                        f"symbol={name!r} kind={kind!r} raw='{name} {kind} 0 0'", text)
                    self.assertIn("host nm archive='host.lib'", text)

    def test_strdup_near_names_do_not_get_reference_exception(self):
        for name in ("__strdup", "strdup_custom", "_strdup_custom", "neverc_cpp_strdup"):
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, "intersection: " + name):
                    self.audit_inventory([(name, "U", name)], [(name, "T", name)])

    def test_strdup_coff_index_reports_limited_evidence_without_exception(self):
        for name in ("strdup", "_strdup"):
            with self.subTest(name=name), \
                    mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
                with self.assertRaisesRegex(ValueError, "intersection: " + name):
                    self.audit_inventory([(name, "U", name)], [(name, "T", name)], "coff-index")
                text = output.getvalue()
                self.assertIn(f"kind='U' raw='{name} U 0 0'", text)
                self.assertIn("host coff-index archive='host.lib'", text)
                self.assertIn("object kind, member and raw nm row unavailable", text)
                self.assertIn("strdup exception disabled", text)
                self.assertNotIn("host nm", text)

    def test_strdup_batch_provenance_identifies_each_actual_host_archive(self):
        args = argparse.Namespace(nm="controlled-nm", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"), host_format="nm", host_nm=None)
        hosts = [Path("first.a"), Path("second.a")]

        def inventory(_nm, paths, *options):
            if paths == [args.archive]:
                if "--format=posix" in options:
                    return "private.o:\nneverc_cpp_frontend_main T 0 0\n_strdup U 0 0\n"
                return "neverc_cpp_frontend_main\n_strdup\n"
            if "--format=posix" not in options:
                return "_strdup\nother_host_symbol\n"
            return "".join("same-member.o:\n" + ("_strdup T 1 2\n" if archive == hosts[0]
                                                  else "other_host_symbol T 3 4\n")
                           for archive in paths)

        with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory) as reader, \
                mock.patch.object(AuditArchive, "host_archives", return_value=hosts), \
                mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
            AuditArchive.audit(args)
            text = output.getvalue()
            self.assertIn("host nm archive='first.a' member_header='same-member.o:'", text)
            self.assertIn("raw='_strdup T 1 2'", text)
            self.assertNotIn("archive='second.a'", text)
            for archive in hosts:
                reader.assert_any_call("controlled-nm", [archive], "--format=posix")

    def test_strdup_changed_host_inventory_fails_closed(self):
        args = argparse.Namespace(nm="controlled-nm", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"), host_format="nm", host_nm=None)
        hosts = [Path("first.a"), Path("second.a")]

        def inventory(_nm, paths, *options):
            if paths == [args.archive]:
                if "--format=posix" in options:
                    return "private.o:\nneverc_cpp_frontend_main T 0 0\n_strdup U 0 0\n"
                return "neverc_cpp_frontend_main\n_strdup\n"
            self.assertIn("--format=posix", options)
            if paths == hosts:
                return "same-member.o:\n_strdup T 1 2\n"
            if paths == [hosts[0]]:
                return "same-member.o:\n_strdup W 1 2\n"
            self.assertEqual(paths, [hosts[1]])
            return "same-member.o:\nother_host_symbol T 3 4\n"

        with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                mock.patch.object(AuditArchive, "host_archives", return_value=hosts), \
                mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
            with self.assertRaisesRegex(ValueError, "Host strdup symbol inventory changed"):
                AuditArchive.audit(args)
            self.assertIn("host nm archive='first.a' member_header='same-member.o:' "
                          "symbol='_strdup' kind='W' raw='_strdup W 1 2'", output.getvalue())
            self.assertIn("host nm batch_archives=['first.a', 'second.a'] "
                          "member_header='same-member.o:' symbol='_strdup' kind='T' "
                          "raw='_strdup T 1 2'", output.getvalue())

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
                     "malloc_wrong_abi", "strdup", "_strdup", "strdup_custom", "_strdup_custom",
                     "neverc_cpp_strdup", "__Znwcustom", "DW.ref.host_llvm",
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
                return "neverc_cpp_frontend_main\nhost_counter\n"
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
                return ("neverc_cpp_frontend_main T 0 0\n" if "--format=posix" in options
                        else "neverc_cpp_frontend_main\n")
            self.assertEqual(reader, "host-llvm-nm22")
            return ("host_only_function T 0 0\n" if "--format=posix" in options
                    else "host_only_function\n")

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
                return ("neverc_cpp_frontend_main T 0 0\n" if "--format=posix" in options
                        else "neverc_cpp_frontend_main\n")
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
            return ("neverc_cpp_frontend_main T 0 0\nhost_counter U 0 0\n"
                    if "--format=posix" in options else "neverc_cpp_frontend_main\nhost_counter\n")

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

        def inventory(_nm, _paths, *options):
            return ("neverc_cpp_frontend_main T 0 0\n" if "--format=posix" in options
                    else "neverc_cpp_frontend_main\n")

        with mock.patch.dict(sys.modules, {"HostCoffSymbols": reader}), \
                mock.patch.object(AuditArchive, "nm_output",
                                  side_effect=inventory), \
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
