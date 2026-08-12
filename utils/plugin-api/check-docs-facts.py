#!/usr/bin/env python3

"""Hold the plugin and release guides to what the implementation says.

Checks, without a build:

1. Every ``neverc.*`` phase or verifier the guides name exists in
   PhaseSchema.json, so renaming a phase cannot leave prose behind.
2. Every ``Neverc*`` / ``NEVERC_*`` / ``neverc_*`` identifier the guides name
   is declared in a public header or exported by the SDK's CMake package.
3. Every locale of a guide cites the same identifiers as the English page,
   because a technical name is never translated and a divergence means a
   translation dropped or mangled one.
4. The phase totals, policy counts, domain counts and sealed-gate list in
   README match the schema, in every locale.
5. The example CMake targets README advertises are defined by the SDK.
6. Every release guide carries the same user-visible ``--strip`` contract,
   including the two-stage Android release identity boundary, private
   symbol-map permissions, and fail-closed digest verification.
7. Every object-pipeline guide documents the narrower Android release rules
   for ``object.write`` and ``object.post_write``.
8. Android example Makefiles keep debug/release, compiler, and multi-word
   ``EXTRA`` selection, force portable profile rebuilds, record configuration
   only after successful builds, preserve the publication lock, and keep
   ``clean`` as a delete-only operation.
9. Repository and documentation indexes describe the ``.ko`` behavior as
   structural renaming, never as hashing, encryption, or pseudonymization.

Check 3 only sees prose that carries a citable name, so a dropped paragraph
that carries none slips past it; check-docs-links.py compares the shape of
every translation and catches that.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLUGIN_DOCS = ROOT / "docs" / "plugin-api"
RELEASE_DOCS = ROOT / "docs" / "release-builds"
HEADERS = ROOT / "neverc/include/neverc/Plugin"
SCHEMA = HEADERS / "Schema" / "PhaseSchema.json"
SDK = ROOT / "pluginsdk"

RELEASE_FACT_SOURCES = {
    ROOT / "neverc/include/neverc/Linker/Core/Driver/Dispatcher.h": (
        "StripMode stripMode = StripMode::None",
        "stripMode == StripMode::All",
    ),
    ROOT / "neverc/lib/Invoke/ToolChains/CommonArgs.cpp": (
        "Args.hasArg(options::OPT_s)",
        "Cfg.stripMode = ::linker::StripMode::All",
    ),
    ROOT / "neverc/lib/Invoke/ToolChains/NeverC.cpp": (
        "llvm::codegenoptions::DebugInfoKind DebugInfoKind =",
        "llvm::codegenoptions::NoDebugInfo;",
        "Args.getLastArg(options::OPT_g_Group)",
        "DebugInfoKind = llvm::codegenoptions::DebugInfoConstructor;",
    ),
    ROOT / "neverc/lib/Invoke/Core/Driver.cpp": (
        "ProducesFinalAndroidKernelModule",
        "isFinalAndroidKernelModule",
    ),
    ROOT / "neverc/include/neverc/Build/BuildConstants.h": (
        '"NEVERC_MAKE_EXECUTABLE"',
    ),
    ROOT / "neverc/lib/Build/Core/BuildDriver.cpp": (
        "VarNeverCMakeExecutable",
        "PrependArg && *PrependArg",
        'quoteRecipeArgument(Variable.first + "=" + Variable.second)',
    ),
    ROOT / "neverc/main.cpp": (
        '"__neverc_android_kernel_output_integrity"',
        '"__neverc_clean_android_kernel_output"',
    ),
    ROOT / "neverc/include/neverc/Foundation/AndroidKernelModuleSymbolPolicy.h": (
        '"init_module"',
        '"cleanup_module"',
        '"__cfi_check"',
        '"__cfi_check_fail"',
        '"__cfi_jt_init_module"',
        '"__cfi_jt_cleanup_module"',
        'Name.starts_with("__typeid__")',
        'Name.starts_with("__kcfi_typeid_")',
    ),
    ROOT / "neverc/lib/Foundation/Core/AndroidKernelModuleReleaseNames.cpp": (
        'return "fn_"',
        'return "obj_"',
        'return "code_"',
        'return "sym_"',
        'return "abs_"',
    ),
    ROOT / "neverc/lib/Foundation/Core/AndroidKernelReleaseSymbolMap.cpp": (
        'return ImagePath.str() + ".symbols.json"',
        "MapOutput.OwnerOnly = true",
        '"NEVERC_ANDROID_KERNEL_BUILD_ID"',
        '"NEVERC_ANDROID_KERNEL_BUILD_EXTRA"',
        '".nvk-build-flags"',
        '".nvk-build-extra"',
        '".nvk-build-integrity"',
        '"IMAGE_SHA256="',
        '" BUILD_ID_SHA256="',
        '" BUILD_EXTRA_SHA256="',
        "cleanAndroidKernelReleaseOutput",
        '"neverc.android-kernel-symbol-map"',
        '{"version", 2}',
        '"original_encoding"',
    ),
    ROOT / "neverc/lib/Foundation/Core/OutputBundleTransaction.cpp": (
        'sys::path::append(LockPath, ".neverc-output.lock")',
        "publicationLockCoordinator().acquireAll(Paths, IsCancelled)",
        "sys::fs::tryLockFile(FileDescriptor",
        "sys::fs::OF_AccessControl",
        "a removed main output requires a removal-only transaction",
    ),
    ROOT / "neverc/lib/Foundation/Core/OutputPlatform.cpp": (
        "clearExtendedAcl",
        "PROTECTED_DACL_SECURITY_INFORMATION",
        "restrictFileToOwner",
        "probeMissingPathAlias",
    ),
    ROOT / "neverc/lib/Linker/Backends/ELF/Driver/ElfDriver.cpp": (
        "publishAndroidKernelReleaseOutput",
        "OutputDurabilityUnconfirmed",
        "OutputRecoveryRequired",
    ),
    ROOT / "neverc/lib/Plugin/Link/LinkExecutionHooksBridge.cpp": (
        "Android module finalization requires Android module merge semantics",
        "finalized Android module release requires a relocatable output ",
        "request and driver configuration",
        "sameID(FrozenInputFormat, FrozenTarget.ObjectFormatID)",
        "sameID(FrozenOutputFormat, FrozenTarget.ObjectFormatID)",
        "and output object formats to share one identity",
        "captureAndroidKernelReleaseGraphIdentitySeal",
        "captureAndroidKernelReleaseImageIdentitySeal",
        "mayReplaceWriteArtifact(FrozenTarget,",
        "hasPluginOwnedGraphWriter(FrozenOutputFormat)",
        "requires a host-owned graph writer",
        "if (!Provider.Builtin)",
        "audited graph authoritative",
        "host-owned writer establish",
        "outside the structurally verified ABI surface",
    ),
    ROOT / "neverc/lib/Plugin/Link/BuiltinObjectMergeAdapter.cpp": (
        "if (!Config.FinalizeAndroidKernelModule) {",
        "if (Config.FinalizeAndroidKernelModule) {",
        "NativeBytes.begin()",
        "audited native image",
        "external graph/write/post-write hooks",
    ),
    ROOT / "neverc/lib/Plugin/Core/PluginPhaseExecutor.cpp": (
        "providerMatches(Binding, Route)",
        "matchingString(Provider.TargetTriple, Route.TargetTriple)",
        "matchingString(Provider.CPU, Route.CPU)",
        "matchingString(Provider.Features, Route.Features)",
        "matchingString(Provider.ObjectFormat, Route.ObjectFormat)",
        "Constraint.ExecutionLevel == Route.ExecutionLevel",
    ),
    ROOT / "neverc/lib/Plugin/Link/AndroidKernelReleaseIdentitySeal.cpp": (
        "GraphSectionIdentity{Section.ID, Ordinal++, Section.Name}",
        "Identity.OwnerID = Symbol.ID",
        "Identity.SectionID = Section->OwnerID",
        "Snapshot.SymbolCount = Symbols->size()",
        "Identity.Slot = Slot",
        "std::tie(Ordinal, Name)",
        "std::tie(OwnerID, Name, Class, SectionID, Value, Size, Binding, Type",
        "std::tie(Slot, Name, Class, SectionIndex, Value, Size, Binding, Type",
    ),
    ROOT / "neverc/lib/Plugin/Object/ObjectWriterProvider.cpp": (
        "ObjectWriterProvider::hasPluginOwnedGraphWriter",
        "Format && Format->Owner && Format->Writer",
    ),
    ROOT / "neverc/lib/Plugin/Object/ObjectPhaseHooks.cpp": (
        "Validators.BindPrePostWriteImage",
        "BoundPostWriteImage",
        "postWritePhaseID()",
    ),
}

LOCALES = ["", "zh-CN", "zh-TW", "ja", "ko", "fr", "de", "es", "it", "ru", "ar"]

FENCE = re.compile(r"^\s*```")
TICK = re.compile(r"`([^`\n]+)`")
DEFINITION = re.compile(r"^\[([^\]]+)\]:\s*(\S+)\s*$")
PHASE = re.compile(r"^neverc\.[a-z0-9_.]+$")
SYMBOL = re.compile(r"^(NEVERC_[A-Z0-9_]+|Neverc[A-Za-z0-9_]+|neverc_[a-z0-9_]+)$")
EXAMPLE_TARGET = re.compile(r"`(neverc-plugin-example-[a-z0-9-]+)`")
# the phase table packs two domain/count pairs per row, so the trailing pipe
# has to stay available as the next match's opening pipe
COUNT_ROW = re.compile(r"\|\s*`(\w+)`\s*\|\s*(\d+)\s*(?=\|)")

ARABIC_INDIC = str.maketrans("٠١٢٣٤٥٦٧٨٩", "0123456789")
FULL_WIDTH = {ord(c): chr(ord(c) - 0xFEE0) for c in "０１２３４５６７８９"}


def normalize_digits(text: str) -> str:
    return text.translate(ARABIC_INDIC).translate(FULL_WIDTH)


def page(stem: str, locale: str) -> Path:
    return PLUGIN_DOCS / (f"{stem}.md" if not locale else f"{stem}.{locale}.md")


def localized_page(directory: Path, stem: str, locale: str) -> Path:
    return directory / (f"{stem}.md" if not locale else f"{stem}.{locale}.md")


def guide_pages() -> list[Path]:
    pages = []
    for directory, subdirectories, names in os.walk(PLUGIN_DOCS):
        subdirectories[:] = [d for d in subdirectories if d != "__pycache__"]
        pages.extend(Path(directory) / n for n in sorted(names) if n.endswith(".md"))
    return sorted(pages)


def release_pages() -> list[Path]:
    return [localized_page(RELEASE_DOCS, "README", locale) for locale in LOCALES]


def locale_of(name: str) -> str:
    parts = name[:-3].split(".")
    return parts[-1] if len(parts) >= 2 and parts[-1] in LOCALES else ""


def stem_of(name: str) -> str:
    parts = name[:-3].split(".")
    return ".".join(parts[:-1]) if len(parts) >= 2 and parts[-1] in LOCALES else name[:-3]


def declared_symbols() -> set[str]:
    """Names a plugin author can actually write: headers plus the CMake package."""
    text = []
    for directory, _, names in os.walk(HEADERS):
        for name in sorted(names):
            if name.endswith((".h", ".inc")):
                text.append((Path(directory) / name).read_text(
                    encoding="utf-8", errors="replace"))
                # a header is citable by its own name
                text.append(Path(name).stem)
    for directory, _, names in os.walk(SDK):
        for name in sorted(names):
            if name == "CMakeLists.txt" or name.endswith(".cmake.in"):
                text.append((Path(directory) / name).read_text(
                    encoding="utf-8", errors="replace"))
    return set(re.findall(
        r"\b(NEVERC_[A-Z0-9_]+|Neverc[A-Za-z0-9_]+|neverc_[a-z0-9_]+)\b",
        "\n".join(text)))


def prose_citations(path: Path) -> Counter:
    """Backticked spans outside fenced code and link definitions."""
    found: Counter = Counter()
    fenced = False
    for line in path.read_text(encoding="utf-8").split("\n"):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if fenced or DEFINITION.match(line):
            continue
        for span in TICK.findall(line):
            found[span.strip()] += 1
    return found


class Report:
    def __init__(self) -> None:
        self.failures: list[str] = []

    def fail(self, path: Path, message: str) -> None:
        self.failures.append(f"{path.relative_to(ROOT)}: {message}")


def require_literals(path: Path, literals: tuple[str, ...], report: Report) -> None:
    try:
        contents = path.read_text(encoding="utf-8")
    except OSError as error:
        report.fail(path, f"cannot be read: {error}")
        return
    missing = [literal for literal in literals if literal not in contents]
    if missing:
        report.fail(path, f"missing release contract literals: {missing}")


def check_release_source_contract(report: Report) -> None:
    """Fail when the implementation no longer carries the facts we document."""
    for path, literals in RELEASE_FACT_SOURCES.items():
        require_literals(path, literals, report)


def check_release_guides(report: Report) -> None:
    baseline = (
        "--strip",
        "-s",
        "-g",
        "-O2 --strip",
        "DWARF",
        ".symtab",
        ".strtab",
        "SHN_UNDEF",
        "fn_HEX",
        "obj_HEX",
        "code_HEX",
        "sym_HEX",
        "abs_HEX",
        "analysis EA",
        "<module>.ko.symbols.json",
        "image_sha256",
        "0600",
        "Windows ACL",
        "EXTRA",
        ".nvk-build-flags",
        ".nvk-build-integrity",
        "SHA-256",
        "neverc make",
        "make debug",
        "make release",
        "make clean",
        "PROFILE=...",
        "neverc.android-kernel-symbol-map",
        '"version": 2',
        "original_encoding",
        ".neverc-output.lock",
        'actual="$(python3 -c',
        'expected="$(jq -er',
        'test "$actual" = "$expected" &&',
    )
    identity_boundary = (
        "ObjectGraph",
        "`section ID`",
        "`symbol ID`",
        "`final ordinal`",
        "`slot`",
        "`neverc.object.write`",
        "`neverc.object.post_write`",
        "`plugin-owned ObjectFormat graph writer`",
        "`third-party ObjectMergeProvider`",
        "`MergedImage`",
        "`host-owned graph writer`",
        "`built-in finalized input serialization`",
        "`external object phases`",
        "`audited native bytes`",
        "`before sealed commit`",
        "`AFTER_COMMIT`",
        "`route-matching provider`",
        "`frozen input format`",
        "`TargetKey.ObjectFormatID`",
        "`frozen output format`",
        "`one format identity`",
        "`before provider dispatch`",
        "`Android module merge semantics`",
        "`relocatable output request`",
        "`relocatable driver configuration`",
        "`before routing`",
        "`provider`",
        "`interceptor`",
        "`observer`",
        "`REJECTED`",
        "`READ_ONLY`",
        "`VALIDATED`",
    )
    for path in release_pages():
        require_literals(path, baseline + identity_boundary, report)
        contents = path.read_text(encoding="utf-8")
        bare_commands = [
            f"`make {target}`"
            for target in ("debug", "release", "clean", "push", "run")
            if f"`make {target}`" in contents
        ]
        if bare_commands:
            report.fail(
                path,
                f"Android example commands must use neverc make: {bare_commands}",
            )
        if contents.count("`make`") != 1:
            report.fail(
                path,
                "must mention bare `make` exactly once as the rejected external tool",
            )


def check_object_pipeline_release_guides(report: Report) -> None:
    required = (
        "--strip",
        "ObjectGraph",
        "`section ID`",
        "`symbol ID`",
        "`final ordinal`",
        ".symtab",
        "`slot`",
        "`neverc.object.write`",
        "`neverc.object.post_write`",
        "`plugin-owned ObjectFormat graph writer`",
        "`third-party ObjectMergeProvider`",
        "`MergedImage`",
        "`host-owned graph writer`",
        "`built-in finalized input serialization`",
        "`external object phases`",
        "`audited native bytes`",
        "`before sealed commit`",
        "`AFTER_COMMIT`",
        "`route-matching provider`",
        "`frozen input format`",
        "`TargetKey.ObjectFormatID`",
        "`frozen output format`",
        "`one format identity`",
        "`before provider dispatch`",
        "`Android module merge semantics`",
        "`relocatable output request`",
        "`relocatable driver configuration`",
        "`before routing`",
        "`provider`",
        "`interceptor`",
        "`observer`",
        "`REJECTED`",
        "`READ_ONLY`",
        "`VALIDATED`",
    )
    for locale in LOCALES:
        require_literals(page("target-mc-object", locale), required, report)


def make_target_recipes(contents: str, target: str) -> list[list[str]]:
    """Return every recipe block for one literal make target."""
    rule = re.compile(
        rf"(?m)^{re.escape(target)}\s*:[^\n]*\n"
        r"(?P<body>(?:\t[^\n]*(?:\n|$))*)"
    )
    return [
        [line[1:] for line in match.group("body").splitlines()]
        for match in rule.finditer(contents)
    ]


def check_android_example_makefiles(report: Report) -> None:
    makefiles = sorted((ROOT / "examples").glob("android-kernel-*/Makefile"))
    if not makefiles:
        report.fail(ROOT / "examples", "no Android kernel example Makefiles found")
        return
    required = (
        "PROFILE := $(if $(SAVED_PROFILE),$(SAVED_PROFILE),debug)",
        "PROFILE_FLAGS_debug   := -g",
        "PROFILE_FLAGS_release := -O2 --strip",
        "ifeq ($(origin NVK_RECURSIVE_BUILD):$(NVK_RECURSIVE_BUILD),command line:1)",
        "KERNEL := $(NVK_RECURSIVE_KERNEL)",
        "NEVERC := $(NVK_RECURSIVE_NEVERC)",
        "export NVK_RECURSIVE_KERNEL := $(KERNEL)",
        "export NVK_RECURSIVE_NEVERC := $(NEVERC)",
        "all: $(MODULE)",
        "NEVERC=$(NEVERC)",
        "FLAGS_STAMP := $(dir $(MODULE)).nvk-build-flags",
        "EXTRA_STAMP := $(dir $(MODULE)).nvk-build-extra",
        "INTEGRITY_STAMP := $(dir $(MODULE)).nvk-build-integrity",
        "RECORDED_BUILD_INTEGRITY_BEFORE := $(file <$(INTEGRITY_STAMP))",
        "SAVED_BUILD_ID_CANDIDATE := $(file <$(FLAGS_STAMP))",
        "SAVED_EXTRA_CANDIDATE := $(file <$(EXTRA_STAMP))",
        'CURRENT_BUILD_INTEGRITY := $(shell $(NEVERC_MAKE_EXECUTABLE) __neverc_android_kernel_output_integrity "$(MODULE)")',
        "BUILD_ID_AFTER := $(file <$(FLAGS_STAMP))",
        "EXTRA_AFTER := $(file <$(EXTRA_STAMP))",
        "RECORDED_BUILD_INTEGRITY_AFTER := $(file <$(INTEGRITY_STAMP))",
        "export NEVERC_ANDROID_KERNEL_BUILD_ID := $(BUILD_ID)",
        "MAKE_MODE_FLAGS := $(firstword -$(MAKEFLAGS))",
        "RECURSIVE_MAKE_FLAGS := $(if $(findstring n,$(MAKE_MODE_FLAGS)),-n,) $(if $(findstring k,$(MAKE_MODE_FLAGS)),-k,) $(if $(findstring s,$(MAKE_MODE_FLAGS)),-s,)",
        "REBUILD_CONFIG := force-config-rebuild",
        "ifneq ($(SAVED_BUILD_ID),$(BUILD_ID))",
        "$(MODULE): $(SRCS) $(REBUILD_CONFIG)",
        "force-config-rebuild:",
    )
    forbidden_bundle_workarounds = (
        "&:",
        "RELEASE_BUNDLE_STAMP",
        "release-bundle-output",
        "if test",
    )
    required_neverc_make_guard = """\
