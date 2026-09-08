#!/usr/bin/env python3
"""Exercise the frontend ABI audit with real, tiny Microsoft COFF archives."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import tarfile
import time
import urllib.request


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
                extra_sources=(), prefix_header=None, static_runtime=False,
                msvc_string_pooling=False):
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
                                      *(["/GF"] if msvc_string_pooling else []),
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
                            case="owner", raw_prefix=None):
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
                       (raw_prefix is None or name.startswith(raw_prefix)) and
                       re.search(pattern, private_declarations[name])}
            self.assertTrue(
                matches,
                f"The {compiler} fixture must emit shared external definitions "
                f"matching {pattern!r} with raw prefix {raw_prefix!r}; "
                f"actual private declarations: "
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

    def test_std_pair_less_template_is_not_a_shift_operator(self):
        source = """
#include <utility>
using Pair = std::pair<int, int>;
using Compare = bool (*)(Pair const &, Pair const &);
extern "C" { Compare FIXTURE_ANCHOR = &std::operator<; }
"""
        # The escaped pointer requires the runner's actual STL specialization
        # even at -O2. Its target type deduces either the two- or four-parameter
        # pair overload; do not hard-code one STL revision's template arity.
        # Microsoft ?M denotes less-than. The following template '<' makes the
        # decoded spelling start with 'operator<<', not a left-shift operator.
        for msvc in (False, True):
            self.check_owner_fixture(source, (
                r"^bool __cdecl std::operator<<(?!<)[^()]*>\("
                r"struct std::pair<int, int> const &, "
                r"struct std::pair<int, int> const &\)$",
            ), msvc=msvc, raw_prefix="??$?M", case="std-pair-less")

    def test_host_pair_less_template_does_not_inherit_std_ownership(self):
        source = """
#include <utility>
namespace Host {
template <class A, class B, class C, class D>
bool operator<(std::pair<A, B> const &left, std::pair<C, D> const &right) {
  return left.first < right.first ||
         (!(right.first < left.first) && left.second < right.second);
}
}
using Pair = std::pair<int, int>;
using Compare = bool (*)(Pair const &, Pair const &);
extern "C" { Compare FIXTURE_ANCHOR = &Host::operator<; }
"""
        # This uses real std::pair parameter types but the declaration belongs
        # to Host. Require that exact shared raw specialization to be rejected;
        # an unrelated collision cannot make the negative case pass.
        for msvc in (False, True):
            self.check_owner_fixture(source, (
                r"^bool __cdecl Host::operator<<int, int, int, int>\("
                r"struct std::pair<int, int> const &, "
                r"struct std::pair<int, int> const &\)$",
            ), msvc=msvc, rejected=True, raw_prefix="??$?M",
                case="host-pair-less")

    def test_shift_operator_model_distinguishes_template_and_plain_names(self):
        # These controlled declarations exercise only operator token boundaries,
        # not a complete STL ABI. Both address initializers force real external
        # definitions; their raw ?6 operator code must differ from pair's ?M.
        model = """
namespace std {
template <class T> struct ShiftBox { T value; };
template <class T>
ShiftBox<T> operator<<(ShiftBox<T> left, int amount) {
  left.value <<= amount;
  return left;
}
ShiftBox<int> operator<<(ShiftBox<int> left, unsigned int amount) {
  left.value <<= amount;
  return left;
}
}
using Box = std::ShiftBox<int>;
"""
        cases = (
            ("template", "int", "??$?6", r"operator<<<int>"),
            ("plain", "unsigned int", "??6", r"operator<<"),
        )
        for case, amount, raw_prefix, spelling in cases:
            source = model + f"""
using Shift = Box (*)(Box, {amount});
extern "C" {{ Shift FIXTURE_ANCHOR = &std::operator<<; }}
"""
            pattern = (r"^struct std::ShiftBox<int> __cdecl std::" + spelling +
                       r"\(struct std::ShiftBox<int>, " + amount + r"\)$")
            for msvc in (False, True):
                with self.subTest(operator=case):
                    self.check_owner_fixture(
                        source, (pattern,), msvc=msvc, raw_prefix=raw_prefix,
                        case="shift-model-" + case)

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

    def test_setup_sdk_inputs_and_object_provenance(self):
        # Evidence collection only: these non-LTO objects are neither linked
        # nor run. Symbol/section data does not prove GUID storage identity,
        # QueryInterface behavior, or equivalence to every production mode.
        repository = Path(__file__).resolve().parents[2]
        pin = repository / "neverc/cmake/modules/BuiltinCppFrontend.cmake"
        archive_url = (
            "https://github.com/llvm/llvm-project/releases/download/"
            "llvmorg-20.1.8/llvm-project-20.1.8.src.tar.xz")
        archive_sha = "6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d"
        archive_size = 147242952
        member_name = (
            "llvm-project-20.1.8.src/llvm/include/llvm/WindowsDriver/"
            "MSVCSetupApi.h")
        header_sha = "d4341b292369b13be4c4b3de1c7dd87bc9f79d3aa25eb80a632d742e20da44a8"
        header_size = 19823
        pin_text = pin.read_text(encoding="utf-8")
        self.assertEqual(re.findall(
            r'^\s*set\(_url "(https://[^"]+)"\)\s*$', pin_text, re.MULTILINE),
            [archive_url], "Update the witness when the production LLVM pin changes")
        self.assertEqual(re.findall(
            r"^\s*URL_HASH SHA256=([0-9a-f]{64})\s*$", pin_text, re.MULTILINE),
            [archive_sha], "The witness must use the production archive hash")

        # This runs only in the existing Windows GitHub toolchain jobs, before
        # the main ExternalProject has extracted its source. Download the same
        # content-addressed release here; never substitute the host LLVM header.
        archive_path = self.root / "llvm-project-20.1.8.src.tar.xz"
        digest = hashlib.sha256()
        downloaded = 0
        started = time.monotonic()
        with urllib.request.urlopen(archive_url, timeout=30) as response:
            self.assertEqual(response.status, 200)
            with archive_path.open("wb") as output:
                while chunk := response.read(1024 * 1024):
                    downloaded += len(chunk)
                    self.assertLessEqual(downloaded, archive_size)
                    self.assertLess(time.monotonic() - started, 300,
                                    "Pinned source download exceeded five minutes")
                    digest.update(chunk)
                    output.write(chunk)
        self.assertEqual(downloaded, archive_size)
        self.assertEqual(digest.hexdigest(), archive_sha)
        print(f"SETUP source archive: url={archive_url} bytes={downloaded} "
              f"sha256={digest.hexdigest()} pin_file_sha256="
              f"{hashlib.sha256(pin.read_bytes()).hexdigest()}", flush=True)

        headers = []
        with tarfile.open(archive_path, mode="r|xz") as archive:
            for member in archive:
                if member.name != member_name:
                    continue
                self.assertTrue(member.isfile(), member.name)
                self.assertEqual(member.size, header_size)
                with archive.extractfile(member) as source:
                    headers.append(source.read(header_size + 1))
        self.assertEqual(len(headers), 1, "Expected one exact release header member")
        header_bytes = headers[0]
        self.assertEqual(len(header_bytes), header_size)
        self.assertEqual(hashlib.sha256(header_bytes).hexdigest(), header_sha)
        include_root = self.root / "pinned-include"
        header = include_root / "llvm/WindowsDriver/MSVCSetupApi.h"
        header.parent.mkdir(parents=True)
        header.write_bytes(header_bytes)
        print(f"SETUP pinned header: member={member_name} bytes={header_size} "
              f"sha256={header_sha} path={header}", flush=True)

        common = """
