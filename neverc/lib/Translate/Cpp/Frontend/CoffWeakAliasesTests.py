#!/usr/bin/env python3
"""Controlled llvm-readobj records for the private COFF fallback proof."""

import io
import subprocess
import sys
import unittest
from unittest import mock

sys.dont_write_bytecode = True
import CoffWeakAliases as coff


def symbol(name, storage=2, section=1, value=0, linked=None, search=1):
    storage_name = {2: "External", 3: "Static", 105: "WeakExternal"}[storage]
    section_name = ".text" if section > 0 else "IMAGE_SYM_UNDEFINED"
    result = ("  Symbol {\n"
              f"    Name: {name}\n"
              f"    Value: {value}\n"
              f"    Section: {section_name} ({section})\n"
              "    BaseType: Null (0x0)\n"
              "    ComplexType: Function (0x2)\n"
              f"    StorageClass: {storage_name} (0x{storage:X})\n"
              f"    AuxSymbolCount: {int(linked is not None)}\n")
    if linked is not None:
        search_name = {1: "NoLibrary", 2: "Library", 3: "Alias", 4: "AntiDependency"}[search]
        result += ("    AuxWeakExternal {\n"
                   f"      Linked: {linked[0]} ({linked[1]})\n"
                   f"      Search: {search_name} (0x{search:X})\n"
                   "    }\n")
    return result + "  }\n"


def member(*symbols, name="private.lib(member.obj)"):
    return (f"File: {name}\nFormat: COFF-x86-64\nArch: x86_64\n"
            "AddressSize: 64bit\nSymbols [\n" + "".join(symbols) + "]\n")


def weak(name="private_alias", target="private_body", index=0, search=1):
    return symbol(name, storage=105, section=0, linked=(target, index), search=search)


