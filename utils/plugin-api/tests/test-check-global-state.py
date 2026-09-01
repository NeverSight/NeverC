#!/usr/bin/env python3
"""Self-test for check-global-state.py.

Guards the fragile parts of the checker so the release gate does not silently
degrade into a single easy-to-miss ``rg``:

* comment/string stripping (a forbidden token in a comment or literal must not
  count, but the same token in real code must);
* forbidden-symbol detection across the plugin/dyncode/linker trees;
* the composite regex spellings (e.g. ``static CommonLinkerContext *lctx``).
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
from unittest import mock


HERE = pathlib.Path(__file__).resolve().parent
CHECKER = HERE.parent / "check-global-state.py"


def load_checker():
    spec = importlib.util.spec_from_file_location("check_global_state", CHECKER)
    mod = importlib.util.module_from_spec(spec)
    # Register before exec so @dataclass can resolve the module namespace.
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


_PERFORMED = 0


def expect(cond: bool, message: str, failures: list[str]) -> None:
    global _PERFORMED
    _PERFORMED += 1
    if not cond:
        failures.append(message)


def main() -> int:
    mod = load_checker()
    failures: list[str] = []

    strip = mod.strip_comments_and_strings

    # 1. Line comment hides the token.
    code = strip("int x; // getGlobalPluginLoader\n")
    expect("getGlobalPluginLoader" not in code,
           "line-comment token leaked past stripper", failures)

    # 2. Block comment hides the token.
    code = strip("/* getCurrentDynCodeOptions */ int y;\n")
    expect("getCurrentDynCodeOptions" not in code,
           "block-comment token leaked past stripper", failures)
    code = strip("getGlobal/* a token separator */PluginLoader;\n")
    expect("getGlobalPluginLoader" not in code,
           "block-comment removal merged two C++ tokens", failures)

    # 3. String literal hides the token.
    code = strip('const char *s = "setDynCodeModeState";\n')
    expect("setDynCodeModeState" not in code,
           "string-literal token leaked past stripper", failures)

    # 4. Real code keeps the token.
    code = strip("auto &o = currentDynCodeOptionsStorage();\n")
    expect("currentDynCodeOptionsStorage" in code,
           "real-code token wrongly stripped", failures)

    # 5. Newlines preserved so line numbers stay accurate.
    code = strip("a\n/* c\nc */\nb\n")
    expect(code.count("\n") == 4,
           f"line count changed by stripper: {code.count(chr(10))}", failures)

    # 6. Composite forbidden regex spelling.
    matched = any(
        pat.search("static CommonLinkerContext *lctx = nullptr;")
        for pat, _ in mod.FORBIDDEN_REGEXES
    )
    expect(matched, "composite lctx regex did not match", failures)

    # 7. Forbidden symbol table is non-empty and includes the dyncode singleton.
    expect("getCurrentDynCodeOptions" in mod.FORBIDDEN_SYMBOLS,
           "FORBIDDEN_SYMBOLS missing dyncode current-options", failures)
    expect("getGlobalPluginLoader" in mod.FORBIDDEN_SYMBOLS,
           "FORBIDDEN_SYMBOLS missing prototype loader", failures)

    # 8. Allowlist parses and only documents thread_local/static facades.
    allowlist = mod.load_allowlist()
    expect(isinstance(allowlist.get("entries"), list) and allowlist["entries"],
           "allowlist did not load any entries", failures)
    expect(all(entry.get("max_count") == 1 and
               entry.get("min_count") ==
               (0 if entry.get("artifact_optional") is True else 1)
               for entry in allowlist.get("binary_symbols", [])),
           "binary allowlist entries have invalid presence contracts",
           failures)
    for entry in allowlist.get("entries", []):
        expect(all(isinstance(entry.get(field), str) and
                   bool(entry[field].strip())
                   for field in ("symbol", "declaration",
                                 "definition_sha256", "owner", "lifetime",
                                 "justification", "cleared_by_test")),
               f"incomplete TLS allowlist entry: {entry}", failures)
    header_constants = allowlist.get("header_constants", [])
    expect(bool(header_constants),
           "complex header-constant manifest is missing", failures)
    for entry in header_constants:
        expect(all(isinstance(entry.get(field), str) and
                   bool(entry[field].strip())
                   for field in ("file", "symbol", "declaration",
                                 "definition_sha256", "owner",
                                 "justification")),
               f"incomplete header-constant entry: {entry}", failures)
    for entry in allowlist.get("source_objects", []):
        expect(all(isinstance(entry.get(field), str) and
                   bool(entry[field].strip())
                   for field in ("file", "symbol", "declaration",
                                 "definition_sha256", "owner", "kind",
                                 "justification")),
               f"incomplete source-object entry: {entry}", failures)
    audited_keys = set(mod.audited_source_files())
    documented_tls_paths = {
        path.replace("\\", "/")
        for entry in allowlist.get("entries", [])
        for path in entry.get("files", [])
    }
    expect(documented_tls_paths <= audited_keys,
           f"TLS allowlist contains dead paths outside the audit closure: "
           f"{sorted(documented_tls_paths - audited_keys)}", failures)
    expect("neverc/lib/Foundation/Core/ProcessResourceBroker.cpp" in
           mod.EXTRA_AUDIT_FILES,
           "ProcessResourceBroker TLS dependency is outside audit scope",
           failures)
    expect("neverc/lib/Merge/Common/MergerCommon.h" in mod.EXTRA_AUDIT_FILES,
           "Merger fatal TLS allowlist path is outside audit scope", failures)
    for entry in allowlist.get("binary_symbols", []):
        expect(entry.get("max_count") == 1 and
               entry.get("min_count") ==
               (0 if entry.get("artifact_optional") is True else 1),
               f"binary allowlist multiplicity is not exact: {entry}",
               failures)
        expect(all(isinstance(entry.get(field), str) and
                   bool(entry[field].strip())
                   for field in ("file", "source_symbol", "declaration",
                                 "definition_sha256")),
               f"binary exception is not source-bound: {entry}", failures)

    # 9. Writability classification. A conventional section/segment name is
    # not proof of its runtime permissions: a custom ELF script can leave
    # `.data.rel.ro` outside PT_GNU_RELRO, and Mach-O segment names do not encode
    # vm_prot. Without parsed artifact protections these must fail closed.
    writable = mod.symbol_is_writable
    expect(writable("d", ".data.rel.ro"),
           ".data.rel.ro name was trusted without PT_GNU_RELRO proof",
           failures)
    expect(writable("d", ".data.rel.ro.local"),
           ".data.rel.ro.* name was trusted without PT_GNU_RELRO proof",
           failures)
    expect(not writable("r", ".rodata"), ".rodata treated as writable", failures)
    expect(writable("b", ".bss"), ".bss not treated as writable", failures)
    expect(writable("d", ".data"), ".data not treated as writable", failures)
    expect(writable("b", ".bss.someSymbol"),
           "per-symbol .bss section not treated as writable", failures)
    expect(writable("d", ".plugin_state"),
           "custom SHF_WRITE section not treated as writable", failures)
    expect(writable("b", ".sbss"),
           "small BSS section not treated as writable", failures)
    for sym_class, section in (
        ("V", ".bss"),
        ("v", ".data"),
        ("u", ".data.unique"),
        ("S", ".sdata"),
        ("s", ".sbss"),
        ("C", "*COM*"),
        ("c", "*COM*"),
        ("W", ".data"),
    ):
        expect(writable(sym_class, section),
               f"ELF writable object class escaped: {sym_class} {section}",
               failures)
    expect(writable("s", ""),
           "defined BSD small-object class did not fail closed", failures)
    expect(not writable("W", "*UND*"),
           "undefined weak symbol was treated as writable", failures)
    expect(not writable("v", "*UND*"),
           "undefined weak object was treated as writable", failures)
    expect(writable("V", ".rodata"),
           "weak object trusted a section name without flag proof", failures)
    expect(writable("S", "__DATA,__common"),
           "Mach-O __DATA,__common not treated as writable", failures)
    expect(writable("s", "__AUTH,__data"),
           "Mach-O __AUTH data not treated as writable", failures)
    for section in ("__DATA_CONST,__const", "__AUTH_CONST,__const",
                    "__TEXT,__const"):
        expect(writable("S", section),
               f"Mach-O {section} name was trusted without vm_prot proof",
               failures)
    expect(not writable("d", ".data.rel.ro", runtime_read_only=True),
           "parsed PT_GNU_RELRO protection was ignored", failures)
    expect(not writable("S", "__DATA_CONST,__const",
                        runtime_read_only=True),
           "parsed Mach-O SG_READ_ONLY protection was ignored", failures)
    expect(writable("r", ".rodata", runtime_read_only=False),
           "parsed writable protection did not fail closed", failures)

    elf_sections = (
        "  [ 1] .rodata PROGBITS 0000000000001000 001000 000020 "
        "00   A  0   0  8\n"
        "  [ 2] .data.rel.ro PROGBITS 0000000000002000 002000 000030 "
        "00  WA  0   0  8\n"
        "  [ 3] .data PROGBITS 0000000000003000 003000 000040 "
        "00  WA  0   0  8\n")
    elf_segments = (
        "  GNU_RELRO      0x002000 0x0000000000002000 "
        "0x0000000000002000 0x000030 0x000030 R   0x1\n")
    elf_protections = mod.parse_elf_section_protections(
        elf_sections, elf_segments)
    expect(elf_protections == {
               ".rodata": True,
               ".data.rel.ro": True,
               ".data": False,
           }, f"ELF runtime protections parsed incorrectly: "
              f"{elf_protections}", failures)
    macho_protections = mod.parse_macho_segment_protections(
        "      cmd LC_SEGMENT_64\n"
        "  segname __DATA_CONST\n"
        " initprot 0x00000003\n"
        "    flags 0x10\n"
        "      cmd LC_SEGMENT_64\n"
        "  segname __DATA\n"
        " initprot 0x00000003\n"
        "    flags 0x0\n")
    expect(macho_protections == {
               "__DATA_CONST": True,
               "__DATA": False,
           }, f"Mach-O runtime protections parsed incorrectly: "
              f"{macho_protections}", failures)
    expect(mod.parse_elf_tls_sections(
               elf_sections +
               "  [ 4] .tbss NOBITS 0000000000004000 004000 000008 "
               "00 WAT  0   0  8\n") == {".tbss"},
           "ELF TLS decision did not use SHF_TLS", failures)
    expect(mod.parse_macho_tls_sections(
               "      cmd LC_SEGMENT_64\n"
               "  segname __DATA\n"
               " initprot 0x00000003\n"
               "    flags 0x0\n"
               "Section\n"
               "  sectname __thread_vars\n"
               "   segname __DATA\n"
               "     flags 0x00000013\n") ==
           {"__DATA,__thread_vars"},
           "Mach-O TLS decision did not use section type flags", failures)

    # 10. Thread-local recognition. The binary layer skips TLS and leaves it to
    # the source layer, so both platforms' spellings must be recognised: ELF
    # names the section, Mach-O only decorates the symbol.
    is_tls = mod.symbol_is_thread_local
    expect(is_tls("neverc::plugin::(anonymous namespace)::ActivePhases",
                  ".tbss"), "ELF .tbss not recognised as TLS", failures)
    expect(is_tls("whatever", ".tdata"), "ELF .tdata not recognised as TLS",
           failures)
    expect(is_tls("__ZN6neverc6plugin12_GLOBAL__N_112ActivePhasesE$tlv$init",
                  ""), "Mach-O $tlv$ suffix not recognised as TLS", failures)
    expect(is_tls("neverc::plugin::(anonymous namespace)::ActivePhases",
                  "__DATA,__thread_vars"),
           "Mach-O __thread_vars not recognised as TLS", failures)
    expect(is_tls("neverc::plugin::State", "__AUTH,__thread_data"),
           "Mach-O TLS basename depended on the __DATA segment", failures)
    expect(not is_tls("neverc::plugin::(anonymous namespace)::Table", ".bss"),
           "plain .bss misclassified as TLS", failures)

    # 11. Scope matching must survive a failed demangle: nm gives up on Mach-O
    # decorations and hands back the raw Itanium spelling.
    in_scope = mod.symbol_in_audited_scope
    expect(in_scope("neverc::plugin::(anonymous namespace)::Table"),
           "demangled plugin scope not matched", failures)
    expect(in_scope("__ZN6neverc7dyncode12_GLOBAL__N_19IncompatsE"),
           "mangled dyncode scope not matched", failures)
    for linker_name in (
        "linker::elf::RogueCache",
        "_ZN6linker3elf10RogueCacheE",
        "__ZN6linker3elf10RogueCacheE",
        "_ZZN6linker3elf3fooEvE10RogueCache",
        "__ZZNK6linker3elf3Foo3getEvE10RogueCache",
    ):
        expect(in_scope(linker_name),
               f"linker owner scope escaped: {linker_name}", failures)
    for wrapped_name in (
        "foo::linker::RogueCache",
        "llvm::detail::Holder<linker::elf::Task>::Callbacks",
        "std::__1::vector<linker::elf::InputFile *>",
        "llvm::detail::Holder<neverc::plugin::Task>::Callbacks",
        "_ZN3foo6linker10RogueCacheE",
        "__Z11takeLinkerPN6linker3elf3FooE",
        "(anonymous namespace)::Collision",
    ):
        expect(not in_scope(wrapped_name),
               f"type/reference substring misread as owner scope: {wrapped_name}",
               failures)
    expect(not in_scope("llvm::cl::(anonymous namespace)::Opt"),
           "unrelated namespace matched as in-scope", failures)

    linker_hits = mod.audit_symbols(
        [("linker::elf::RogueCache", "b", ".bss"),
         ("llvm::detail::Holder<linker::elf::Task>::Callbacks", "b", ".bss"),
         ("std::__1::vector<linker::elf::InputFile *>", "b", ".bss")],
        {"binary_symbols": []},
    )
    expect(len(linker_hits) == 1 and "RogueCache" in linker_hits[0],
           f"anchored linker audit scope was not exact: {linker_hits}", failures)

    # Global anonymous namespaces lose reliable translation-unit provenance in
    # the final nm name even when nested inside a broader named namespace. The
    # lexical layer must therefore retain the anonymous member instead of
    # assuming every named parent is covered by the binary owner allowlist.
    tu_source = (
        "int Bare;\n"
        "static int FileStatic;\n"
        "constexpr int Safe = 1;\n"
        "void external() { static int Local; }\n"
        "namespace { int Rogue; constexpr int SafeAnon = 2; "
        "void f() { if (true) { static int Nested; } } }\n"
        "namespace linker::elf { namespace { int Named; } }\n"
    )
    tu_hits = list(mod.translation_unit_internal_state(tu_source))
    tu_statements = [statement for _, _, statement in tu_hits]
    expect(len(tu_hits) == 6,
           f"unqualified source provenance had wrong findings: {tu_hits}",
           failures)
    for name in ("Bare", "FileStatic", "Local", "Rogue", "Nested", "Named"):
        expect(any(name in statement for statement in tu_statements),
               f"unqualified source state escaped: {name}: {tu_hits}",
               failures)
    for name in ("Safe", "SafeAnon"):
        expect(not any(name in statement for statement in tu_statements),
               f"read-only/named source state was misreported: {name}: "
               f"{tu_hits}", failures)

    # Release mode may delegate only an owner namespace that the artifact
    # layer actually recognises. An arbitrary namespace -- including its
    # nested anonymous namespace -- keeps exact source provenance. This is the
    # contract exercised when main() receives --build-dir.
    delegated_source = (
        "namespace surprise { int Surprise; namespace { int Nested; } }\n"
        "namespace neverc::plugin { int ArtifactCovered; }\n"
    )
    delegated_hits = list(mod.translation_unit_internal_state(
        delegated_source, named_namespace_provenance=False))
    delegated_statements = [statement for _, _, statement in delegated_hits]
    expect(any("Surprise" in statement
               for statement in delegated_statements),
           f"release-mode source audit delegated an unknown owner: "
           f"{delegated_hits}", failures)
    expect(any("Nested" in statement for statement in delegated_statements),
           f"release-mode source audit lost nested anonymous provenance: "
           f"{delegated_hits}", failures)
    expect(not any("ArtifactCovered" in statement
                   for statement in delegated_statements),
           f"known binary owner was not delegated: {delegated_hits}",
           failures)
    known_nested_source = (
        "namespace neverc::plugin { namespace { int ArtifactNested; "
        "void f() { static int ArtifactLocal; } } }\n")
    expect(not list(mod.translation_unit_internal_state(
                   known_nested_source,
                   named_namespace_provenance=False)),
           "ELF/Mach-O release source scan retained storage covered by an "
           "auditable binary owner", failures)
    pe_preserved = list(mod.translation_unit_internal_state(
        known_nested_source, named_namespace_provenance=False,
        preserve_function_locals=True))
    expect(len(pe_preserved) == 1 and
           "ArtifactLocal" in pe_preserved[0][2],
           f"PE release scan delegated an unassignable function-local: "
           f"{pe_preserved}", failures)

    # Exercise main(), not just the helper: release-mode delegation begins
    # only after the requested artifact scan proves an auditable table.
    old_contract_config = (mod.ROOT, mod.AUDIT_DIRS,
                           mod.EXTRA_AUDIT_FILES, mod.HEADER_AUDIT_DIRS)
    try:
        with tempfile.TemporaryDirectory() as tmp:
            contract_root = pathlib.Path(tmp)
            contract_source = contract_root / "neverc/lib/Plugin/state.cpp"
            contract_source.parent.mkdir(parents=True)
            contract_source.write_text(
                delegated_source, encoding="utf-8")
            mod.ROOT = contract_root
            mod.AUDIT_DIRS = ("neverc/lib/Plugin",)
            mod.EXTRA_AUDIT_FILES = ()
            mod.HEADER_AUDIT_DIRS = ()
            empty_manifest = {
                "entries": [], "header_constants": [],
                "source_objects": [], "binary_symbols": [],
            }
            for coverage, covered_expected in (("macho-thin", False),
                                                (None, True)):
                output = io.StringIO()
                with mock.patch.object(
                        mod, "load_allowlist", return_value=empty_manifest), \
                        mock.patch.object(mod, "scan_binary",
                                          return_value=coverage), \
                        mock.patch.object(
                            sys, "argv", [str(CHECKER), "--build-dir",
                                          str(contract_root), "--strict",
                                          "--json"]), \
                        contextlib.redirect_stdout(output):
                    mod.main()
                payload = json.loads(output.getvalue())
                has_covered = any(
                    "ArtifactCovered" in item
                    for item in payload["source_globals"])
                expect(has_covered is covered_expected,
                       "main() delegated source state without successful "
                       f"artifact coverage={coverage!r}: "
                       f"{payload['source_globals']}", failures)
                expect(any("Surprise" in item
                           for item in payload["source_globals"]) and
                       any("Nested" in item
                           for item in payload["source_globals"]),
                       "main() release contract lost unknown-owner source "
                       f"findings: {payload['source_globals']}", failures)
    finally:
        (mod.ROOT, mod.AUDIT_DIRS, mod.EXTRA_AUDIT_FILES,
         mod.HEADER_AUDIT_DIRS) = old_contract_config
    reviewed_source = {
        "source_objects": [{
            "file": "audit/state.cpp",
            "symbol": "Rogue",
            "declaration": "int Rogue",
            "definition_sha256": mod.object_definition_sha256("int Rogue;"),
            "owner": "fixture",
            "kind": "immutable fixture",
            "justification": "exact matching test",
        }],
    }
    expect(mod.source_object_allowlisted(
               "audit/state.cpp", "int Rogue;", reviewed_source),
           "exact reviewed source object was rejected", failures)
    expect(not mod.source_object_allowlisted(
               "audit/other.cpp", "int Rogue;", reviewed_source),
           "source-object manifest escaped its exact path", failures)
    expect(not mod.source_object_allowlisted(
               "audit/state.cpp", "int RogueShadow;", reviewed_source),
           "source-object manifest accepted a symbol prefix", failures)
    expect(not mod.source_object_allowlisted(
               "audit/state.cpp", "static MutableRecord Rogue{};",
               reviewed_source),
           "source-object manifest accepted a changed type/qualifier",
           failures)
    expect(not mod.source_object_allowlisted(
               "audit/state.cpp", "int Rogue = 1;", reviewed_source),
           "source-object manifest accepted a changed initializer",
           failures)
    expect(mod.object_definition_sha256(
               'static const char *Label = "alpha";') !=
           mod.object_definition_sha256(
               'static const char *Label = "beta";'),
           "definition digest erased string-literal initializer semantics",
           failures)
    expect(mod.object_definition_sha256(
               'static const char *Label = "alpha beta";') !=
           mod.object_definition_sha256(
               'static const char *Label = "alpha  beta";'),
           "definition digest normalized semantic whitespace inside a "
           "string initializer", failures)
    semantic_statements = [stmt for _, _, stmt in
                           mod.translation_unit_internal_state(
                               'namespace { MutableRecord Cache{"alpha"}; }')]
    expect(len(semantic_statements) == 1 and
           '"alpha"' in semantic_statements[0],
           f"source scanner erased initializer semantics before manifest "
           f"binding: {semantic_statements}", failures)

    # 12. Same source, both hosts, same verdict. ELF and Mach-O describe the
    # identical set of objects very differently -- section names vs. bare class
    # letters, one TLS symbol vs. a descriptor plus a `$tlv$init` storage
    # symbol -- and the gate once passed on one host while failing on the other.
    allowed = "neverc::plugin::(anonymous namespace)::PluginMachinePass::ID"
    rogue = "neverc::plugin::(anonymous namespace)::RogueCache"
    listing_allowlist = {
        "entries": [
            {"symbol": "ActivePhases"},
            {"symbol": "GateOwnership"},
        ],
        "binary_symbols": [{"symbol": allowed, "max_count": 1}],
    }
    elf_listing = [
        ("neverc::plugin::(anonymous namespace)::ActivePhases", "b", ".tbss"),
        ("neverc::plugin::(anonymous namespace)::GateOwnership", "b", ".tbss"),
        (allowed, "b", ".bss"),
        ("neverc::plugin::(anonymous namespace)::PassAPI", "d",
         ".data.rel.ro"),
        (rogue, "b", ".bss"),
    ]
    macho_listing = [
        ("neverc::plugin::(anonymous namespace)::ActivePhases", "s",
         "__DATA,__thread_vars"),
        ("__ZN6neverc6plugin12_GLOBAL__N_112ActivePhasesE$tlv$init", "s",
         "__DATA,__thread_bss"),
        ("neverc::plugin::(anonymous namespace)::GateOwnership", "s",
         "__DATA,__thread_vars"),
        ("__ZN6neverc6plugin12_GLOBAL__N_113GateOwnershipE$tlv$init", "s",
         "__DATA,__thread_bss"),
        (allowed, "s", "__DATA,__bss"),
        ("neverc::plugin::(anonymous namespace)::PassAPI", "s",
         "__DATA_CONST,__const"),
        (rogue, "S", "__DATA,__common"),
    ]
    elf_hits = mod.audit_symbols(
        elf_listing, listing_allowlist, {".data.rel.ro": True},
        {".tbss"})
    macho_hits = mod.audit_symbols(
        macho_listing, listing_allowlist, {"__DATA_CONST": True},
        {"__DATA,__thread_vars", "__DATA,__thread_bss"})
    expect(len(elf_hits) == 1 and rogue in elf_hits[0],
           f"ELF listing should report only the rogue global, got {elf_hits}",
           failures)
    expect(len(macho_hits) == 1 and rogue in macho_hits[0],
           f"Mach-O listing should report only the rogue global, "
           f"got {macho_hits}", failures)
    for sym_class, section in (
        ("V", ".bss"),
        ("v", ".data"),
        ("u", ".data.unique"),
        ("S", ".sdata"),
        ("s", ".sbss"),
        ("C", "*COM*"),
        ("c", "*COM*"),
        ("W", ".data"),
    ):
        hits = mod.audit_symbols([(rogue, sym_class, section)],
                                 {"binary_symbols": []})
        expect(bool(hits),
               f"ELF {sym_class}/{section} rogue escaped audit_symbols",
               failures)
    unknown_hits = mod.audit_symbols(
        [(rogue, "?", "")], {"binary_symbols": []})
    expect(bool(unknown_hits),
           "defined in-scope symbol with unknown class/section failed open",
           failures)
    for spoofed_name, spoofed_section in (
        ("neverc::plugin::Rogue$tlv$State", ".bss"),
        ("neverc::plugin::Rogue", ".tdata.fake"),
        ("neverc::plugin::Rogue", "__DATA,__thread_data"),
    ):
        spoofed_hits = mod.audit_symbols(
            [(spoofed_name, "b", spoofed_section)],
            {"entries": [], "binary_symbols": []})
        expect(bool(spoofed_hits),
               "ordinary writable symbol spoofed TLS classification: "
               f"{spoofed_name} {spoofed_section}", failures)

    # 12b. Binary allowlist entries are exact symbols, with only LLVM's
    # explicit internal-linkage promotion suffixes accepted. A substring gate
    # would silently allow a second writable global with a shadowed name.
    root_token = "neverc::time_trace_detail::LLVMTimeTraceRootGeneration"
    local_token = (
        "neverc::plugin::(anonymous namespace)::ExclusiveWaitEpoch")
    sentinel_allowlist = {
        "binary_symbols": [
            {"symbol": root_token, "max_count": 1},
            {"symbol": local_token, "max_count": 1},
        ],
    }
    root_allowlist = {
        "binary_symbols": [{"symbol": root_token}],
    }
    allow_binary = mod.allowlisted_binary_symbol
    expect(allow_binary(root_token, root_allowlist),
           "exact binary allowlist symbol rejected", failures)
    expect(allow_binary(root_token + ".llvm.123", root_allowlist),
           "LLVM-promoted binary symbol rejected", failures)
    expect(allow_binary(root_token + ".llvm.123.llvm.456", root_allowlist),
           "chained LLVM-promoted binary symbol rejected", failures)
    expect(allow_binary(root_token + " (.llvm.123)", root_allowlist),
           "demangled LLVM-promoted binary symbol rejected", failures)
    expect(allow_binary(root_token + " (.llvm.123.llvm.456)",
                        root_allowlist),
           "chained demangled LLVM-promoted binary symbol rejected",
           failures)
    expect(allow_binary(root_token + " [clone .llvm.123]", root_allowlist),
           "GNU-demangled LLVM-promoted binary symbol rejected", failures)
    for shadow in (
        root_token + "Shadow",
        "Prefix" + root_token,
        root_token + ".llvm.bad",
        root_token + ".llvm.123Shadow",
        root_token + " (.llvm.bad)",
        root_token + " (.llvm.123)Shadow",
        root_token + " (.llvm.123 extra)",
        root_token + " [clone .llvm.bad]",
        root_token + " [clone .llvm.123]Shadow",
        root_token + ".llvm.00",
        root_token + ".llvm.18446744073709551616",
        root_token + ".__uniq.123",
        root_token + ".__uniq.0123456789abcdef0123456789abcdef",
        root_token + "::Nested",
    ):
        expect(not allow_binary(shadow, root_allowlist),
               f"binary allowlist accepted shadow symbol: {shadow}",
               failures)

    promoted_elf = [(root_token + " (.llvm.123)", "b", ".bss")]
    promoted_macho = [(root_token + " (.llvm.123.llvm.456)", "b", "")]
    expect(not mod.audit_symbols(promoted_elf, root_allowlist),
           "ELF demangled LLVM promotion failed exact allowlist audit",
           failures)
    expect(not mod.audit_symbols(promoted_macho, root_allowlist),
           "Mach-O demangled LLVM promotion failed exact allowlist audit",
           failures)

    duplicate_allowed = [
        (root_token, "b", ".bss"),
        (root_token, "b", ".bss"),
    ]
    duplicate_hits = mod.audit_symbols(duplicate_allowed, root_allowlist)
    expect(len(duplicate_hits) == 1 and "appears 2 times" in duplicate_hits[0],
           f"duplicate allowlisted symbol bypassed multiplicity: "
           f"{duplicate_hits}", failures)
    required_binary = {
        "binary_symbols": [{
            "symbol": root_token,
            "min_count": 1,
            "max_count": 1,
            "owner": "fixture",
            "kind": "identity",
            "justification": "presence contract fixture",
        }],
    }
    missing_hits = mod.audit_symbols([], required_binary)
    expect(len(missing_hits) == 1 and "appears 0 times" in missing_hits[0],
           f"missing allowlisted symbol was treated as clean: {missing_hits}",
           failures)

    # 13. A thread_local belongs to exactly one list. Re-adding TLS to
    # `binary_symbols` would resurrect the split-verdict bug that 12 pins down.
    tls_symbols = {e.get("symbol") for e in allowlist.get("entries", [])}
    overlap = [e.get("symbol") for e in allowlist.get("binary_symbols", [])
               if any(t and t in e.get("symbol", "") for t in tls_symbols)]
    expect(not overlap,
           f"thread_local symbols duplicated into binary_symbols: {overlap}",
           failures)

    # 13b. Allowlist paths are matched independently of the host's path
    # separator. Windows produced `neverc\lib\Plugin\...`, which matched none of
    # the forward-slash entries, so every documented thread_local was reported
    # and the gate failed there while passing on Linux and macOS.
    documented = allowlist["entries"][0]
    posix_rel = documented["files"][0]
    windows_rel = posix_rel.replace("/", "\\")
    snippet = documented["declaration"] + ";"
    expect(mod.allowlisted(posix_rel, snippet, allowlist),
           "documented entry not matched via posix path", failures)
    expect(mod.allowlisted(windows_rel, snippet, allowlist),
           f"documented entry not matched via windows path {windows_rel}",
           failures)
    expect(not mod.allowlisted("neverc/lib/Plugin/Core/Undocumented.cpp",
                               snippet, allowlist),
           "undocumented file wrongly allowlisted", failures)
    grouped_snippet = (
        f"thread_local ScopedThreadLocalStack<Item, 4> "
        f"{documented['symbol']} = makeStack(1, 2);")
    grouped_allowlist = {"entries": [{
        "files": [posix_rel],
        "symbol": documented["symbol"],
        "declaration": mod.canonical_object_declaration(grouped_snippet),
        "definition_sha256": mod.object_definition_sha256(grouped_snippet),
    }]}
    expect(mod.allowlisted(posix_rel, grouped_snippet, grouped_allowlist),
           "single template declarator with grouped initializer rejected",
           failures)
    for shadow in (
        f"thread_local int {documented['symbol']}, RogueState;",
        f"thread_local int RogueState, {documented['symbol']};",
        f"thread_local int {documented['symbol']} = 0, RogueState = 1;",
        f"thread_local {documented['symbol']} RogueState;",
        f"thread_local int RogueState = {documented['symbol']};",
        f"thread_local int {documented['symbol']}Shadow;",
        f"thread_local Broken<Type, {documented['symbol']};",
    ):
        expect(not mod.allowlisted(posix_rel, shadow, allowlist),
               f"source allowlist accepted non-exact declaration: {shadow}",
               failures)

    # Every supported TLS spelling feeds the same declaration parser. Merely
    # detecting these spellings is not enough: an exact, documented exception
    # must also be able to name one, while a second declarator still fails
    # closed.
    for spelling in (
        "thread_local int TLSState;",
        "_Thread_local int TLSState;",
        "__thread int TLSState;",
        "__declspec(thread) int TLSState;",
        "LLVM_THREAD_LOCAL int TLSState;",
    ):
        exact_tls = {"entries": [{
            "files": ["audit/state.cpp"],
            "symbol": "TLSState",
            "declaration": mod.canonical_object_declaration(spelling),
            "definition_sha256": mod.object_definition_sha256(spelling),
        }]}
        expect(mod.allowlisted("audit/state.cpp", spelling, exact_tls),
               f"exact TLS spelling was not allowlisted: {spelling}",
               failures)
        expect(not mod.allowlisted(
                   "audit/state.cpp",
                   spelling[:-1] + ", TLSStateShadow;", exact_tls),
               f"TLS spelling accepted a shadow declarator: {spelling}",
               failures)

    # 13c. The Mach-O archive observer is a test-only, dynamically scoped TLS
    # facade. Pin its narrow exception so broadening the path/symbol match or
    # dropping its ownership and clearing contract cannot silently weaken the
    # release gate.
    observer_path = "neverc/lib/Linker/Backends/MachO/Input/InputFiles.cpp"
    observer_symbol = "ArchiveMemberParseObserver"
    observer_entries = [
        entry for entry in allowlist.get("entries", [])
        if entry.get("symbol") == observer_symbol
    ]
    expect(len(observer_entries) == 1,
           f"expected one {observer_symbol} allowlist entry, "
           f"got {len(observer_entries)}", failures)
    if len(observer_entries) == 1:
        observer = observer_entries[0]
        expect(observer.get("files") == [observer_path],
               f"{observer_symbol} has imprecise files: "
               f"{observer.get('files')}", failures)
        for field in ("declaration", "owner", "lifetime", "justification",
                      "cleared_by_test"):
            value = observer.get(field)
            expect(isinstance(value, str) and bool(value.strip()),
                   f"{observer_symbol} missing {field}", failures)
        observer_declaration = (
            "thread_local ArchiveMemberParseObserverState "
            "ArchiveMemberParseObserver;"
        )
        expect(mod.allowlisted(observer_path, observer_declaration, allowlist),
               f"{observer_symbol} exact declaration not allowlisted",
               failures)
        expect(not mod.allowlisted(
                   observer_path,
                   observer_declaration[:-1] + " = {};", allowlist),
               f"{observer_symbol} allowlist accepted changed initializer",
               failures)
        expect(mod.allowlisted(observer_path.replace("/", "\\"),
                               observer_declaration, allowlist),
               f"{observer_symbol} Windows path not allowlisted", failures)
        expect(not mod.allowlisted(
                   "neverc/lib/Linker/Backends/MachO/Input/Other.cpp",
                   observer_declaration, allowlist),
               f"{observer_symbol} allowlist escaped its exact file",
               failures)
        expect(not mod.allowlisted(
                   observer_path,
                   "thread_local int DifferentArchiveObserver;", allowlist),
               f"{observer_symbol} allowlist matched a different symbol",
               failures)
        expect(not mod.allowlisted(observer_path + ".shadow.cpp",
                                   observer_declaration, allowlist),
               f"{observer_symbol} allowlist matched a path prefix collision",
               failures)
        expect(not mod.allowlisted(
                   observer_path,
                   "thread_local ArchiveMemberParseObserverState "
                   "DifferentObserver;", allowlist),
               f"{observer_symbol} allowlist matched only its type name",
               failures)
        expect(not mod.allowlisted(
                   observer_path,
                   "thread_local int ArchiveMemberParseObserverShadow;",
                   allowlist),
               f"{observer_symbol} allowlist matched a symbol prefix",
               failures)
        expect(not mod.allowlisted(
                   observer_path,
                   "thread_local MutableTaskState ArchiveMemberParseObserver;",
                   allowlist),
               f"{observer_symbol} allowlist accepted a changed TLS type",
               failures)

        explicit_match = {
            "entries": [{
                "files": [observer_path],
                "match": "ArchiveObserver",
            }]
        }
        expect(not mod.allowlisted(
                   observer_path,
                   "thread_local int PrefixArchiveObserverSuffix;",
                   explicit_match),
               "substring-only source allowlist entry was accepted", failures)
        expect(not mod.allowlisted(
                   observer_path,
                   "thread_local int PrefixArchiveObserverSuffix, Rogue;",
                   explicit_match),
               "explicit match entry accepted multiple declarators", failures)
        missing_marker = {"entries": [{"files": [observer_path]}]}
        expect(not mod.allowlisted(observer_path, observer_declaration,
                                   missing_marker),
               "markerless allowlist entry accepted every declaration",
               failures)

    # 14. Locating the compiler must not depend on the host's executable
    # suffix: looking only for the extensionless name made Windows report a
    # missing binary, which the scan then treated as "nothing to do".
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "bin").mkdir()
        expect(mod.locate_compiler(root) is None,
               "empty build dir should locate no compiler", failures)
        win = root / "bin" / "neverc.exe"
        win.write_bytes(b"MZ")
        expect(mod.locate_compiler(root) == win,
               "neverc.exe not located", failures)
        posix = root / "bin" / "neverc"
        posix.write_bytes(b"\x7fELF")
        expect(mod.locate_compiler(root) == posix,
               "extensionless neverc not preferred once present", failures)

    # 15. A missing compiler is reported rather than skipped, so a mistyped
    # --build-dir cannot read as a clean run. It lands in `unscannable`, not
    # `binary`: an unbuilt tree is not a bad symbol.
    with tempfile.TemporaryDirectory() as tmp:
        report = mod.Report()
        with contextlib.redirect_stdout(io.StringIO()):
            mod.scan_binary(report, pathlib.Path(tmp), allowlist)
        expect(bool(report.unscannable),
               "missing compiler silently skipped the binary scan", failures)
        expect(not report.binary,
               "missing compiler misreported as a writable symbol", failures)
        expect(report.failed(strict=False),
               "missing compiler did not fail the gate", failures)

    # 16. A PE image must fail closed until the binary layer can inspect COFF
    # section characteristics and Microsoft symbol ownership. Source-only
    # scanning cannot prove that macro-generated writable symbols are absent.
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "bin").mkdir()
        (root / "bin" / "neverc.exe").write_bytes(b"MZ")
        report = mod.Report()
        with contextlib.redirect_stdout(io.StringIO()):
            mod.scan_binary(report, root, sentinel_allowlist)
        expect(not report.binary, "PE image wrongly reported as a finding",
               failures)
        expect(any("PE" in item for item in report.unscannable),
               "unsupported PE artifact did not fail closed", failures)
        expect(report.failed(strict=False),
               "unsupported PE artifact passed the gate", failures)

    # 16a. Binary format dispatch uses magic, not a filename guess. A fat
    # Mach-O needs per-slice sentinels; until the scanner groups architectures,
    # accepting an aggregate table would let one intact slice hide a stripped
    # one, so it fails closed.
    thin_macho_magics = (
        "feedface", "cefaedfe", "feedfacf", "cffaedfe",
    )
    fat_macho_magics = (
        "cafebabe", "bebafeca", "cafebabf", "bfbafeca",
    )
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        sample = root / "sample"
        for magic in thin_macho_magics:
            sample.write_bytes(bytes.fromhex(magic) + b"payload")
            expect(mod.binary_image_format(sample)[0] == "macho-thin",
                   f"thin Mach-O magic not recognised: {magic}", failures)
        for magic in fat_macho_magics:
            sample.write_bytes(bytes.fromhex(magic) + b"payload")
            expect(mod.binary_image_format(sample)[0] == "macho-fat",
                   f"fat Mach-O magic not recognised: {magic}", failures)
        sample.write_bytes(b"\x7fELFpayload")
        expect(mod.binary_image_format(sample)[0] == "elf",
               "ELF magic not recognised", failures)
        sample.write_bytes(b"MZpayload")
        expect(mod.binary_image_format(sample)[0] == "pe",
               "PE magic not recognised", failures)
        sample.write_bytes(b"x")
        expect(mod.binary_image_format(sample)[0] == "unknown",
               "short image did not fail format recognition", failures)
        missing_kind, missing_error = mod.binary_image_format(root / "missing")
        expect(missing_kind is None and bool(missing_error),
               "binary magic read failure was not reported", failures)

        (root / "bin").mkdir()
        (root / "bin" / "neverc").write_bytes(
            bytes.fromhex(fat_macho_magics[0]) + b"payload")
        report = mod.Report()
        mod.scan_binary(report, root, allowlist)
        expect(any("fat Mach-O" in item for item in report.unscannable),
               "fat Mach-O aggregate scan did not fail closed", failures)

    # 16b. nm output is a three-state result: a successfully read symbol table
    # may be empty, while execution failures and malformed/partial output must
    # never be mistaken for the same empty table.
    def process(args, returncode=0, stdout="", stderr=""):
        return subprocess.CompletedProcess(
            args, returncode, stdout=stdout, stderr=stderr)

    fake_binary = pathlib.Path("/tmp/neverc-global-state-self-test")

    darwin_statuses = (
        "weak private external",
        "private external",
        "weak external automatically hidden",
        "weak external",
        "external",
        "non-external (was a private external)",
        "non-external",
    )
    darwin_rows = "".join(
        f"{index + 1:016x} (__DATA,__common) {status} "
        f"neverc::plugin::State {index}\n"
        for index, status in enumerate(darwin_statuses)
    )
    darwin_rows += (
        "0000000000000010 (__DATA,__bss) [referenced dynamically] external "
        "[no dead strip] [symbol resolver] [alt entry] [cold func] [Thumb] "
        "neverc::plugin::operator|(Foo, Foo)\n"
        "0000000000000011 (absolute) external AbsoluteSymbol\n"
        "0000000000000012 (indirect) external IndirectSymbol\n"
        "0000000000000013 (common) non-external "
        "neverc::plugin::CommonState\n"
    )
    parsed_darwin = mod._parse_nm_darwin(darwin_rows)
    expect(parsed_darwin is not None and len(parsed_darwin) == 9,
           f"realistic Darwin rows were not parsed: {parsed_darwin}", failures)
    expect(parsed_darwin is not None and any(
               name.endswith("operator|(Foo, Foo)") and
               section == "__DATA,__bss"
               for name, _, section in parsed_darwin),
           "Darwin demangled name/flags were split", failures)
    expect(parsed_darwin is not None and any(
               name.endswith("CommonState") and sym_class == "s" and
               section == "__DATA,__common"
               for name, sym_class, section in parsed_darwin),
           "Darwin common symbol was not conservatively writable", failures)
    for malformed in (
        darwin_rows + "garbage output\n",
        "0000000000000001 (?,?) external BadSection\n",
        "0000000000000001 (__DATA,__bss) exported BadStatus\n",
    ):
        expect(mod._parse_nm_darwin(malformed) is None,
               f"malformed Darwin output was partially accepted: {malformed!r}",
               failures)

    raw_root = (
        "__ZN6neverc17time_trace_detail27LLVMTimeTraceRootGenerationE")
    raw_local = (
        "__ZN6neverc6plugin12_GLOBAL__N_118ExclusiveWaitEpochE")
    raw_rogue = "__ZN6neverc6plugin10RogueCacheE"
    raw_tls = "__ZN6neverc6plugin8TLSStateE$tlv$init"
    raw_macho = (
        f"0000000000000001 (__DATA,__bss) external {raw_root}\n"
        f"0000000000000002 (__DATA,__bss) non-external {raw_local}\n"
        f"0000000000000003 (__DATA,__common) external {raw_rogue}\n"
        f"0000000000000004 (__DATA,__thread_bss) non-external {raw_tls}\n"
    )
    demangled_macho = (
        f"{root_token}\n"
        "neverc::plugin::(anonymous namespace)::ExclusiveWaitEpoch\n"
        "neverc::plugin::RogueCache\n"
        f"{raw_tls}\n"
    )
    with mock.patch.object(mod.shutil, "which", side_effect=lambda name: (
            "/tools/llvm-cxxfilt" if name == "llvm-cxxfilt" else None)), \
            mock.patch.object(mod.subprocess, "run", side_effect=[
                process(["llvm-nm"], stdout=raw_macho),
                process(["llvm-cxxfilt"], stdout=demangled_macho),
            ]) as run:
        result = mod._nm_symbols("llvm-nm", fake_binary, mach_o=True)
    expect(result.status is mod.NmReadStatus.OK and len(result.symbols) == 4,
           f"Mach-O rows were not read/demangled: {result}", failures)
    expect(run.call_args_list[0].args[0] == [
               "llvm-nm", "-U", "--no-demangle", "--no-dyldinfo",
               "--format=darwin", str(fake_binary)],
           f"Mach-O modern nm argv lost hardening: "
           f"{run.call_args_list[0].args[0]}", failures)
    expect(run.call_args_list[1].args[0] == [
               "/tools/llvm-cxxfilt", "--strip-underscore"],
           "Mach-O names were not canonicalised with strip-underscore",
           failures)

    macho_fallback = [
        process(["nm"], returncode=1, stderr="unknown option"),
        process(["nm"], stdout=(
            "0000000000000001 (__DATA,__bss) external PlainCState\n")),
    ]
    with mock.patch.object(mod.subprocess, "run",
                           side_effect=macho_fallback) as run:
        result = mod._nm_symbols("nm", fake_binary, mach_o=True)
    expect(result.status is mod.NmReadStatus.OK and
           result.symbols == (("PlainCState", "S", "__DATA,__bss"),),
           f"classic Darwin fallback failed: {result}", failures)
    expect(run.call_args_list[1].args[0] == [
               "nm", "-U", "-m", str(fake_binary)],
           f"classic Darwin fallback argv changed: "
           f"{run.call_args_list[1].args[0]}", failures)

    with mock.patch.object(mod.shutil, "which", return_value=(
            "/tools/c++filt")), mock.patch.object(
                mod.subprocess, "run", side_effect=[
                    process(["llvm-nm"], stdout=raw_macho),
                    process(["c++filt"], stdout=f"{root_token}\n"),
                    process(["llvm-nm"], returncode=1,
                            stderr="classic failed"),
                ]):
        result = mod._nm_symbols("llvm-nm", fake_binary, mach_o=True)
    expect(result.status is mod.NmReadStatus.ERROR,
           "c++filt line-count mismatch was accepted", failures)

    with mock.patch.object(mod.subprocess, "run", return_value=process(
            ["llvm-nm"], stdout="")) as run:
        result = mod._nm_symbols("llvm-nm", fake_binary)
    expect(result.status is mod.NmReadStatus.OK and not result.symbols,
           "empty successful nm output was not a valid empty table", failures)
    expect(run.call_count == 1,
           "valid empty SysV table unnecessarily fell back to BSD", failures)

    with mock.patch.object(mod.subprocess, "run", return_value=process(
            ["llvm-nm"], stdout="", stderr=(
                "llvm-nm: error: unable to read object"))) as run:
        result = mod._nm_symbols("llvm-nm", fake_binary)
    expect(result.status is mod.NmReadStatus.ERROR,
           "rc=0 llvm-nm error diagnostic was accepted as success", failures)
    expect(run.call_count == 2,
           "SysV error diagnostic did not try the same tool's BSD mode",
           failures)

    sysv_header = (
        "Symbols from neverc:\n"
        "Name | Value | Class | Type | Size | Line | Section\n"
    )
    with mock.patch.object(mod.subprocess, "run", return_value=process(
            ["llvm-nm"], stdout=sysv_header)) as run:
        result = mod._nm_symbols("llvm-nm", fake_binary)
    expect(result.status is mod.NmReadStatus.OK and not result.symbols,
           "header-only SysV output was not a valid empty table", failures)
    expect(run.call_count == 1,
           "header-only SysV table unnecessarily fell back to BSD", failures)

    sysv_rows = (
        "Symbols from neverc:\n"
        "Name Value Class Type Size Line Section\n"
        "neverc::plugin::ElfState|0000000000000000| B | OBJECT |"
        "0000000000000008| |.bss\n"
        "neverc::plugin::MachState|0000000000000010| B | OBJECT |"
        "0000000000000008| |\n"
        "neverc::plugin::operator|(Foo, Foo)|0000000000000020| B |"
        " OBJECT |0000000000000008| |.bss\n"
    )
    with mock.patch.object(mod.subprocess, "run", return_value=process(
            ["llvm-nm"], stdout=sysv_rows)) as run:
        result = mod._nm_symbols("llvm-nm", fake_binary)
    expect(result.status is mod.NmReadStatus.OK and len(result.symbols) == 3,
           f"realistic SysV rows were not parsed: {result}", failures)
    expect(any(name.endswith("operator|(Foo, Foo)")
               for name, _, _ in result.symbols),
           "demangled operator| split the SysV columns", failures)
    expect(any(name.endswith("MachState") and not section
               for name, _, section in result.symbols),
           "Mach-O SysV row with an empty section was rejected", failures)
    expect(run.call_count == 1,
           "valid nonempty SysV rows unnecessarily fell back to BSD", failures)

    fallback = [
        process(["llvm-nm", "--format=sysv"], returncode=1,
                stderr="unsupported format"),
        process(["llvm-nm"], stdout=(
            "0000000000000000 B neverc::plugin::RogueCache\n")),
    ]
    with mock.patch.object(mod.subprocess, "run", side_effect=fallback) as run:
        result = mod._nm_symbols("llvm-nm", fake_binary)
    expect(result.status is mod.NmReadStatus.OK and
           result.symbols == (("neverc::plugin::RogueCache", "B", ""),),
           f"valid BSD fallback was not parsed: {result}", failures)
    expect(run.call_count == 2,
           "unsupported SysV format did not try BSD", failures)

    fallback = [
        process(["llvm-nm", "--format=sysv"],
                stdout="not a SysV symbol row\n"),
        process(["llvm-nm"], stdout="U external_only\n"),
    ]
    with mock.patch.object(mod.subprocess, "run", side_effect=fallback):
        result = mod._nm_symbols("llvm-nm", fake_binary)
    expect(result.status is mod.NmReadStatus.OK and
           result.symbols == (("external_only", "U", "*UND*"),),
           "two-field undefined BSD symbol was treated as malformed", failures)

    partial_nonzero = [
        process(["nm", "--format=sysv"], returncode=1,
                stderr="unsupported format"),
        process(["nm"], returncode=1,
                stdout="0000000000000000 B partial\n", stderr="read failed"),
    ]
    with mock.patch.object(mod.subprocess, "run",
                           side_effect=partial_nonzero):
        result = mod._nm_symbols("nm", fake_binary)
    expect(result.status is mod.NmReadStatus.ERROR,
           "nonzero nm output was accepted as a complete symbol table",
           failures)

    mixed_malformed = [
        process(["nm", "--format=sysv"], returncode=1,
                stderr="unsupported format"),
        process(["nm"], stdout=(
            "0000000000000000 B neverc::plugin::RogueCache\n"
            "garbage output\n")),
    ]
    with mock.patch.object(mod.subprocess, "run",
                           side_effect=mixed_malformed):
        result = mod._nm_symbols("nm", fake_binary)
    expect(result.status is mod.NmReadStatus.ERROR,
           "mixed valid and malformed nm output was partially accepted",
           failures)

    malformed_two_field_defined = [
        process(["nm", "--format=sysv"], returncode=1,
                stderr="unsupported format"),
        process(["nm"], stdout="B neverc::plugin::RogueCache\n"),
    ]
    with mock.patch.object(mod.subprocess, "run",
                           side_effect=malformed_two_field_defined):
        result = mod._nm_symbols("nm", fake_binary)
    expect(result.status is mod.NmReadStatus.ERROR,
           "defined BSD symbol without an address was accepted", failures)

    # 16c. A broken preferred tool falls back to every other installed,
    # de-duplicated candidate. Exhausting installed tools or having no reader
    # for an explicitly requested binary scan is a hard error.
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "bin").mkdir()
        (root / "bin" / "neverc").write_bytes(b"\x7fELF")

        paths = {"llvm-nm": "/tools/llvm-nm", "nm": "/tools/nm"}
        results = [
            mod.NmReadResult(mod.NmReadStatus.ERROR,
                             diagnostics=("llvm-nm failed",)),
            mod.NmReadResult(mod.NmReadStatus.OK,
                             symbols=((root_token, "B", ".bss"),
                                      (local_token, "b", ".bss"))),
        ]
        report = mod.Report()
        with mock.patch.object(mod.shutil, "which",
                               side_effect=lambda name: paths.get(name)), \
                mock.patch.object(mod, "_nm_symbols",
                                  side_effect=results) as read:
            mod.scan_binary(report, root, sentinel_allowlist)
        expect(read.call_count == 2,
               "broken llvm-nm prevented fallback to installed nm", failures)
        expect(not report.unscannable and not report.failed(strict=False),
               f"successful secondary nm still failed the gate: {report}",
               failures)

        report = mod.Report()
        empty_then_auditable = [
            mod.NmReadResult(mod.NmReadStatus.OK),
            mod.NmReadResult(mod.NmReadStatus.OK,
                             symbols=((root_token + " (.llvm.123)",
                                       "B", ".bss"),
                                      (local_token, "b", ".bss"))),
        ]
        with mock.patch.object(mod.shutil, "which",
                               side_effect=lambda name: paths.get(name)), \
                mock.patch.object(mod, "_nm_symbols",
                                  side_effect=empty_then_auditable) as read:
            mod.scan_binary(report, root, sentinel_allowlist)
        expect(read.call_count == 2,
               "empty symbol table prevented trying another installed nm",
               failures)
        expect(not report.unscannable and not report.failed(strict=False),
               f"auditable fallback after empty table failed: {report}",
               failures)

        report = mod.Report()
        with mock.patch.object(mod.shutil, "which",
                               side_effect=lambda name: paths.get(name)), \
                mock.patch.object(mod, "_nm_symbols",
                                  side_effect=[
                                      mod.NmReadResult(mod.NmReadStatus.OK),
                                      mod.NmReadResult(mod.NmReadStatus.OK),
                                  ]):
            mod.scan_binary(report, root, sentinel_allowlist)
        expect(bool(report.unscannable),
               "all-empty installed nm tables were accepted as audited",
               failures)
        expect(report.failed(strict=False),
               "all-empty installed nm tables did not fail the gate", failures)

        report = mod.Report()
        unrelated_tables = [
            mod.NmReadResult(mod.NmReadStatus.OK,
                             symbols=(("main", "T", ".text"),)),
            mod.NmReadResult(mod.NmReadStatus.OK,
                             symbols=(("_main", "T", ""),)),
        ]
        with mock.patch.object(mod.shutil, "which",
                               side_effect=lambda name: paths.get(name)), \
                mock.patch.object(mod, "_nm_symbols",
                                  side_effect=unrelated_tables):
            mod.scan_binary(report, root, sentinel_allowlist)
        expect(bool(report.unscannable),
               "nonempty tables without the audit sentinel were trusted",
               failures)

        report = mod.Report()
        sentinel_and_rogue = mod.NmReadResult(
            mod.NmReadStatus.OK,
            symbols=((root_token, "B", ".bss"),
                     (local_token, "b", ".bss"),
                     ("neverc::plugin::RogueCache", "B", ".bss")),
        )
        with mock.patch.object(mod.shutil, "which",
                               side_effect=lambda name: paths.get(name)), \
                mock.patch.object(mod, "_nm_symbols",
                                  return_value=sentinel_and_rogue):
            mod.scan_binary(report, root, sentinel_allowlist)
        expect(any("RogueCache" in item for item in report.binary),
               "audit sentinel caused a real writable finding to be skipped",
               failures)

        expect(not mod.symbol_table_is_auditable(
                   ((root_token, "B", ".bss"),)),
               "external-only root sentinel trusted a stripped local table",
               failures)
        expect(not mod.symbol_table_is_auditable(
                   ((root_token, "B", ".bss"),
                    (local_token, "s", "__DATA_CONST,__const")),
                   {"__DATA_CONST": True}),
               "read-only local sentinel was accepted", failures)
        expect(not mod.symbol_table_is_auditable(
                   ((root_token, "S", "__DATA,__bss"),
                    (local_token, "S", "__DATA,__bss"))),
               "external local-sentinel spelling was accepted", failures)
        expect(mod.symbol_table_is_auditable(
                   ((root_token, "S", "__DATA,__bss"),
                    (local_token, "s", "__DATA,__bss"))),
               "dual Mach-O static-table sentinels were rejected", failures)

        report = mod.Report()
        failures_by_tool = [
            mod.NmReadResult(mod.NmReadStatus.ERROR,
                             diagnostics=("llvm-nm failed",)),
            mod.NmReadResult(mod.NmReadStatus.ERROR,
                             diagnostics=("nm failed",)),
        ]
        with mock.patch.object(mod.shutil, "which",
                               side_effect=lambda name: paths.get(name)), \
                mock.patch.object(mod, "_nm_symbols",
                                  side_effect=failures_by_tool):
            mod.scan_binary(report, root, sentinel_allowlist)
        expect(bool(report.unscannable),
               "all installed nm failures were silently skipped", failures)
        expect(report.failed(strict=False),
               "all installed nm failures did not fail the gate", failures)

        report = mod.Report()
        with mock.patch.object(mod.shutil, "which", return_value=None):
            mod.scan_binary(report, root, sentinel_allowlist)
        expect(bool(report.unscannable) and report.failed(strict=False),
               "an unavailable nm toolchain passed a requested binary scan",
               failures)
        expect(any("unavailable" in item for item in report.unscannable),
               "missing nm toolchain did not explain the failure", failures)

    # Thin Mach-O dispatch must use the Darwin reader, demand both sentinels,
    # and still audit a writable S-class symbol by its section.
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "bin").mkdir()
        binary = root / "bin" / "neverc"
        binary.write_bytes(bytes.fromhex("cffaedfe") + b"payload")
        macho_result = mod.NmReadResult(
            mod.NmReadStatus.OK,
            symbols=((root_token, "S", "__DATA,__bss"),
                     (local_token, "s", "__DATA,__bss"),
                     ("neverc::plugin::RogueCache", "S",
                      "__DATA,__common")),
        )
        with mock.patch.object(mod.shutil, "which", side_effect=lambda name: (
                "/tools/llvm-nm" if name == "llvm-nm" else None)), \
                mock.patch.object(mod, "_nm_symbols",
                                  return_value=macho_result) as read:
            report = mod.Report()
            mod.scan_binary(report, root, sentinel_allowlist)
        read.assert_called_once_with("/tools/llvm-nm", binary, mach_o=True)
        expect(any("RogueCache" in item for item in report.binary),
               "Mach-O writable S-class rogue escaped the binary audit",
               failures)

    # 16d. Source coverage is fail-closed too: TLS storage specifiers may
    # follow any legal declaration modifier, forbidden qualified names may be
    # split by whitespace, and a configured audit root may not disappear
    # silently after a move or typo.
    old_root = mod.ROOT
    old_audit_dirs = mod.AUDIT_DIRS
    old_extra_files = mod.EXTRA_AUDIT_FILES
    old_header_dirs = mod.HEADER_AUDIT_DIRS
    try:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            audit = root / "audit"
            audit.mkdir()
            source = audit / "state.cpp"
            source.write_text(
                "inline thread_local int InlineState;\n"
                "extern thread_local int ExternState;\n"
                "constinit thread_local int ConstinitState;\n"
                "alignas(64) thread_local int AlignedState;\n"
                "[[gnu::used]] thread_local int AttributedState;\n"
                "__attribute__((used)) thread_local int GNUState;\n"
                "inline/**/thread_local int CommentSeparatedState;\n"
                "_Thread_local int C11State;\n"
                "__thread int GNUThreadState;\n"
                "__declspec(thread) int MSVCThreadState;\n"
                "LLVM_THREAD_LOCAL int LLVMThreadState;\n"
                "__declspec(\nthread\n) int MultilineMSVCState;\n"
                "__declspec(\\\nthread) int SplicedMSVCState;\n"
                "thread_\\\nlocal int SplicedStandardState;\n"
                "// inline thread_local int CommentState;\n"
                "const char *Text = \"thread_local int StringState\";\n"
                "auto Strategy = parallel\n :: \nstrategy;\n"
                "auto Similar = parallel::strategyCache;\n"
                "auto ThroughComment = parallel/* gap */::strategy;\n"
                "static CommonLinkerContext\n*\nlctx = nullptr;\n"
                "static/**/CommonLinkerContext *lctx = nullptr;\n"
                "static CommonLinkerContext *lctx2 = nullptr;\n",
                encoding="utf-8",
            )
            present = root / "present.cpp"
            present.write_text("int Present;\n", encoding="utf-8")
            (root / "present-headers").mkdir()

            mod.ROOT = root
            mod.AUDIT_DIRS = ("audit", "missing-dir")
            mod.EXTRA_AUDIT_FILES = ("present.cpp", "missing.cpp")
            report = mod.Report()
            mod.scan_sources(report, {"entries": []})
            expect(len(report.heuristic) == 14,
                   f"TLS modifiers escaped or literals leaked: "
                   f"{report.heuristic}", failures)
            expect(len(report.forbidden) == 4,
                   f"whitespace-split forbidden names escaped: "
                   f"{report.forbidden}", failures)
            expect(any("missing-dir" in item for item in report.unscannable),
                   "missing source audit directory was silently skipped",
                   failures)
            expect(any("missing.cpp" in item for item in report.unscannable),
                   "missing exact audit file was silently skipped", failures)
            expect(report.failed(strict=False),
                   "missing configured source paths did not fail the gate",
                   failures)

            exact = {"entries": [{
                "files": ["audit/state.cpp"],
                "symbol": "InlineState",
                "declaration": "inline thread_local int InlineState",
                "definition_sha256": mod.object_definition_sha256(
                    "inline thread_local int InlineState;"),
            }]}
            expect(mod.allowlisted("audit/state.cpp",
                                   "inline thread_local int InlineState;",
                                   exact),
                   "modifier-prefixed exact TLS declaration was not allowlisted",
                   failures)

            mod.HEADER_AUDIT_DIRS = (
                "present-headers", "missing-headers", "missing.h")
            report = mod.Report()
            mod.scan_headers(report, {"entries": []})
            expect(any("missing-headers" in item
                       for item in report.unscannable),
                   "missing header audit directory was silently skipped",
                   failures)
            expect(any("missing.h" in item for item in report.unscannable),
                   "missing exact header was silently skipped", failures)
            expect(report.failed(strict=False),
                   "missing configured header paths did not fail the gate",
                   failures)
    finally:
        mod.ROOT = old_root
        mod.AUDIT_DIRS = old_audit_dirs
        mod.EXTRA_AUDIT_FILES = old_extra_files
        mod.HEADER_AUDIT_DIRS = old_header_dirs

    # 16e. Manifest entries are live contracts, not comments: every exact path
    # must be in the configured audit closure and must still contain exactly one
    # matching declaration with complete review metadata.
    old_root = mod.ROOT
    old_audit_dirs = mod.AUDIT_DIRS
    old_extra_files = mod.EXTRA_AUDIT_FILES
    old_header_dirs = mod.HEADER_AUDIT_DIRS
    try:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "audit").mkdir()
            (root / "headers").mkdir()
            (root / "audit" / "state.cpp").write_text(
                "thread_local int ScopedState;\n"
                "int ProcessToken;\n", encoding="utf-8")
            (root / "headers" / "constants.h").write_text(
                "namespace x { struct C { int Value; }; "
                "static constexpr C Table{1}; }\n", encoding="utf-8")
            mod.ROOT = root
            mod.AUDIT_DIRS = ("audit",)
            mod.EXTRA_AUDIT_FILES = ()
            mod.HEADER_AUDIT_DIRS = ("headers",)
            fixture_allowlist = {
                "entries": [{
                    "files": ["audit/state.cpp"],
                    "symbol": "ScopedState",
                    "declaration": "thread_local int ScopedState",
                    "definition_sha256": mod.object_definition_sha256(
                        "thread_local int ScopedState;"),
                    "owner": "fixture",
                    "lifetime": "one lexical scope",
                    "justification": "fixture depth only",
                    "cleared_by_test": "this test",
                }],
                "header_constants": [{
                    "file": "headers/constants.h",
                    "symbol": "Table",
                    "declaration": "static constexpr C Table",
                    "definition_sha256": mod.object_definition_sha256(
                        "static constexpr C Table{1};"),
                    "owner": "fixture",
                    "justification": "immutable fixture record",
                }],
                "source_objects": [{
                    "file": "audit/state.cpp",
                    "symbol": "ProcessToken",
                    "declaration": "int ProcessToken",
                    "definition_sha256": mod.object_definition_sha256(
                        "int ProcessToken;"),
                    "owner": "fixture",
                    "kind": "identity token",
                    "justification": "manifest validation fixture",
                }],
                "binary_symbols": [{
                    "symbol": "neverc::plugin::Fixture",
                    "min_count": 1,
                    "max_count": 1,
                    "file": "audit/state.cpp",
                    "source_symbol": "ProcessToken",
                    "declaration": "int ProcessToken",
                    "definition_sha256": mod.object_definition_sha256(
                        "int ProcessToken;"),
                    "owner": "fixture",
                    "kind": "test identity",
                    "justification": "fixture only",
                }],
            }
            manifest_report = mod.Report()
            mod.validate_allowlist(manifest_report, fixture_allowlist)
            expect(not manifest_report.unscannable,
                   f"valid manifest was rejected: "
                   f"{manifest_report.unscannable}", failures)

            dead = {**fixture_allowlist, "entries": [{
                **fixture_allowlist["entries"][0],
                "files": ["outside/state.cpp"],
            }]}
            dead_report = mod.Report()
            mod.validate_allowlist(dead_report, dead)
            expect(any("outside source audit scope" in item
                       for item in dead_report.unscannable),
                   f"dead TLS manifest path passed: {dead_report.unscannable}",
                   failures)

            weakened = {**fixture_allowlist, "binary_symbols": [{
                **fixture_allowlist["binary_symbols"][0],
                "min_count": 0,
                "max_count": 999,
            }]}
            weakened_report = mod.Report()
            mod.validate_allowlist(weakened_report, weakened)
            expect(any("multiplicity" in item
                       for item in weakened_report.unscannable),
                   "production validator accepted a weakened binary "
                   f"multiplicity contract: {weakened_report.unscannable}",
                   failures)
            optional_external = {
                **fixture_allowlist,
                "binary_symbols": [{
                    **fixture_allowlist["binary_symbols"][0],
                    "min_count": 0,
                    "artifact_optional": True,
                }],
            }
            optional_external_report = mod.Report()
            mod.validate_allowlist(optional_external_report,
                                   optional_external)
            expect(any("function-local" in item
                       for item in optional_external_report.unscannable),
                   "artifact_optional weakened a namespace-scope binary "
                   f"contract: {optional_external_report.unscannable}",
                   failures)
    finally:
        mod.ROOT = old_root
        mod.AUDIT_DIRS = old_audit_dirs
        mod.EXTRA_AUDIT_FILES = old_extra_files
        mod.HEADER_AUDIT_DIRS = old_header_dirs

    # 16f. Compiler-generated names have a narrow ABI grammar. Substrings such
    # as VTableCache or typeinfoRegistry remain user state and must reach the
    # writable-symbol audit.
    for compiler_name in (
        "vtable for neverc::plugin::Widget",
        "construction vtable for neverc::plugin::Widget-in-Base",
        "typeinfo for neverc::plugin::Widget",
        "typeinfo name for neverc::plugin::Widget",
        "guard variable for neverc::plugin::getWidget()::Value",
        "_ZTVN6neverc6plugin6WidgetE",
        "__ZTIN6neverc6plugin6WidgetE",
    ):
        expect(mod.symbol_is_compiler_noise(compiler_name),
               f"canonical compiler symbol no longer filtered: {compiler_name}",
               failures)
    for user_name in (
        "neverc::plugin::VTableCache",
        "neverc::plugin::typeinfoRegistry",
        "neverc::plugin::guard variable cache",
        "neverc::plugin::prefix_ZTVN6neverc6plugin6WidgetE",
        "neverc::plugin::Widget::dummy_",
        "neverc::plugin::Widget::test_info_",
        "neverc::plugin::dummy_cache",
        "neverc::plugin::test_info_cache",
    ):
        hits = mod.audit_symbols([(user_name, "b", ".bss")], {"entries": []})
        expect(bool(hits),
               f"user writable symbol misclassified as noise: {user_name}",
               failures)

    # 17. C++14 digit separators. `0x3b00'0000` is one number; reading that
    # apostrophe as a character literal makes the scanner run to the next quote
    # and drop everything between -- a forbidden symbol there goes unseen.
    # ARM64Common.h already spells constants this way, so an odd quote count is
    # one edit away.
    token = "getGlobalPluginLoader"
    separated = ("constexpr uint64_t Mask = 0x1000'0000;\n"
                 "char Sep = ',';\n"
                 f"void f() {{ {token}(); }}\n")
    expect(token in strip(separated),
           "digit separator swallowed a forbidden symbol", failures)
    expect(mod.is_digit_separator("0x10'00", 4),
           "digit separator not recognised", failures)
    expect(not mod.is_digit_separator("u8'x'", 2),
           "u8 prefix misread as a digit separator", failures)
    expect(not mod.is_digit_separator("L'x'", 1),
           "L prefix misread as a digit separator", failures)
    expect(token in strip(f"char c = '\\''; {token}();"),
           "escaped quote in a char literal swallowed real code", failures)
    expect("x" not in strip("char c = 'x';"),
           "character literal no longer stripped", failures)

    # 18. Raw strings ignore escapes and may embed quotes, so the ordinary
    # literal scanner ends one early and then reads its contents as code.
    raw = ('const char *s = R"(say "hi" // not a comment)";\n'
           f"void f() {{ {token}(); }}\n")
    stripped = strip(raw)
    expect("say" not in stripped, "raw string contents leaked as code",
           failures)
    expect(token in stripped, "raw string swallowed the code after it",
           failures)
    expect(token not in strip(f'auto s = R"({token})";'),
           "forbidden symbol inside a raw string counted as code", failures)
    expect(token in strip(f'auto x = FOOR"notraw"; {token}();'),
           "R preceded by an identifier char misread as a raw string", failures)
    expect(token in strip(f'auto j = R"json({{"a":1}})json"; {token}();'),
           "custom raw delimiter mishandled", failures)
    multiline = 'a\nauto x = R"(\nmulti\n)";\nb\n'
    expect(strip(multiline).count("\n") == multiline.count("\n"),
           "multiline raw string changed the line count", failures)

    # 19. A backslash-newline inside a literal is a line continuation. Skipping
    # it as a plain two-character escape drops the newline, and every line
    # reported after that one is off by one.
    continued = 'int a;\nconst char *s = "abc\\\ndef";\nint b;\n'
    expect(strip(continued).count("\n") == continued.count("\n"),
           "line continuation inside a literal shifted the line numbers",
           failures)

    # 20. Header internal-linkage scan. This is the layer the binary scan
    # cannot cover: such objects demangle to a bare `(anonymous namespace)::X`
    # with no `neverc::` scope to match, and two copies of one object look
    # exactly like two same-named objects from different .cpp files.
    def kinds(source: str) -> list[str]:
        return [kind for _, kind, _ in mod.header_internal_state(source)]

    # `static` at namespace scope in a header: one object per includer.
    expect(kinds("namespace llvm {\nstatic bool Flag = false;\n}\n")
           == ["file-scope static"],
           "namespace-scope static in a header not reported", failures)

    # `inline static` reads as "one per program" but `static` wins, so it is
    # the same bug wearing a disguise -- this is how every real instance in
    # this tree was spelled.
    expect(kinds("namespace llvm {\ninline static bool Flag = false;\n}\n")
           == ["file-scope static"],
           "`inline static` not reported", failures)

    # The same declaration without `static` is correct and must stay silent.
    expect(kinds("namespace llvm {\ninline bool Flag = false;\n}\n") == [],
           "correct inline variable wrongly reported", failures)

    # An anonymous namespace in a header has the opposite meaning it has in the
    # .cpp this code was ported from.
    expect(kinds("namespace {\nstruct G { int x; };\n"
                 "inline G &globals() { static G g; return g; }\n}\n")
           == ["function-local static of an unmergeable function"],
           "anonymous-namespace function-local static not reported", failures)

    # The same helper in a named namespace is one object program-wide.
    expect(kinds("namespace llvm::detail {\nstruct G { int x; };\n"
                 "inline G &globals() { static G g; return g; }\n}\n") == [],
           "named-namespace function-local static wrongly reported", failures)

    # Builtin constant data is exempt. Complex constexpr objects are checked
    # against an exact header-constant manifest because a constexpr record can
    # still contain a writable `mutable` subobject.
    expect(kinds("namespace llvm {\nstatic constexpr int N = 4;\n"
                 "static const char Env[] = \"X\";\n"
                 "static constexpr char const *Name = \"X\";\n}\n")
           == ["file-scope static"],
           "complex constexpr pointer bypassed manifest review", failures)

    # In `const char *Msg` the const qualifies the characters, not the
    # pointer, so the pointer is still assignable -- and this is exactly how
    # the real BugReportMsg, which has a setter, was spelled.
    expect(kinds('namespace llvm {\ninline static const char *Msg = "x";\n}\n')
           == ["file-scope static"],
           "mutable pointer to const wrongly treated as read-only", failures)
    expect(kinds('namespace llvm {\ninline static char *const P = nullptr;\n}\n')
           == ["file-scope static"],
           "const pointer escaped the positive-proof read-only policy",
           failures)

    # Only constexpr objects and const builtin scalar/array data receive a
    # source-level read-only exemption. Const records, templates, auto,
    # pointers and references need object semantics that this scanner cannot
    # prove, so they fail closed.
    expect(kinds("namespace llvm {\n"
                 "static int const Count = 1;\n"
                 "static const bool Enabled = true;\n"
                 "static const unsigned long Mask = 1;\n"
                 "static const std::byte Bytes[2] = {};\n"
                 "}\n") == [],
           "const builtin scalar or array data wrongly reported", failures)
    for declaration in (
        "static int const Count",
        "static const bool Enabled",
        "static const unsigned long Mask",
        "static const std::byte Bytes[2]",
        "static const std::uint64_t Words[2]",
    ):
        expect(mod.is_read_only(declaration),
               f"builtin read-only proof rejected: {declaration}", failures)
    for declaration in (
        "static const Foo Config",
        "static const std::unique_ptr<Foo> Owner",
        "static const auto Inferred",
        "static const char *Message",
        "static char *const Pointer",
        "static const Foo &Alias",
    ):
        expect(not mod.is_read_only(declaration),
               f"unproven read-only declaration accepted: {declaration}",
               failures)
    conservative_const = kinds(
        "namespace llvm {\n"
        "static const Foo Config;\n"
        "static const std::unique_ptr<Foo> Owner;\n"
        "static const auto Inferred = makeFoo();\n"
        "static const Foo &Alias = getFoo();\n"
        "}\n")
    expect(len(conservative_const) == 4,
           f"unproven const objects escaped: {conservative_const}", failures)

    # A call in a copy initializer belongs to the object, while a default
    # argument belongs to a function declaration. The old blanket `(` check
    # silently skipped both.
    expect(kinds("namespace llvm {\n"
                 "static Registry Cache = makeRegistry();\n"
                 "}\n") == ["file-scope static"],
           "copy-initialized static object escaped as a function", failures)
    expect(kinds("namespace llvm {\n"
                 "static void fn(int x = makeDefault());\n"
                 "}\n") == [],
           "function default argument misread as an object", failures)

    expect(kinds("namespace llvm {\n"
                 "static int CppAttr [[maybe_unused]] = 0;\n"
                 "static int GNUAttr __attribute__((used)) = 0;\n"
                 "}\n") == ["file-scope static", "file-scope static"],
           "post-declarator attributes hid static objects", failures)

    expect(kinds("namespace llvm {\n"
                 "template <typename T> static int Variable = 0;\n"
                 "template <typename T> static void function(T);\n"
                 "}\n") == ["file-scope static"],
           "variable template escaped or function template was misread",
           failures)
    expect(kinds("namespace {\n"
                 "template <typename T> inline int Variable = 0;\n"
                 "}\n") == ["anonymous-namespace object"],
           "anonymous-namespace variable template escaped", failures)

    expect(kinds("extern \"C\" { static int CState = 0; }\n")
           == ["file-scope static"],
           "extern-C block hid a static object", failures)
    expect(kinds("namespace { struct X { inline static int Cache = 0; }; }\n")
           == ["static data member of anonymous-namespace type"],
           "anonymous-namespace type's static member escaped", failures)

    expect(kinds("namespace llvm {\n"
                 "struct MutableRecord { mutable int Value; };\n"
                 "static constexpr MutableRecord Cache{};\n"
                 "}\n") == ["file-scope static"],
           "constexpr record with mutable state was treated as read-only",
           failures)

    reviewed_constant = {
        "header_constants": [{
            "file": "audit/constants.h",
            "symbol": "Cache",
            "declaration": "static constexpr MutableRecord Cache",
            "definition_sha256": mod.object_definition_sha256(
                "static constexpr MutableRecord Cache{};"),
            "owner": "test owner",
            "justification": "reviewed immutable record",
        }],
    }
    constant_decl = "static constexpr MutableRecord Cache{};"
    expect(mod.header_constant_allowlisted(
               "audit/constants.h", constant_decl, reviewed_constant),
           "exact reviewed header constant was rejected", failures)
    expect(mod.header_constant_allowlisted(
               "audit\\constants.h", constant_decl, reviewed_constant),
           "Windows header-constant path was rejected", failures)
    expect(not mod.header_constant_allowlisted(
               "audit/other.h", constant_decl, reviewed_constant),
           "header-constant manifest escaped its exact path", failures)
    expect(not mod.header_constant_allowlisted(
               "audit/constants.h",
               "static constexpr MutableRecord CacheShadow{};",
               reviewed_constant),
           "header-constant manifest accepted a symbol prefix", failures)
    expect(not mod.header_constant_allowlisted(
               "audit/constants.h", "static MutableRecord Cache{};",
               reviewed_constant),
           "header-constant manifest accepted changed mutability", failures)
    expect(not mod.header_constant_allowlisted(
               "audit/constants.h",
               "static constexpr MutableRecord Cache{7};",
               reviewed_constant),
           "header-constant manifest accepted a changed initializer",
           failures)
    incomplete_constant = {
        "header_constants": [{
            "file": "audit/constants.h",
            "symbol": "Cache",
        }],
    }
    expect(not mod.header_constant_allowlisted(
               "audit/constants.h", constant_decl, incomplete_constant),
           "header constant without review metadata was accepted", failures)

    # Function-pointer variables are objects even though their declarator ends
    # in a function signature. Parameters and function-returning-function-
    # pointer declarations remain functions.
    pointer_objects = kinds(
        "namespace llvm {\n"
        "static void (*Hook)(int) = nullptr;\n"
        "static void (NEVERC_CALL *Hook2)(int) = nullptr;\n"
        "static void (*Hooks[2])(int);\n"
        "}\n")
    expect(pointer_objects == ["file-scope static"] * 3,
           f"function-pointer objects escaped: {pointer_objects}", failures)
    expect(kinds("namespace llvm {\n"
                 "static void install(void (*Hook)(int));\n"
                 "static void (*factory())(int);\n"
                 "}\n") == [],
           "function declaration misread as a function-pointer object",
           failures)
    expect(kinds("namespace llvm {\n"
                 "struct Registry { explicit Registry(int); int Value; };\n"
                 "static Registry Cache(42);\n"
                 "}\n") == ["file-scope static"],
           "direct-initialized namespace object escaped", failures)
    direct_objects = kinds(
        "namespace llvm {\n"
        "struct Registry { Registry(const char *); Registry(double); };\n"
        "static Registry StringCache(\"x\");\n"
        "static Registry FloatCache(1.5);\n"
        "static int (ParenthesizedState);\n"
        "}\n")
    expect(direct_objects == ["file-scope static"] * 3,
           f"legal direct/parenthesized object escaped: {direct_objects}",
           failures)
    expect(kinds("namespace llvm {\n"
                 "struct Factory {}; struct Registry {};\n"
                 "static Registry Vexing(Factory());\n"
                 "static void f() noexcept(false);\n"
                 "}\n") == [],
           "most-vexing/noexcept function declaration misread as an object",
           failures)
    expect(kinds("namespace llvm {\n"
                 "static void deleted() noexcept = delete;\n"
                 "static auto deletedTrailing() noexcept -> int = delete;\n"
                 "}\n") == [],
           "deleted noexcept function declaration misread as an object",
           failures)
    defaulted_tu = list(mod.translation_unit_internal_state(
        "namespace llvm { struct T { T(); T(T &&); "
        "T &operator=(T &&); }; "
        "T::T(T &&) noexcept = default; "
        "T &T::operator=(T &&) noexcept = default; }"))
    expect(not defaulted_tu,
           f"defaulted noexcept member definitions became globals: "
           f"{defaulted_tu}", failures)
    for declaration in (
        "struct State Rogue;",
        "extern int Rogue = 0;",
        "returnRecord Rogue;",
    ):
        found = kinds(
            "namespace llvm { namespace { " + declaration + " } }\n")
        expect(found == ["anonymous-namespace object"],
               f"legal object spelling escaped lexical scan: "
               f"{declaration}: {found}", failures)
    extern_direct = kinds(
        "namespace llvm { namespace {\n"
        "struct Registry { Registry(int); };\n"
        "extern int BracedExtern{0};\n"
        "extern Registry DirectExtern(1);\n"
        "static int AsmNamed asm(\"innocent\");\n"
        "} }\n")
    expect(extern_direct == ["anonymous-namespace object"] * 3,
           f"extern direct-init or asm-labelled object escaped: "
           f"{extern_direct}", failures)
    expect(kinds("namespace llvm { namespace {\n"
                 "class Adapter final : public Base {};\n"
                 "struct Plain {}; enum : unsigned { Value = 1 };\n"
                 "} }\n") == [],
           "type definition head was misread as process storage", failures)
    compact_objects = kinds(
        "namespace llvm { namespace { int*Rogue; Record&RogueRef = get(); } }\n")
    expect(compact_objects == ["anonymous-namespace object"] * 2,
           f"object declaration without type/name whitespace escaped: "
           f"{compact_objects}", failures)

    # A variable whose *type* comes from an anonymous namespace is internally
    # linked however the variable itself is spelled, so `inline` cannot make
    # the program share one copy. Only the binary shows this; the declaration
    # looks correct in isolation.
    borrowed = kinds("namespace llvm {\n"
                     "namespace { struct Impl { int x; }; }\n"
                     "inline Impl Registry;\n}\n")
    expect(len(borrowed) == 1 and "anonymous-namespace type" in borrowed[0],
           f"anonymous-namespace type not reported: {borrowed}", failures)
    expect(kinds("namespace llvm {\nstruct Impl { int x; };\n"
                 "inline Impl Registry;\n}\n") == [],
           "named type wrongly reported as anonymous", failures)

    # `static` on a member function means "no implicit object", not internal
    # linkage, so its locals are still one per program.
    expect(kinds("namespace llvm {\nstruct T {\n"
                 "  static int &get() { static int V = 0; return V; }\n};\n}\n")
           == [],
           "static member function's local wrongly reported", failures)

    expect(kinds("namespace llvm {\n"
                 "static inline void f() {\n"
                 "  if (true) { static int Nested = 0; }\n"
                 "}\n}\n")
           == ["function-local static of an unmergeable function"],
           "nested block lost unmergeable-function scope", failures)

    # A static data member is declared `inline static` on purpose; that is a
    # member declaration, not a namespace-scope definition.
    expect(kinds("namespace llvm {\nstruct T { inline static bool F = false; };\n}\n")
           == [],
           "static data member wrongly reported", failures)

    # A directive must not be absorbed into the statement below it, and a
    # function declaration is not an object.
    expect(kinds("namespace llvm {\n#endif\ninline static bool Flag = false;\n}\n")
           == ["file-scope static"],
           "preprocessor directive broke statement recognition", failures)
    expect(kinds("namespace llvm {\ninline static void fn(const char *m);\n}\n")
           == [],
           "function declaration misread as an object", failures)
    expect(kinds("namespace llvm {\n"
                 "inline static void fn(const char *m) LLVM_ATTRIBUTE_UNUSED;\n"
                 "}\n") == [],
           "attribute-suffixed function misread as an object", failures)

    # Generated .inc fragments can contain macro invocations and enum entries
    # before an unrelated static function. A later `static` token must not turn
    # the accumulated preprocessor fragment into an object declaration.
    expect(kinds("ATTRIBUTE_ENUM(A,a)\n"
                 "First = 1, Last = 2,\n"
                 "static inline bool compatible(int x);\n") == [],
           "later static token tainted an incomplete .inc fragment", failures)
    expect(kinds('"", "", "", static const uint8_t Table[] = {};\n') == [],
           "string-table .inc fragment became a false declaration", failures)

    # The reported line must point at the definition, not at the block start.
    found = list(mod.header_internal_state(
        "namespace llvm {\n\n\ninline static bool Flag = false;\n}\n"))
    expect(found and found[0][0] == 4,
           f"wrong line reported: {found[0][0] if found else 'none'}", failures)

    # HEADER_AUDIT_DIRS also accepts exact files. The root-lease header is
    # intentionally listed this way; using Path.rglob() directly on a file
    # used to skip it without a diagnostic.
    old_root = mod.ROOT
    old_header_dirs = mod.HEADER_AUDIT_DIRS
    try:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "single.h").write_text(
                "namespace neverc {\ninline static bool Flag = false;\n}\n",
                encoding="utf-8",
            )
            (root / "allowed.h").write_text(
                "namespace neverc {\n"
                "static thread_local int AllowedHeaderState;\n}\n",
                encoding="utf-8",
            )
            (root / "rogue.h").write_text(
                "namespace neverc {\n"
                "static thread_local int AllowedHeaderState, RogueState;\n"
                "}\n",
                encoding="utf-8",
            )
            (root / "constant.h").write_text(
                "namespace neverc {\n"
                "struct ConstantRecord { int Value; };\n"
                "static constexpr ConstantRecord Reviewed{1};\n}\n",
                encoding="utf-8",
            )
            (root / "headers").mkdir()
            (root / "headers" / "rogue.hpp").write_text(
                "namespace neverc {\ninline static bool HppFlag = false;\n}\n",
                encoding="utf-8",
            )
            (root / "headers" / "rogue.inc").write_text(
                "namespace neverc {\ninline static bool IncFlag = false;\n}\n",
                encoding="utf-8",
            )
            (root / "headers" / "control.cpp").write_text(
                "namespace neverc {\ninline static bool CppFlag = false;\n}\n",
                encoding="utf-8",
            )
            (root / "exact.inc").write_text(
                "namespace neverc {\ninline static bool ExactInc = false;\n}\n",
                encoding="utf-8",
            )
            mod.ROOT = root
            mod.HEADER_AUDIT_DIRS = (
                "single.h", "allowed.h", "rogue.h", "constant.h",
                "headers", "exact.inc")
            report = mod.Report()
            mod.scan_headers(report, {
                "entries": [{
                    "files": ["allowed.h", "rogue.h"],
                    "symbol": "AllowedHeaderState",
                    "declaration":
                        "static thread_local int AllowedHeaderState",
                    "definition_sha256": mod.object_definition_sha256(
                        "static thread_local int AllowedHeaderState;"),
                }],
                "header_constants": [{
                    "file": "constant.h",
                    "symbol": "Reviewed",
                    "declaration":
                        "static constexpr ConstantRecord Reviewed",
                    "definition_sha256": mod.object_definition_sha256(
                        "static constexpr ConstantRecord Reviewed{1};"),
                    "owner": "test",
                    "justification": "immutable record fixture",
                }],
            })
            expect(any(hit.startswith("single.h:2:")
                       for hit in report.headers),
                   f"exact header file was not audited: {report.headers}",
                   failures)
            expect(not any(hit.startswith("allowed.h:")
                           for hit in report.headers),
                   f"exact header declaration was not allowlisted: "
                   f"{report.headers}", failures)
            expect(any(hit.startswith("rogue.h:2:")
                       for hit in report.headers),
                   f"header multi-declarator escaped allowlist: "
                   f"{report.headers}", failures)
            expect(not any(hit.startswith("constant.h:")
                           for hit in report.headers),
                   f"reviewed complex constant was not suppressed: "
                   f"{report.headers}", failures)
            expect(any(hit.startswith("headers/rogue.hpp:2:")
                       for hit in report.headers),
                   f"directory-contained .hpp was not audited: "
                   f"{report.headers}", failures)
            expect(any(hit.startswith("headers/rogue.inc:2:")
                       for hit in report.headers),
                   f"directory-contained .inc was not audited: "
                   f"{report.headers}", failures)
            expect(any(hit.startswith("exact.inc:2:")
                       for hit in report.headers),
                   f"exact .inc file was not audited: {report.headers}",
                   failures)
            expect(not any(hit.startswith("headers/control.cpp:")
                           for hit in report.headers),
                   f".cpp file entered the header-only audit: "
                   f"{report.headers}", failures)
    finally:
        mod.ROOT = old_root
        mod.HEADER_AUDIT_DIRS = old_header_dirs

    # 20z. A PE audit is authoritative only when it joins each COFF symbol to
    # its numbered output section and uses IMAGE_SCN_MEM_WRITE. Microsoft
    # demangling must classify the variable owner, not a type mentioned later
    # in the mangled name.
    coff_root_raw = (
        "?LLVMTimeTraceRootGeneration@time_trace_detail@neverc@@"
        "3V?$atomic@_K@std@@A")
    coff_local_raw = (
        "?ExclusiveWaitEpoch@?A0x12345678@plugin@neverc@@"
        "3V?$atomic@_K@std@@A")
    coff_rogue_raw = "?RogueCache@Owner@plugin@neverc@@2HA"
    coff_foreign_raw = "?Foreign@other@@3VThing@plugin@neverc@@A"
    coff_readonly_raw = "?ReadOnly@plugin@neverc@@3HB"
    coff_listing = f"""