ifneq ($(origin NEVERC_MAKE_EXECUTABLE),default)
$(error this Makefile requires 'neverc make'; external make is unsupported)
endif
"""
    required_verified_state = """\
ifneq ($(CURRENT_BUILD_INTEGRITY),)
ifeq ($(RECORDED_BUILD_INTEGRITY_BEFORE),$(CURRENT_BUILD_INTEGRITY))
ifeq ($(RECORDED_BUILD_INTEGRITY_AFTER),$(CURRENT_BUILD_INTEGRITY))
ifeq ($(SAVED_BUILD_ID_CANDIDATE),$(BUILD_ID_AFTER))
ifeq ($(SAVED_EXTRA_CANDIDATE),$(EXTRA_AFTER))
SAVED_BUILD_ID := $(SAVED_BUILD_ID_CANDIDATE)
SAVED_EXTRA := $(SAVED_EXTRA_CANDIDATE)
endif
endif
endif
endif
endif
"""
    required_goal_guard = """\
ifneq ($(filter debug release clean,$(MAKECMDGOALS)),)
ifneq ($(word 2,$(MAKECMDGOALS)),)
$(error debug/release/clean must each be invoked as the sole goal)
endif
endif
"""
    required_profile_validation = """\
ifneq ($(MAKECMDGOALS),clean)
VALID_PROFILES := debug release
ifneq ($(PROFILE),debug)
ifneq ($(PROFILE),release)
$(error unsupported PROFILE '$(PROFILE)'; expected one of: $(VALID_PROFILES))
endif
endif
endif
"""
    required_sidecar_rebuild = """\
