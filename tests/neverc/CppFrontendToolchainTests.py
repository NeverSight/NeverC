#!/usr/bin/env python3
"""Exercise the frontend ABI audit with real, tiny Microsoft COFF archives."""

import argparse
import os
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
                extra_sources=(), prefix_header=None, static_runtime=False):
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
                runtime = ["/MT"] if static_runtime else []
                self.require_success([self.msvc, "/nologo", "/std:c++17", "/c", "/Od", "/GL-",
                                      "/GR-", "/EHsc", *runtime, *prefix,
                                      "/Fo" + str(obj), cpp])
            else:
                prefix = ["-include", prefix_header] if prefix_header else []
                runtime = ["-fms-runtime-lib=static"] if static_runtime else []
                self.require_success([
                    self.clang, "--target=" + self.target, "-std=c++17", "-O2",
                    "-fms-extensions", "-fmerge-all-constants", "-fno-exceptions",
                    "-fno-rtti", *runtime, *prefix, "-c", cpp, "-o", obj,
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

    def defined_declarations(self, archive):
        # Pair actual raw and demangled definitions without depending on sort
        # order or splitting declarations that contain spaces. No fabricated
        # Microsoft spelling is passed to the audit in these fixtures.
        inventories = []
        for decoding in ((), ("--demangle",)):
            output = self.require_success([
                self.nm, "--extern-only", "--defined-only", "--no-sort",
                "--format=just-symbols", *decoding, archive,
            ])
            inventories.append([line for line in output.splitlines()
                                if line and not line.endswith(":")])
        self.assertEqual(len(inventories[0]), len(inventories[1]))
        return dict(zip(*inventories))

    def check_owner_fixture(self, source, patterns, *, msvc=False, rejected=False,
                            case="owner"):
        compiler = "msvc" if msvc else "clang"
        directory = self.root / case / compiler
        private = self.archive(
            directory / "private", "private",
            ENTRY + source.replace("FIXTURE_ANCHOR", "neverc_cpp_owner_anchor"),
            msvc=msvc)
        host = directory / "host"
        host_archive = self.archive(
            host, "host", source.replace("FIXTURE_ANCHOR", "host_owner_anchor"),
            msvc=msvc)
        private_declarations = self.defined_declarations(private)
        host_declarations = self.defined_declarations(host_archive)
        shared = private_declarations.keys() & host_declarations.keys()
        selected = set()
        for pattern in patterns:
            matches = {name for name in shared if name.startswith("?") and
                       re.search(pattern, private_declarations[name])}
            self.assertTrue(
                matches,
                f"The {compiler} fixture must emit shared external definitions "
                f"matching {pattern!r}; actual private declarations: "
                f"{private_declarations!r}; host declarations: {host_declarations!r}")
            selected.update(matches)
        for name in selected:
            self.assertEqual(private_declarations[name], host_declarations[name])
        for host_format in ("nm", "coff-index"):
            with self.subTest(compiler=compiler, host_format=host_format):
                # Rejection deletes its input: each path gets a fresh copy of
                # the real aggregate, so both readers must reach the same gate.
                checked = directory / (host_format + ".lib")
                shutil.copyfile(private, checked)
                if rejected:
                    self.check_audit(
                        checked, host, host_format,
                        error="private/host symbol intersection: " + sorted(selected)[0])
                else:
                    self.check_audit(checked, host, host_format)

    def test_std_local_static_and_guard_follow_the_enclosing_function(self):
        source = """
namespace std {
int owner_seed();
__declspec(noinline) inline int *local_owner() {
  static int local_value = owner_seed();
  return &local_value;
}
}
extern "C" int *FIXTURE_ANCHOR() { return std::local_owner(); }
"""
        # Unknown initialization forces a guard; escaping the address prevents
        # removal of the local. Inline linkage makes both COFF entities public.
        for msvc in (False, True):
            self.check_owner_fixture(source, (
                r"std::local_owner.*::local_value$",
                r"std::local_owner.*(?:::\$TSS[0-9]+$|"
                r"`local static(?: thread)? guard')",
            ), msvc=msvc)

    def test_std_static_pointer_field_has_no_required_space_after_star(self):
        source = """
namespace std {
struct PointerOwner { static int *field; };
int *PointerOwner::field = nullptr;
}
extern "C" int **FIXTURE_ANCHOR() { return &std::PointerOwner::field; }
"""
        # The out-of-line definition and escaped address require storage. The
        # demangler prints '*std::', which is a declarator boundary, not a token
        # whose namespace may be inferred from the preceding pointer type.
        for msvc in (False, True):
            self.check_owner_fixture(
                source, (r"public: static int \*std::PointerOwner::field$",),
                msvc=msvc)

    def test_std_dynamic_initializer_follows_the_initialized_field(self):
        source = """
namespace std {
int *owner_pointer_seed();
struct DynamicOwner { static int *field; };
__declspec(selectany) int *DynamicOwner::field = owner_pointer_seed();
}
extern "C" int **FIXTURE_ANCHOR() { return &std::DynamicOwner::field; }
"""
        # Clang's Microsoft ABI promotes this nonlocal weak variable's guarded
        # initializer to linkonce_odr with its own COMDAT (MicrosoftCXXABI.cpp,
        # EmitCXXGuardedInit; CodeGenCXX/microsoft-abi-static-initializers.cpp).
        # The unknown seed and escaped field require dynamic initialization.
        # Require the real external initializer, not merely the shared field.
        self.check_owner_fixture(source, (
            r"dynamic initializer for .*std::DynamicOwner::field",
        ))

    def test_std_nested_lambda_method_follows_its_enclosing_declarations(self):
        source = """
namespace std {
inline auto lambda_owner() {
  return []() { return [](int value) { return value + 7; }; };
}
}
using OwnerInnerLambda = decltype(std::lambda_owner()());
extern "C" {
int (OwnerInnerLambda::*FIXTURE_ANCHOR)(int) const =
    &OwnerInnerLambda::operator();
}
"""
        # Taking the inner operator's address in an externally visible variable
        # forces an out-of-line method even under the Clang fixture's -O2.
        self.check_owner_fixture(source, (
            r"std::lambda_owner.*<lambda_[^>]+>.*operator\(\).*"
            r"<lambda_[^>]+>.*operator\(\)",
        ))
        # MSVC emits only the lambda hash, losing the enclosing declarations.
        # That spelling cannot establish std ownership: require the real shared
        # operator to be emitted and explicitly rejected by both host readers.
        self.check_owner_fixture(source, (
            r"^public: (?:__cdecl )?<lambda_[0-9a-f]+>::operator\(\)\(int\) const$",
        ), msvc=True, rejected=True)

    def test_host_entities_do_not_inherit_std_ownership_from_their_types(self):
        declarations = "namespace std { struct OwnerPayload { int value; }; }\n"
        cases = (
            ("return", declarations + """
namespace Host {
std::OwnerPayload *return_owner() { return nullptr; }
}
""", r"std::OwnerPayload \*.*Host::return_owner\("),
            ("parameter", declarations + """
namespace Host {
int parameter_owner(std::OwnerPayload *value) { return value != nullptr; }
}
""", r"Host::parameter_owner\(struct std::OwnerPayload \*"),
            ("field", declarations + """
namespace Host {
struct TypedOwner { static std::OwnerPayload *field; };
std::OwnerPayload *TypedOwner::field = nullptr;
}
""", r"std::OwnerPayload \*Host::TypedOwner::field$"),
            ("quoted_type", """
namespace std {
inline auto quoted_type_owner() { return []() { return 7; }; }
}
namespace Host {
using ForeignLambda = decltype(std::quoted_type_owner());
ForeignLambda *quoted_pointer = nullptr;
}
""", r"`[^\n]*std::quoted_type_owner.*<lambda_[^>]+>.*\*Host::quoted_pointer$"),
        )
        for name, source, pattern in cases:
            for msvc in (False, True):
                with self.subTest(case=name, compiler="msvc" if msvc else "clang"):
                    selected_pattern = pattern
                    if name == "quoted_type" and msvc:
                        # MSVC retains the foreign lambda type as a hash, not
                        # Clang's quoted enclosing std declaration. Its owner
                        # is still Host, and the exact raw collision must fail.
                        selected_pattern = (
                            r"^class <lambda_[0-9a-f]+> \*Host::quoted_pointer$")
                    # Each negative archive contains only this Host collision;
                    # another rejected declaration cannot make the case pass.
                    self.check_owner_fixture(
                        source, (selected_pattern,), msvc=msvc,
                        rejected=True, case=name)

    def test_sdk_fenv1_definition_reference_and_runtime_are_isolated(self):
        # Separate from the std-owner cases: this proves only the single UCRT
        # object selected by the runner's actual fenv.h. No copied fenv_t layout,
        # initializer constants, renamed CRT function, or pointer sentinel is
        # used. The __midl header branch suppresses just its compound definition;
        # corecrt.h/float.h have already been included in ordinary compiler mode.
        definition = ENTRY + """
#include <fenv.h>
extern "C" { const fenv_t *neverc_cpp_fenv_anchor = FE_DFL_ENV; }
"""
        reference = """
#include <corecrt.h>
#include <float.h>
#ifdef __midl
#error The fixture must start in ordinary C++ compilation mode
#endif
#define __midl
#include <fenv.h>
#undef __midl
extern "C" const fenv_t _Fenv1;
extern "C" const fenv_t *neverc_cpp_fenv_env() { return FE_DFL_ENV; }
"""
        host_source = """
#include <fenv.h>
extern "C" const fenv_t *host_fenv_env() { return FE_DFL_ENV; }
"""
        probe_source = """
#include <fenv.h>
extern "C" const fenv_t *host_fenv_env();
extern "C" const fenv_t *neverc_cpp_fenv_env();

static int compare_environments(const fenv_t *host, const fenv_t *isolated,
                                const fenv_t *saved) {
  // Exercise a distinct private address, rather than an ICF-merged constant.
  if (!host || !isolated || host == isolated) return 2;
  if (host->_Fe_ctl != isolated->_Fe_ctl ||
      host->_Fe_stat != isolated->_Fe_stat) return 3;
  fenv_t host_after, isolated_after;
  // Start from a nondefault mode on both paths, so a successful no-op cannot
  // impersonate applying the default environment from the supplied object.
  if (fesetround(FE_UPWARD) != 0 || fegetround() != FE_UPWARD) return 10;
  const int host_result = fesetenv(host);
  const int host_round = fegetround();
  if (fegetenv(&host_after) != 0) return 4;
  if (fesetenv(saved) != 0) return 5;
  if (fesetround(FE_UPWARD) != 0 || fegetround() != FE_UPWARD) return 10;
  const int isolated_result = fesetenv(isolated);
  const int isolated_round = fegetround();
  if (fegetenv(&isolated_after) != 0) return 6;
  if (host_result != 0 || isolated_result != host_result) return 7;
  if (host_round != FE_TONEAREST || isolated_round != FE_TONEAREST) return 11;
  if (host_after._Fe_ctl != isolated_after._Fe_ctl ||
      host_after._Fe_stat != isolated_after._Fe_stat) return 8;
  return 0;
}

int main() {
  fenv_t saved;
  if (fegetenv(&saved) != 0) return 1;
  const int result = compare_environments(
      host_fenv_env(), neverc_cpp_fenv_env(), &saved);
  // Every path after the first successful save restores the caller's state.
  const int restore_result = fesetenv(&saved);
  return restore_result == 0 ? result : 9;
}
"""
        header = self.root / "FenvPrivatePrefix.h"
        header.write_text("#define _Fenv1 neverc_cpp__Fenv1\n", encoding="utf-8")
        # These are the developer-environment paths selected by the workflow's
        # MSVC setup for its native target, not hard-coded SDK installation paths.
        library_dirs = [Path(value.strip().strip('"'))
                        for value in os.environ.get("LIB", "").split(";")
                        if value.strip()]
        self.assertTrue(library_dirs, "The native MSVC LIB environment is required")
        library_dirs = [path for path in library_dirs if path.is_dir()]
        for library in ("libcmt.lib", "libucrt.lib", "libvcruntime.lib",
                        "oldnames.lib", "kernel32.lib"):
            self.assertTrue(any((path / library).is_file() for path in library_dirs),
                            f"The runner's native CRT/SDK must provide {library}")
        linker = self.llvm_root / "bin/lld-link.exe"
        print(f"Fenv probe linker: {linker}; exists={linker.is_file()}", flush=True)
        self.assertTrue(linker.is_file(), "The GNU Clang runtime probe requires lld-link")
        for msvc in (False, True):
            compiler = "msvc" if msvc else "clang"
            with self.subTest(compiler=compiler):
                directory = self.root / "fenv1" / compiler
                host = directory / "host"
                host_archive = self.archive(
                    host, "host", host_source, msvc=msvc, static_runtime=True)
                unisolated = self.archive(
                    directory / "unisolated", "private", definition,
                    extra_sources=(reference,), msvc=msvc, static_runtime=True)
                isolated = self.archive(
                    directory / "isolated", "private", definition,
                    extra_sources=(reference,), prefix_header=header,
                    msvc=msvc, static_runtime=True)
                for archive, symbol in ((unisolated, "_Fenv1"),
                                        (isolated, "neverc_cpp__Fenv1")):
                    inventory = self.require_success([
                        self.nm, "--extern-only", "--format=posix", archive,
                    ])
                    self.assertRegex(inventory, r"(?m)^" + symbol + r" R\s")
                    self.assertRegex(inventory, r"(?m)^" + symbol + r" U\s")
                    if archive == isolated:
                        self.assertNotRegex(inventory, r"(?m)^_Fenv1\s")
                host_inventory = self.require_success([
                    self.nm, "--extern-only", "--format=posix", host_archive,
                ])
                self.assertRegex(host_inventory, r"(?m)^_Fenv1 R\s")
                for host_format in ("nm", "coff-index"):
                    with self.subTest(compiler=compiler, host_format=host_format):
                        rejected = directory / (host_format + "-unisolated.lib")
                        shutil.copyfile(unisolated, rejected)
                        self.check_audit(
                            rejected, host, host_format, prefix_header=header,
                            error="private/host symbol intersection: _Fenv1")
                        self.check_audit(isolated, host, host_format,
                                         prefix_header=header)
                        missing = self.archive(
                            directory / (host_format + "-missing"), "private",
                            ENTRY + reference, prefix_header=header,
                            msvc=msvc, static_runtime=True)
                        self.check_audit(
                            missing, host, host_format, prefix_header=header,
                            error="unresolved private dependency: neverc_cpp__Fenv1")
                source = directory / "fenv-probe.cpp"
                source.write_text(probe_source, encoding="utf-8")
                executable = directory / "fenv-probe.exe"
                if msvc:
                    self.require_success([
                        self.msvc, "/nologo", "/std:c++17", "/Od", "/GL-",
                        "/MT", "/EHsc", "/fp:strict", source,
                        isolated, host_archive, "/Fe" + str(executable),
                        "/Fo" + str(directory / "fenv-probe.obj"), "/link",
                        "/OPT:NOICF",
                        *("/libpath:" + str(path) for path in library_dirs),
                    ])
                else:
                    # GNU-mode clang needs an explicit MSVC CRT choice. Its
                    # -fms-runtime-lib=static maps to libcmt. Compile separately
                    # and call the checked absolute linker: the Windows driver
                    # does not use --ld-path to select this tool. Match its
                    # GNU-mode libcmt/oldnames defaults and preserve each native
                    # LIB directory, including spaces, as one argument.
                    obj = directory / "fenv-probe.obj"
                    compile_command = [
                        self.clang, "--target=" + self.target, "-std=c++17",
                        "-O2", "-fno-lto", "-fms-extensions",
                        "-fms-runtime-lib=static", "-ffp-model=strict",
                        "-v", "-c", source, "-o", obj,
                    ]
                    machine = ("x64" if self.target.startswith("x86_64-")
                               else "arm64")
                    link_command = [
                        linker, "/nologo", "/out:" + str(executable),
                        "/machine:" + machine, "/subsystem:console", "/OPT:NOICF",
                        "/defaultlib:libcmt", "/defaultlib:oldnames",
                        *("/libpath:" + str(path) for path in library_dirs),
                        obj, isolated, host_archive,
                    ]
                    for command in (compile_command, link_command):
                        print("Fenv probe command: " + subprocess.list2cmdline(
                            [str(argument) for argument in command]), flush=True)
                        result = self.run_command(command)
                        print(result.stdout + result.stderr, end="", flush=True)
                        self.assertEqual(result.returncode, 0,
                                         f"{command!r}\n{result.stdout}\n{result.stderr}")
                self.require_success([executable])

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