File: C:\\build\\bin\\neverc.exe
Format: COFF-x86-64
Arch: x86_64
AddressSize: 64bit
ImageFileHeader {{
  Machine: IMAGE_FILE_MACHINE_AMD64 (0x8664)
  SectionCount: 2
  TimeDateStamp: 0x0
  PointerToSymbolTable: 0x600
  SymbolCount: 9
  StringTableSize: 256
  OptionalHeaderSize: 240
  Characteristics [ (0x22)
    IMAGE_FILE_EXECUTABLE_IMAGE (0x2)
    IMAGE_FILE_LARGE_ADDRESS_AWARE (0x20)
  ]
}}
ImageOptionalHeader {{
  Magic: 0x20B
  ImageBase: 0x140000000
}}
Sections [
  Section {{
    Number: 1
    Name: .same (2E 73 61 6D 65 00 00 00)
    VirtualSize: 0x100
    VirtualAddress: 0x1000
    Characteristics [ (0x40000040)
      IMAGE_SCN_CNT_INITIALIZED_DATA (0x40)
      IMAGE_SCN_MEM_READ (0x40000000)
    ]
  }}
  Section {{
    Number: 2
    Name: .tls$X (2E 74 6C 73 24 58 00 00)
    VirtualSize: 0x100
    VirtualAddress: 0x2000
    Characteristics [ (0xC0000040)
      IMAGE_SCN_CNT_INITIALIZED_DATA (0x40)
      IMAGE_SCN_MEM_READ (0x40000000)
      IMAGE_SCN_MEM_WRITE (0x80000000)
    ]
  }}
]
Symbols [
  Symbol {{
    Name: .same
    Value: 0
    Section: .same (1)
    BaseType: Null (0x0)
    ComplexType: Null (0x0)
    StorageClass: Static (0x3)
    AuxSymbolCount: 1
    AuxSectionDef {{
      Length: 256
    }}
  }}
  Symbol {{
    Name: .tls$X
    Value: 0
    Section: .tls$X (2)
    BaseType: Null (0x0)
    ComplexType: Null (0x0)
    StorageClass: Static (0x3)
    AuxSymbolCount: 1
    AuxSectionDef {{
      Length: 256
    }}
  }}
  Symbol {{
    Name: {coff_root_raw}
    Value: 0
    Section: .tls$X (2)
    BaseType: Null (0x0)
    ComplexType: Null (0x0)
    StorageClass: External (0x2)
    AuxSymbolCount: 0
  }}
  Symbol {{
    Name: {coff_local_raw}
    Value: 8
    Section: .tls$X (2)
    BaseType: Null (0x0)
    ComplexType: Null (0x0)
    StorageClass: Static (0x3)
    AuxSymbolCount: 0
  }}
  Symbol {{
    Name: {coff_rogue_raw}
    Value: 16
    Section: .tls$X (2)
    BaseType: Null (0x0)
    ComplexType: Null (0x0)
    StorageClass: External (0x2)
    AuxSymbolCount: 0
  }}
  Symbol {{
    Name: {coff_foreign_raw}
    Value: 24
    Section: .tls$X (2)
    BaseType: Null (0x0)
    ComplexType: Null (0x0)
    StorageClass: External (0x2)
    AuxSymbolCount: 0
  }}
  Symbol {{
    Name: {coff_readonly_raw}
    Value: 0
    Section: .same (1)
    BaseType: Null (0x0)
    ComplexType: Null (0x0)
    StorageClass: External (0x2)
    AuxSymbolCount: 0
  }}
]
TLSDirectory {{
}}
""".lstrip()
    coff_local_name = local_token.replace(
        "(anonymous namespace)", "`anonymous namespace'")
    coff_undname = "".join((
        f"{coff_root_raw}\n{root_token}\n\n",
        f"{coff_local_raw}\n{coff_local_name}\n\n",
        f"{coff_rogue_raw}\nneverc::plugin::Owner::RogueCache\n\n",
        f"{coff_foreign_raw}\nother::Foreign\n\n",
    ))
    pe_allowlist = {
        "binary_symbols": sentinel_allowlist["binary_symbols"] + [{
            "symbol": "neverc::plugin::pluginLLVMOptionGate()::Gate",
            "min_count": 1,
            "max_count": 1,
        }],
    }
    with tempfile.TemporaryDirectory() as tmp:
        build = pathlib.Path(tmp)
        (build / "bin").mkdir()
        (build / "bin" / "neverc.exe").write_bytes(b"MZ\x90\x00")
        coff_processes = [
            process(["llvm-readobj"], stdout=coff_listing),
            process(["llvm-undname"], stdout=coff_undname),
        ]
        with mock.patch.object(
                mod.shutil, "which", side_effect=lambda name: {
                    "llvm-readobj": "/tools/llvm-readobj",
                    "llvm-undname": "/tools/llvm-undname",
                }.get(name)), mock.patch.object(
                    mod.subprocess, "run", side_effect=coff_processes) as run:
            report = mod.Report()
            mod.scan_binary(report, build, pe_allowlist)
        expect(not report.unscannable,
               f"valid PE/COFF image was not audited: {report.unscannable}",
               failures)
        expect(len(report.binary) == 1 and
               "neverc::plugin::Owner::RogueCache" in report.binary[0],
               f"PE write flag/owner audit returned {report.binary}", failures)
        first_argv = (run.call_args_list[0].args[0]
                      if run.call_args_list else None)
        expect(first_argv == [
                   "/tools/llvm-readobj", "--file-header", "--sections",
                   "--symbols", "--coff-tls-directory", "--no-demangle",
                   str(build / "bin" / "neverc.exe")],
               f"PE readobj argv lost section/symbol facts: "
               f"{first_argv}", failures)
        expect(run.call_args_list[1].args[0] == [
                   "/tools/llvm-undname", "--no-variable-type",
                   "--no-access-specifier", "--no-member-type",
                   "--warn-trailing"],
               f"PE owner demangler kept type/access prefixes: "
               f"{run.call_args_list[1].args[0]}", failures)

    # The pinned official Windows LLVM installer does not always ship
    # llvm-undname. The native DbgHelp fallback must preserve the same strict
    # ownership view instead of turning that packaging difference into either
    # a skipped audit or an unscannable image.
    with tempfile.TemporaryDirectory() as tmp:
        build = pathlib.Path(tmp)
        (build / "bin").mkdir()
        (build / "bin" / "neverc.exe").write_bytes(b"MZ\x90\x00")
        dbghelp_names = (
            root_token,
            local_token,
            "neverc::plugin::Owner::RogueCache",
            "other::Foreign",
        )
        with mock.patch.object(
                mod.shutil, "which", side_effect=lambda name: {
                    "llvm-readobj": "/tools/llvm-readobj",
                }.get(name)), mock.patch.object(
                    mod.sys, "platform", "win32"), mock.patch.object(
                    mod.subprocess, "run",
                    return_value=process(["llvm-readobj"],
                                         stdout=coff_listing)), \
                mock.patch.object(
                    mod, "_dbghelp_demangle_coff_names",
                    return_value=(dbghelp_names, ())) as dbghelp:
            report = mod.Report()
            mod.scan_binary(report, build, pe_allowlist)
        expect(not report.unscannable,
               f"DbgHelp PE fallback was not authoritative: "
               f"{report.unscannable}", failures)
        expect(len(report.binary) == 1 and
               "neverc::plugin::Owner::RogueCache" in report.binary[0],
               f"DbgHelp PE owner audit returned {report.binary}", failures)
        expect(dbghelp.call_count == 1 and
               dbghelp.call_args.args[0] == (
                   coff_root_raw, coff_local_raw,
                   coff_rogue_raw, coff_foreign_raw),
               "DbgHelp fallback did not receive the exact decorated "
               "writable COFF names", failures)

    # The PE parser must reject stripped/truncated or contradictory listings as
    # a whole. In particular, SymbolCount counts auxiliary records even though
    # llvm-readobj prints them nested under one primary Symbol block.
    arm64_coff_listing = (coff_listing
                          .replace("Format: COFF-x86-64",
                                   "Format: COFF-ARM64", 1)
                          .replace("Arch: x86_64", "Arch: aarch64", 1)
                          .replace("IMAGE_FILE_MACHINE_AMD64 (0x8664)",
                                   "IMAGE_FILE_MACHINE_ARM64 (0xAA64)", 1))
    try:
        arm64_symbols = mod._parse_coff_readobj(arm64_coff_listing)
    except ValueError:
        arm64_symbols = ()
    expect(len(arm64_symbols) == 7,
           "valid ARM64 PE/COFF listing was rejected", failures)

    duplicate_section_listing = (coff_listing
        .replace("Name: .tls$X (2E 74 6C 73 24 58 00 00)",
                 "Name: .same (2E 73 61 6D 65 00 00 00)", 1)
        .replace("Name: .tls$X\n", "Name: .same\n", 1)
        .replace("Section: .tls$X (2)", "Section: .same (2)"))
    duplicate_symbols = mod._parse_coff_readobj(duplicate_section_listing)
    duplicate_rogue = next(
        symbol for symbol in duplicate_symbols
        if symbol.raw_name == coff_rogue_raw)
    duplicate_readonly = next(
        symbol for symbol in duplicate_symbols
        if symbol.raw_name == coff_readonly_raw)
    expect(bool(duplicate_rogue.section_characteristics & 0x80000000) and
           not (duplicate_readonly.section_characteristics & 0x80000000),
           "COFF symbols were joined to duplicate section names instead of "
           "section numbers", failures)

    malformed_coff = (
        ("zero symbol-table pointer",
         coff_listing.replace("PointerToSymbolTable: 0x600",
                              "PointerToSymbolTable: 0x0", 1)),
        ("auxiliary-symbol count mismatch",
         coff_listing.replace("SymbolCount: 9", "SymbolCount: 8", 1)),
        ("format/machine mismatch",
         coff_listing.replace("IMAGE_FILE_MACHINE_AMD64 (0x8664)",
                              "IMAGE_FILE_MACHINE_ARM64 (0xAA64)", 1)),
        ("missing writable-section characteristics",
         coff_listing.replace("Characteristics [ (0xC0000040)",
                              "UnknownCharacteristics: 0xC0000040", 1)),
    )
    for label, listing in malformed_coff:
        try:
            mod._parse_coff_readobj(listing)
            rejected = False
        except ValueError:
            rejected = True
        expect(rejected, f"PE parser accepted {label}", failures)

    # A section spelling that merely looks like TLS is not evidence. Only the
    # loader's TLS-directory VA range excludes a symbol from process storage.
    tls_listing = coff_listing.replace(
        "TLSDirectory {\n}",
        "TLSDirectory {\n"
        "  StartAddressOfRawData: 0x140002010\n"
        "  EndAddressOfRawData: 0x140002014\n"
        "  AddressOfIndex: 0x140003000\n"
        "  AddressOfCallBacks: 0x140004000\n"
        "  SizeOfZeroFill: 0x0\n"
        "  Characteristics [ (0x0)\n"
        "  ]\n"
        "}", 1)
    tls_undname = "".join((
        f"{coff_root_raw}\n{root_token}\n\n",
        f"{coff_local_raw}\n{coff_local_name}\n\n",
        f"{coff_foreign_raw}\nother::Foreign\n\n",
    ))
    with tempfile.TemporaryDirectory() as tmp:
        build = pathlib.Path(tmp)
        (build / "bin").mkdir()
        (build / "bin" / "neverc.exe").write_bytes(b"MZ\x90\x00")
        with mock.patch.object(
                mod.shutil, "which", side_effect=lambda name: {
                    "llvm-readobj": "/tools/llvm-readobj",
                    "llvm-undname": "/tools/llvm-undname",
                }.get(name)), mock.patch.object(
                    mod.subprocess, "run", side_effect=[
                        process(["llvm-readobj"], stdout=tls_listing),
                        process(["llvm-undname"], stdout=tls_undname),
                    ]):
            tls_report = mod.Report()
            mod.scan_binary(tls_report, build, sentinel_allowlist)
        expect(not tls_report.unscannable and not tls_report.binary,
               "PE TLS-directory range was not distinguished from a fake "
               f".tls$ section: {tls_report}", failures)

    # The local sentinel must actually be a Static COFF record. An external
    # export plus an external duplicate does not prove local symbols survived.
    local_prefix, local_suffix = coff_listing.split(
        f"Name: {coff_local_raw}", 1)
    bad_local_listing = local_prefix + f"Name: {coff_local_raw}" + \
        local_suffix.replace("StorageClass: Static (0x3)",
                             "StorageClass: External (0x2)", 1)
    with tempfile.TemporaryDirectory() as tmp:
        build = pathlib.Path(tmp)
        (build / "bin").mkdir()
        (build / "bin" / "neverc.exe").write_bytes(b"MZ\x90\x00")
        with mock.patch.object(
                mod.shutil, "which", side_effect=lambda name: {
                    "llvm-readobj": "/tools/llvm-readobj",
                    "llvm-undname": "/tools/llvm-undname",
                }.get(name)), mock.patch.object(
                    mod.subprocess, "run", side_effect=[
                        process(["llvm-readobj"], stdout=bad_local_listing),
                        process(["llvm-undname"], stdout=coff_undname),
                    ]):
            bad_local_report = mod.Report()
            mod.scan_binary(bad_local_report, build, sentinel_allowlist)
        expect(any("sentinel" in item for item in
                   bad_local_report.unscannable),
               "PE external-only symbol table was trusted as locally "
               f"complete: {bad_local_report}", failures)

    # Tool discovery is part of the hard gate, never an optional enhancement.
    with tempfile.TemporaryDirectory() as tmp:
        build = pathlib.Path(tmp)
        (build / "bin").mkdir()
        (build / "bin" / "neverc.exe").write_bytes(b"MZ\x90\x00")
        with mock.patch.object(mod.shutil, "which", return_value=None), \
                mock.patch.object(mod.subprocess, "run") as run:
            missing_tool_report = mod.Report()
            mod.scan_binary(missing_tool_report, build, sentinel_allowlist)
        expect(bool(missing_tool_report.unscannable) and not run.called,
               "missing PE/COFF tools did not fail closed before execution",
               failures)

    # Captured LLVM output must never contaminate the checker's JSON stdout.
    with tempfile.TemporaryDirectory() as tmp:
        build = pathlib.Path(tmp)
        (build / "bin").mkdir()
        (build / "bin" / "neverc.exe").write_bytes(b"MZ\x90\x00")
        empty_allowlist = build / "allowlist.json"
        empty_allowlist.write_text(json.dumps({
            "entries": [],
            "header_constants": [],
            "source_objects": [],
            "binary_symbols": [],
        }), encoding="utf-8")
        json_stdout = io.StringIO()
        json_stderr = io.StringIO()
        old_config = (mod.ROOT, mod.ALLOWLIST_PATH, mod.AUDIT_DIRS,
                      mod.EXTRA_AUDIT_FILES, mod.HEADER_AUDIT_DIRS)
        try:
            mod.ROOT = build
            mod.ALLOWLIST_PATH = empty_allowlist
            mod.AUDIT_DIRS = ()
            mod.EXTRA_AUDIT_FILES = ()
            mod.HEADER_AUDIT_DIRS = ()
            with mock.patch.object(
                    mod.shutil, "which", side_effect=lambda name: {
                        "llvm-readobj": "/tools/llvm-readobj",
                        "llvm-undname": "/tools/llvm-undname",
                    }.get(name)), mock.patch.object(
                        mod.subprocess, "run", side_effect=[
                            process(["llvm-readobj"], stdout=coff_listing),
                            process(["llvm-undname"], stdout=coff_undname),
                        ]), mock.patch.object(
                            sys, "argv", [str(CHECKER), "--build-dir",
                                          str(build), "--strict", "--json"]), \
                    contextlib.redirect_stdout(json_stdout), \
                    contextlib.redirect_stderr(json_stderr):
                mod.main()
        finally:
            (mod.ROOT, mod.ALLOWLIST_PATH, mod.AUDIT_DIRS,
             mod.EXTRA_AUDIT_FILES, mod.HEADER_AUDIT_DIRS) = old_config
        try:
            json_payload = json.loads(json_stdout.getvalue())
        except json.JSONDecodeError:
            json_payload = None
        expect(isinstance(json_payload, dict) and
               isinstance(json_payload.get("unscannable"), list) and
               not json_stderr.getvalue(),
               "PE tools contaminated JSON output: "
               f"stdout={json_stdout.getvalue()[:160]!r} "
               f"stderr={json_stderr.getvalue()[:160]!r}", failures)

    # 21. The release-SDK wrapper must run the strict gate and must not skip all
    # compiler checks merely because the packaged compiler has a Windows
    # suffix. Exercise the real subprocess boundary: the fake global checker
    # fails only when it receives --strict, so either omission makes this test
    # falsely return success and therefore RED.
    with tempfile.TemporaryDirectory() as tmp:
        fixture = pathlib.Path(tmp)
        plugin_api = fixture / "utils" / "plugin-api"
        plugin_api.mkdir(parents=True)
        shutil.copy2(HERE.parent / "check-release-sdk.py",
                     plugin_api / "check-release-sdk.py")
        build = fixture / "build"
        (build / "bin").mkdir(parents=True)
        (build / "bin" / "neverc.exe").write_bytes(b"MZ")
        prefix = fixture / "prefix"
        prefix.mkdir()
        argv_log = fixture / "global-state-argv.txt"

        ordinary_stubs = (
            "test-installed-sdk.py",
            "gen-single-header.py",
            "gen-sdk-manifest.py",
            "gen-abi-manifest.py",
            "check-single-header.py",
            "check-public-c.py",
            "check-capability-bindings.py",
            "check-phase-inventory.py",
            "check-coverage.py",
        )
        for name in ordinary_stubs:
            (plugin_api / name).write_text(
                "import sys\nraise SystemExit(0)\n", encoding="utf-8")
        (plugin_api / "check-global-state.py").write_text(
            "import pathlib, sys\n"
            f"pathlib.Path({str(argv_log)!r}).write_text("
            "'\\n'.join(sys.argv[1:]), encoding='utf-8')\n"
            "raise SystemExit(23 if '--strict' in sys.argv else 0)\n",
            encoding="utf-8",
        )
        wrapped = subprocess.run(
            [sys.executable, str(plugin_api / "check-release-sdk.py"),
             "--build-dir", str(build), "--prefix", str(prefix)],
            cwd=fixture, capture_output=True, text=True, check=False,
        )
        expect(wrapped.returncode == 1,
               f"release SDK wrapper did not propagate strict failure: "
               f"rc={wrapped.returncode} out={wrapped.stdout!r} "
               f"err={wrapped.stderr!r}", failures)
        expect("FAILED (check-global-state.py)" in wrapped.stderr,
               f"release SDK wrapper lost the failing gate name: "
               f"{wrapped.stderr!r}", failures)
        logged_args = (argv_log.read_text(encoding="utf-8")
                       if argv_log.exists() else "")
        expect("--strict" in logged_args.splitlines(),
               f"release SDK wrapper omitted --strict: {logged_args!r}",
               failures)

        (build / "bin" / "neverc.exe").unlink()
        missing_compiler = subprocess.run(
            [sys.executable, str(plugin_api / "check-release-sdk.py"),
             "--build-dir", str(build), "--prefix", str(prefix)],
            cwd=fixture, capture_output=True, text=True, check=False,
        )
        expect(missing_compiler.returncode == 1,
               "release SDK wrapper passed without a built compiler: "
               f"rc={missing_compiler.returncode} "
               f"out={missing_compiler.stdout!r} "
               f"err={missing_compiler.stderr!r}", failures)
        expect("compiler" in (missing_compiler.stdout +
                              missing_compiler.stderr).lower(),
               "missing release compiler failure was not explained",
               failures)

    if failures:
        for f in failures:
            print(f"FAIL: {f}", file=sys.stderr)
        return 1
    print(f"test-check-global-state: OK ({_PERFORMED} checks passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
