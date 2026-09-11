#!/usr/bin/env python3
"""Controlled symbol inventories for the builtin frontend link gate."""

import argparse
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

sys.dont_write_bytecode = True
import AuditArchive
import SetupGuidSymbols


# Fixed witnesses copied from the c942 native SDK symbol reports, independent
# of the production policy's template-name construction.
SETUP_GUID_PAIRS = (
    ("_GUID_00000000_0000_0000_c000_000000000046", "neverc_cpp0000000000000000c000000000000046"),
    ("_GUID_177f0c4a_1cd3_4de7_a32c_71dbbb9fa36d", "neverc_cpp177f0c4a1cd34de7a32c71dbbb9fa36d"),
    ("_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b", "neverc_cpp42843719db4c46c28e7c64f1816efd5b"),
    ("_GUID_26aab78c_4a60_49d6_af3b_3c35bc93365d", "neverc_cpp26aab78c4a6049d6af3b3c35bc93365d"),
    ("_GUID_42b21b78_6192_463e_87bf_d577838f1d5c", "neverc_cpp42b21b786192463e87bfd577838f1d5c"),
)
SETUP_GET_IID = (
    "?GetIID@?$_com_IIID@UISetupConfiguration@@$1?"
    "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b@@3U__s_GUID@@B@@SAAEBU_GUID@@XZ")
PRIVATE_SETUP_GET_IID = (
    "?GetIID@?$_com_IIID@UISetupConfiguration@@$1?"
    "neverc_cpp42843719db4c46c28e7c64f1816efd5b@@3U__s_GUID@@B@@SAAEBU_GUID@@XZ")
SETUP_CONVERT = (
    "??$?0V?$_com_IIID@UISetupConfiguration@@$1?"
    "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b@@3U__s_GUID@@B@@$0A@@"
    "?$_com_ptr_t@V?$_com_IIID@UISetupHelper@@$1?"
    "_GUID_42b21b78_6192_463e_87bf_d577838f1d5c@@3U__s_GUID@@B@@@@QEAA@AEBV"
    "?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration@@$1?"
    "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b@@3U__s_GUID@@B@@@@@Z")
PRIVATE_SETUP_CONVERT = (
    "??$?0V?$_com_IIID@UISetupConfiguration@@$1?"
    "neverc_cpp42843719db4c46c28e7c64f1816efd5b@@3U__s_GUID@@B@@$0A@@"
    "?$_com_ptr_t@V?$_com_IIID@UISetupHelper@@$1?"
    "neverc_cpp42b21b786192463e87bfd577838f1d5c@@3U__s_GUID@@B@@@@QEAA@AEBV"
    "?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration@@$1?"
    "neverc_cpp42843719db4c46c28e7c64f1816efd5b@@3U__s_GUID@@B@@@@@Z")
SETUP_RELEASE = (
    "?_Release@?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration@@$1?"
    "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b@@3U__s_GUID@@B@@@@AEAAXXZ")
PRIVATE_SETUP_RELEASE = (
    "?_Release@?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration@@$1?"
    "neverc_cpp42843719db4c46c28e7c64f1816efd5b@@3U__s_GUID@@B@@@@AEAAXXZ")
SETUP_DEFAULT = (
    "??0?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration@@$1?"
    "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b@@3U__s_GUID@@B@@@@QEAA@XZ")
PRIVATE_SETUP_DEFAULT = (
    "??0?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration@@$1?"
    "neverc_cpp42843719db4c46c28e7c64f1816efd5b@@3U__s_GUID@@B@@@@QEAA@XZ")
SETUP_INTERFACE_PTR = (
    "?GetInterfacePtr@?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration2@@$1?"
    "_GUID_26aab78c_4a60_49d6_af3b_3c35bc93365d@@3U__s_GUID@@B@@@@QEAAAEAPEAUISetupConfiguration2@@XZ")
PRIVATE_SETUP_INTERFACE_PTR = (
    "?GetInterfacePtr@?$_com_ptr_t@V?$_com_IIID@UISetupConfiguration2@@$1?"
    "neverc_cpp26aab78c4a6049d6af3b3c35bc93365d@@3U__s_GUID@@B@@@@QEAAAEAPEAUISetupConfiguration2@@XZ")


class SetupGuidPolicyTests(unittest.TestCase):
    def test_exact_five_data_mapping(self):
        self.assertEqual(SetupGuidSymbols.GUID_RENAMES, dict(SETUP_GUID_PAIRS))
        self.assertEqual(SetupGuidSymbols.build_rename_map(old for old, _ in SETUP_GUID_PAIRS),
                         dict(SETUP_GUID_PAIRS))
        for old, new in SETUP_GUID_PAIRS:
            with self.subTest(old=old):
                self.assertTrue(SetupGuidSymbols.contains_old_guid_name(old))
                self.assertEqual(SetupGuidSymbols.rewrite_name(old), new)
                self.assertTrue(SetupGuidSymbols.is_private_guid_name(new))
                self.assertFalse(SetupGuidSymbols.contains_old_guid_name(new))
                self.assertIsNone(SetupGuidSymbols.rewrite_name(new))
                self.assertEqual(len(old.encode("ascii")), 42)
                self.assertEqual(len(new.encode("ascii")), 42)
                self.assertFalse(SetupGuidSymbols.is_private_guid_metadata_name(new))

    def test_observed_full_names_rewrite_every_nttp(self):
        for old, new in ((SETUP_GET_IID, PRIVATE_SETUP_GET_IID),
                         (SETUP_CONVERT, PRIVATE_SETUP_CONVERT)):
            with self.subTest(old=old):
                self.assertEqual(SetupGuidSymbols.rewrite_name(old), new)
                self.assertTrue(SetupGuidSymbols.is_private_guid_name(new))
                self.assertFalse(SetupGuidSymbols.contains_old_guid_name(new))
                self.assertEqual(len(old.encode("ascii")), len(new.encode("ascii")))
                self.assertFalse(SetupGuidSymbols.is_private_guid_metadata_name(new))

    def test_unrelated_guid_and_symbols_are_unchanged(self):
        for name in ("_GUID_6380bcff_41d3_4b2e_8b2e_bf8a6810c848", "_GUID_unrelated",
                     "?GetIID@Unrelated@@SAAEBU_GUID@@XZ", "neverc_cpp_frontend_main"):
            with self.subTest(name=name):
                self.assertIsNone(SetupGuidSymbols.rewrite_name(name))
                self.assertFalse(SetupGuidSymbols.contains_old_guid_name(name))
                self.assertFalse(SetupGuidSymbols.is_private_guid_name(name))

    def test_unobserved_old_grammar_is_rejected(self):
        old = SETUP_GUID_PAIRS[2][0]
        for name in ("prefix" + old, old + "suffix", "$1?" + old + "@@3U__s_GUID@@B",
                     SETUP_GET_IID.replace("GetIID", "InventedMethod"),
                     SETUP_GET_IID.replace("UISetupConfiguration@@", "UOther@@"),
                     SETUP_GET_IID + "suffix", SETUP_CONVERT.replace(old, SETUP_GUID_PAIRS[2][1], 1)):
            with self.subTest(name=name):
                self.assertTrue(SetupGuidSymbols.contains_old_guid_name(name))
                with self.assertRaisesRegex(ValueError, "Unsupported original Setup GUID symbol grammar"):
                    SetupGuidSymbols.build_rename_map([name])

    def test_unobserved_new_grammar_is_rejected(self):
        for name in ("prefix" + SETUP_GUID_PAIRS[0][1], SETUP_GUID_PAIRS[0][1] + "suffix",
                     PRIVATE_SETUP_GET_IID.replace("GetIID", "InventedMethod")):
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, "Unsupported private Setup GUID symbol grammar"):
                    SetupGuidSymbols.is_private_guid_name(name)
                with self.assertRaisesRegex(ValueError, "Unsupported private Setup GUID symbol grammar"):
                    SetupGuidSymbols.build_rename_map([name])

    def test_existing_target_collides_but_duplicate_source_occurrences_are_valid(self):
        for old, new in (SETUP_GUID_PAIRS[0], (SETUP_GET_IID, PRIVATE_SETUP_GET_IID)):
            with self.subTest(old=old):
                with self.assertRaisesRegex(ValueError, "Setup GUID target-name collision"):
                    SetupGuidSymbols.build_rename_map([old, new])
                self.assertEqual(SetupGuidSymbols.build_rename_map([old, old]), {old: new})

    def test_mapping_must_be_injective(self):
        with mock.patch.object(SetupGuidSymbols, "rewrite_name", return_value=SETUP_GUID_PAIRS[0][1]):
            with self.assertRaisesRegex(ValueError, "Non-injective Setup GUID symbol mapping"):
                SetupGuidSymbols.build_rename_map(["A" * 42, "B" * 42])

    def test_partially_rewritten_inventory_is_rejected(self):
        # All five old data definitions can still exist after an incomplete
        # template-only rename. The writer must not accept that mixed state.
        for existing in (SETUP_GUID_PAIRS[0][1], PRIVATE_SETUP_GET_IID,
                         "$pdata$" + PRIVATE_SETUP_RELEASE):
            with self.subTest(existing=existing):
                with self.assertRaisesRegex(ValueError, "Setup GUID target-name collision"):
                    SetupGuidSymbols.build_rename_map([*(old for old, _ in SETUP_GUID_PAIRS), existing])

    def test_exact_observed_metadata_combinations_preserve_full_names_and_lengths(self):
        cases = [(prefix + SETUP_RELEASE, prefix + PRIVATE_SETUP_RELEASE)
                 for prefix in ("$pdata$", "$unwind$", "$cppxdata$", "$ip2state$")]
        cases += [(prefix + SETUP_CONVERT, prefix + PRIVATE_SETUP_CONVERT)
                  for prefix in ("$pdata$", "$unwind$")]
        cases += [("$pdata$" + SETUP_DEFAULT, "$pdata$" + PRIVATE_SETUP_DEFAULT),
                  ("$pdata$" + SETUP_INTERFACE_PTR, "$pdata$" + PRIVATE_SETUP_INTERFACE_PTR)]
        for old, new in cases:
            with self.subTest(old=old):
                self.assertEqual(SetupGuidSymbols.build_rename_map([old]), {old: new})
                self.assertTrue(SetupGuidSymbols.is_private_guid_name(new))
                self.assertTrue(SetupGuidSymbols.is_private_guid_metadata_name(new))
                self.assertFalse(SetupGuidSymbols.is_private_guid_metadata_name(old))
                self.assertEqual(len(old.encode("ascii")), len(new.encode("ascii")))
                self.assertFalse(SetupGuidSymbols.contains_old_guid_name(new))

    def test_metadata_prefixes_do_not_admit_unobserved_combinations(self):
        cases = [(prefix + SETUP_GET_IID, prefix + PRIVATE_SETUP_GET_IID)
                 for prefix in ("$pdata$", "$unwind$", "$cppxdata$", "$ip2state$")]
        cases += [(prefix + SETUP_CONVERT, prefix + PRIVATE_SETUP_CONVERT)
                  for prefix in ("$cppxdata$", "$ip2state$")]
        cases += [(prefix + old, prefix + new)
                  for old, new in ((SETUP_DEFAULT, PRIVATE_SETUP_DEFAULT),
                                   (SETUP_INTERFACE_PTR, PRIVATE_SETUP_INTERFACE_PTR))
                  for prefix in ("$unwind$", "$cppxdata$", "$ip2state$")]
        cases += [("$future$" + SETUP_RELEASE, "$future$" + PRIVATE_SETUP_RELEASE),
                  ("$pdata$" + SETUP_GUID_PAIRS[0][0], "$pdata$" + SETUP_GUID_PAIRS[0][1])]
        for old, new in cases:
            with self.subTest(old=old):
                with self.assertRaisesRegex(ValueError, "Unsupported original Setup GUID symbol grammar"):
                    SetupGuidSymbols.build_rename_map([old])
                with self.assertRaisesRegex(ValueError, "Unsupported private Setup GUID symbol grammar"):
                    SetupGuidSymbols.is_private_guid_name(new)

    def test_mapping_cannot_change_a_symbol_byte_length(self):
        with mock.patch.object(SetupGuidSymbols, "rewrite_name", return_value=SETUP_GUID_PAIRS[0][1]):
            with self.assertRaisesRegex(ValueError, "changes byte length"):
                SetupGuidSymbols.build_rename_map(["short_source"])