class CoffWeakAliasesTests(unittest.TestCase):
    def parse(self, text, definitions=("private_body",), private=("private_alias",)):
        return coff.parse_resolved_aliases(io.StringIO(text), set(definitions), set(private))

    def test_all_supported_search_modes_require_an_actual_target(self):
        for search in (1, 2, 3):
            with self.subTest(search=search):
                self.assertEqual(self.parse(member(symbol("private_body"), weak(search=search))),
                                 {"private_alias"})

    def test_exact_runtime_fallback_requires_every_edge_and_real_target(self):
        wrapper = "?__global_delete@@YAXPEAX_K@Z"
        fallback = "?__empty_global_delete@@YAXPEAX_K@Z"

        def exact(text, definitions=(fallback,)):
            return coff.parse_resolved_aliases(
                io.StringIO(text), set(definitions), {wrapper},
                expected_fallbacks={wrapper: fallback})

        valid = member(symbol(fallback), weak(name=wrapper, target=fallback))
        self.assertIn(wrapper, exact(valid))
        split = (member(symbol(fallback, section=0), weak(name=wrapper, target=fallback),
                        name="private.lib(use.obj)") +
                 member(symbol(fallback), name="private.lib(body.obj)"))
        self.assertIn(wrapper, exact(split))
        invalid = (
            (member(symbol(fallback)), (fallback,)),
            (member(symbol(fallback, section=0), weak(name=wrapper, target=fallback)), (fallback,)),
            (member(symbol(fallback, storage=3), weak(name=wrapper, target=fallback)), (fallback,)),
            (member(symbol("wrong"), weak(name=wrapper, target="wrong")), ("wrong", fallback)),
            (member(symbol(fallback), weak(name=wrapper, target=fallback)), ()),
            (member(symbol("body"), weak(name=fallback, target="body"),
                    weak(name=wrapper, target=fallback, index=1)), (fallback, "body")),
            (member(weak(name=fallback, target=wrapper, index=2),
                    weak(name=wrapper, target=fallback)), (fallback, wrapper)),
            (valid + member(symbol("wrong"), weak(name=wrapper, target="wrong"),
                            name="private.lib(other.obj)"), (fallback, "wrong")),
        )
        for text, definitions in invalid:
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    exact(text, definitions)

    def test_global_fallback_definition_may_be_in_another_member(self):
        text = (member(symbol("private_body", section=0), weak(), name="private.lib(a.obj)") +
                member(symbol("private_body"), name="private.lib(b.obj)"))
        self.assertEqual(self.parse(text), {"private_alias"})

    def test_plain_reference_in_another_member_does_not_hide_the_alias(self):
        text = (member(symbol("private_alias", section=0), name="private.lib(use.obj)") +
                member(symbol("private_body"), weak(), name="private.lib(alias.obj)"))
        self.assertEqual(self.parse(text), {"private_alias"})

    def test_local_fallback_is_proved_by_its_own_member_definition(self):
        self.assertEqual(self.parse(member(symbol("local_body", storage=3),
                                           weak(target="local_body")), definitions=()),
                         {"private_alias"})

    def test_other_member_static_name_cannot_satisfy_a_global_fallback(self):
        text = (member(symbol("private_body", section=0), weak(), name="private.lib(a.obj)") +
                member(symbol("private_body", storage=3), name="private.lib(b.obj)"))
        with self.assertRaisesRegex(ValueError, "no actual private definition"):
            self.parse(text)

    def test_nm_definition_name_is_not_enough_without_real_coff_definition(self):
        with self.assertRaisesRegex(ValueError, "no actual private definition"):
            self.parse(member(symbol("private_body", section=0), weak()))

    def test_host_or_standard_named_target_is_not_a_terminal_escape(self):
        for target in ("host_function", "?function@std@@YAXXZ"):
            with self.subTest(target=target):
                with self.assertRaisesRegex(ValueError, "no actual private definition"):
                    self.parse(member(symbol(target, section=0), weak(target=target)),
                               definitions=(target,))

    def test_nm_uppercase_weak_alias_is_not_a_definition_seed(self):
        text = member(weak(name="private_alias", target="other_alias", index=2, search=3),
                      weak(name="other_alias", target="private_alias", index=0, search=3))
        with self.assertRaisesRegex(ValueError, "cycle"):
            self.parse(text, definitions=("private_alias", "other_alias"))

    def test_separate_actual_definition_overrides_a_weak_fallback(self):
        text = (member(symbol("missing", section=0), weak(target="missing"),
                       name="private.lib(weak.obj)") +
                member(symbol("private_alias"), name="private.lib(body.obj)"))
        self.assertEqual(self.parse(text, definitions=("private_alias",)),
                         {"private_alias"})

    def test_multihop_fallback_is_proved_to_its_actual_terminal(self):
        text = member(symbol("private_body"),
                      weak(name="middle", index=0, search=3),
                      weak(index=1, target="middle"))
        self.assertEqual(self.parse(text, definitions=("private_body", "middle")),
                         {"middle", "private_alias"})

    def test_tag_index_must_point_to_primary_symbol_with_the_same_name(self):
        for target, index in (("private_body", 999), ("wrong_name", 0),
                              ("private_alias", 2)):
            with self.subTest(target=target, index=index):
                with self.assertRaisesRegex(ValueError, "TagIndex/name"):
                    self.parse(member(symbol("private_body"), weak(target=target, index=index)))

    def test_unknown_or_anti_dependency_search_is_not_accepted(self):
        text = member(symbol("private_body"), weak(search=4))
        with self.assertRaisesRegex(ValueError, "Unsupported COFF weak search"):
            self.parse(text)
        with self.assertRaisesRegex(ValueError, "Unsupported COFF weak search"):
            self.parse(text.replace("AntiDependency (0x4)", "Unknown (0xFF)"))

    def test_ordinary_undefined_storage_cannot_be_treated_as_a_weak_alias(self):
        text = member(symbol("private_body"), symbol(
            "private_alias", storage=2, section=0, linked=("private_body", 0)))
        with self.assertRaisesRegex(ValueError, "Malformed COFF weak external"):
            self.parse(text)

    def test_conflicting_fallback_edges_are_rejected_even_if_both_bodies_exist(self):
        text = (member(symbol("one"), weak(target="one"), name="private.lib(a.obj)") +
                member(symbol("two"), weak(target="two"), name="private.lib(b.obj)"))
        with self.assertRaisesRegex(ValueError, "Conflicting COFF fallback"):
            self.parse(text, definitions=("one", "two"))

    def test_unresolved_optional_nonprivate_alias_is_not_falsely_proved(self):
        text = member(symbol("platform_optional", section=0),
                      weak(name="platform_weak", target="platform_optional"))
        self.assertEqual(self.parse(text, definitions=(), private=()), set())

    def test_malformed_truncated_or_unknown_schema_fails_closed(self):
        valid = member(symbol("private_body"), weak())
        for text in ("", valid[:-2], valid.replace("Symbols [", "Symbols {"),
                     valid.replace("    Name: private_body", "    Unknown: private_body"),
                     valid.replace("      Linked:", "     Linked:"),
                     valid.replace("    Name: private_body\n", "    Name: private_body\n    Name: duplicate\n"),
                     valid.replace("private_body", "bad\x00name")):
            with self.subTest(text=text[:80]):
                with self.assertRaises(ValueError):
                    self.parse(text)

    def test_line_and_member_inventory_limits_are_bounded(self):
        with mock.patch.object(coff, "MAX_LINE", 16):
            with self.assertRaisesRegex(ValueError, "line exceeds"):
                self.parse(member(symbol("private_body"), weak()))
        with mock.patch.object(coff, "MAX_SYMBOLS_PER_MEMBER", 1):
            with self.assertRaisesRegex(ValueError, "member symbol inventory"):
                self.parse(member(symbol("private_body"), weak()))

    def test_fallback_depth_is_bounded_before_python_recursion_limit(self):
        text = member(symbol("private_body"),
                      weak(name="z_middle", index=0),
                      weak(index=1, target="z_middle"))
        with mock.patch.object(coff, "MAX_CHAIN", 1):
            with self.assertRaisesRegex(ValueError, "chain exceeds"):
                self.parse(text)

    def inspect(self, text, status=0, diagnostic=b""):
        process = mock.Mock()
        process.wait.return_value = status
        process.poll.return_value = status

        def launch(arguments, stdout, stderr):
            self.assertEqual(arguments, ["private-readobj", "--symbols", "--no-demangle", "private.lib"])
            stdout.write(text.encode("utf-8"))
            stdout.flush()
            stderr.write(diagnostic)
            stderr.flush()
            return process

        with mock.patch.object(coff.subprocess, "Popen", side_effect=launch):
            return coff.read_resolved_aliases("private-readobj", "private.lib",
                                              {"private_body"}, {"private_alias"})

    def test_tool_output_is_parsed_from_bounded_temporary_files(self):
        self.assertEqual(self.inspect(member(symbol("private_body"), weak())), {"private_alias"})

    def test_tool_errors_and_warnings_cannot_silently_omit_members(self):
        text = member(symbol("private_body"), weak())
        for status, diagnostic in ((1, b"corrupt object"), (0, b"unsupported member")):
            with self.subTest(status=status):
                with self.assertRaisesRegex(ValueError, "llvm-readobj inspection failed"):
                    self.inspect(text, status, diagnostic)

    def test_tool_output_size_is_limited(self):
        with mock.patch.object(coff, "MAX_OUTPUT", 4):
            with self.assertRaisesRegex(ValueError, "output exceeds"):
                self.inspect(member(symbol("private_body"), weak()))

    def test_timeout_kills_and_reaps_the_reader(self):
        process = mock.Mock()
        process.poll.return_value = None
        process.wait.return_value = 0
        with mock.patch.object(coff.subprocess, "Popen", return_value=process), \
                mock.patch.object(coff.time, "monotonic", side_effect=[0, coff.READ_TIMEOUT + 1]):
            with self.assertRaisesRegex(ValueError, "timed out"):
                coff.read_resolved_aliases("private-readobj", "private.lib", set())
        process.kill.assert_called_once_with()
        process.wait.assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