// Match the Windows include controls in the pinned MSVCPaths.cpp.
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Match MSVCPaths.cpp's required order; let comdef choose its own SDK headers.
#include <comdef.h>
#include "llvm/WindowsDriver/MSVCSetupApi.h"
#include <stddef.h>
static_assert(sizeof(GUID) == 16, "GUID size");
static_assert(offsetof(GUID, Data1) == 0, "GUID Data1 offset");
static_assert(offsetof(GUID, Data2) == 4, "GUID Data2 offset");
static_assert(offsetof(GUID, Data3) == 6, "GUID Data3 offset");
static_assert(offsetof(GUID, Data4) == 8, "GUID Data4 offset");
static_assert(sizeof(void *) == 8, "Native 64-bit witness required");
#define NEVERC_SETUP_STRING_IMPL(x) #x
#define NEVERC_SETUP_STRING(x) NEVERC_SETUP_STRING_IMPL(x)
"""
        architecture_macro = (
            "_M_ARM64" if self.target == "aarch64-pc-windows-msvc" else "_M_X64")
        common += (f"#ifndef {architecture_macro}\n"
                   '#error The active compiler does not match the native CI target\n'
                   "#endif\n")
        for macro in ("_MSC_VER", "_MSC_FULL_VER", "_MT", "_DLL", "_CPPUNWIND",
                      "_HAS_EXCEPTIONS", "_M_X64", "_M_ARM64", "__clang_major__",
                      "__clang_minor__", "__clang_patchlevel__", "__EXCEPTIONS",
                      "WIN32_LEAN_AND_MEAN", "NOGDI", "NOMINMAX"):
            common += (f"#ifdef {macro}\n"
                       f'#pragma message("NEVERC_SETUP_MACRO {macro}=" '
                       f'NEVERC_SETUP_STRING({macro}))\n'
                       "#else\n"
                       f'#pragma message("NEVERC_SETUP_MACRO {macro}=undefined")\n'
                       "#endif\n")
        guid_types = (
            ("unknown", "IUnknown"),
            ("class", "SetupConfiguration"),
            ("configuration", "ISetupConfiguration"),
            ("configuration2", "ISetupConfiguration2"),
            ("helper", "ISetupHelper"),
        )
        values = "".join(
            f'extern "C" const GUID *neverc_cpp_setup_{name}_guid() '
            f'{{ return &__uuidof({kind}); }}\n' for name, kind in guid_types)
        # Keep this TU separate: the explicit value accessors above must not
        # force symbols into the smart-pointer call-shape inventory.
        calls = """
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration, __uuidof(ISetupConfiguration));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration2, __uuidof(ISetupConfiguration2));
_COM_SMARTPTR_TYPEDEF(ISetupHelper, __uuidof(ISetupHelper));
_COM_SMARTPTR_TYPEDEF(IEnumSetupInstances, __uuidof(IEnumSetupInstances));
extern "C" HRESULT neverc_cpp_setup_smart_pointer_shapes(
    BSTR version, ULONGLONG *parsed) {
  ISetupConfigurationPtr query;
  HRESULT result = query.CreateInstance(__uuidof(SetupConfiguration));
  if (FAILED(result)) return result;
  IEnumSetupInstancesPtr instances;
  result = ISetupConfiguration2Ptr(query)->EnumAllInstances(&instances);
  if (FAILED(result)) return result;
  return ISetupHelperPtr(query)->ParseVersion(version, parsed);
}
"""
        expected_guids = {
            "_GUID_00000000_0000_0000_c000_000000000046",
            "_GUID_177f0c4a_1cd3_4de7_a32c_71dbbb9fa36d",
            "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b",
            "_GUID_26aab78c_4a60_49d6_af3b_3c35bc93365d",
            "_GUID_42b21b78_6192_463e_87bf_d577838f1d5c",
        }
        provenance = {}
        for msvc in (False, True):
            compiler = "msvc" if msvc else "clang"
            if not msvc:
                print(self.require_success([self.clang, "--version"]), flush=True)
            for unit, body in (("explicit-values", values), ("smart-pointers", calls)):
                with self.subTest(compiler=compiler, unit=unit):
                    directory = self.root / compiler / unit
                    directory.mkdir(parents=True)
                    source = directory / "setup.cpp"
                    obj = directory / "setup.obj"
                    source.write_text(common + body, encoding="utf-8")
                    if msvc:
                        command = [
                            self.msvc, "/nologo", "/Bv", "/std:c++17", "/c",
                            "/Od", "/GL-", "/GR-", "/EHsc", "/MT", "/showIncludes",
                            "/I" + str(include_root), "/Fo" + str(obj), source,
                        ]
                    else:
                        command = [
                            self.clang, "--target=" + self.target, "-std=c++17",
                            "-O2", "-fno-lto", "-fms-extensions",
                            "-fmerge-all-constants", "-fno-exceptions", "-fno-rtti",
                            "-fms-runtime-lib=static", "-Xclang", "--show-includes",
                            "-Xclang", "-sys-header-deps", "-I", include_root,
                            "-c", source, "-o", obj,
                        ]
                    print(f"SETUP compile: compiler={compiler} unit={unit} "
                          f"target={self.target} command=" + subprocess.list2cmdline(
                              [str(argument) for argument in command]), flush=True)
                    environment = os.environ.copy()
                    if msvc:
                        environment["VSLANG"] = "1033"
                    result = subprocess.run(
                        [str(argument) for argument in command], cwd=self.root,
                        env=environment, capture_output=True, text=True,
                        encoding="utf-8", errors="replace", timeout=120, check=False)
                    output = result.stdout + result.stderr
                    print(output, end="", flush=True)
                    self.assertEqual(result.returncode, 0, output)
                    consumed = {name: set() for name in (
                        "comdef.h", "comip.h", "unknwn.h", "unknwnbase.h",
                        "guiddef.h", "msvcsetupapi.h")}
                    for line in output.splitlines():
                        prefix = "Note: including file:"
                        if not line.startswith(prefix):
                            continue
                        path = Path(line[len(prefix):].lstrip(" "))
                        name = path.name.lower()
                        if name not in consumed:
                            continue
                        self.assertTrue(path.is_absolute(), str(path))
                        path = path.resolve(strict=True)
                        self.assertTrue(path.is_file(), str(path))
                        consumed[name].add(path)
                    for name in ("comdef.h", "msvcsetupapi.h"):
                        self.assertEqual(len(consumed[name]), 1,
                                         f"Actual compilation must trace one {name}")
                    self.assertTrue(consumed["unknwn.h"] or consumed["unknwnbase.h"],
                                    "The actual IUnknown header must be traced")
                    for name, paths in consumed.items():
                        self.assertLessEqual(len(paths), 1, f"Ambiguous {name}: {paths}")
                        if not paths:
                            # Header layout may differ by SDK. Never manufacture
                            # provenance by explicitly including an absent header.
                            print(f"SETUP SDK provenance: compiler={compiler} "
                                  f"unit={unit} header={name} observed=false", flush=True)
                            continue
                        path = next(iter(paths))
                        evidence = (str(path), hashlib.sha256(path.read_bytes()).hexdigest())
                        if name == "msvcsetupapi.h":
                            self.assertEqual(path, header.resolve(strict=True))
                            self.assertEqual(evidence[1], header_sha)
                        if name in provenance:
                            self.assertEqual(evidence, provenance[name],
                                             "Both compilers must consume identical named headers")
                        else:
                            provenance[name] = evidence
                        print(f"SETUP SDK provenance: compiler={compiler} unit={unit} "
                              f"header={name} path={evidence[0]} sha256={evidence[1]}",
                              flush=True)
                    inventory = self.require_success([
                        self.nm, "--extern-only", "--format=posix", obj])
                    print(f"SETUP actual inventory: compiler={compiler} unit={unit} "
                          f"object={obj}\n{inventory}", end="", flush=True)
                    if unit == "explicit-values":
                        definitions = self.defined_declarations(obj)
                        self.assertTrue(expected_guids <= definitions.keys(),
                                        f"Missing actual __uuidof definitions: {definitions}")
                    sections = self.require_success([
                        self.readobj, "--file-headers", "--symbols", "--sections",
                        "--section-data", "--relocations", obj])
                    print(f"SETUP actual COFF data: compiler={compiler} unit={unit} "
                          f"object={obj}\n{sections}", end="", flush=True)
        print("SETUP input/object capture completed; no linked address identity, "
              "COM runtime behavior, or production-mode equivalence was tested.",
              flush=True)

    def test_sdk_url_history_clsids_and_alias_are_isolated(self):
        # These definitions come from the runner's shlguid.h, not fixture GUID
        # initializers. Without INITGUID, its other DEFINE_GUID declarations do
        # not instantiate unrelated SDK objects. GUID_DEFS_ONLY only avoids the
        # automation interfaces; both selectany definitions and SID remain live.
        definition = """