class ArchiveAuditTests(unittest.TestCase):
    # Controlled ABI spellings use the documented byte length / CRC / encoded
    # byte grammar. Real compiler+nm coverage lives in the CI toolchain test.
    llvm_literal = ('??_C@_06BCDEFGHI@llvm?3?3?$AA@', 'R', '"llvm::"')
    clang_literal = ('??_C@_07CDEFGHIJ@clang?3?3?$AA@', 'R', '"clang::"')
    # Actual fe866 MSVC x64/ARM64 private R row and complete nm decoding.
    # Unlike the controlled payload spellings above, this has no encoded bytes.
    msvc_empty_literal = ('??_C@_00CNPNBAHC@@', 'R', '""...')
    # Complete LLVM 20 demangling from the 984608 Windows Clang x64 audit.
    # Nested local declarations retain their own return and parameter types.
    microsoft_gcd_lambda = (
        "public: <auto> __cdecl `unsigned int __cdecl std::gcd<unsigned int, "
        "unsigned int>(unsigned int, unsigned int)'::`1'::<lambda_1>::operator()<"
        "class `decltype(auto) __cdecl std::_Select_countr_zero_impl<unsigned int, "
        "class `unsigned int __cdecl std::gcd<unsigned int, unsigned int>"
        "(unsigned int, unsigned int)'::`1'::<lambda_1>>(class `unsigned int "
        "__cdecl std::gcd<unsigned int, unsigned int>(unsigned int, unsigned int)'"
        "::`1'::<lambda_1>)'::`1'::<lambda_1>>(class `decltype(auto) __cdecl "
        "std::_Select_countr_zero_impl<unsigned int, class `unsigned int __cdecl "
        "std::gcd<unsigned int, unsigned int>(unsigned int, unsigned int)'::`1'"
        "::<lambda_1>>(class `unsigned int __cdecl std::gcd<unsigned int, "
        "unsigned int>(unsigned int, unsigned int)'::`1'::<lambda_1>)'::`1'"
        "::<lambda_1>) const")

    def audit_inventory(self, private, host=None, host_format="nm", coff_readobj=None,
                        prefix_header=None):
        private = [("neverc_cpp_frontend_main", "T", "neverc_cpp_frontend_main"),
                   *private]
        args = argparse.Namespace(nm="controlled-nm", nm_file=None,
                                  archive=Path("private.lib"), prefix_header=prefix_header,
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

    def test_original_setup_guid_definitions_and_references_fail_without_host(self):
        for old, _ in SETUP_GUID_PAIRS:
            for kind in ("R", "D", "T", "W", "V", "U", "w", "v"):
                with self.subTest(name=old, kind=kind):
                    with self.assertRaisesRegex(ValueError, "unisolated Setup GUID symbol: " + old):
                        self.audit_inventory([(old, kind, old)])

    def test_original_setup_guid_gate_does_not_depend_on_host_intersection(self):
        for host_format in ("nm", "coff-index"):
            for old, _ in SETUP_GUID_PAIRS:
                for host in ([], [("host_only", "T", "host_only")], [(old, "R", old)]):
                    with self.subTest(host_format=host_format, name=old, host=host):
                        with self.assertRaisesRegex(ValueError, "unisolated Setup GUID symbol: " + old):
                            self.audit_inventory([(old, "U", old)], host, host_format)

    def test_original_setup_templates_and_unknown_spellings_fail_without_host(self):
        for name in (SETUP_GET_IID, SETUP_CONVERT, "$pdata$" + SETUP_RELEASE,
                     "invented_" + SETUP_GUID_PAIRS[0][0]):
            for kind in ("T", "U"):
                with self.subTest(name=name, kind=kind):
                    with self.assertRaises(ValueError) as failure:
                        self.audit_inventory([(name, kind, "void __cdecl std::controlled(void)")])
                    self.assertIn("unisolated Setup GUID symbol: " + name, str(failure.exception))

    def test_private_setup_definitions_close_references_without_host(self):
        for _, new in (*SETUP_GUID_PAIRS, (SETUP_GET_IID, PRIVATE_SETUP_GET_IID),
                       (SETUP_CONVERT, PRIVATE_SETUP_CONVERT)):
            with self.subTest(name=new):
                self.audit_inventory([(new, "R", new), (new, "U", new)])

    def test_host_cannot_supply_missing_private_setup_definitions(self):
        for old, new in (*SETUP_GUID_PAIRS, (SETUP_GET_IID, PRIVATE_SETUP_GET_IID)):
            for host_format in ("nm", "coff-index"):
                for host in (None, [(old, "R", old)], [(new, "R", new)]):
                    with self.subTest(name=new, host_format=host_format, host=host):
                        with self.assertRaises(ValueError) as failure:
                            self.audit_inventory([(new, "U", new)], host, host_format)
                        self.assertIn("unresolved private dependency: " + new, str(failure.exception))

    def test_closed_private_setup_names_remain_subject_to_host_intersection(self):
        old, new = SETUP_GUID_PAIRS[0]
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                self.audit_inventory([(new, "R", new), (new, "U", new)],
                                     [(old, "R", old)], host_format)
                with self.assertRaisesRegex(ValueError, "private/host symbol intersection: " + new):
                    self.audit_inventory([(new, "R", new)], [(new, "R", new)], host_format)

    def test_unknown_private_setup_spelling_is_rejected(self):
        name = "invented_" + SETUP_GUID_PAIRS[0][1]
        with self.assertRaisesRegex(ValueError, "Unsupported private Setup GUID symbol grammar: " + name):
            self.audit_inventory([(name, "R", name)])

    def test_metadata_in_extern_only_nm_is_rejected_not_an_external_obligation(self):
        for prefix in ("$pdata$", "$unwind$", "$cppxdata$", "$ip2state$"):
            name = prefix + PRIVATE_SETUP_RELEASE
            for kind in ("R", "T", "U", "W", "w"):
                with self.subTest(name=name, kind=kind):
                    with self.assertRaises(ValueError) as failure:
                        self.audit_inventory([(name, kind, name)])
                    self.assertIn("Setup GUID metadata has external linkage: " + name,
                                  str(failure.exception))
                    self.assertNotIn("unresolved private dependency: " + name, str(failure.exception))

    def test_private_setup_weak_names_require_actual_coff_closure(self):
        for new in (SETUP_GUID_PAIRS[0][1], PRIVATE_SETUP_GET_IID):
            for kind in ("W", "V", "w", "v"):
                with self.subTest(name=new, kind=kind):
                    with self.assertRaises(ValueError) as failure:
                        self.audit_inventory([(new, kind, new)])
                    self.assertIn("Setup GUID weak closure requires a COFF reader: " + new,
                                  str(failure.exception))
                    reader = types.ModuleType("CoffWeakAliases")
                    reader.read_resolved_aliases = mock.Mock(return_value={new})
                    with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
                        self.audit_inventory([(new, kind, new)], coff_readobj="controlled-readobj")
                    self.assertIn(new, reader.read_resolved_aliases.call_args.args[3])

    def test_coff_missing_private_setup_definition_keeps_specific_diagnostic(self):
        new = SETUP_GUID_PAIRS[0][1]
        reader = types.ModuleType("CoffWeakAliases")
        reader.read_resolved_aliases = mock.Mock(
            side_effect=ValueError("unresolved private dependency: " + new + "; COFF definition missing"))
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            with self.assertRaisesRegex(ValueError, "unresolved private dependency: " + new):
                self.audit_inventory([(new, "W", new)], [(new, "R", new)],
                                     coff_readobj="controlled-readobj")

    def test_coff_original_setup_name_keeps_specific_diagnostic(self):
        old = SETUP_GUID_PAIRS[0][0]
        reader = types.ModuleType("CoffWeakAliases")
        reader.read_resolved_aliases = mock.Mock(
            side_effect=ValueError("unisolated Setup GUID symbol: " + old + " in private.obj"))
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            with self.assertRaisesRegex(ValueError, "unisolated Setup GUID symbol: " + old):
                self.audit_inventory([(old, "R", old)], coff_readobj="controlled-readobj")

    def test_windows_abort_handler_prefix_checks_definitions_and_references(self):
        with tempfile.TemporaryDirectory(prefix="neverc-abort-prefix-") as temporary:
            header = Path(temporary) / "PrivatePrefix.h"
            header.write_text("#define HandleAbort neverc_cpp_HandleAbort\n",
                              encoding="utf-8")
            for name in ("HandleAbort", "_HandleAbort"):
                for kind in ("T", "U"):
                    with self.subTest(name=name, kind=kind):
                        with self.assertRaisesRegex(ValueError, "HandleAbort"):
                            self.audit_inventory([(name, kind, name)], prefix_header=header)
            for name in ("neverc_cpp_HandleAbort", "_neverc_cpp_HandleAbort"):
                with self.subTest(name=name):
                    with self.assertRaisesRegex(ValueError, "unresolved private dependency"):
                        self.audit_inventory([(name, "U", name)], prefix_header=header)
                    for host_format in ("nm", "coff-index"):
                        self.audit_inventory(
                            [(name, "T", name), (name, "U", name)],
                            [("HandleAbort", "T", "HandleAbort")], host_format,
                            prefix_header=header)

    def test_windows_default_fenv_object_requires_private_definition_and_references(self):
        with tempfile.TemporaryDirectory(prefix="neverc-fenv-prefix-") as temporary:
            header = Path(temporary) / "PrivatePrefix.h"
            header.write_text("#define _Fenv1 neverc_cpp__Fenv1\n", encoding="utf-8")
            original, renamed = "_Fenv1", "neverc_cpp__Fenv1"
            host = [(original, "R", original)]
            unrelated_host = [("host_only", "T", "host_only")]
            for host_format in ("nm", "coff-index"):
                with self.subTest(host_format=host_format):
                    for kind in ("T", "R", "W", "U"):
                        with self.subTest(original_kind=kind):
                            with self.assertRaisesRegex(ValueError, "_Fenv1"):
                                # An unrelated host keeps the prefix check from
                                # passing merely because of an intersection.
                                self.audit_inventory([(original, kind, original)],
                                                     unrelated_host, host_format,
                                                     prefix_header=header)
                    for kind in ("T", "R"):
                        with self.subTest(renamed_kind=kind):
                            self.audit_inventory(
                                [(renamed, kind, renamed), (renamed, "U", renamed)],
                                host, host_format, prefix_header=header)
                    with self.assertRaisesRegex(ValueError, "unresolved private dependency"):
                        self.audit_inventory([(renamed, "U", renamed)], host, host_format,
                                             prefix_header=header)

    def test_pointer_bounds_patch_runs_real_script_with_checked_source_states(self):
        # This synthetic source tree tests the transformation script's input
        # contract. It does not model or compile the LLVM implementation.
        original_record = ("struct PointerBounds {\n"
                           "  TrackingVH<Value> Start;\n"
                           "  TrackingVH<Value> End;\n"
                           "  Value *StrideToCheck;\n"
                           "};")
        renamed_record = ("struct neverc_cpp_PointerBounds {\n"
                          "  TrackingVH<Value> Start;\n"
                          "  TrackingVH<Value> End;\n"
                          "  Value *StrideToCheck;\n"
                          "};\n"
                          "using PointerBounds = neverc_cpp_PointerBounds;")
        # Retain the six type uses from the pinned LoopUtils.cpp declarations
        # and lambda; the omitted first function body is irrelevant to rewriting.
        uses = """static PointerBounds expandBounds(const RuntimeCheckingPtrGroup *CG,
                                  Loop *TheLoop, Instruction *Loc,
                                  SCEVExpander &Exp, bool HoistRuntimeChecks);
static SmallVector<std::pair<PointerBounds, PointerBounds>, 4>
expandBounds(const SmallVectorImpl<RuntimePointerCheck> &PointerChecks, Loop *L,
             Instruction *Loc, SCEVExpander &Exp, bool HoistRuntimeChecks) {
  SmallVector<std::pair<PointerBounds, PointerBounds>, 4> ChecksWithBounds;
  transform(PointerChecks, std::back_inserter(ChecksWithBounds),
            [&](const RuntimePointerCheck &Check) {
              PointerBounds First = expandBounds(Check.first, L, Loc, Exp,
                                                 HoistRuntimeChecks),
                            Second = expandBounds(Check.second, L, Loc, Exp,
                                                  HoistRuntimeChecks);
              return std::make_pair(First, Second);
            });
  return ChecksWithBounds;
}
"""
        original_loop = original_record + "\n\n" + uses
        expected_loop = renamed_record + "\n\n" + uses
        notice_names = (
            "MD5.cpp", "xxhash.cpp", "UnicodeNameToCodepointGenerated.cpp",
            "ConvertUTF.cpp", "regex2.h", "regutils.h", "regex_impl.h",
            "regcomp.c", "regexec.c", "regerror.c", "regfree.c",
        )
        intrinsic = ("constexpr bool isVPIntrinsic(int id) { return id != 0; }\n"
                     "bool fixture_query(int id) {\n"
                     "  if (::isVPIntrinsic(id)) return true;\n"
                     "  return ::isVPIntrinsic(id);\n"
                     "}\n")
        expected_intrinsic = (
            "constexpr bool neverc_cpp_isVPIntrinsic(int id) { return id != 0; }\n"
            "bool fixture_query(int id) {\n"
            "  if (::neverc_cpp_isVPIntrinsic(id)) return true;\n"
            "  return ::neverc_cpp_isVPIntrinsic(id);\n"
            "}\n")
        debugify = ("#ifndef LLVM_TRANSFORMS_UTILS_DEBUGIFY_H\n"
                    "#define LLVM_TRANSFORMS_UTILS_DEBUGIFY_H\n"
                    "struct DebugInfoPerPass {};\n#endif\n")
        expected_debugify = (
            "#ifndef LLVM_TRANSFORMS_UTILS_DEBUGIFY_H\n"
            "#define LLVM_TRANSFORMS_UTILS_DEBUGIFY_H\n"
            "// Private NeverC frontend ABI: this upstream type is global.\n"
            "#define DebugInfoPerPass neverc_cpp_DebugInfoPerPass\n"
            "struct DebugInfoPerPass {};\n#endif\n")
        # Independent fixed input/output expressions, not imported from the
        # production script. The third spelling models an incomplete rewrite.
        math_calls = {
            "llvm/lib/Support/Signals.cpp": (
                ("std::log10(Depth)",
                 "std::log10(static_cast<double>(Depth))",
                 "std::log10(static_cast<double>(Depth)"),),
            "llvm/lib/Support/APFixedPoint.cpp": (
                ("std::pow(2, Sema.getLsbWeight())",
                 "std::pow(2.0, static_cast<double>(Sema.getLsbWeight()))",
                 "std::pow(2.0, Sema.getLsbWeight())"),
                ("std::pow(2, -DstFXSema.getLsbWeight())",
                 "std::pow(2.0, static_cast<double>(-DstFXSema.getLsbWeight()))",
                 "std::pow(2.0, -DstFXSema.getLsbWeight())"),
                ("std::pow(2, DstFXSema.getLsbWeight())",
                 "std::pow(2.0, static_cast<double>(DstFXSema.getLsbWeight()))",
                 "std::pow(2.0, DstFXSema.getLsbWeight())")),
            "llvm/lib/Analysis/ConstantFolding.cpp": (
                ("std::pow(Op1V.convertToFloat(), Exp)",
                 "std::pow(static_cast<double>(Op1V.convertToFloat()), "
                 "static_cast<double>(Exp))",
                 "std::pow(static_cast<double>(Op1V.convertToFloat()), Exp)"),
                ("std::pow(Op1V.convertToDouble(), Exp)",
                 "std::pow(Op1V.convertToDouble(), static_cast<double>(Exp))",
                 "std::pow(Op1V.convertToDouble(), static_cast<double>(Exp)")),
        }
        math_sources = {name: "".join(before + ";\n" for before, _, _ in calls)
                        for name, calls in math_calls.items()}
        expected_math = {name: "".join(after + ";\n" for _, after, _ in calls)
                         for name, calls in math_calls.items()}
        # Verbatim relevant spans from LLVM 20.1.8 MSVCPaths.cpp, not generated
        # from the patcher's constants. Omitted unrelated functions are outside
        # this source-state contract; this fixture is never a compiler proof.
        setup_preamble = """#ifdef _MSC_VER
// Don't support SetupApi on MinGW.
#define USE_MSVC_SETUP_API

// Make sure this comes before MSVCSetupApi.h
#include <comdef.h>

#include "llvm/Support/COM.h"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#endif
#include "llvm/WindowsDriver/MSVCSetupApi.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration, __uuidof(ISetupConfiguration));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration2, __uuidof(ISetupConfiguration2));
_COM_SMARTPTR_TYPEDEF(ISetupHelper, __uuidof(ISetupHelper));
_COM_SMARTPTR_TYPEDEF(IEnumSetupInstances, __uuidof(IEnumSetupInstances));
_COM_SMARTPTR_TYPEDEF(ISetupInstance, __uuidof(ISetupInstance));
_COM_SMARTPTR_TYPEDEF(ISetupInstance2, __uuidof(ISetupInstance2));
#endif
"""
        setup_calls = """  ISetupInstancePtr NewestInstance;
  std::optional<uint64_t> NewestVersionNum;
  do {
    bstr_t VersionString;
    uint64_t VersionNum;
    HR = Instance->GetInstallationVersion(VersionString.GetAddress());
    if (FAILED(HR))
      continue;
    HR = ISetupHelperPtr(Query)->ParseVersion(VersionString, &VersionNum);
    if (FAILED(HR))
      continue;
    if (!NewestVersionNum || (VersionNum > NewestVersionNum)) {
      NewestInstance = Instance;
      NewestVersionNum = VersionNum;
    }
  } while ((HR = EnumInstances->Next(1, &Instance, nullptr)) == S_OK);

  if (!NewestInstance)
    return false;

  bstr_t VCPathWide;
  HR = NewestInstance->ResolvePath(L"VC", VCPathWide.GetAddress());
  if (FAILED(HR))
    return false;

  std::string VCRootPath;
  convertWideToUTF8(std::wstring(VCPathWide), VCRootPath);
"""
        original_setup = setup_preamble + "\n" + setup_calls
        # Independent expected owner, not obtained by importing or extracting
        # SETUP_BSTR_OWNER from the script being tested.
        expected_owner = """namespace llvm {
class NeverCSetupBstr final {
  BSTR Value = nullptr;

public:
  NeverCSetupBstr() noexcept = default;
  NeverCSetupBstr(const NeverCSetupBstr &) = delete;
  NeverCSetupBstr &operator=(const NeverCSetupBstr &) = delete;
  NeverCSetupBstr(NeverCSetupBstr &&) = delete;
  NeverCSetupBstr &operator=(NeverCSetupBstr &&) = delete;
  ~NeverCSetupBstr() noexcept { reset(); }

  void reset() noexcept {
    ::SysFreeString(Value);
    Value = nullptr;
  }
  BSTR *out() noexcept {
    reset();
    return &Value;
  }
  BSTR get() const noexcept { return Value; }
};
} // namespace llvm
"""
        expected_setup_calls = """  ISetupInstancePtr NewestInstance;
  std::optional<uint64_t> NewestVersionNum;
  do {
    NeverCSetupBstr VersionString;
    uint64_t VersionNum;
    HR = Instance->GetInstallationVersion(VersionString.out());
    if (FAILED(HR) || !VersionString.get())
      continue;
    HR = ISetupHelperPtr(Query)->ParseVersion(VersionString.get(), &VersionNum);
    if (FAILED(HR))
      continue;
    if (!NewestVersionNum || (VersionNum > NewestVersionNum)) {
      NewestInstance = Instance;
      NewestVersionNum = VersionNum;
    }
  } while ((HR = EnumInstances->Next(1, &Instance, nullptr)) == S_OK);

  if (!NewestInstance)
    return false;

  NeverCSetupBstr VCPathWide;
  HR = NewestInstance->ResolvePath(L"VC", VCPathWide.out());
  if (FAILED(HR) || !VCPathWide.get())
    return false;

  std::string VCRootPath;
  convertWideToUTF8(std::wstring(VCPathWide.get()), VCRootPath);
"""
        expected_setup_preamble = (setup_preamble[:-len("#endif\n")] + "\n" +
                                   expected_owner + "#endif\n")
        expected_setup = expected_setup_preamble + "\n" + expected_setup_calls
        files = {
            "llvm/lib/WindowsDriver/MSVCPaths.cpp": original_setup,
            "llvm/lib/IR/IntrinsicInst.cpp": intrinsic,
            "llvm/include/llvm/Transforms/Utils/Debugify.h": debugify,
            "llvm/lib/Transforms/Utils/LoopUtils.cpp": original_loop,
            "llvm/include/llvm-c/Core.h": (
                "#define LLVM_FOR_EACH_VALUE_SUBCLASS(macro) \\\n"
                "  macro(Argument)\n\n" +
                "".join(f"void LLVMFixture{index:04d}(void);\n"
                        for index in range(900))),
            "llvm/lib/Support/BLAKE3/llvm_blake3_prefix.h": (
                "#define blake3_compress_in_place llvm_blake3_compress_in_place\n"),
        }
        files.update(math_sources)
        for name in notice_names:
            files["llvm/lib/Support/" + name] = (
                "// Controlled transformation fixture notice.\nint fixture_value;\n")

        with tempfile.TemporaryDirectory(prefix="neverc-isolate-source-") as temporary:
            root = Path(temporary)
            source = root / "source"
            for name, contents in files.items():
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents, encoding="utf-8")
            output = root / "generated" / "PrivatePrefix.h"
            command = [sys.executable, "-I", "-B",
                       str(Path(__file__).resolve().with_name("IsolateSymbols.py")),
                       "--source", str(source), "--output", str(output)]

            def run_script(success, expected_error=None):
                try:
                    result = subprocess.run(
                        command, cwd=root, capture_output=True, text=True,
                        encoding="utf-8", errors="replace", timeout=30, check=False)
                except subprocess.TimeoutExpired as error:
                    self.fail(f"IsolateSymbols timed out: {command!r}\n"
                              f"stdout: {error.stdout!r}\nstderr: {error.stderr!r}")
                diagnostic = (f"{command!r}\nexit: {result.returncode}\n"
                              f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
                if success:
                    self.assertEqual(result.returncode, 0, diagnostic)
                    self.assertIn("Isolated llvm namespace and", result.stdout, diagnostic)
                else:
                    self.assertNotEqual(result.returncode, 0, diagnostic)
                    if expected_error is None:
                        self.assertIn("Unexpected pinned LLVM PointerBounds", result.stderr,
                                      diagnostic)
                    else:
                        self.assertEqual(result.stderr, expected_error + "\n", diagnostic)

            def snapshot_all_files():
                return {path.relative_to(root): path.read_bytes()
                        for path in root.rglob("*") if path.is_file()}

            # The new source check must run before *any* existing rewrite or
            # generated-file creation, including when the pinned file is absent.
            setup_path = source / "llvm/lib/WindowsDriver/MSVCPaths.cpp"
            setup_path.unlink()
            untouched = snapshot_all_files()
            run_script(False, "Unexpected pinned LLVM Setup BSTR source in " + str(setup_path))
            self.assertEqual(snapshot_all_files(), untouched)
            self.assertFalse(output.exists())
            setup_path.write_text(original_setup, encoding="utf-8")

            run_script(True)
            rewritten_setup = setup_path.read_text(encoding="utf-8")
            self.assertEqual(rewritten_setup, expected_setup)
            self.assertEqual(rewritten_setup.count(expected_owner), 1)
            self.assertNotIn("GetAddress", rewritten_setup)
            self.assertNotIn("bstr_t", rewritten_setup)
            loop = source / "llvm/lib/Transforms/Utils/LoopUtils.cpp"
            self.assertEqual(loop.read_text(encoding="utf-8"), expected_loop)
            self.assertEqual((source / "llvm/lib/IR/IntrinsicInst.cpp").read_text(
                encoding="utf-8"), expected_intrinsic)
            self.assertEqual((source / "llvm/include/llvm/Transforms/Utils/Debugify.h").read_text(
                encoding="utf-8"), expected_debugify)
            math_paths = {name: source / name for name in math_calls}
            for name, path in math_paths.items():
                self.assertEqual(path.read_text(encoding="utf-8"), expected_math[name], name)
            prefix = output.read_text(encoding="utf-8")
            for name in ("llvm", "LLVMFixture0000", "LLVMFixture0899", "LLVMIsAArgument",
                         "llvm_blake3_compress_in_place"):
                self.assertIn(f"#define {name} neverc_cpp_{name}\n", prefix)
            self.assertNotIn("#define PointerBounds", prefix)
            self.assertNotRegex(prefix, r"(?m)^\s*#\s*(?:define|undef)\s+_?bstr_t\b")
            notices = output.parent / "NeverCCppThirdPartyNotices.txt"
            expected_notices = "Additional notices from the pinned LLVM 20.1.8 sources.\n"
            for name in notice_names:
                expected_notices += ("\n\n===== llvm/lib/Support/" + name + " =====\n\n"
                                     "// Controlled transformation fixture notice.\n")
            self.assertEqual(notices.read_text(encoding="utf-8"), expected_notices)
            stable = {path: path.read_bytes() for path in (
                loop, output, notices, source / "llvm/lib/IR/IntrinsicInst.cpp",
                source / "llvm/include/llvm/Transforms/Utils/Debugify.h",
                setup_path, *math_paths.values())}
            run_script(True)
            for path, contents in stable.items():
                self.assertEqual(path.read_bytes(), contents, str(path))

            anchor = ("_COM_SMARTPTR_TYPEDEF(ISetupInstance2, "
                      "__uuidof(ISetupInstance2));\n")
            original_version = "    bstr_t VersionString;\n"
            private_version = "    NeverCSetupBstr VersionString;\n"
            setup_states = {
                "missing anchor": original_setup.replace(anchor, "", 1),
                "duplicate anchor": original_setup.replace(anchor, anchor + anchor, 1),
                "missing version declaration": original_setup.replace(original_version, "", 1),
                "duplicate original declaration": original_setup.replace(
                    original_version, original_version * 2, 1),
                "duplicate rewritten declaration": rewritten_setup.replace(
                    private_version, private_version * 2, 1),
                "mixed declarations": original_setup.replace(
                    original_version, original_version + private_version, 1),
                "one declaration rewritten": original_setup.replace(
                    original_version, private_version, 1),
                "one declaration original": rewritten_setup.replace(
                    private_version, original_version, 1),
                "only out call rewritten": original_setup.replace(
                    "VersionString.GetAddress()", "VersionString.out()", 1),
                "old out call remains": rewritten_setup.replace(
                    "VCPathWide.out()", "VCPathWide.GetAddress()", 1),
                "changed version call": original_setup.replace(
                    "GetInstallationVersion(VersionString.GetAddress())",
                    "GetInstallationVersion(nullptr)", 1),
                "changed path call": original_setup.replace(
                    'ResolvePath(L"VC", VCPathWide.GetAddress())',
                    'ResolvePath(L"Other", VCPathWide.GetAddress())', 1),
                "missing parsed get": rewritten_setup.replace(
                    "ParseVersion(VersionString.get(), &VersionNum)",
                    "ParseVersion(VersionString, &VersionNum)", 1),
                "missing path get": rewritten_setup.replace(
                    "std::wstring(VCPathWide.get())", "std::wstring(VCPathWide)", 1),
                "missing version null guard": rewritten_setup.replace(
                    "if (FAILED(HR) || !VersionString.get())", "if (FAILED(HR))", 1),
                "missing path null guard": rewritten_setup.replace(
                    "if (FAILED(HR) || !VCPathWide.get())", "if (FAILED(HR))", 1),
                "extra original declaration": original_setup + "bstr_t Extra;\n",
                "extra private declaration": rewritten_setup + "NeverCSetupBstr Extra;\n",
                "unexpected wrapper macro": original_setup + "#define _bstr_t NeverCSetupBstr\n",
                "owner missing": rewritten_setup.replace(expected_owner, "", 1),
                "duplicate owner": rewritten_setup.replace(
                    expected_owner, expected_owner * 2, 1),
                "owner only rewritten": expected_setup_preamble + "\n" + setup_calls,
                "owner outside guard": setup_preamble + "\n" + expected_owner +
                    expected_setup_calls,
                "owner in global namespace": rewritten_setup.replace(
                    expected_owner, expected_owner.replace("namespace llvm {\n", "", 1), 1),
                "changed owner destructor": rewritten_setup.replace(
                    "~NeverCSetupBstr() noexcept { reset(); }",
                    "~NeverCSetupBstr() noexcept {}", 1),
                "owner without copy deletion": rewritten_setup.replace(
                    "  NeverCSetupBstr(const NeverCSetupBstr &) = delete;\n", "", 1),
                "owner without out reset": rewritten_setup.replace(
                    "  BSTR *out() noexcept {\n    reset();\n",
                    "  BSTR *out() noexcept {\n", 1),
            }
            for state, invalid_source in setup_states.items():
                with self.subTest(setup_bstr_state=state):
                    # Every other file is an original, valid input. If Setup
                    # validation ran later, the intrinsic/math patches would
                    # modify these bytes before the failure was reported.
                    for name, contents in files.items():
                        (source / name).write_text(contents, encoding="utf-8")
                    setup_path.write_text(invalid_source, encoding="utf-8")
                    untouched = snapshot_all_files()
                    run_script(False, "Unexpected pinned LLVM Setup BSTR source in " +
                               str(setup_path))
                    self.assertEqual(snapshot_all_files(), untouched, state)
            for name, contents in files.items():
                (source / name).write_text(contents, encoding="utf-8")
            run_script(True)
            for path, contents in stable.items():
                self.assertEqual(path.read_bytes(), contents, str(path))

            missing_use = original_loop.replace("static PointerBounds expandBounds(",
                                                "static int expandBounds(", 1)
            invalid = {
                "missing declaration": uses,
                "duplicate original": original_record + "\n" + original_loop,
                "duplicate rewritten": renamed_record + "\n" + expected_loop,
                "mixed declarations": original_record + "\n" + expected_loop,
                "partial rename": original_loop.replace(
                    "struct PointerBounds", "struct neverc_cpp_PointerBounds", 1),
                "changed member": original_loop.replace(
                    "Value *StrideToCheck;", "Value *Changed;", 1),
                "extra use": original_loop + "PointerBounds Unexpected;\n",
                "missing use": missing_use,
                # Preserve the count of six uses so these exercise the specific
                # declaration/alias/macro checks rather than just the count.
                "unknown declaration": missing_use + "struct [[nodiscard]] PointerBounds;\n",
                "extra alias": missing_use + "using PointerBounds = Unrelated;\n",
                "extra macro": missing_use + "#define PointerBounds Unrelated\n",
                "unexpected private name": original_loop + "neverc_cpp_PointerBounds *Unexpected;\n",
            }
            for name, contents in invalid.items():
                with self.subTest(state=name):
                    before = contents.encode("utf-8")
                    loop.write_bytes(before)
                    run_script(False)
                    self.assertEqual(loop.read_bytes(), before)

            # The preceding negatives leave an invalid LoopUtils.cpp. Restore
            # it so no PointerBounds error can hide a math validation failure.
            loop.write_text(expected_loop, encoding="utf-8")
            # Interrupted runs may leave whole files rewritten independently.
            # Original/all-rewritten states were checked above; exercise the
            # other six combinations of the three files as valid inputs.
            for rewritten_mask in range(1, 7):
                with self.subTest(math_rewritten_files=rewritten_mask):
                    for index, (name, path) in enumerate(math_paths.items()):
                        contents = (expected_math[name] if rewritten_mask & (1 << index)
                                    else math_sources[name])
                        path.write_text(contents, encoding="utf-8")
                    run_script(True)
                    for path, contents in stable.items():
                        self.assertEqual(path.read_bytes(), contents, str(path))

            for name, calls in math_calls.items():
                for before, after, partial in calls:
                    original, rewritten = math_sources[name], expected_math[name]
                    invalid_math = {
                        "missing call": original.replace(before, "0.0", 1),
                        "duplicate original": original + before + ";\n",
                        "duplicate rewritten": rewritten + after + ";\n",
                        "same call original and rewritten": original + after + ";\n",
                        "partial rewrite": original.replace(before, partial, 1),
                    }
                    if len(calls) > 1:
                        invalid_math.update({
                            "one call rewritten": original.replace(before, after, 1),
                            "one call original": rewritten.replace(after, before, 1),
                        })
                    for state, invalid_source in invalid_math.items():
                        with self.subTest(math_file=name, call=before, state=state):
                            for other_name, path in math_paths.items():
                                contents = (invalid_source if other_name == name
                                            else math_sources[other_name])
                                path.write_bytes(contents.encode("utf-8"))
                            unchanged = {path: path.read_bytes() for path in stable}
                            run_script(False, "Unexpected pinned LLVM math calls in " +
                                       str(math_paths[name]))
                            # Even when the last file is invalid, the earlier
                            # valid original math files must remain unmodified.
                            for path, contents in unchanged.items():
                                self.assertEqual(path.read_bytes(), contents, str(path))

    def test_global_pointer_bounds_record_identity_has_explicit_type_context(self):
        for record in ("PointerBounds", "neverc_cpp_PointerBounds"):
            cases = [
                ("?controlled@@", f"public: struct {record} & __cdecl "
                 f"{record}::operator=(struct {record} &&)"),
                ("?controlled@@", f"void __cdecl consume(struct {record} const &)"),
                ("?controlled@@", f"class {record} `RTTI Type Descriptor'"),
                ("_Zcontrolled", f"{record}::operator=({record}&&)"),
                ("_Zcontrolled", f"consume({record} const&)"),
                ("_Zcontrolled", f"consume({record}* const&)"),
                ("_Zcontrolled", f"consume({record} const* volatile* const&&)"),
                ("_Zcontrolled", f"typeinfo for {record}"),
                ("_Zcontrolled", f"Host::operator {record}() const"),
            ]
            for symbol, declaration in cases:
                with self.subTest(record=record, declaration=declaration):
                    self.assertTrue(AuditArchive.global_cpp_record_entity(
                        symbol, declaration, record))

    def test_global_record_identity_does_not_use_identifier_substrings(self):
        for record in ("PointerBounds", "neverc_cpp_PointerBounds"):
            for symbol in ("?controlled@@", "_Zcontrolled"):
                for declaration in (
                    f"Host::{record}::f()", f"struct Host::{record}",
                    f"Host::${record}::f()", f"More${record}::f()",
                    f"Ω{record}::f()", f"A\u0301{record}::f()", f"{record}Extra::f()",
                    f"void f(int {record})", f"{record}()", record,
                    f"void f<{record}>()", f"consume({record}&&&)",
                    f"consume({record}* constSuffix&)",
                ):
                    with self.subTest(symbol=symbol, declaration=declaration):
                        self.assertFalse(AuditArchive.global_cpp_record_entity(
                            symbol, declaration, record))

    def test_original_global_record_definitions_and_references_are_rejected(self):
        # Actual MSVC ARM64 984608 spelling from LoopUtils.cpp.obj. Its global
        # record contains version-specific LLVM TrackingVH<Value> members.
        msvc = "??4PointerBounds@@QEAAAEAU0@$$QEAU0@@Z"
        declaration = ("public: struct PointerBounds & __cdecl "
                       "PointerBounds::operator=(struct PointerBounds &&)")
        for raw, decoded in (
            (msvc, declaration),
            ("_ZN13PointerBoundsaSEOS_", "PointerBounds::operator=(PointerBounds&&)"),
        ):
            for kind in ("T", "W", "U"):
                with self.subTest(raw=raw, kind=kind):
                    with self.assertRaisesRegex(ValueError, "PointerBounds"):
                        self.audit_inventory([(raw, kind, decoded)])

    def test_private_global_record_methods_require_a_closed_definition(self):
        original = "??4PointerBounds@@QEAAAEAU0@$$QEAU0@@Z"
        renamed = original.replace("PointerBounds", "neverc_cpp_PointerBounds")
        original_decoded = ("public: struct PointerBounds & __cdecl "
                            "PointerBounds::operator=(struct PointerBounds &&)")
        renamed_decoded = original_decoded.replace(
            "PointerBounds", "neverc_cpp_PointerBounds")
        host = [(original, "T", original_decoded)]
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                self.audit_inventory(
                    [(renamed, "T", renamed_decoded), (renamed, "U", renamed_decoded)],
                    host, host_format)
                with self.assertRaisesRegex(ValueError, "unresolved private dependency"):
                    self.audit_inventory([(renamed, "U", renamed_decoded)], host,
                                         host_format)
                # Isolating the global record must not hide an unresolved
                # method of the private LLVM value-handle implementation.
                handle = "?assign@ValueHandleBase@neverc_cpp_llvm@@QEAAXXZ"
                with self.assertRaisesRegex(ValueError, "unresolved private dependency"):
                    self.audit_inventory(
                        [(renamed, "T", renamed_decoded),
                         (handle, "U", "void __cdecl neverc_cpp_llvm::ValueHandleBase::assign(void)")],
                        host, host_format)

    def test_record_reference_closure_keeps_nested_host_types_separate(self):
        nested = "?f@neverc_cpp_PointerBounds@Host@@QEAAXXZ"
        self.audit_inventory(
            [(nested, "U", "void __cdecl Host::neverc_cpp_PointerBounds::f(void)")])
        raw = "_Z7consumeRKP24neverc_cpp_PointerBounds"
        with self.assertRaisesRegex(ValueError, "unresolved private dependency"):
            self.audit_inventory(
                [(raw, "U", "consume(neverc_cpp_PointerBounds* const&)")])

    def test_complete_failure_list_includes_count_and_private_symbol_identity(self):
        names = [f"host_collision_{index:03d}" for index in range(105)]
        names[0] = "?collision@private_detail@@YAXXZ"
        decoded = {name: name for name in names}
        decoded[names[0]] = "void __cdecl private_detail::collision(void)"
        private = [(name, "T", decoded[name]) for name in names]
        private.append((names[0], "U", decoded[names[0]]))
        host = [(name, "T", decoded[name]) for name in names]
        with mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
            with self.assertRaises(ValueError) as failure:
                self.audit_inventory(private, host)
        message = str(failure.exception)
        self.assertIn("Unisolated symbols in builtin C++ frontend (total=105):", message)
        for name in names:
            self.assertIn("private/host symbol intersection: " + name + ";", message)
        self.assertIn("private_demangled='void __cdecl private_detail::collision(void)'; "
                      "private_definition=True; private_reference=True", message)
        evidence = output.getvalue()
        self.assertIn("ABI audit provenance: private nm archive='private.lib' "
                      "member_header='private.cpp.obj:' symbol='host_collision_104' "
                      "kind='T' raw='host_collision_104 T 0 0'", evidence)
        self.assertIn("ABI audit provenance: host nm archive='host.lib' "
                      "member_header='host.cpp.obj:' symbol='host_collision_104' "
                      "kind='T' raw='host_collision_104 T 0 0'", evidence)

    def test_failure_provenance_assigns_each_host_member_to_its_actual_archive(self):
        args = argparse.Namespace(nm="controlled-nm", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"), host_format="nm", host_nm=None)
        hosts = [Path("first.a"), Path("second.a"), Path("unrelated.a")]
        inventories = {
            args.archive: [("neverc_cpp_frontend_main", "T"),
                           ("shared_left", "U"), ("shared_right", "T")],
            hosts[0]: [("shared_left", "T")],
            hosts[1]: [("shared_right", "D")],
            hosts[2]: [("unrelated_symbol", "T")],
        }

        def inventory(_reader, paths, *options):
            rows = [(path, name, kind) for path in paths for name, kind in inventories[path]]
            if "--defined-only" in options:
                rows = [row for row in rows if not AuditArchive.is_undefined(row[2])]
            if "--format=posix" in options:
                # Equal member basenames in different archives must not be
                # attributed from the batch's filename-less header alone.
                return "".join(f"same-member.obj:\n{name} {kind} 1 2\n"
                               for _, name, kind in rows)
            return "".join(name + "\n" for _, name, _ in rows)

        with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory) as reader, \
                mock.patch.object(AuditArchive, "host_archives", return_value=hosts), \
                mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
            with self.assertRaisesRegex(ValueError, "total=2"):
                AuditArchive.audit(args)
        evidence = output.getvalue()
        self.assertIn("ABI audit provenance: host nm archive='first.a' "
                      "member_header='same-member.obj:' symbol='shared_left' "
                      "kind='T' raw='shared_left T 1 2'", evidence)
        self.assertIn("ABI audit provenance: host nm archive='second.a' "
                      "member_header='same-member.obj:' symbol='shared_right' "
                      "kind='D' raw='shared_right D 1 2'", evidence)
        self.assertNotIn("archive='unrelated.a'", evidence)
        self.assertNotIn("archive='first.a' member_header='same-member.obj:' "
                         "symbol='shared_right'", evidence)
        for archive in hosts[:2]:
            reader.assert_any_call("controlled-nm", [archive], "--format=posix")

    def test_coff_collision_provenance_never_invents_object_kind_or_member(self):
        with mock.patch.object(sys, "stdout", new_callable=io.StringIO) as output:
            with self.assertRaises(ValueError) as failure:
                self.audit_inventory([("HandleAbort", "U", "HandleAbort")],
                                     [("HandleAbort", "T", "HandleAbort")], "coff-index")
        self.assertIn("private_demangled='HandleAbort'; private_definition=False; "
                      "private_reference=True", str(failure.exception))
        evidence = output.getvalue()
        self.assertIn("ABI audit provenance: private nm archive='private.lib' "
                      "member_header='private.cpp.obj:' symbol='HandleAbort' "
                      "kind='U' raw='HandleAbort U 0 0'", evidence)
        self.assertIn("ABI audit provenance: host coff-index archive='host.lib' "
                      "symbol='HandleAbort' index-only; object kind, member and "
                      "raw nm row unavailable", evidence)
        self.assertNotIn("ABI audit provenance: host nm", evidence)
        for line in evidence.splitlines():
            if "ABI audit provenance: host coff-index" in line:
                self.assertNotIn("kind=", line)
                self.assertNotIn("member_header=", line)
                self.assertNotIn("raw=", line)

    def test_provenance_read_failure_preserves_the_original_complete_diagnostic(self):
        args = argparse.Namespace(nm="controlled-nm", nm_file=None,
                                  archive=Path("private.a"), prefix_header=None,
                                  host_lib_dir=Path("host-libs"), host_format="nm", host_nm=None)
        for change in ("read-error", "type-change"):
            with self.subTest(change=change):
                private_reads = 0

                def inventory(_reader, paths, *options):
                    nonlocal private_reads
                    if paths == [args.archive]:
                        if "--format=posix" in options:
                            private_reads += 1
                            if private_reads > 1 and change == "read-error":
                                raise OSError("evidence reader unavailable")
                            kind = "T" if private_reads > 1 else "U"
                            return ("private.obj:\nneverc_cpp_frontend_main T 0 0\n"
                                    f"blocked_symbol {kind} 0 0\n")
                        return "neverc_cpp_frontend_main\nblocked_symbol\n"
                    if "--format=posix" in options:
                        return "host.obj:\nblocked_symbol T 0 0\n"
                    return "blocked_symbol\n"

                with mock.patch.object(AuditArchive, "nm_output", side_effect=inventory), \
                        mock.patch.object(AuditArchive, "host_archives",
                                          return_value=[Path("host.a")]), \
                        mock.patch.object(sys, "stdout", new_callable=io.StringIO):
                    with self.assertRaises(ValueError) as failure:
                        AuditArchive.audit(args)
                message = str(failure.exception)
                self.assertIn("Unisolated symbols in builtin C++ frontend (total=1):", message)
                self.assertIn("private/host symbol intersection: blocked_symbol; "
                              "private_demangled='blocked_symbol'; private_definition=False; "
                              "private_reference=True", message)
                self.assertIn("ABI audit provenance collection failed:", message)
                self.assertIn("evidence reader unavailable" if change == "read-error" else
                              "Private symbol inventory changed", message)

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

    def test_msvc_empty_literal_actual_pair_is_accepted(self):
        raw, _, decoded = self.msvc_empty_literal
        self.assertTrue(AuditArchive.microsoft_string_literal(raw, decoded))
        self.assertTrue(AuditArchive.standard_shared_symbol(raw, decoded))
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                # The private R is observed; the matching host R is a controlled
                # model. COFF index mode makes no claim about the host kind.
                self.audit_inventory([self.msvc_empty_literal],
                                     [self.msvc_empty_literal], host_format)

    def test_msvc_empty_literal_requires_the_exact_zero_payload_raw_name(self):
        raw, _, decoded = self.msvc_empty_literal
        # Keep all mutations payload-free: an ordinary literal with a legal
        # encoded payload must still use the existing byte+ grammar.
        cases = (
            ("different-crc", "??_C@_00CNPNBAHD@@", decoded),
            ("different-length", "??_C@_01CNPNBAHC@@", decoded),
            ("wide-kind", "??_C@_10CNPNBAHC@@", decoded),
            ("raw-prefix", "prefix" + raw, decoded),
            ("raw-suffix", raw + "suffix", decoded),
            ("missing-terminator", raw[:-1], decoded),
            ("extra-terminator", raw + "@", decoded),
            ("extra-crc-terminator", "??_C@_00CNPNBAHC@@@@", decoded),
            ("host-raw-with-literal-decoding", "?call@Host@@YAXXZ", decoded),
            ("host-declaration-containing-literal", "?call@Host@@YAXXZ",
             'void __cdecl Host::call<""...>(void)'))
        for case, mutated_raw, mutated_decoded in cases:
            with self.subTest(case=case):
                self.assertFalse(AuditArchive.microsoft_string_literal(
                    mutated_raw, mutated_decoded))
                self.assertFalse(AuditArchive.standard_shared_symbol(
                    mutated_raw, mutated_decoded))
                for host_format in ("nm", "coff-index"):
                    with self.subTest(host_format=host_format):
                        row = (mutated_raw, "R", mutated_decoded)
                        with self.assertRaisesRegex(
                                ValueError, "private/host symbol intersection") as failure:
                            self.audit_inventory([row], [row], host_format)
                        self.assertIn("intersection: " + mutated_raw +
                                      "; private_demangled=", str(failure.exception))

    def test_msvc_empty_literal_requires_the_exact_decoding(self):
        raw, _, _ = self.msvc_empty_literal
        # Empty output, newline and NUL cannot represent one nm output row;
        # exercise those directly instead of turning a framing error into
        # supposed evidence that the intersection policy rejected the token.
        cases = (
            ("empty-output", "", False),
            ("missing-ellipsis", '""', True),
            ("wide-prefix", 'L""...', True),
            ("quoted-space", '" "', True),
            ("quoted-space-with-ellipsis", '" "...', True),
            ("escaped-nul", '"\\0"...', True),
            ("escaped-hex-nul", '"\\x00"...', True),
            ("actual-nul", '"\x00"...', False),
            ("newline", '"\n"...', False),
            ("carriage-return", '"\r"...', False),
            ("tab", '"\t"...', True),
            ("control-127", '"\x7f"...', True),
            ("leading-space", ' ""...', True),
            ("trailing-space", '""... ', True),
            ("suffix", '""...suffix', True))
        for case, decoded, line_safe in cases:
            with self.subTest(case=case):
                self.assertFalse(AuditArchive.microsoft_string_literal(raw, decoded))
                self.assertFalse(AuditArchive.standard_shared_symbol(raw, decoded))
                if line_safe:
                    for host_format in ("nm", "coff-index"):
                        with self.subTest(host_format=host_format):
                            row = (raw, "R", decoded)
                            with self.assertRaisesRegex(
                                    ValueError, "private/host symbol intersection") as failure:
                                self.audit_inventory([row], [row], host_format)
                            self.assertIn("intersection: " + raw +
                                          "; private_demangled=", str(failure.exception))

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

    def test_msvc_runtime_constants_require_actual_definition_kinds(self):
        raw = "__real@3ff0000000000000"
        self.audit_inventory([(raw, "R", raw)], [(raw, "R", raw)], "coff-index")
        for private in ([(raw, "T", raw)], [(raw, "U", raw)],
                        [(raw, "R", raw), (raw, "T", raw)],
                        [(raw, "R", raw), (raw, "U", raw)]):
            with self.subTest(private=private):
                with self.assertRaisesRegex(ValueError, "private/host symbol intersection"):
                    self.audit_inventory(private, [(raw, "R", raw)], "coff-index")
        with self.assertRaisesRegex(ValueError, "private/host symbol intersection"):
            self.audit_inventory([(raw, "R", raw)], [(raw, "R", raw)], "nm")

    def test_msvc_runtime_observed_definition_inventory(self):
        fixture = json.loads(Path(__file__).with_name(
            "MsvcRuntimeSymbolsFixture.json").read_text(encoding="utf-8"))
        self.assertEqual(len(fixture), 229)
        definitions = [row for row in fixture if row[1] != ["w"]]
        self.assertEqual(len(definitions), 228)
        # Keep all original observations. These exact five R definitions are
        # runtime-literal candidates, but the independent Setup gate must reject
        # them even when the host exports the same names.
        setup_names = {old for old, _ in SETUP_GUID_PAIRS}
        setup = [row for row in definitions if row[0] in setup_names]
        self.assertEqual(len(setup), 5)
        self.assertEqual({name for name, _, _ in setup}, setup_names)
        for name, kinds, decoded in setup:
            self.assertEqual((kinds, decoded), (["R"], name))
        private = [(name, kinds[0], decoded) for name, kinds, decoded in definitions
                   if name not in setup_names]
        self.assertEqual(len(private), 223)
        self.audit_inventory(private, private, "coff-index")
        for name, kinds, decoded in setup:
            with self.subTest(setup_guid=name):
                rows = [(name, kinds[0], decoded)]
                with self.assertRaisesRegex(ValueError, "unisolated Setup GUID symbol: " + name):
                    self.audit_inventory(rows, rows, "coff-index")

    def test_msvc_delete_wrapper_requires_proven_exact_fallback(self):
        wrapper = "?__global_delete@@YAXPEAX_K@Z"
        fallback = "?__empty_global_delete@@YAXPEAX_K@Z"
        declaration = "void __cdecl __global_delete(void *, unsigned __int64)"
        fallback_decl = "void __cdecl __empty_global_delete(void *, unsigned __int64)"
        private = [(wrapper, "w", declaration), (fallback, "T", fallback_decl)]
        host = [(wrapper, "T", declaration), (fallback, "T", fallback_decl)]
        reader = types.ModuleType("CoffWeakAliases")
        reader.read_resolved_aliases = mock.Mock(return_value={wrapper})
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            self.audit_inventory(private, host, "coff-index", "controlled-readobj")
        reader.read_resolved_aliases.assert_called_once_with(
            "controlled-readobj", Path("private.lib"),
            {"neverc_cpp_frontend_main", fallback}, {wrapper},
            expected_fallbacks={wrapper: fallback})

        for evidence in (None, set()):
            reader.read_resolved_aliases = mock.Mock(return_value=evidence)
            with self.subTest(evidence=evidence), \
                    mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
                with self.assertRaisesRegex(ValueError, "private/host symbol intersection"):
                    self.audit_inventory(private, host, "coff-index",
                                         "controlled-readobj" if evidence is not None else None)
        for kind in ("U", "T", "W", "v"):
            with self.subTest(kind=kind), \
                    mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
                reader.read_resolved_aliases = mock.Mock(return_value={wrapper})
                with self.assertRaisesRegex(ValueError, "private/host symbol intersection"):
                    self.audit_inventory([(wrapper, kind, declaration), private[1]],
                                         host, "coff-index", "controlled-readobj")
        reader.read_resolved_aliases = mock.Mock(side_effect=ValueError("wrong exact fallback"))
        with mock.patch.dict(sys.modules, {"CoffWeakAliases": reader}):
            with self.assertRaisesRegex(ValueError, "wrong exact fallback"):
                self.audit_inventory(private, host, "coff-index", "controlled-readobj")

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

    def test_microsoft_global_allocation_ci_references_allow_exact_comma_spacing(self):
        # Exact raw/decoded pairs from a551 MSVC ARM64 diagnostics at lines
        # 20156, 20158, 20159, 20160 and 20191. All private rows were U; host
        # evidence was only the mimalloc.lib COFF definition index. The T rows
        # below are a synthetic definition model, not observed host object kinds.
        declarations = (
            ("??2@YAPEAX_KAEBUnothrow_t@std@@@Z",
             "void * __cdecl operator new(unsigned __int64, "
             "struct std::nothrow_t const &)"),
            ("??2@YAPEAX_KW4align_val_t@std@@AEBUnothrow_t@1@@Z",
             "void * __cdecl operator new(unsigned __int64, enum std::align_val_t, "
             "struct std::nothrow_t const &)"),
            ("??3@YAXPEAX_K@Z",
             "void __cdecl operator delete(void *, unsigned __int64)"),
            ("??3@YAXPEAX_KW4align_val_t@std@@@Z",
             "void __cdecl operator delete(void *, unsigned __int64, "
             "enum std::align_val_t)"),
            ("??_V@YAXPEAX_K@Z",
             "void __cdecl operator delete[](void *, unsigned __int64)"))
        for raw, spaced in declarations:
            for decoded in (spaced, spaced.replace(", ", ",")):
                with self.subTest(raw=raw, decoded=decoded):
                    self.assertTrue(AuditArchive.standard_shared_symbol(raw, decoded))
                    for host_format in ("nm", "coff-index"):
                        with self.subTest(host_format=host_format):
                            self.audit_inventory([(raw, "U", decoded)],
                                                 [(raw, "T", decoded)], host_format)

    def test_microsoft_allocation_spacing_does_not_expand_the_symbol_policy(self):
        declarations = (
            # Placement's second void* remains outside the existing policy.
            ("??2@YAPEAX_KPEAX@Z",
             "void * __cdecl operator new(unsigned __int64, void *)"),
            ("??_U@YAPEAX_KPEAX@Z",
             "void * __cdecl operator new[](unsigned __int64, void *)"),
            ("??2Host@@SAPEAX_KAEBUnothrow_t@std@@@Z",
             "public: static void * __cdecl Host::operator new(unsigned __int64, "
             "struct std::nothrow_t const &)"),
            ("?host_function@@YAPEAX_KAEBUnothrow_t@std@@@Z",
             "void * __cdecl host_function(unsigned __int64, "
             "struct std::nothrow_t const &)"),
            ("??2@YAPEAX_KAEBUnothrow_t@Host@@@Z",
             "void * __cdecl operator new(unsigned __int64, "
             "struct Host::nothrow_t const &)"),
            ("??2@YAPEAX_KW4align_val_t@Host@@@Z",
             "void * __cdecl operator new(unsigned __int64, enum Host::align_val_t)"),
            # These actual a551 helper names are not global operator names.
            ("?__empty_global_delete@@YAXPEAX@Z",
             "void __cdecl __empty_global_delete(void *)"),
            ("?__empty_global_delete@@YAXPEAXW4align_val_t@std@@@Z",
             "void __cdecl __empty_global_delete(void *, enum std::align_val_t)"),
            ("?__empty_global_delete@@YAXPEAX_K@Z",
             "void __cdecl __empty_global_delete(void *, unsigned __int64)"),
            ("?__empty_global_delete@@YAXPEAX_KW4align_val_t@std@@@Z",
             "void __cdecl __empty_global_delete(void *, unsigned __int64, "
             "enum std::align_val_t)"),
            ("?__global_delete@@YAXPEAX_K@Z",
             "void __cdecl __global_delete(void *, unsigned __int64)"))
        for raw, spaced in declarations:
            for decoded in (spaced, spaced.replace(", ", ",")):
                with self.subTest(raw=raw, decoded=decoded):
                    self.assertFalse(AuditArchive.standard_shared_symbol(raw, decoded))
                    for host_format in ("nm", "coff-index"):
                        with self.subTest(host_format=host_format):
                            with self.assertRaisesRegex(ValueError, "private/host symbol intersection"):
                                self.audit_inventory([(raw, "U", decoded)],
                                                     [(raw, "T", decoded)], host_format)

    def test_microsoft_allocation_rejects_other_whitespace_and_trailing_text(self):
        raw = "??2@YAPEAX_KW4align_val_t@std@@AEBUnothrow_t@1@@Z"
        decoded = ("void * __cdecl operator new(unsigned __int64, "
                   "enum std::align_val_t, struct std::nothrow_t const &)")
        parts = decoded.split(", ")
        for separator in (",  ", ",\t", ",\r", ",\n", ",\0"):
            for position in (0, 1):
                malformed = (parts[0] + (separator if position == 0 else ", ") +
                             parts[1] + (separator if position == 1 else ", ") + parts[2])
                with self.subTest(separator=separator, position=position):
                    self.assertFalse(AuditArchive.standard_shared_symbol(raw, malformed))
                    # CR/LF are line framing in the inventory reader, so their
                    # rejection above deliberately tests the policy directly.
                    if separator == ",  ":
                        for host_format in ("nm", "coff-index"):
                            with self.assertRaisesRegex(ValueError, "private/host symbol intersection"):
                                self.audit_inventory([(raw, "U", malformed)],
                                                     [(raw, "T", malformed)], host_format)
        for suffix in (" ", " const", " junk", ")", "\n", "\0"):
            with self.subTest(suffix=suffix):
                self.assertFalse(AuditArchive.standard_shared_symbol(raw, decoded + suffix))

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

    def test_microsoft_all_19_ci_standard_declarations_have_standard_owners(self):
        declarations = [self.microsoft_gcd_lambda]
        for category in ("_Future_error_category2", "_Generic_error_category",
                         "_Iostream_error_category2", "_System_error_category"):
            function = (f"class std::{category} const & __cdecl "
                        f"std::_Immortalize_memcpy_image<class std::{category}>(void)")
            declarations.extend((f"int `{function}'::`2'::$TSS0",
                                 f"class std::{category} `{function}'::`2'::_Static"))
        for facet in (
                "std::codecvt<char, char, struct _Mbstatet>",
                "std::ctype<char>",
                "std::num_put<char, class std::ostreambuf_iterator<char, "
                "struct std::char_traits<char>>>",
                "std::numpunct<char>"):
            # The real demangler attaches the pointer '*' to the entity name.
            declarations.append("public: static class std::locale::facet const *"
                                f"std::_Facetptr<class {facet}>::_Psave")
        for facet in (
                "std::codecvt<char, char, struct _Mbstatet>",
                "std::num_put<char, class std::ostreambuf_iterator<char, "
                "struct std::char_traits<char>>>",
                "std::numpunct<char>"):
            declarations.append("void __cdecl `dynamic initializer for `public: "
                                f"static class std::locale::id {facet}::id''(void)")
        declarations.extend((
            "char const *const `public: virtual class std::basic_string<char, "
            "struct std::char_traits<char>, class std::allocator<char>> __cdecl "
            "std::_Iostream_error_category2::message(int) const'::`5'::_Iostream_error",
            "struct _Mbstatet `protected: void __cdecl std::basic_filebuf<char, "
            "struct std::char_traits<char>>::_Init(struct _iobuf *, enum "
            "std::basic_filebuf<char, struct std::char_traits<char>>::_Initfl)'"
            "::`2'::_Stinit",
            "char const *const `public: virtual class std::basic_string<char, "
            "struct std::char_traits<char>, class std::allocator<char>> __cdecl "
            "std::_System_error_category::message(int) const'::`7'::_Unknown_error"))
        self.assertEqual(len(declarations), 19)
        for declaration in declarations:
            with self.subTest(declaration=declaration):
                self.assertTrue(AuditArchive.microsoft_std_entity(declaration))

    def test_microsoft_standard_owner_reaches_the_existing_intersection_policy(self):
        declarations = (
            ("?_Psave@?$_Facetptr@V?$ctype@D@std@@@std@@2PEBVfacet@locale@2@EB", "B",
             "public: static class std::locale::facet const "
             "*std::_Facetptr<class std::ctype<char>>::_Psave"),
            ("??__E?id@?$numpunct@D@std@@2V0locale@2@A@@YAXXZ", "T",
             "void __cdecl `dynamic initializer for `public: static class "
             "std::locale::id std::numpunct<char>::id''(void)"))
        for name, kind, declaration in declarations:
            with self.subTest(name=name):
                self.assertTrue(AuditArchive.standard_shared_symbol(name, declaration))
                self.audit_inventory([(name, kind, declaration)],
                                     [(name, "W", declaration)])

    def test_microsoft_std_eh_arrays_and_throw_info_share_nm_identity(self):
        # Exact CTA/TI spellings from the cac5 Windows Clang audit. This
        # checks shared std ownership, not record layout or ABI equivalence.
        symbols = (
            "_CTA2?AVbad_cast@std@@",
            "_CTA2?AVbad_optional_access@std@@",
            "_CTA2?AVbad_variant_access@std@@",
            "_CTA3?AVbad_array_new_length@std@@",
            "_CTA3?AVfuture_error@std@@",
            "_CTA5?AVfailure@ios_base@std@@",
            "_TI2?AVbad_cast@std@@",
            "_TI2?AVbad_optional_access@std@@",
            "_TI2?AVbad_variant_access@std@@",
            "_TI3?AVbad_array_new_length@std@@",
            "_TI3?AVfuture_error@std@@",
            "_TI5?AVfailure@ios_base@std@@",
        )
        for name in symbols:
            with self.subTest(name=name):
                self.audit_inventory([(name, "R", name)],
                                     [(name, "W", name)], "nm")

    def test_microsoft_std_catchable_types_share_nm_identity(self):
        # Exact CT spellings from the cac5 Windows Clang audit. Only shared
        # std type/copy-constructor ownership is tested; the numeric suffix
        # does not establish size or layout.
        symbols = (
            "_CT??_R0?AV_System_error@std@@@8"
            "??0_System_error@std@@QEAA@AEBV01@@Z40",
            "_CT??_R0?AVbad_alloc@std@@@8"
            "??0bad_alloc@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVbad_array_new_length@std@@@8"
            "??0bad_array_new_length@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVbad_cast@std@@@8"
            "??0bad_cast@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVbad_optional_access@std@@@8"
            "??0bad_optional_access@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVbad_variant_access@std@@@8"
            "??0bad_variant_access@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVexception@std@@@8"
            "??0exception@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVfailure@ios_base@std@@@8"
            "??0failure@ios_base@std@@QEAA@AEBV012@@Z40",
            "_CT??_R0?AVfuture_error@std@@@8"
            "??0future_error@std@@QEAA@AEBV01@@Z40",
            "_CT??_R0?AVlogic_error@std@@@8"
            "??0logic_error@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVruntime_error@std@@@8"
            "??0runtime_error@std@@QEAA@AEBV01@@Z24",
            "_CT??_R0?AVsystem_error@std@@@8"
            "??0system_error@std@@QEAA@AEBV01@@Z40",
        )
        for name in symbols:
            with self.subTest(name=name):
                self.audit_inventory([(name, "R", name)],
                                     [(name, "W", name)], "nm")

    def test_microsoft_std_catchable_type_plain_owners_and_raw_tail_bounds(self):
        # These tails are accepted as raw decimal spellings, not decoded sizes.
        names = (
            "_CT??_R0?AVsample@std@@@8??0sample@std@@QEAA@AEBV01@@Z0",
            "_CT??_R0?AVsample@std@@@8??0sample@std@@QEAA@AEBV01@@Z4294967295",
            "_CT??_R0?AV_Error1@nested_2@std@@@8"
            "??0_Error1@nested_2@std@@QEAA@AEBV012@@Z24",
        )
        for name in names:
            for private in ([(name, "R", name)],
                            [(name, "R", name), (name, "R", name)]):
                with self.subTest(name=name, definitions=len(private)):
                    self.audit_inventory(private, [(name, "W", name)], "nm")

    def test_microsoft_std_catchable_type_rejects_unknown_or_mismatched_names(self):
        plain = "_CT??_R0?AVbad_cast@std@@@8??0bad_cast@std@@QEAA@AEBV01@@Z24"
        nested = ("_CT??_R0?AVfailure@ios_base@std@@@8"
                  "??0failure@ios_base@std@@QEAA@AEBV012@@Z40")
        names = (
            plain.replace("??0bad_cast@", "??0exception@"),
            nested.replace("??0failure@ios_base@", "??0failure@ios_other@"),
            nested.replace("??0failure@ios_base@", "??0ios_base@failure@"),
            plain.replace("@std@", "@Host@"),
            plain.replace("@std@", "@std@Host@"),
            plain.replace("@std@", "@std_extra@"),
            plain.replace("bad_cast@std", "std@std"),
            nested.replace("failure@ios_base@std", "failure@failure@std"),
            nested.replace("failure@ios_base@std", "failure@std@std"),
            nested.replace("failure@ios_base@std", "failure@ios_base@nested@std"),
            plain.replace("?AVbad_cast@", "?AV?$bad_cast@H@"),
            plain.replace("??0bad_cast@", "??0?$bad_cast@H@"),
            plain.replace("@std@", "@?A0x1234@"),
            plain.replace("?AV", "?AU"),
            plain.replace("?AV", "?BV"),
            plain.replace("@@@8", "@@8"),
            plain.replace("@@@8", "@@@9"),
            plain.replace("@@@8", "@@@@8"),
            plain.replace("@@@8", "@@@8extra"),
            plain.replace("??0bad_cast@std@@QEAA@AEBV01@@Z", ""),
            plain.replace("??0", "??_O"),
            plain.replace("??0", "??_F"),
            plain.replace("QEAA", "QAE"),
            plain.replace("AEBV01@@Z", "AEBV012@@Z"),
            plain.replace("AEBV01@@Z", "AEBV10@@Z"),
            plain.replace("AEBV01@@Z", "AEBV00@@Z"),
            plain.replace("AEBV01@@Z", "AEBV09@@Z"),
            plain.replace("AEBV01@@Z", "AEBV010@@Z"),
            plain.replace("AEBV01@@Z", "AEBV01@H@Z"),
            plain.replace("AEBV01@@Z", "AEBV01@@ZZ"),
            nested.replace("AEBV012@@Z", "AEBV01@@Z"),
            nested.replace("AEBV012@@Z", "AEBV021@@Z"),
            nested.replace("AEBV012@@Z", "AEBV013@@Z"),
            "_CT??@0123456789abcdef@@24",
            plain + "@",
            plain + nested,
        )
        for name in names:
            with self.subTest(name=name):
                with self.assertRaisesRegex(
                        ValueError, "private/host symbol intersection"):
                    self.audit_inventory([(name, "R", name)],
                                         [(name, "W", name)], "nm")

    def test_microsoft_std_catchable_type_rejects_unknown_numeric_tails(self):
        prefix = "_CT??_R0?AVbad_cast@std@@@8??0bad_cast@std@@QEAA@AEBV01@@Z"
        for tail in ("", "00", "024", "+24", "-24", "0x18", "\u0662",
                     "4294967296", "10000000000", "24x", "24@", "24_1", "24-120"):
            name = prefix + tail
            with self.subTest(tail=tail):
                with self.assertRaisesRegex(
                        ValueError, "private/host symbol intersection"):
                    self.audit_inventory([(name, "R", name)],
                                         [(name, "W", name)], "nm")

    def test_microsoft_std_catchable_type_requires_only_read_only_definitions(self):
        name = "_CT??_R0?AVbad_cast@std@@@8??0bad_cast@std@@QEAA@AEBV01@@Z24"
        for kinds in (("U",), ("w",), ("v",), ("B",), ("D",), ("T",), ("W",),
                      ("V",), ("R", "U"), ("U", "R"), ("R", "w"), ("R", "v"),
                      ("R", "B"), ("R", "D"), ("R", "T"), ("R", "W"), ("R", "V")):
            with self.subTest(kinds=kinds):
                with self.assertRaisesRegex(
                        ValueError, "private/host symbol intersection"):
                    self.audit_inventory([(name, kind, name) for kind in kinds],
                                         [(name, "W", name)], "nm")

    def test_microsoft_std_catchable_type_preserves_independent_private_findings(self):
        name = "_CT??_R0?AVbad_cast@std@@@8??0bad_cast@std@@QEAA@AEBV01@@Z24"
        for old, _ in SETUP_GUID_PAIRS:
            with self.subTest(setup_guid=old):
                with self.assertRaisesRegex(
                        ValueError, "unisolated Setup GUID symbol: " + old):
                    self.audit_inventory([(name, "R", name), (old, "R", old)],
                                         [(name, "W", name), (old, "R", old)], "nm")
        with self.assertRaisesRegex(ValueError, "LLVMContextCreate"):
            self.audit_inventory([(name, "R", name),
                                  ("LLVMContextCreate", "T", "LLVMContextCreate")],
                                 [(name, "W", name)], "nm")

    def test_microsoft_std_eh_simple_owners_and_count_bounds(self):
        for prefix in ("_CTA", "_TI"):
            for spelling in ("0?AVerror@std@@", "4294967295?AVerror@std@@",
                             "12?AV_Error1@nested_2@std@@",
                             "2?AVerror@" + "nested@" * 30 + "std@@"):
                name = prefix + spelling
                with self.subTest(name=name):
                    self.audit_inventory([(name, "R", name)],
                                         [(name, "W", name)], "nm")

    def test_microsoft_std_eh_rejects_unknown_or_malformed_names(self):
        spellings = (
            "2?AVerror@Host@@",
            "2?AVerror@std@Host@@",
            "2?AVerror@std_extra@@",
            "2?AVstd@@",
            "2?AVerror@@",
            "2?AVerror@@std@@",
            "2?AVerror@std@@suffix",
            "2?AVerror@std@@@",
            "2?AVerror@std@",
            "2?AVerror@0@@",
            "2?AVerror@0@std@@",
            "2?AV?$error@H@std@@",
            "2?AVerror@?$owner@H@std@@",
            "2?AVerror@?A0x1234@std@@",
            "2?AVerror$1@std@@",
            "2?AV9error@std@@",
            "2?AVerror@std::@@",
            "2?AUerror@std@@",
            "2?ATerror@std@@",
            "2?AW4error@std@@",
            "2?BVerror@std@@",
            "?AVerror@std@@",
            "00?AVerror@std@@",
            "02?AVerror@std@@",
            "+2?AVerror@std@@",
            "-2?AVerror@std@@",
            "0x2?AVerror@std@@",
            "\u0662?AVerror@std@@",
            "4294967296?AVerror@std@@",
            "10000000000?AVerror@std@@",
            "C2?AVerror@std@@",
            "V2?AVerror@std@@",
            "U2?AVerror@std@@",
            "CVU2?AVerror@std@@",
            "2?AV" + "a" * 65536 + "@std@@",
            "2?AVerror@" + "nested@" * 31 + "std@@",
        )
        for prefix in ("_CTA", "_TI"):
            for spelling in spellings:
                name = prefix + spelling
                with self.subTest(prefix=prefix, spelling=spelling[:100]):
                    with self.assertRaisesRegex(
                            ValueError, "private/host symbol intersection"):
                        self.audit_inventory([(name, "R", name)],
                                             [(name, "W", name)], "nm")

    def test_microsoft_std_eh_requires_only_read_only_definitions(self):
        for name in ("_CTA2?AVbad_cast@std@@", "_TI2?AVbad_cast@std@@"):
            for kinds in (("U",), ("w",), ("v",), ("B",), ("D",), ("T",),
                          ("W",), ("V",), ("R", "U"), ("U", "R"),
                          ("R", "w"), ("R", "v"), ("R", "B"),
                          ("R", "D"), ("R", "T"), ("R", "W"), ("R", "V")):
                with self.subTest(name=name, kinds=kinds):
                    with self.assertRaisesRegex(
                            ValueError, "private/host symbol intersection"):
                        self.audit_inventory([(name, kind, name) for kind in kinds],
                                             [(name, "W", name)], "nm")

    def test_microsoft_std_eh_does_not_share_other_runtime_records(self):
        records = (
            ("__real@3ff0000000000000", "R"),
            ("__local_stdio_printf_options", "T"),
            ("__local_stdio_scanf_options", "T"),
            ("fprintf", "T"), ("sprintf_s", "T"), ("snprintf", "T"),
            ("_snprintf", "T"), ("sscanf", "T"),
            ("?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA", "B"),
            ("?_OptionsStorage@?1??__local_stdio_scanf_options@@9@4_KA", "B"),
            ("_Avx2WmemEnabledWeakValue", "B"),
        )
        for name, kind in records:
            with self.subTest(name=name):
                with self.assertRaisesRegex(
                        ValueError, "private/host symbol intersection"):
                    self.audit_inventory([(name, kind, name)],
                                         [(name, "W", name)], "nm")

    def test_microsoft_std_eh_sharing_preserves_independent_private_findings(self):
        for name in ("_CTA2?AVbad_cast@std@@", "_TI2?AVbad_cast@std@@"):
            for old, _ in SETUP_GUID_PAIRS:
                with self.subTest(name=name, setup_guid=old):
                    with self.assertRaisesRegex(
                            ValueError, "unisolated Setup GUID symbol: " + old):
                        self.audit_inventory([(name, "R", name), (old, "R", old)],
                                             [(name, "W", name), (old, "R", old)],
                                             "nm")
            with self.subTest(name=name, llvm_symbol="LLVMContextCreate"):
                with self.assertRaisesRegex(ValueError, "LLVMContextCreate"):
                    self.audit_inventory(
                        [(name, "R", name),
                         ("LLVMContextCreate", "T", "LLVMContextCreate")],
                        [(name, "W", name)], "nm")

    def test_microsoft_nonstandard_owners_cannot_hide_in_nested_quotes(self):
        declarations = (
            "int `class std::string __cdecl Host::get(void)'::`2'::$TSS0",
            "class std::string `class std::string __cdecl Host::get(void)'"
            "::`2'::_Static",
            "public: <auto> __cdecl `class std::vector<int> __cdecl Host::run(void)'"
            "::`1'::<lambda_1>::operator()(void) const",
            "class `void __cdecl std::run(void)'::`1'::<lambda_1> "
            "`void __cdecl Host::run(void)'::`2'::callback",
            "public: static class std::vector<int> "
            "`void __cdecl Host::run(void)'::`2'::Local::state",
            # std owns a nested argument's implementation, but not this lambda.
            self.microsoft_gcd_lambda.replace("std::gcd<", "Host::gcd<"),
            "int `class std::shared_ptr<struct Concurrency::scheduler_interface> "
            "* __cdecl Concurrency::details::_GetStaticAmbientSchedulerStorage(void)'"
            "::`2'::$TSS0")
        for declaration in declarations:
            with self.subTest(declaration=declaration):
                self.assertFalse(AuditArchive.microsoft_std_entity(declaration))

    def test_microsoft_variable_type_and_pointer_punctuation_do_not_change_owner(self):
        for declaration in (
                "public: static class std::string *Host::state",
                "public: static class std::string &Host::state",
                "public: static class std::string *Host<class std::string>::state",
                "public: static int vendor::std::state",
                "public: static int std_extra::state",
                "public: static int Host<std::vector<int>>::state",
                "public: void __cdecl Host<std::vector<int>>::method(void)",
                "public: __cdecl Host<std::vector<int>>::operator class std::string(void)",
                "class std::string __cdecl Host::method(class std::vector<int>)"):
            with self.subTest(declaration=declaration):
                self.assertFalse(AuditArchive.microsoft_std_entity(declaration))

    def test_microsoft_compound_operator_tokens_preserve_the_declared_owner(self):
        # These operator spellings/signatures occur in LLVM's ms-operators.test.
        for operator, parameters in (("->", "void"), ("->*", "int"),
                                     ("&&", "int"), ("||", "int"),
                                     ("++", "void"), ("++", "int"),
                                     ("--", "void"), ("--", "int")):
            for owner, expected in (("std::Box", True), ("Host", False)):
                declaration = f"int __cdecl {owner}::operator{operator}({parameters})"
                with self.subTest(declaration=declaration):
                    self.assertEqual(AuditArchive.microsoft_std_entity(declaration), expected)

    def test_microsoft_less_template_preserves_the_actual_ci_pair_owner(self):
        # Complete b24 Windows Clang x64 declaration: ?M is operator<, and
        # MicrosoftDemangleNodes appends the template '<' without a separator.
        string = ("class std::basic_string<char, struct std::char_traits<char>, "
                  "class std::allocator<char>>")
        pair = f"struct std::pair<{string}, {string}>"
        declaration = (f"bool __cdecl std::operator<<{string}, {string}, {string}, {string}>"
                       f"({pair} const &, {pair} const &)")
        raw = ("??$?MV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@"
               "V01@V01@V01@@std@@YA_NAEBU?$pair@V?$basic_string@DU?$char_traits@D@std@@"
               "V?$allocator@D@2@@std@@V12@@0@0@Z")
        self.assertTrue(AuditArchive.standard_shared_symbol(raw, declaration))
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                self.audit_inventory([(raw, "T", declaration)],
                                     [(raw, "W", declaration)], host_format)
        # Keep every std type in the signature; only the declaration owner
        # changes. The enclosing function also owns its quoted local symbols.
        host = declaration.replace("std::operator<", "Host::operator<", 1)
        for parent, expected in ((declaration, True), (host, False)):
            for value in (parent, f"int `{parent}'::`2'::state"):
                with self.subTest(declaration=value):
                    self.assertEqual(AuditArchive.microsoft_std_entity(value), expected)

    def test_microsoft_less_and_shift_template_punctuation_preserves_owners(self):
        for name in ("operator<", "operator<<", "operator<<=",
                     "operator<<int>", "operator<<<int>",
                     "operator<<=<int>",
                     "operator<<class <unnamed-type-1>>",
                     "operator<<<<unnamed-type-1>>"):
            for owner, expected in (("std", True), ("Host", False)):
                parent = f"bool __cdecl {owner}::{name}(int, int)"
                for declaration in (parent, f"int `{parent}'::`2'::state"):
                    with self.subTest(declaration=declaration):
                        self.assertEqual(AuditArchive.microsoft_std_entity(declaration),
                                         expected)

    def test_microsoft_template_operator_malformed_and_ambiguous_forms_fail_closed(self):
        for declaration in (
                "bool __cdecl std::operator<<int(int, int)",
                "bool __cdecl std::operator<<int>>(int, int)",
                "bool __cdecl std::operator<<<int(int, int)",
                "bool __cdecl std::operator<<<int>>(int, int)",
                "bool __cdecl std::operator<<int>(int, int) trailing",
                "bool __cdecl std::operator<<int>(int, int) constconst",
                "int `bool __cdecl std::operator<<int>(int, int)::`2'::state",
                "int `bool __cdecl std::operator<<",
                # A tagless anonymous first argument is ambiguous with the
                # shift spelling. Do not guess an alternative operator split.
                "bool __cdecl std::operator<<<unnamed-type-1>>(int, int)",
                "bool __cdecl std::operator<<<<unnamed-type-1>>>(int, int)"):
            with self.subTest(declaration=declaration):
                self.assertFalse(AuditArchive.microsoft_std_entity(declaration))
        for control in ("\n", "\r", "\t", "\x00", "\x1f", "\x7f"):
            for declaration in ("bool __cdecl std::operator<<(int, int)",
                                "int `bool __cdecl std::operator<<(int, int)'::`2'::state"):
                with self.subTest(declaration=declaration, control=repr(control)):
                    self.assertFalse(AuditArchive.microsoft_std_entity(declaration + control))

    def test_microsoft_template_operator_nesting_remains_bounded(self):
        for operator in ("<", "<<"):
            for depth, expected in ((32, True), (33, False)):
                argument = "Box<" * (depth - 1) + "int" + ">" * (depth - 1)
                declaration = f"bool __cdecl std::operator{operator}<{argument}>(int, int)"
                with self.subTest(operator=operator, depth=depth):
                    self.assertEqual(AuditArchive.microsoft_std_entity(declaration), expected)

    def test_microsoft_function_suffix_requires_separate_ordered_qualifiers(self):
        declaration = "public: void __cdecl std::Box::f(void)"
        # MicrosoftDemangleNodes.cpp emits noexcept before the ref qualifier.
        for suffix in ("const noexcept &",
                       "const volatile __restrict __unaligned noexcept &&"):
            with self.subTest(suffix=suffix):
                self.assertTrue(AuditArchive.microsoft_std_entity(declaration + " " + suffix))
        for suffix in ("constvolatile", "const const", "&&&", "noexcept const",
                       "const & noexcept"):
            with self.subTest(suffix=suffix):
                self.assertFalse(AuditArchive.microsoft_std_entity(declaration + " " + suffix))

    def test_microsoft_dynamic_wrappers_follow_the_variable_owner(self):
        for operation in ("dynamic initializer", "dynamic atexit destructor"):
            standard = (f"void __cdecl `{operation} for `public: static "
                        "class std::locale::id std::numpunct<char>::id''(void)")
            host = (f"void __cdecl `{operation} for `public: static "
                    "class std::vector<int> Host::state''(void)")
            with self.subTest(operation=operation):
                self.assertTrue(AuditArchive.microsoft_std_entity(standard))
                self.assertFalse(AuditArchive.microsoft_std_entity(host))
        # Real negative from LLVM 20 llvm/test/Demangle/ms-operators.test.
        self.assertFalse(AuditArchive.microsoft_std_entity(
            "void __cdecl `dynamic atexit destructor for `private: static class "
            "std::vector<class antlr4::dfa::DFA, class std::allocator<class "
            "antlr4::dfa::DFA>> XPathLexer::_decisionToDFA''(void)"))

    def test_microsoft_malformed_standard_declarations_fail_closed(self):
        for declaration in (
                "", "std::", "void __cdecl std::f(",
                "int std::A:::value",
                "int `void __cdecl std::f(void)'::::value",
                "void __cdecl std::vector<int::clear(void)",
                "void __cdecl std::vector<int>>::clear(void)",
                "void __cdecl std::f(void))",
                "void __cdecl std::f(void) trailing_garbage",
                "void __cdecl std::f(void); void __cdecl Host::f(void)",
                "int `void __cdecl std::f(void)::`2'::state",
                "int `void __cdecl std::f(void)'::`2::state",
                "int `void __cdecl std::f(void)'::`2'::",
                "void __cdecl `dynamic initializer for `int std::state'(void)",
                "void __cdecl `dynamic initializer for `int std::state''(void) garbage",
                "void __cdecl std::f(void)\n",
                "void __cdecl std::f(void)\x00"):
            with self.subTest(declaration=declaration):
                self.assertFalse(AuditArchive.microsoft_std_entity(declaration))

    def test_microsoft_input_length_boundary_is_enforced(self):
        prefix, suffix = "int std::", "::value"
        declaration = prefix + "X" * (65536 - len(prefix) - len(suffix)) + suffix
        self.assertEqual(len(declaration), 65536)
        self.assertTrue(AuditArchive.microsoft_std_entity(declaration))
        self.assertFalse(AuditArchive.microsoft_std_entity(declaration + "x"))

    def test_microsoft_template_nesting_boundary_is_enforced(self):
        for depth, expected in ((32, True), (33, False)):
            declaration = "int " + "std::Box<" * depth + "int" + ">" * depth + "::value"
            with self.subTest(depth=depth):
                self.assertEqual(AuditArchive.microsoft_std_entity(declaration), expected)

    def test_microsoft_nested_quotes_and_parentheses_are_bounded(self):
        parent = "void __cdecl std::root(void)"
        for _ in range(40):
            parent = ("void __cdecl `" + parent + "'::`1'::<lambda_1>"
                      "::operator()(void)")
        self.assertFalse(AuditArchive.microsoft_std_entity(parent))
        # Parentheses count even when they occur in a template argument's type.
        self.assertFalse(AuditArchive.microsoft_std_entity(
            "void __cdecl std::function<" + "(" * 33 + "int" + ")" * 33 + ">::f(void)"))

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
        self.assertEqual(reader.read_defined_symbols.call_args_list,
                         [mock.call(Path("host.lib")), mock.call(Path("host.lib"))])

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