ifeq ($(PROFILE),release)
ifeq ($(wildcard $(MODULE).symbols.json),)
REBUILD_CONFIG := force-config-rebuild
endif
else
ifneq ($(wildcard $(MODULE).symbols.json),)
REBUILD_CONFIG := force-config-rebuild
endif
endif
"""
    for path in makefiles:
        require_literals(path, required, report)
        contents = path.read_text(encoding="utf-8")
        uses_extra = (
            "export NEVERC_ANDROID_KERNEL_BUILD_EXTRA := $(EXTRA)" in contents
        )
        if uses_extra:
            require_literals(
                path,
                (
                    "SAVED_EXTRA := $(SAVED_EXTRA_CANDIDATE)",
                    "EXTRA := $(NVK_RECURSIVE_EXTRA)",
                    "export NVK_RECURSIVE_EXTRA := $(EXTRA)",
                    "export NEVERC_ANDROID_KERNEL_BUILD_EXTRA := $(EXTRA)",
                ),
                report,
            )
        elif not re.search(
            r"(?m)^export NEVERC_ANDROID_KERNEL_BUILD_EXTRA :=\s*$", contents
        ):
            report.fail(
                path,
                "examples without EXTRA must clear inherited build-extra state",
            )
        if "export NVK_RECURSIVE_BUILD" in contents:
            report.fail(
                path,
                "the recursion marker must be passed explicitly, not exported",
            )
        if "$(file >" in contents or "record-build-config" in contents:
            report.fail(
                path,
                "build stamps must be published by the compiler output bundle",
            )
        for token in forbidden_bundle_workarounds:
            if token in contents:
                report.fail(
                    path,
                    f"uses unsupported or non-portable release bundle workaround {token!r}",
                )
        required_blocks = (
            (
                required_goal_guard,
                "must reject profile-changing or destructive goals combined "
                "with another goal",
            ),
            (
                required_neverc_make_guard,
                "must reject external make because lock-aware helpers are required",
            ),
            (
                required_verified_state,
                "must restore state only from a stable, non-empty integrity snapshot",
            ),
            (
                required_profile_validation,
                "must allow clean to recover from an invalid saved profile",
            ),
            (
                required_sidecar_rebuild,
                "must rebuild when sidecar presence disagrees with PROFILE",
            ),
        )
        for block, message in required_blocks:
            if block not in contents:
                report.fail(path, message)
        build_ids = re.findall(r"(?m)^BUILD_ID\s*:=\s*(.+)$", contents)
        if len(build_ids) != 1:
            report.fail(path, "must define exactly one BUILD_ID")
            continue
        if "NEVERC=$(NEVERC)" not in build_ids[0].split():
            report.fail(path, "BUILD_ID must track the selected compiler path")
        if uses_extra and "EXTRA=$(EXTRA)" in build_ids[0].split():
            report.fail(path, "multi-word EXTRA must use its lossless side stamp")
        expected_debug = [
            '+$(MAKE) $(RECURSIVE_MAKE_FLAGS) -f "$(MAKEFILE_LIST)" '
            "-B NVK_RECURSIVE_BUILD=1 PROFILE=debug all"
        ]
        expected_release = [
            '+$(MAKE) $(RECURSIVE_MAKE_FLAGS) -f "$(MAKEFILE_LIST)" '
            "-B NVK_RECURSIVE_BUILD=1 PROFILE=release all"
        ]
        expected_compile = [
            '"$(NEVERC)" $(FLAGS) -r -nostdlib -o $@ $(SRCS)',
        ]
        expected_clean = [
            '$(NEVERC_MAKE_EXECUTABLE) '
            '__neverc_clean_android_kernel_output "$(MODULE)"',
            "rm -f *.o",
        ]
        if make_target_recipes(contents, "debug") != [expected_debug]:
            report.fail(path, "debug must run exactly one explicit-profile build")
        if make_target_recipes(contents, "release") != [expected_release]:
            report.fail(path, "release must run exactly one forced bundle rebuild")
        if make_target_recipes(contents, "$(MODULE)") != [expected_compile]:
            report.fail(
                path,
                "module recipe must invoke exactly one quoted compiler path; "
                "the compiler transaction publishes its build state",
            )
        if make_target_recipes(contents, "clean") != [expected_clean]:
            report.fail(
                path,
                "clean must use the serialized bundle remover before deleting objects",
            )


def release_summary_line(path: Path, report: Report) -> str:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        report.fail(path, f"cannot be read: {error}")
        return ""
    matches = [line for line in lines if "release-builds/README" in line]
    if len(matches) != 1:
        report.fail(path, "must contain exactly one release-builds summary link")
        return ""
    return matches[0]


def check_release_summary_terminology(report: Report) -> None:
    roots = [ROOT / "README.md"] + [
        localized_page(ROOT / "docs/i18n", "README", locale)
        for locale in LOCALES
        if locale
    ]
    indexes = [localized_page(ROOT / "docs", "README", locale) for locale in LOCALES]
    stale = re.compile(
        r"(?i)pseudonym|pseudonim|seudonim|псевд|假名|仮名|가명"
    )
    for path in roots + indexes:
        summary = release_summary_line(path, report)
        if not summary:
            continue
        missing = [token for token in (".ko", "hash", "encryption")
                   if token not in summary]
        if missing:
            report.fail(path, f"release summary misses {missing}")
        if stale.search(summary):
            report.fail(path, "release summary still calls structural names pseudonyms")


def check_names(pages: list[Path], schema: dict, report: Report) -> dict[Path, Counter]:
    phases = {p["name"] for p in schema["phases"]}
    phases |= {p["verifier"] for p in schema["phases"] if p.get("verifier")}
    symbols = declared_symbols()
    citations: dict[Path, Counter] = {}
    for page_path in pages:
        found = prose_citations(page_path)
        citations[page_path] = found
        for span in found:
            base = span.split(".")[0].split("(")[0].strip()
            if PHASE.match(span) and span not in phases:
                report.fail(page_path, f"`{span}` is not a phase in PhaseSchema.json")
            elif SYMBOL.match(base) and base not in symbols:
                report.fail(page_path, f"`{base}` is not declared in a public header")
    return citations


def locale_groups(pages: list[Path]) -> list[dict[str, Path]]:
    """One entry per guide, mapping locale to page; a page with no English
    original has nothing to be compared against and is dropped."""
    groups: dict[tuple[Path, str], dict[str, Path]] = defaultdict(dict)
    for page_path in pages:
        groups[(page_path.parent, stem_of(page_path.name))][
            locale_of(page_path.name)] = page_path
    return [found for _, found in sorted(groups.items()) if "" in found]


def check_locale_parity(
    pages: list[Path], citations: dict[Path, Counter], report: Report
) -> None:
    def technical(found: Counter) -> set[str]:
        names = set()
        for span in found:
            base = span.split(".")[0].split("(")[0].strip()
            if PHASE.match(span) or SYMBOL.match(base):
                names.add(span)
        return names

    for found in locale_groups(pages):
        english = technical(citations[found[""]])
        for locale, page_path in sorted(found.items()):
            if not locale:
                continue
            here = technical(citations[page_path])
            missing = sorted(english - here)
            extra = sorted(here - english)
            if missing or extra:
                report.fail(
                    page_path,
                    f"identifiers differ from English: missing={missing} "
                    f"unexpected={extra}",
                )


def check_statistics(schema: dict, report: Report) -> None:
    phases = schema["phases"]
    policy: Counter = Counter()
    for entry in phases:
        policy.update(entry["policy"])
    domains = Counter(entry["domain"] for entry in phases)
    sealed = sorted(
        e["name"] for e in phases if "SEALED_HOST_GATE" in e["policy"]
    )
    facts = {
        str(len(phases)): "phase total",
        str(policy["INTERCEPTABLE"]): "interceptable count",
        str(policy["REPLACEABLE"]): "replaceable count",
        str(policy["SKIPPABLE_WITH_PROOF"]): "skippable count",
        str(policy["SEALED_HOST_GATE"]): "sealed count",
        str(len(schema.get("extension_families", []))): "extension families",
    }

    for locale in LOCALES:
        path = page("README", locale)
        if not path.exists():
            continue
        text = normalize_digits(path.read_text(encoding="utf-8"))
        for value, meaning in facts.items():
            if not re.search(rf"(?<!\d){value}(?!\d)", text):
                report.fail(path, f"{meaning} ({value}) does not appear")
        for name in sealed:
            short = name[len("neverc."):]
            if short not in text and name not in text:
                report.fail(path, f"sealed gate {name} is not listed")
        # the per-domain table carries raw counts in every locale
        found = {m.group(1): int(m.group(2)) for m in COUNT_ROW.finditer(text)}
        for domain, expected in domains.items():
            if domain in found and found[domain] != expected:
                report.fail(
                    path,
                    f"domain {domain} is listed as {found[domain]}, "
                    f"schema has {expected}",
                )
            elif domain not in found:
                report.fail(path, f"domain {domain} is missing from the phase table")


def check_example_targets(report: Report) -> None:
    cmake = []
    for directory, _, names in os.walk(SDK):
        for name in sorted(names):
            if name == "CMakeLists.txt":
                cmake.append((Path(directory) / name).read_text(encoding="utf-8"))
    defined = "\n".join(cmake)
    readme = page("README", "")
    for target in sorted(set(EXAMPLE_TARGET.findall(
            readme.read_text(encoding="utf-8")))):
        if target not in defined:
            report.fail(readme, f"example target {target} is not defined by the SDK")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true", help="only report failures")
    arguments = parser.parse_args()

    try:
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"check-docs-facts: cannot read phase schema: {error}", file=sys.stderr)
        return 1

    pages = guide_pages()
    if not pages:
        print("check-docs-facts: no documentation pages found", file=sys.stderr)
        return 1

    report = Report()
    citations = check_names(pages, schema, report)
    check_locale_parity(pages, citations, report)
    check_statistics(schema, report)
    check_example_targets(report)
    check_release_source_contract(report)
    check_release_guides(report)
    check_object_pipeline_release_guides(report)
    check_android_example_makefiles(report)
    check_release_summary_terminology(report)

    if report.failures:
        print("check-docs-facts: documentation disagrees with the source:",
              file=sys.stderr)
        for failure in report.failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    if not arguments.quiet:
        cited = len({s for found in citations.values() for s in found})
        print(
            f"check-docs-facts: {len(pages)} plugin pages and "
            f"{len(release_pages())} release locales agree with "
            f"{len(schema['phases'])} schema phases, the public headers, and "
            f"the Android release contract ({cited} distinct citations)"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