#ifdef INITGUID
#error The fixture must not instantiate all SDK GUIDs
#endif
#include <guiddef.h>
#define GUID_DEFS_ONLY
#include <shlguid.h>
#ifndef SID_SUrlHistory
#error The actual SDK must provide the history alias
#endif
extern "C" const GUID *SIDE_history_definition() { return &CLSID_CUrlHistory; }
extern "C" const GUID *SIDE_both_definition() { return &CLSID_CUrlHistoryBoth; }
extern "C" const GUID *SIDE_sid() { return &SID_SUrlHistory; }
"""
        # A separate, declaration-only TU must have U records, so removing an
        # SDK definition cannot be masked by another selectany header instance.
        reference = ENTRY + """
#include <guiddef.h>
extern "C" const GUID CLSID_CUrlHistory;
extern "C" const GUID CLSID_CUrlHistoryBoth;
extern "C" const GUID *neverc_cpp_history_reference() { return &CLSID_CUrlHistory; }
extern "C" const GUID *neverc_cpp_both_reference() { return &CLSID_CUrlHistoryBoth; }
"""
        symbols = ("CLSID_CUrlHistory", "CLSID_CUrlHistoryBoth")
        header = self.root / "UrlHistoryPrivatePrefix.h"
        header.write_text("".join(f"#define {name} neverc_cpp_{name}\n"
                                  for name in symbols), encoding="utf-8")
        provenance = {}

        def compile_definition(directory, side, msvc, *, isolated=False,
                               missing=None):
            directory.mkdir(parents=True, exist_ok=True)
            source = directory / "sdk-definition.cpp"
            obj = directory / "sdk-definition.obj"
            # Keep the actual SDK initializer even in missing-D cases: rename
            # only the selected definition to a different fixture-private name.
            # The independent reference object still requests its normal name.
            omit = (f"#undef {missing}\n"
                    f"#define {missing} neverc_cpp_fixture_omitted_{missing}\n"
                    if missing else "")
            source.write_text(omit + definition.replace("SIDE", side), encoding="utf-8")
            if msvc:
                command = [
                    self.msvc, "/nologo", "/std:c++17", "/c", "/Od", "/GL-",
                    "/GR-", "/EHsc", "/MT", "/showIncludes",
                    *(["/FI" + str(header)] if isolated else []),
                    "/Fo" + str(obj), source,
                ]
            else:
                command = [
                    self.clang, "--target=" + self.target, "-std=c++17", "-O2",
                    "-fms-extensions", "-fmerge-all-constants", "-fno-exceptions",
                    "-fno-rtti", "-fms-runtime-lib=static",
                    # Match clang-cl /showIncludes: SDK headers are system headers,
                    # so their trace also requires -sys-header-deps.
                    "-Xclang", "--show-includes",
                    "-Xclang", "-sys-header-deps",
                    *(["-include", header] if isolated else []),
                    "-c", source, "-o", obj,
                ]
            print("CLSID SDK compile: " + subprocess.list2cmdline(
                [str(argument) for argument in command]), flush=True)
            compile_environment = os.environ.copy()
            if msvc:
                compile_environment["VSLANG"] = "1033"
            result = subprocess.run(
                [str(argument) for argument in command], cwd=self.root,
                env=compile_environment, capture_output=True, text=True,
                encoding="utf-8", errors="replace", timeout=120, check=False,
            )
            output = result.stdout + result.stderr
            self.assertEqual(result.returncode, 0, output)
            # Both traces belong to this actual object compilation. MSVC's
            # language is fixed above; Clang's MS-style trace has the same
            # literal prefix and leaves the full path unescaped.
            consumed = {name: set() for name in ("guiddef.h", "shlguid.h")}
            for line in output.splitlines():
                prefix = "Note: including file:"
                if not line.startswith(prefix):
                    continue
                path = Path(line[len(prefix):].lstrip(" "))
                if path.name.lower() not in consumed:
                    continue
                self.assertTrue(path.is_absolute(), str(path))
                path = path.resolve(strict=True)
                self.assertTrue(path.is_file(), str(path))
                consumed[path.name.lower()].add(path)
            for name, paths in consumed.items():
                self.assertEqual(len(paths), 1,
                                 f"Actual compilation must trace one {name}: {output}")
                path = next(iter(paths))
                evidence = (str(path), hashlib.sha256(path.read_bytes()).hexdigest())
                print(f"CLSID SDK provenance: compiler={'msvc' if msvc else 'clang'} "
                      f"object={obj} header={name} path={evidence[0]} "
                      f"sha256={evidence[1]}", flush=True)
                if name in provenance:
                    self.assertEqual(evidence, provenance[name],
                                     "Host/private and both compilers must consume the same SDK")
                else:
                    provenance[name] = evidence
            return obj

        def inventory(obj):
            output = self.require_success([
                self.nm, "--extern-only", "--format=posix", obj,
            ])
            print(f"CLSID actual inventory: {obj}\n{output}", end="", flush=True)
            return output

        def assert_symbol(output, name, kind):
            self.assertRegex(output, r"(?m)^" + re.escape(name) + " " + kind + r"\s")

        def pack(directory, name, *objects):
            directory.mkdir(parents=True, exist_ok=True)
            archive = directory / (name + ".lib")
            self.require_success([
                self.librarian, "/nologo", "/out:" + str(archive), *objects,
            ])
            return archive

        probe_source = """
#include <windows.h>
#include <combaseapi.h>
#include <stddef.h>
static_assert(sizeof(GUID) == 16, "SDK GUID size");
static_assert(offsetof(GUID, Data1) == 0, "SDK GUID Data1 offset");
static_assert(offsetof(GUID, Data2) == 4, "SDK GUID Data2 offset");
static_assert(offsetof(GUID, Data3) == 6, "SDK GUID Data3 offset");
static_assert(offsetof(GUID, Data4) == 8, "SDK GUID Data4 offset");
extern "C" const GUID *host_history_definition();
extern "C" const GUID *host_both_definition();
extern "C" const GUID *host_sid();
extern "C" const GUID *neverc_cpp_history_definition();
extern "C" const GUID *neverc_cpp_both_definition();
extern "C" const GUID *neverc_cpp_sid();
extern "C" const GUID *neverc_cpp_history_reference();
extern "C" const GUID *neverc_cpp_both_reference();

static wchar_t upper_hex(wchar_t c) {
  return c >= L'a' && c <= L'f' ? c - L'a' + L'A' : c;
}

static int compare_slot(const GUID *host, const GUID *isolated,
                        const wchar_t *expected) {
  if (!host || !isolated || host == isolated) return 1;
  if (host->Data1 != isolated->Data1 || host->Data2 != isolated->Data2 ||
      host->Data3 != isolated->Data3) return 2;
  for (unsigned int i = 0; i != 8; ++i)
    if (host->Data4[i] != isolated->Data4[i]) return 3;
  wchar_t host_text[40], isolated_text[40];
  for (unsigned int i = 0; i != 40; ++i) {
    host_text[i] = L'!';
    isolated_text[i] = L'?';
  }
  const int host_length = StringFromGUID2(*host, host_text, 40);
  const int isolated_length = StringFromGUID2(*isolated, isolated_text, 40);
  if (host_length != 39 || isolated_length != 39 ||
      host_text[38] != 0 || isolated_text[38] != 0) return 4;
  // Compare every code unit, including NUL, before normalizing hexadecimal
  // letter case against a separate expected value for each fixed slot.
  for (unsigned int i = 0; i != 39; ++i) {
    if (host_text[i] != isolated_text[i]) return 5;
    if (upper_hex(host_text[i]) != expected[i] ||
        upper_hex(isolated_text[i]) != expected[i]) return 6;
  }
  return 0;
}

int main() {
  const GUID *host_history = host_history_definition();
  const GUID *host_both = host_both_definition();
  const GUID *private_history = neverc_cpp_history_reference();
  const GUID *private_both = neverc_cpp_both_reference();
  if (private_history != neverc_cpp_history_definition() ||
      private_both != neverc_cpp_both_definition()) return 7;
  if (host_history == host_both || private_history == private_both) return 8;
  if (host_sid() != host_history || host_sid() == host_both ||
      neverc_cpp_sid() != private_history || neverc_cpp_sid() == private_both)
    return 9;
  const int history = compare_slot(host_history, private_history,
      L"{3C374A40-BAE4-11CF-BF7D-00AA006946EE}");
  if (history) return 10 + history;
  const int both = compare_slot(host_both, private_both,
      L"{6659983C-8476-4EB4-B78C-E5968F326BA0}");
  return both ? 20 + both : 0;
}
"""
        library_dirs = [Path(value.strip().strip('"'))
                        for value in os.environ.get("LIB", "").split(";")
                        if value.strip()]
        self.assertTrue(library_dirs, "The native MSVC LIB environment is required")
        library_dirs = [path for path in library_dirs if path.is_dir()]
        for library in ("libcmt.lib", "libucrt.lib", "libvcruntime.lib",
                        "oldnames.lib", "kernel32.lib", "ole32.lib"):
            self.assertTrue(any((path / library).is_file() for path in library_dirs),
                            f"The runner's native CRT/SDK must provide {library}")
        linker = self.llvm_root / "bin/lld-link.exe"
        print(f"CLSID probe linker: {linker}; exists={linker.is_file()}", flush=True)
        self.assertTrue(linker.is_file(), "The GNU Clang runtime probe requires lld-link")
        for msvc in (False, True):
            compiler = "msvc" if msvc else "clang"
            with self.subTest(compiler=compiler):
                directory = self.root / "url-history" / compiler
                host = directory / "host"
                host_obj = compile_definition(host, "host", msvc)
                host_archive = pack(host, "host", host_obj)
                host_inventory = inventory(host_obj)
                for name in symbols:
                    assert_symbol(host_inventory, name, "R")
                    self.assertNotRegex(host_inventory, r"(?m)^" + name + r" U\s")
                archives = {}
                reference_objects = {}
                for isolated in (False, True):
                    mode = "isolated" if isolated else "unisolated"
                    current = directory / mode
                    definition_obj = compile_definition(
                        current, "neverc_cpp", msvc, isolated=isolated)
                    ref_archive = self.archive(
                        current / "reference", "reference", reference,
                        prefix_header=header if isolated else None,
                        msvc=msvc, static_runtime=True)
                    reference_obj = ref_archive.with_suffix(".obj")
                    definition_inventory = inventory(definition_obj)
                    reference_inventory = inventory(reference_obj)
                    for name in symbols:
                        raw = "neverc_cpp_" + name if isolated else name
                        assert_symbol(definition_inventory, raw, "R")
                        self.assertNotRegex(definition_inventory,
                                            r"(?m)^" + raw + r" U\s")
                        assert_symbol(reference_inventory, raw, "U")
                        # Match any non-U kind, not only one anticipated D kind.
                        self.assertNotRegex(reference_inventory,
                                            r"(?m)^" + raw + r" (?!U\s)\S\s")
                    archives[isolated] = pack(
                        current, "private", definition_obj, reference_obj)
                    reference_objects[isolated] = reference_obj
                    if isolated:
                        aggregate_inventory = inventory(archives[isolated])
                        for name in symbols:
                            self.assertNotRegex(aggregate_inventory,
                                                r"(?m)^" + name + r"\s")
                for host_format in ("nm", "coff-index"):
                    for name in symbols:
                        with self.subTest(compiler=compiler, host_format=host_format,
                                          rejected=name):
                            rejected = directory / (host_format + "-" + name + ".lib")
                            shutil.copyfile(archives[False], rejected)
                            self.check_audit(
                                rejected, host, host_format, prefix_header=header,
                                error="private/host symbol intersection: " + name +
                                      "; private_demangled=")
                    self.check_audit(archives[True], host, host_format,
                                     prefix_header=header)
                for omitted in symbols:
                    current = directory / ("missing-" + omitted)
                    definition_obj = compile_definition(
                        current, "neverc_cpp", msvc, isolated=True, missing=omitted)
                    defined = inventory(definition_obj)
                    wanted = "neverc_cpp_" + omitted
                    self.assertNotRegex(defined, r"(?m)^" + wanted + r"\s")
                    assert_symbol(defined, "neverc_cpp_fixture_omitted_" + omitted, "R")
                    for name in symbols:
                        if name != omitted:
                            assert_symbol(defined, "neverc_cpp_" + name, "R")
                    missing_archive = pack(
                        current, "private", definition_obj, reference_objects[True])
                    missing_inventory = inventory(missing_archive)
                    assert_symbol(missing_inventory, wanted, "U")
                    self.assertNotRegex(missing_inventory,
                                        r"(?m)^" + wanted + r" (?!U\s)\S\s")
                    for host_format in ("nm", "coff-index"):
                        with self.subTest(compiler=compiler, host_format=host_format,
                                          missing=omitted):
                            rejected = current / (host_format + ".lib")
                            shutil.copyfile(missing_archive, rejected)
                            self.check_audit(
                                rejected, host, host_format, prefix_header=header,
                                error="unresolved private dependency: " + wanted + "\n")
                source = directory / "clsid-probe.cpp"
                source.write_text(probe_source, encoding="utf-8")
                executable = directory / "clsid-probe.exe"
                obj = directory / "clsid-probe.obj"
                if msvc:
                    commands = [[
                        self.msvc, "/nologo", "/std:c++17", "/Od", "/GL-",
                        "/MT", "/EHsc", source, archives[True], host_archive,
                        "/Fe" + str(executable), "/Fo" + str(obj), "/link",
                        "/OPT:NOICF", "ole32.lib",
                        *("/libpath:" + str(path) for path in library_dirs),
                    ]]
                else:
                    # Match the existing Fenv fixture's native static CRT and
                    # absolute linker. No uuid.lib or COM activation is needed.
                    machine = "x64" if self.target.startswith("x86_64-") else "arm64"
                    commands = [[
                        self.clang, "--target=" + self.target, "-std=c++17",
                        "-O2", "-fno-lto", "-fms-extensions",
                        "-fms-runtime-lib=static", "-v", "-c", source, "-o", obj,
                    ], [
                        linker, "/nologo", "/out:" + str(executable),
                        "/machine:" + machine, "/subsystem:console", "/OPT:NOICF",
                        "/defaultlib:libcmt", "/defaultlib:oldnames",
                        *("/libpath:" + str(path) for path in library_dirs),
                        obj, archives[True], host_archive, "ole32.lib",
                    ]]
                for command in commands:
                    print("CLSID probe command: " + subprocess.list2cmdline(
                        [str(argument) for argument in command]), flush=True)
                    result = self.run_command(command)
                    print(result.stdout + result.stderr, end="", flush=True)
                    self.assertEqual(result.returncode, 0,
                                     f"{command!r}\n{result.stdout}\n{result.stderr}")
                self.require_success([executable])
                print(f"CLSID runtime PASS: compiler={compiler}; both distinct addresses, "
                      "SID alias, all GUID fields and complete strings", flush=True)

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

    def test_pointer_bounds_model_rename_and_alias_preserve_private_closure(self):
        # This tiny model checks mangled owners and archive closure only. Its
        # integer handle does not model LLVM TrackingVH lifetime semantics.
        # Separate translation units require real references to both methods;
        # their out-of-line definitions must appear as strong external text.
        prefix = self.root / "PointerBoundsModelPrefix.h"
        prefix.write_text("#define llvm neverc_cpp_llvm\n", encoding="utf-8")

        def sources(isolated, anchor):
            record = "neverc_cpp_PointerBounds" if isolated else "PointerBounds"
            declaration = """
namespace llvm {
struct ModelHandle {
  int value;
  ModelHandle &operator=(ModelHandle &&other);
};
}
""" + f"""
struct {record} {{
  llvm::ModelHandle handle;
  {record} &operator=({record} &&other);
}};
"""
            if isolated:
                declaration += "using PointerBounds = neverc_cpp_PointerBounds;\n"
            reference = declaration + f"""
extern "C" PointerBounds *{anchor}(PointerBounds *target, PointerBounds *source) {{
  *target = static_cast<PointerBounds &&>(*source);
  return target;
}}
"""
            record_body = declaration + """
PointerBounds &PointerBounds::operator=(PointerBounds &&other) {
  handle = static_cast<llvm::ModelHandle &&>(other.handle);
  return *this;
}
"""
            handle_body = declaration + """
llvm::ModelHandle &llvm::ModelHandle::operator=(llvm::ModelHandle &&other) {
  value = other.value;
  return *this;
}
"""
            return reference, record_body, handle_body

        def inventory(library):
            return self.require_success([
                self.nm, "--extern-only", "--format=posix", library])

        def require_method(library, raw_prefix, owner):
            declarations = self.defined_declarations(library)
            matches = [raw for raw, decoded in declarations.items()
                       if raw.startswith(raw_prefix) and
                       owner + "::operator=(" in decoded]
            self.assertEqual(len(matches), 1, declarations)
            return matches[0]

        def require_kinds(output, raw, *, definition, reference):
            for kind, present in (("T", definition), ("U", reference)):
                pattern = r"(?m)^" + re.escape(raw) + " " + kind + r"\s"
                if present:
                    self.assertRegex(output, pattern)
                else:
                    self.assertNotRegex(output, pattern)

        for msvc in (False, True):
            compiler = "msvc" if msvc else "clang"
            with self.subTest(compiler=compiler):
                directory = self.root / "pointer-bounds-model" / compiler
                host = directory / "host"
                host_sources = sources(False, "host_bounds_assign")
                host_archive = self.archive(
                    host, "host", host_sources[0], extra_sources=host_sources[1:],
                    msvc=msvc)
                original_sources = sources(False, "neverc_cpp_bounds_assign")
                isolated_sources = sources(True, "neverc_cpp_bounds_assign")

                def build(case, selected):
                    return self.archive(
                        directory / case, "private", ENTRY + selected[0],
                        extra_sources=selected[1:], prefix_header=prefix, msvc=msvc)

                original = build("original", original_sources)
                isolated = build("isolated", isolated_sources)
                original_raw = require_method(
                    original, "??4PointerBounds@@", "PointerBounds")
                self.assertEqual(original_raw, require_method(
                    host_archive, "??4PointerBounds@@", "PointerBounds"))
                isolated_raw = require_method(
                    isolated, "??4neverc_cpp_PointerBounds@@", "neverc_cpp_PointerBounds")
                handle_raw = require_method(
                    isolated, "??4ModelHandle@neverc_cpp_llvm@@",
                    "neverc_cpp_llvm::ModelHandle")
                require_kinds(inventory(original), original_raw,
                              definition=True, reference=True)
                isolated_inventory = inventory(isolated)
                require_kinds(isolated_inventory, isolated_raw,
                              definition=True, reference=True)
                require_kinds(isolated_inventory, handle_raw,
                              definition=True, reference=True)
                self.assertNotIn(original_raw, isolated_inventory)

                # Original definitions and reference-only leaks are rejected
                # even without any matching host symbol.
                no_host = directory / "original-no-host.lib"
                shutil.copyfile(original, no_host)
                self.check_audit(no_host, prefix_header=prefix,
                                 error=original_raw + " => ")
                original_reference = build("original-reference", original_sources[:1])
                require_kinds(inventory(original_reference), original_raw,
                              definition=False, reference=True)
                self.check_audit(original_reference, prefix_header=prefix,
                                 error=original_raw + " => ")

                for host_format in ("nm", "coff-index"):
                    with self.subTest(compiler=compiler, host_format=host_format):
                        rejected = directory / (host_format + "-original.lib")
                        shutil.copyfile(original, rejected)
                        self.check_audit(rejected, host, host_format,
                                         prefix_header=prefix, error=original_raw + " => ")
                        self.check_audit(isolated, host, host_format,
                                         prefix_header=prefix)
                        missing_record = build(host_format + "-missing-record", (
                            isolated_sources[0], isolated_sources[2]))
                        require_kinds(inventory(missing_record), isolated_raw,
                                      definition=False, reference=True)
                        self.check_audit(
                            missing_record, host, host_format, prefix_header=prefix,
                            error="unresolved private dependency: " + isolated_raw)
                        missing_handle = build(host_format + "-missing-handle",
                                               isolated_sources[:2])
                        missing_inventory = inventory(missing_handle)
                        require_kinds(missing_inventory, isolated_raw,
                                      definition=True, reference=True)
                        require_kinds(missing_inventory, handle_raw,
                                      definition=False, reference=True)
                        self.check_audit(
                            missing_handle, host, host_format, prefix_header=prefix,
                            error="unresolved private dependency: " + handle_raw)

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

    def test_msvc_empty_literal_requires_exact_shared_identity_and_runtime_value(self):
        # /GF must emit the observed zero-payload spelling, not merely some
        # shared nonempty literal that happens to pass the existing grammar.
        raw, decoded = "??_C@_00CNPNBAHC@@", '""...'
        private = self.archive(
            self.root / "private", "private",
            ENTRY + 'extern "C" const char *neverc_cpp_empty() { return ""; }\n',
            msvc=True, static_runtime=True, msvc_string_pooling=True)
        host = self.root / "host"
        host_archive = self.archive(
            host, "host", 'extern "C" const char *host_empty() { return ""; }\n',
            msvc=True, static_runtime=True, msvc_string_pooling=True)
        for archive in (private, host_archive):
            declarations = self.defined_declarations(archive)
            self.assertIn(raw, declarations,
                          f"MSVC /GF must emit the exact empty-string definition: "
                          f"{declarations!r}")
            self.assertEqual(declarations[raw], decoded)
            output = self.require_success([
                self.nm, "--extern-only", "--format=posix", archive,
            ])
            records = [line for line in output.splitlines()
                       if line.split() and line.split()[0] == raw]
            self.assertTrue(records, output)
            self.assertEqual({line.split()[1] for line in records}, {"R"}, output)
            print(f"MSVC empty literal provenance: archive={str(archive)!r} "
                  f"symbol={raw!r} decoded={declarations[raw]!r} "
                  f"nm_records={records!r}", flush=True)
        for host_format in ("nm", "coff-index"):
            with self.subTest(host_format=host_format):
                checked = self.root / (host_format + ".lib")
                shutil.copyfile(private, checked)
                self.check_audit(checked, host, host_format)

        # Keep literals out of the harness. Both archive members must be
        # extracted, and /OPT:NOICF prevents function folding from supplying
        # an alternative reason for the two returned addresses to be equal.
        source = self.root / "empty-probe.cpp"
        source.write_text('''
extern "C" const char *neverc_cpp_empty();
extern "C" const char *host_empty();
int main() {
  const char *private_value = neverc_cpp_empty();
  const char *host_value = host_empty();
  if (!private_value || !host_value) return 1;
  if (*private_value != 0 || *host_value != 0) return 2;
  if (private_value != host_value) return 3;
  return 0;
}
''', encoding="utf-8")
        for order, archives in (("private-first", (private, host_archive)),
                                ("host-first", (host_archive, private))):
            with self.subTest(link_order=order):
                executable = self.root / (order + ".exe")
                command = [
                    self.msvc, "/nologo", "/std:c++17", "/Od", "/GL-",
                    "/MT", "/EHsc", source, *archives,
                    "/Fe" + str(executable),
                    "/Fo" + str(self.root / (order + ".obj")), "/link", "/OPT:NOICF",
                ]
                print("MSVC empty literal link: " + subprocess.list2cmdline(
                    [str(argument) for argument in command]), flush=True)
                self.require_success(command)
                self.require_success([executable])
                print(f"MSVC empty literal runtime PASS: {order}; both nonnull, "
                      "NUL-valued and coalesced under /OPT:NOICF", flush=True)

    def test_cmath_explicit_double_calls_preserve_values_and_remove_templates(self):
        # This is a bounded SDK/compiler experiment, not a claim that these
        # fixture flags reproduce every production LLVM compile command.
        expected = {
            "??$log10@H$0A@@@YANH@Z": "double __cdecl log10<int, 0>(int)",
            "??$pow@HH$0A@@@YANHH@Z": "double __cdecl pow<int, int, 0>(int, int)",
            "??$pow@MH$0A@@@YANMH@Z": "double __cdecl pow<float, int, 0>(float, int)",
            "??$pow@NH$0A@@@YANNH@Z": "double __cdecl pow<double, int, 0>(double, int)",
        }
        preamble = r"""
#include <cmath>
#include <type_traits>
// Check ordinary overload resolution separately from explicitly taking the
// templates' addresses. These decltype operands never execute math calls.
static_assert(std::is_same_v<decltype(std::log10(0)), double>);
static_assert(std::is_same_v<decltype(std::pow(0, 0)), double>);
static_assert(std::is_same_v<decltype(std::pow(0.0f, 0)), double>);
static_assert(std::is_same_v<decltype(std::pow(0.0, 0)), double>);
#if !defined(_MT) || defined(_DLL)
#error This fixture requires the static Microsoft CRT
#endif
#define NC_STRING_IMPL(x) #x
#define NC_STRING(x) NC_STRING_IMPL(x)
#pragma message("NEVERC_CMATH_MACROS _MSC_VER=" NC_STRING(_MSC_VER) \
 ";_MSC_FULL_VER=" NC_STRING(_MSC_FULL_VER) \
 ";_MSVC_STL_VERSION=" NC_STRING(_MSVC_STL_VERSION) \
 ";_MSVC_STL_UPDATE=" NC_STRING(_MSVC_STL_UPDATE) \
 ";_MSVC_LANG=" NC_STRING(_MSVC_LANG) \
 ";_HAS_EXCEPTIONS=" NC_STRING(_HAS_EXCEPTIONS) ";_MT=" NC_STRING(_MT))
#if defined(__clang__)
#pragma STDC FENV_ACCESS ON
#else
#pragma fenv_access(on)
#endif
"""
        preamble += ("#if !defined(_M_ARM64)\n" if self.target.startswith("aarch64-")
                     else "#if !defined(_M_X64)\n")
        preamble += "#error The compiler must use the requested native target\n#endif\n"
        wrappers = r"""
static int SIDE_integer(int value, unsigned *calls) { ++*calls; return value; }
static float SIDE_single(float value, unsigned *calls) { ++*calls; return value; }
static double SIDE_double(double value, unsigned *calls) { ++*calls; return value; }
extern "C" double SIDE_log(int depth, unsigned *calls) {
  // Signals evaluates this only inside its positive-Depth loop.
  if (depth <= 0) return 0.0;
  return LOG_EXPRESSION;
}
extern "C" double SIDE_ap(int weight, int negate, unsigned *calls) {
  // The caller supplies the signed-13-bit domain. Do not store the negated
  // -4096 value back into that bit field: the original negation has type int.
  if (negate) return NEGATIVE_AP_EXPRESSION;
  return POSITIVE_AP_EXPRESSION;
}
extern "C" float SIDE_fp(float base, int exponent, unsigned *a, unsigned *b) {
  // base is the already-converted float, not APFloat converted straight to
  // double. The outer narrowing remains present. This does not model half.
  return static_cast<float>(FLOAT_EXPRESSION);
}
extern "C" double SIDE_dp(double base, int exponent, unsigned *a, unsigned *b) {
  return DOUBLE_EXPRESSION;
}
"""
        escapes = r"""
extern "C" {
double (*SIDE_keep_log)(int) = static_cast<double (*)(int)>(&std::log10<int>);
double (*SIDE_keep_ii)(int, int) =
    static_cast<double (*)(int, int)>(&std::pow<int, int>);
double (*SIDE_keep_fi)(float, int) =
    static_cast<double (*)(float, int)>(&std::pow<float, int>);
double (*SIDE_keep_di)(double, int) =
    static_cast<double (*)(double, int)>(&std::pow<double, int>);
}
"""

        def wrapper_source(side, changed):
            expressions = {
                "LOG_EXPRESSION": "std::log10(SIDE_integer(depth, calls))",
                "NEGATIVE_AP_EXPRESSION": "std::pow(2, -SIDE_integer(weight, calls))",
                "POSITIVE_AP_EXPRESSION": "std::pow(2, SIDE_integer(weight, calls))",
                "FLOAT_EXPRESSION": (
                    "std::pow(SIDE_single(base, a), SIDE_integer(exponent, b))"),
                "DOUBLE_EXPRESSION": (
                    "std::pow(SIDE_double(base, a), SIDE_integer(exponent, b))"),
            }
            if changed:
                expressions = {
                    "LOG_EXPRESSION": (
                        "std::log10(static_cast<double>(SIDE_integer(depth, calls)))"),
                    "NEGATIVE_AP_EXPRESSION": (
                        "std::pow(2.0, static_cast<double>(-SIDE_integer(weight, calls)))"),
                    "POSITIVE_AP_EXPRESSION": (
                        "std::pow(2.0, static_cast<double>(SIDE_integer(weight, calls)))"),
                    "FLOAT_EXPRESSION": (
                        "std::pow(static_cast<double>(SIDE_single(base, a)), "
                        "static_cast<double>(SIDE_integer(exponent, b)))"),
                    "DOUBLE_EXPRESSION": (
                        "std::pow(SIDE_double(base, a), "
                        "static_cast<double>(SIDE_integer(exponent, b)))"),
                }
            result = wrappers
            for token, expression in expressions.items():
                result = result.replace(token, expression)
            return (result + ("" if changed else escapes)).replace("SIDE", side)

        harness = r"""
#include <cerrno>
#include <cfenv>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
static_assert(CHAR_BIT == 8 && sizeof(int) == 4 && INT_MAX == 2147483647);
static_assert(sizeof(float) == 4 && sizeof(double) == 8);
static_assert(std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<float>::radix == 2 &&
              std::numeric_limits<float>::digits == 24 &&
              std::numeric_limits<float>::min_exponent == -125 &&
              std::numeric_limits<float>::max_exponent == 128);
static_assert(std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::radix == 2 &&
              std::numeric_limits<double>::digits == 53 &&
              std::numeric_limits<double>::min_exponent == -1021 &&
              std::numeric_limits<double>::max_exponent == 1024);
#define DECLARE(SIDE) \
extern "C" double SIDE##_log(int, unsigned *); \
extern "C" double SIDE##_ap(int, int, unsigned *); \
extern "C" float SIDE##_fp(float, int, unsigned *, unsigned *); \
extern "C" double SIDE##_dp(double, int, unsigned *, unsigned *);
DECLARE(neverc_cpp_original)
DECLARE(neverc_cpp_changed)
#undef DECLARE
struct Input { unsigned kind; std::uint64_t bits; int argument; };
struct Observation {
  std::uint64_t bits;
  int error, flags, rounding;
  unsigned a, b;
};
// kind: guarded log10, positive AP scale, negative AP scale, float powi, double powi.
static int observe(const Input &in, bool changed, int mode, int initial,
                   Observation &out) {
  fenv_t saved;
  if (std::feholdexcept(&saved) != 0) return 10;
  int status = 0;
  if (std::fesetround(mode) != 0 || std::fegetround() != mode ||
      std::feclearexcept(FE_ALL_EXCEPT) != 0 ||
      (initial && std::feraiseexcept(initial) != 0) ||
      std::fetestexcept(FE_ALL_EXCEPT) != initial) {
    status = 11;
  } else {
    out.a = out.b = 0;
    errno = 73;
    if (in.kind == 3) {
      const std::uint32_t raw = static_cast<std::uint32_t>(in.bits);
      float base;
      std::memcpy(&base, &raw, sizeof(base));
      float value = changed
          ? neverc_cpp_changed_fp(base, in.argument, &out.a, &out.b)
          : neverc_cpp_original_fp(base, in.argument, &out.a, &out.b);
      out.error = errno;
      out.flags = std::fetestexcept(FE_ALL_EXCEPT);
      out.rounding = std::fegetround();
      std::uint32_t bits;
      std::memcpy(&bits, &value, sizeof(bits));
      out.bits = bits;
    } else {
      double value;
      if (in.kind == 0)
        value = changed ? neverc_cpp_changed_log(in.argument, &out.a)
                        : neverc_cpp_original_log(in.argument, &out.a);
      else if (in.kind == 1 || in.kind == 2)
        value = changed ? neverc_cpp_changed_ap(in.argument, in.kind == 2, &out.a)
                        : neverc_cpp_original_ap(in.argument, in.kind == 2, &out.a);
      else {
        double base;
        std::memcpy(&base, &in.bits, sizeof(base));
        value = changed
            ? neverc_cpp_changed_dp(base, in.argument, &out.a, &out.b)
            : neverc_cpp_original_dp(base, in.argument, &out.a, &out.b);
      }
      out.error = errno;
      out.flags = std::fetestexcept(FE_ALL_EXCEPT);
      out.rounding = std::fegetround();
      std::memcpy(&out.bits, &value, sizeof(value));
    }
    const unsigned expected_a = in.kind == 0 && in.argument <= 0 ? 0 : 1;
    const unsigned expected_b = in.kind >= 3 ? 1 : 0;
    if (out.a != expected_a || out.b != expected_b || out.rounding != mode ||
        (out.flags & initial) != initial)
      status = 12;
    // No log call, floating exception, errno change, or getter evaluation may
    // sneak past the nonpositive Depth guard.
    if (in.kind == 0 && in.argument <= 0 &&
        (out.bits != 0 || out.error != 73 || out.flags != initial))
      status = 13;
  }
  // Restore, rather than re-raise measured flags with feupdateenv.
  const int restored = std::fesetenv(&saved);
  return restored == 0 ? status : 14;
}
static int compare(Input in, int mode, int initial, unsigned &pairs) {
  Observation before{}, after{};
  int status = observe(in, false, mode, initial, before);
  if (!status) status = observe(in, true, mode, initial, after);
  if (!status && (before.bits != after.bits || before.error != after.error ||
                 before.flags != after.flags || before.a != after.a ||
                 before.b != after.b || before.rounding != after.rounding))
    status = 20;
  if (status) {
    std::printf("CMATH mismatch status=%d kind=%u input=%llx exponent=%d "
                "round=%d initial=%d before=%llx/%d/%d/%u/%u "
                "after=%llx/%d/%d/%u/%u\n",
                status, in.kind, static_cast<unsigned long long>(in.bits),
                in.argument, mode, initial,
                static_cast<unsigned long long>(before.bits), before.error,
                before.flags, before.a, before.b,
                static_cast<unsigned long long>(after.bits), after.error,
                after.flags, after.a, after.b);
    return status;
  }
  ++pairs;
  return 0;
}
template <unsigned B, unsigned E>
static int group(unsigned kind, const std::uint64_t (&bases)[B],
                 const int (&exponents)[E], int mode, int initial, unsigned &pairs) {
  for (std::uint64_t base : bases)
    for (int exponent : exponents) {
      const int status = compare({kind, base, exponent}, mode, initial, pairs);
      if (status) return status;
    }
  return 0;
}
static int run(unsigned &pairs) {
  const int depths[] = {1, 9, 10, 99, 100, 1023, INT_MAX, 0, -1, INT_MIN};
  const int weights[] = {-4096, -1075, -1074, -1022, -1, 0, 1, 1023, 1024, 4095};
  const std::uint64_t doubles[] = {
    0x0000000000000000ULL, 0x8000000000000000ULL,
    0x3ff0000000000000ULL, 0xbff0000000000000ULL,
    0x4000000000000000ULL, 0xc000000000000000ULL,
    0x0000000000000001ULL, 0x8000000000000001ULL,
    0x7fefffffffffffffULL, 0xffefffffffffffffULL,
    0x7ff0000000000000ULL, 0xfff0000000000000ULL,
    0x7ff8000000000123ULL, 0xfff8000000000123ULL
  };
  const int double_exponents[] = {
    INT_MIN, -1075, -1074, -1022, -3, -2, -1, 0, 1, 2, 3, 1023, 1024, INT_MAX
  };
  // These float domains avoid a finite double result outside float's range
  // before the retained outer cast. Do not add float-max squared or 2^128.
  const std::uint64_t maxima[] = {0x7f7fffffULL, 0xff7fffffULL};
  const int max_exponents[] = {-1, 0, 1};
  const std::uint64_t ones[] = {0x3f800000ULL, 0xbf800000ULL};
  const int one_exponents[] = {INT_MIN, -3, -2, -1, 0, 1, 2, 3, INT_MAX};
  const std::uint64_t specials[] = {
    0, 0x80000000ULL, 0x7f800000ULL, 0xff800000ULL, 0x7fc00123ULL, 0xffc00123ULL
  };
  const int special_exponents[] = {-3, -2, -1, 0, 1, 2, 3};
  const std::uint64_t twos[] = {0x40000000ULL, 0xc0000000ULL};
  const int two_exponents[] = {-149, -126, -1, 0, 1, 2, 3, 127};
  const std::uint64_t tiny[] = {1, 0x80000001ULL};
  const int tiny_exponents[] = {0, 1};
  const int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
  const int initial_flags[] = {0, FE_DIVBYZERO};
  for (int mode : modes)
    for (int initial : initial_flags) {
      for (int depth : depths) {
        const int status = compare({0, 0, depth}, mode, initial, pairs);
        if (status) return status;
      }
      for (int weight : weights)
        for (unsigned kind = 1; kind <= 2; ++kind) {
          const int status = compare({kind, 0, weight}, mode, initial, pairs);
          if (status) return status;
        }
      int status = group(4, doubles, double_exponents, mode, initial, pairs);
      if (!status) status = group(3, maxima, max_exponents, mode, initial, pairs);
      if (!status) status = group(3, ones, one_exponents, mode, initial, pairs);
      if (!status) status = group(3, specials, special_exponents, mode, initial, pairs);
      if (!status) status = group(3, twos, two_exponents, mode, initial, pairs);
      if (!status) status = group(3, tiny, tiny_exponents, mode, initial, pairs);
      if (status) return status;
    }
  return pairs == 2496 ? 0 : 21;
}
int main() {
  fenv_t saved;
  if (std::fegetenv(&saved) != 0) return 30;
  unsigned pairs = 0;
  const int status = run(pairs);
  const int restored = std::fesetenv(&saved);
  if (restored != 0) return 31;
  if (status) return status;
  std::printf("CMATH differential PASS: pairs=%u; round_modes=4; initial_flags=2; "
              "bits/errno/fenv/getters; half=not-covered\n", pairs);
  return 0;
}
"""
        header_provenance = {}
        configurations = {}
        library_dirs = [Path(value.strip().strip('"'))
                        for value in os.environ.get("LIB", "").split(";")
                        if value.strip()]
        library_dirs = [path for path in library_dirs if path.is_dir()]
        for name in ("libcmt.lib", "libucrt.lib", "libvcruntime.lib",
                     "oldnames.lib", "kernel32.lib"):
            self.assertTrue(any((path / name).is_file() for path in library_dirs),
                            f"The native CRT/SDK must provide {name}")
        linker = self.llvm_root / "bin/lld-link.exe"
        self.assertTrue(linker.is_file(), "The runtime probe requires lld-link")

        def compile_source(directory, name, body, msvc, optimized, trace=True):
            directory.mkdir(parents=True, exist_ok=True)
            source, obj = directory / (name + ".cpp"), directory / (name + ".obj")
            source.write_text((preamble if trace else "") + body, encoding="utf-8")
            if msvc:
                command = [self.msvc, "/nologo", "/std:c++17", "/c",
                           "/O2" if optimized else "/Od", "/GL-", "/MT",
                           "/EHsc", "/GR-", "/fp:strict",
                           *(["/showIncludes"] if trace else []),
                           "/Fo" + str(obj), source]
            else:
                command = [self.clang, "--target=" + self.target, "-std=c++17",
                           "-O2" if optimized else "-O0", "-fno-lto",
                           "-fms-extensions", "-fms-runtime-lib=static",
                           "-fno-exceptions", "-fno-rtti", "-ffp-model=strict",
                           "-ffp-contract=off",
                           *(["-Xclang", "--show-includes",
                              "-Xclang", "-sys-header-deps"] if trace else []),
                           "-c", source, "-o", obj]
            print("CMATH compile: " + subprocess.list2cmdline(
                [str(argument) for argument in command]), flush=True)
            environment = os.environ.copy()
            environment["VSLANG"] = "1033"
            result = subprocess.run(
                [str(argument) for argument in command], cwd=self.root,
                env=environment, capture_output=True, text=True, encoding="utf-8",
                errors="replace", timeout=120, check=False)
            output = result.stdout + result.stderr
            self.assertEqual(result.returncode, 0, output)
            if not trace:
                return obj
            macros = set(re.findall(
                r"NEVERC_CMATH_MACROS (_MSC_VER=\d+;_MSC_FULL_VER=\d+;"
                r"_MSVC_STL_VERSION=\d+;_MSVC_STL_UPDATE=\d+L?;"
                r"_MSVC_LANG=\d+L?;_HAS_EXCEPTIONS=[01];_MT=1)", output))
            self.assertEqual(len(macros), 1, output)
            macro_evidence = next(iter(macros))
            key = (msvc, optimized)
            self.assertEqual(configurations.setdefault(key, macro_evidence),
                             macro_evidence, "Original/changed/host configuration differs")
            print(f"CMATH configuration: object={obj}; {macro_evidence}", flush=True)
            consumed = {name: set() for name in ("cmath", "math.h", "corecrt_math.h")}
            for line in output.splitlines():
                prefix = "Note: including file:"
                if line.startswith(prefix):
                    path = Path(line[len(prefix):].lstrip(" "))
                    if path.name.lower() in consumed:
                        self.assertTrue(path.is_absolute(), str(path))
                        consumed[path.name.lower()].add(path.resolve(strict=True))
            for name, paths in consumed.items():
                self.assertEqual(len(paths), 1,
                                 f"Actual compilation must trace one {name}: {output}")
                path = next(iter(paths))
                data = path.read_bytes()
                evidence = (str(path), hashlib.sha256(data).hexdigest())
                self.assertEqual(header_provenance.setdefault(name, evidence), evidence,
                                 "All fixture TUs must consume the same actual SDK headers")
                print(f"CMATH SDK provenance: object={obj}; header={name}; "
                      f"path={evidence[0]}; sha256={evidence[1]}", flush=True)
                lines = data.decode("utf-8-sig").splitlines()
                if name == "cmath":
                    # Read the runner's actual definitions, never a downloaded
                    # reference tag. Require only the promotion macros at issue.
                    aliases = [i for i, line in enumerate(lines)
                               if re.match(r"\s*using\s+_Common_float_type_t\s*=", line)]
                    self.assertEqual(len(aliases), 1, path)
                    end = aliases[0]
                    while ";" not in lines[end]:
                        end += 1
                        self.assertLess(end, len(lines))
                    for i in range(max(0, aliases[0] - 1), end + 1):
                        print(f"CMATH actual common type {path}:{i + 1}: {lines[i]}",
                              flush=True)
                    for macro in ("_GENERIC_MATH1_BASE", "_GENERIC_MATH1R",
                                  "_GENERIC_MATH1", "_GENERIC_MATH2_BASE",
                                  "_GENERIC_MATH2"):
                        starts = [i for i, line in enumerate(lines)
                                  if re.match(r"\s*#\s*define\s+" + macro + r"\(", line)]
                        self.assertEqual(len(starts), 1, (path, macro))
                        end = starts[0]
                        while lines[end].rstrip().endswith("\\"):
                            end += 1
                            self.assertLess(end, len(lines))
                        for i in range(starts[0], end + 1):
                            print(f"CMATH actual macro {path}:{i + 1}: {lines[i]}", flush=True)
                        body_text = "\n".join(lines[starts[0]:end + 1])
                        if macro.endswith("_BASE"):
                            self.assertIn("static_cast<double>(_Left)", body_text)
                            if macro == "_GENERIC_MATH2_BASE":
                                self.assertIn("static_cast<double>(_Right)", body_text)
                    for pattern in (r"_GENERIC_MATH1\s*\(\s*log10\s*\)",
                                    r"_GENERIC_MATH2\s*\(\s*pow\s*\)"):
                        matches = [(i, line) for i, line in enumerate(lines)
                                   if re.fullmatch(r"\s*" + pattern + r"\s*", line)]
                        self.assertEqual(len(matches), 1, (path, pattern))
                        i, line = matches[0]
                        print(f"CMATH actual instantiation {path}:{i + 1}: {line}", flush=True)
                elif name == "math.h":
                    # The actual UCRT math.h is a forwarding header. Its
                    # included definition file must be in this same trace.
                    matches = [(i, line) for i, line in enumerate(lines)
                               if re.match(r'\s*#\s*include\s*[<"]corecrt_math\.h[>"]',
                                           line)]
                    self.assertEqual(len(matches), 1, path)
                    i, line = matches[0]
                    print(f"CMATH actual forwarding header {path}:{i + 1}: {line}",
                          flush=True)
                else:
                    for function in ("log10", "pow"):
                        matches = [(i, line) for i, line in enumerate(lines)
                                   if re.search(r"\bdouble\s+__cdecl\s+" +
                                                function + r"\s*\(", line)]
                        self.assertTrue(matches, (path, function))
                        for i, line in matches:
                            print(f"CMATH actual CRT declaration {path}:{i + 1}: {line}",
                                  flush=True)
            return obj

        def pack(path, objects):
            self.require_success([self.librarian, "/nologo", "/out:" + str(path), *objects])
            return path

        for msvc in (True, False):
            for optimized in (False, True):
                compiler = "msvc" if msvc else "clang"
                configuration = compiler + ("-O2" if optimized else "-O0")
                with self.subTest(configuration=configuration):
                    directory = self.root / "cmath" / configuration
                    original_obj = compile_source(
                        directory / "original", "original",
                        wrapper_source("neverc_cpp_original", False), msvc, optimized)
                    changed_obj = compile_source(
                        directory / "changed", "changed",
                        wrapper_source("neverc_cpp_changed", True), msvc, optimized)
                    host = directory / "host"
                    host_obj = compile_source(
                        host, "host", escapes.replace("SIDE", "host_cmath"), msvc, optimized)
                    # ENTRY is a separate member, and never enters the runtime
                    # link. Host escapes instantiate the same four templates,
                    # without wrapper literals (such as 2.0) introducing an
                    # unrelated floating-constant COMDAT intersection.
                    entry_obj = compile_source(
                        directory, "entry", ENTRY, msvc, optimized, trace=False)
                    original = pack(directory / "original.lib", (entry_obj, original_obj))
                    changed = pack(directory / "changed.lib", (entry_obj, changed_obj))
                    host_archive = pack(host / "host.lib", (host_obj,))
                    for archive in (original, host_archive):
                        declarations = self.defined_declarations(archive)
                        for raw, decoded in expected.items():
                            self.assertIn(raw, declarations, declarations)
                            self.assertEqual(declarations[raw], decoded)
                        inventory = self.require_success([
                            self.nm, "--extern-only", "--format=posix", archive])
                        for line in inventory.splitlines():
                            if line.split() and line.split()[0] in expected:
                                print(f"CMATH actual original definition: {archive}: {line}; "
                                      f"decoded={declarations[line.split()[0]]!r}", flush=True)
                    inventory = self.require_success([
                        self.nm, "--extern-only", "--format=posix", changed])
                    all_names = {line.split()[0] for line in inventory.splitlines()
                                 if line.split()}
                    self.assertFalse(all_names & expected.keys(),
                                     "Changed calls retain an original template D/U: " + inventory)
                    for host_format in ("nm", "coff-index"):
                        checked = directory / (host_format + "-original.lib")
                        shutil.copyfile(original, checked)
                        command = [sys.executable, "-E", "-B", self.audit,
                                   "--nm", self.nm, "--archive", checked,
                                   "--coff-readobj", self.readobj,
                                   "--host-lib-dir", host, "--host-format", host_format]
                        if host_format == "nm":
                            command.extend(["--host-nm", self.nm])
                        result = self.run_command(command)
                        output = result.stdout + result.stderr
                        self.assertNotEqual(result.returncode, 0, output)
                        self.assertFalse(checked.exists(), output)
                        for raw in expected:
                            self.assertIn("private/host symbol intersection: " + raw + ";",
                                          output)
                        print(f"CMATH original rejection ({configuration}, {host_format}):\n"
                              + output, flush=True)
                        checked = directory / (host_format + "-changed.lib")
                        shutil.copyfile(changed, checked)
                        self.check_audit(checked, host, host_format)
                        print(f"CMATH changed audit PASS: {configuration}, {host_format}; "
                              "all four original template D/U absent", flush=True)
                    harness_obj = compile_source(
                        directory, "harness", harness, msvc, optimized)
                    executable = directory / "cmath-probe.exe"
                    machine = "arm64" if self.target.startswith("aarch64-") else "x64"
                    command = [linker, "/nologo", "/out:" + str(executable),
                               "/machine:" + machine, "/subsystem:console", "/OPT:NOICF",
                               "/defaultlib:libcmt", "/defaultlib:oldnames",
                               *("/libpath:" + str(path) for path in library_dirs),
                               harness_obj, original_obj, changed_obj]
                    print("CMATH link: " + subprocess.list2cmdline(
                        [str(argument) for argument in command]), flush=True)
                    self.require_success(command)
                    output = self.require_success([executable])
                    self.assertIn("CMATH differential PASS: pairs=2496;", output)
                    print(f"CMATH runtime ({configuration}): {output}", flush=True)

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
