#!/usr/bin/env python3
"""Exercise the frontend ABI audit with real, tiny Microsoft COFF archives."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import tarfile
import time
import urllib.request


ENTRY = 'extern "C" int neverc_cpp_frontend_main(int, const char **) { return 0; }\n'


class CppFrontendToolchainTests(unittest.TestCase):
    setup_contract = False

    @classmethod
    def write_setup_report(cls):
        temporary = cls.report_dir / "manifest.json.tmp"
        temporary.write_text(json.dumps(cls.setup_report, indent=2) + "\n",
                             encoding="utf-8")
        temporary.replace(cls.report_dir / "manifest.json")

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
        if hasattr(self, "setup_deadline"):
            return self.setup_command(command)
        return subprocess.run(
            [str(argument) for argument in command], cwd=self.root,
            capture_output=True, text=True, encoding="utf-8", errors="replace",
            timeout=120, check=False,
        )

    def setup_command(self, command, *, env=None, timeout=120):
        # Only the Setup witness enters this path. Persist an intent before
        # starting a process, so a killed job leaves an incomplete record.
        arguments = [str(argument) for argument in command]
        index = len(self.setup_report["commands"]) + 1
        stem = f"command-{index:03d}"
        record = {"argv": arguments, "status": "started",
                  "stdout": stem + ".stdout.txt", "stderr": stem + ".stderr.txt"}
        self.setup_report["commands"].append(record)
        self.write_setup_report()
        remaining = self.setup_deadline - time.monotonic()
        if remaining <= 0:
            record["status"] = "budget-exhausted"
            for stream in ("stdout", "stderr"):
                (self.report_dir / record[stream]).write_text("", encoding="utf-8")
            self.write_setup_report()
            self.fail("Setup witness exceeded its ten-minute budget")
        started = time.monotonic()
        try:
            result = subprocess.run(
                arguments, cwd=self.root, env=env, capture_output=True, text=True,
                encoding="utf-8", errors="replace", timeout=min(timeout, remaining),
                check=False)
        except (OSError, subprocess.TimeoutExpired) as error:
            record.update(status="process-error", error=type(error).__name__,
                          detail=str(error), seconds=time.monotonic() - started)
            for stream in ("stdout", "stderr"):
                content = getattr(error, stream, "") or ""
                if isinstance(content, bytes):
                    content = content.decode("utf-8", errors="replace")
                (self.report_dir / record[stream]).write_text(content, encoding="utf-8")
            self.write_setup_report()
            raise
        for stream in ("stdout", "stderr"):
            (self.report_dir / record[stream]).write_text(
                getattr(result, stream), encoding="utf-8")
        record.update(status="completed", returncode=result.returncode,
                      seconds=time.monotonic() - started)
        self.write_setup_report()
        return result

    def check_setup_budget(self):
        remaining = self.setup_deadline - time.monotonic()
        if remaining <= 0:
            self.setup_report["setup_status"] = "budget-exhausted"
            self.write_setup_report()
            self.fail("Setup witness exceeded its ten-minute cooperative budget")
        return remaining

    def require_success(self, command):
        result = self.run_command(command)
        self.assertEqual(result.returncode, 0,
                         f"{command!r}\n{result.stdout}\n{result.stderr}")
        return result.stdout

    def setup_guid_audit(self, archive, *, original=False, host=None,
                         expected_error=None, host_format="nm"):
        # AuditArchive deliberately removes rejected inputs. Give the SAME
        # production closure gate a disposable copy for both RED and GREEN.
        checked = archive.with_name(archive.stem + "-audit.lib")
        self.assertFalse(checked.exists())
        shutil.copyfile(archive, checked)
        command = [sys.executable, "-E", "-B", self.audit,
                   "--nm", self.setup_writer_tools["nm"], "--archive", checked,
                   "--coff-readobj", self.setup_writer_tools["readobj"]]
        if host is not None:
            command.extend(["--host-lib-dir", host, "--host-format", host_format])
            if host_format == "nm":
                command.extend(["--host-nm", self.setup_writer_tools["nm"]])
        result = self.setup_command(command)
        text = result.stdout + result.stderr
        if original:
            self.assertNotEqual(result.returncode, 0, text)
            self.assertIn("unisolated Setup GUID", text)
            self.assertTrue(any(name in text for name in self.setup_guid_names), text)
            self.assertFalse(checked.exists())
        elif expected_error is not None:
            self.assertNotEqual(result.returncode, 0, text)
            self.assertIn(expected_error, text)
            self.assertFalse(checked.exists())
        else:
            self.assertEqual(result.returncode, 0, text)
            self.assertIn("defined and undefined symbols use the private LLVM ABI", text)
            checked.unlink()
        return len(self.setup_report["commands"])

    def setup_rewrite_archive(self, archive, compiler, label):
        # This is an isolation-contract RED/GREEN. It does not claim the
        # unisolated baseline has incorrect COM behavior.
        original_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
        inventory = self.require_success([
            self.setup_writer_tools["nm"], "--extern-only", "--no-sort",
            "--format=just-symbols", archive])
        names = {line for line in inventory.splitlines()
                 if line and not line.endswith(":")}
        self.assertTrue(self.setup_guid_names <= names, inventory)
        inventory_command = len(self.setup_report["commands"])
        rejected_command = self.setup_guid_audit(archive, original=True)
        changed = archive.with_name(archive.stem + "-isolated.lib")
        report = self.report_dir / ("setup-rewrite-" + compiler + "-" + label + ".txt")
        self.assertFalse(changed.exists())
        self.assertFalse(report.exists())
        record = {"compiler": compiler, "case": label, "status": "started",
                  "original_sha256": original_hash,
                  "original_inventory_command": inventory_command,
                  "original_data_names": sorted(self.setup_guid_names),
                  "original_rejected_command": rejected_command,
                  "report": report.name}
        self.setup_report["closure_cases"].append(record)
        self.write_setup_report()
        self.require_success([
            sys.executable, "-E", "-B", self.audit.with_name("RewriteSetupCoffSymbols.py"),
            "--input", archive, "--output", changed,
            "--nm", self.setup_writer_tools["nm"],
            "--readobj", self.setup_writer_tools["readobj"], "--report", report])
        self.assertTrue(changed.is_file())
        self.assertTrue(report.is_file(), "The rewrite must retain its structural proof")
        proof = json.loads(report.read_text(encoding="utf-8"))
        self.assertEqual(proof["status"], "passed", proof)
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), original_hash,
                         "A rewrite must preserve its input")
        # The no-host audit must prove complete private closure before any
        # executable links the original host provider.
        accepted_command = self.setup_guid_audit(changed)
        record.update(status="passed", proof=proof,
                      changed_sha256=hashlib.sha256(changed.read_bytes()).hexdigest(),
                      changed_accepted_command=accepted_command)
        self.write_setup_report()
        return changed

    def run_setup_closure_edges(self, common, values, include_root):
        # Real D-only and U-only objects complement the linked smart-pointer
        # contract. A host archive must never supply a missing private GUID.
        self.setup_report["closure_edges"] = []
        old_names = sorted(self.setup_guid_names)
        declaration = "".join(
            f'extern "C" const GUID {name};\n'
            f'extern "C" const GUID *neverc_cpp_edge_ref_{index}() '
            f'{{ return &{name}; }}\n' for index, name in enumerate(old_names))
        new_declaration = declaration
        for old, new in self.setup_guid_renames.items():
            new_declaration = new_declaration.replace(old, new)
        for msvc in (False, True):
            compiler = "msvc" if msvc else "clang"
            directory = self.root / "setup-closure" / compiler
            directory.mkdir(parents=True)

            def compile_object(stem, body, *, preamble=common):
                source, obj = directory / (stem + ".cpp"), directory / (stem + ".obj")
                source.write_text(preamble + body, encoding="utf-8")
                if msvc:
                    command = [self.msvc, "/nologo", "/std:c++17", "/Od", "/GL-",
                               "/GR-", "/EHsc", "/MT", "/c", "/I" + str(include_root),
                               "/Fo" + str(obj), source]
                else:
                    command = [self.clang, "--target=" + self.target, "-std=c++17",
                               "-O2", "-fno-lto", "-fms-extensions", "-fno-exceptions",
                               "-fno-rtti", "-fms-runtime-lib=static", "-I", include_root,
                               "-c", source, "-o", obj]
                self.require_success(command)
                return obj

            def pack(stem, members):
                path = directory / (stem + ".lib")
                self.require_success([self.librarian, "/nologo", "/out:" + str(path), *members])
                return path

            def rewrite(case, members, *, reject=False):
                original = pack(case, members)
                changed = directory / (case + "-isolated.lib")
                prefix = "setup-negative-" if reject else "setup-closure-"
                report = self.report_dir / (prefix + compiler + "-" + case + ".txt")
                before_hash = hashlib.sha256(original.read_bytes()).hexdigest()
                record = {"compiler": compiler, "case": case, "status": "started",
                          "expectation": "reject" if reject else "accept", "report": report.name}
                self.setup_report["closure_edges"].append(record)
                self.write_setup_report()
                result = self.setup_command([
                    sys.executable, "-E", "-B", self.audit.with_name("RewriteSetupCoffSymbols.py"),
                    "--input", original, "--output", changed,
                    "--nm", self.setup_writer_tools["nm"],
                    "--readobj", self.setup_writer_tools["readobj"], "--report", report])
                record["command"] = len(self.setup_report["commands"])
                proof = json.loads(report.read_text(encoding="utf-8"))
                self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(), before_hash)
                if reject:
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(proof["status"], "failed", proof)
                    self.assertFalse(proof["published"], proof)
                    self.assertIn("missing original Setup GUID data definition", proof["error"])
                    self.assertFalse(changed.exists())
                    record["negative_error"] = proof["error"]
                else:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual(proof["status"], "passed", proof)
                    self.assertTrue(proof["published"], proof)
                    self.assertEqual(proof["byte_proof"]["status"], "passed", proof)
                    if case == "duplicate-definition":
                        self.assertEqual(proof["member_count"], 4, proof)
                        repeated = [member for member in proof["members"]
                                    if Path(member["name"]).name == definitions.name]
                        self.assertEqual(len(repeated), 2, proof)
                        self.assertEqual(len({member["ordinal"] for member in repeated}), 2)
                        definition_hash = hashlib.sha256(definitions.read_bytes()).hexdigest()
                        self.assertTrue(all(member["input_sha256"] == definition_hash
                                            for member in repeated), proof)
                    record["no_host_command"] = self.setup_guid_audit(changed)
                record.update(status="passed", proof=proof)
                self.write_setup_report()
                return changed

            # Keep consumers independent of comdef.h: MSVC's comdefsp.h already
            # declares the IUnknown GUID as __s_GUID, which conflicts with the
            # GUID declarations here. Only providers need the real SDK objects;
            # the entry must not supply GUID definitions to the U-only cases.
            reference_preamble = "#include <guiddef.h>\n"
            entry = compile_object("entry", ENTRY, preamble="")
            definitions = compile_object("definition", values)
            references = compile_object("reference", declaration,
                                        preamble=reference_preamble)
            private_references = compile_object("private-reference", new_declaration,
                                                preamble=reference_preamble)
            missing = compile_object("missing-definition", "".join(
                line + "\n" for line in values.splitlines() if "setup_helper_guid" not in line))
            # Check actual D/U inventories before packing. The reference TU
            # contains declarations only; SDK inline headers cannot fill the gap.
            for obj, defined in ((definitions, True), (references, False)):
                output = self.require_success([self.setup_writer_tools["nm"], "--extern-only",
                                               "--format=posix", obj])
                for name in old_names:
                    records = [line.split() for line in output.splitlines()
                               if line.split() and line.split()[0] == name]
                    self.assertEqual(len(records), 1, output)
                    self.assertEqual(records[0][1] != "U", defined, output)
            isolated_definitions = rewrite("d-only", [entry, definitions])
            rewrite("definition-first", [entry, definitions, references])
            rewrite("reference-first", [entry, references, definitions])
            duplicate_dir = directory / "duplicate"
            duplicate_dir.mkdir()
            duplicate = duplicate_dir / definitions.name
            shutil.copyfile(definitions, duplicate)
            rewrite("duplicate-definition", [entry, definitions, duplicate, references])
            rewrite("u-only", [entry, references], reject=True)
            rewrite("missing-d", [entry, missing, references], reject=True)
            host = directory / "host"
            host.mkdir()
            shutil.copyfile(isolated_definitions, host / "private-definitions.lib")
            for case, members, host_dir, host_format, diagnostic in (
                    ("old-u", [isolated_definitions, references], None, "nm",
                     "unisolated Setup GUID"),
                    ("private-u", [entry, private_references], None, "nm",
                     "unresolved private dependency"),
                    ("host-fallback", [entry, private_references], host, "nm",
                     "unresolved private dependency"),
                    ("host-fallback-coff-index", [entry, private_references], host, "coff-index",
                     "unresolved private dependency")):
                archive = pack(case, members)
                command = self.setup_guid_audit(archive, host=host_dir, host_format=host_format,
                                                expected_error=diagnostic)
                self.setup_report["closure_edges"].append({
                    "compiler": compiler, "case": case, "status": "passed",
                    "expectation": "reject", "command": command, "negative_error": diagnostic})
                self.write_setup_report()
        self.assertEqual(len(self.setup_report["closure_edges"]), 20)

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

    def run_setup_runtime_witness(self, common, include_root):
        # Keep the original baseline, then run the same SDK implementation
        # with the production object transformation applied to the private side.
        provider = r"""
// Appended to the pinned-header/common preamble; SIDE is host or private.
#define SETUP_JOIN_IMPL(a, b) a##b
#define SETUP_JOIN(a, b) SETUP_JOIN_IMPL(a, b)
#define SETUP_EXPORT(suffix) SETUP_JOIN(SIDE, suffix)

_COM_SMARTPTR_TYPEDEF(ISetupConfiguration, __uuidof(ISetupConfiguration));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration2, __uuidof(ISetupConfiguration2));
_COM_SMARTPTR_TYPEDEF(ISetupHelper, __uuidof(ISetupHelper));
_COM_SMARTPTR_TYPEDEF(IEnumSetupInstances, __uuidof(IEnumSetupInstances));

extern "C" const GUID *SETUP_EXPORT(_setup_guid)(unsigned slot) {
  switch (slot) {
  case 0: return &__uuidof(IUnknown);
  case 1: return &__uuidof(SetupConfiguration);
  case 2: return &__uuidof(ISetupConfiguration);
  case 3: return &__uuidof(ISetupConfiguration2);
  case 4: return &__uuidof(ISetupHelper);
  default: return nullptr;
  }
}

extern "C" HRESULT SETUP_EXPORT(_setup_run)(
    ISetupConfiguration *input, ISetupConfiguration2 *expected_configuration2,
    ISetupHelper *expected_helper, BSTR version, ULONGLONG *parsed) {
  if (!input || !expected_configuration2 || !expected_helper || !version || !parsed)
    return E_POINTER;
  // The raw-pointer constructor acquires its own reference. The harness keeps
  // its original reference, and checks that this entire scope releases its own.
  ISetupConfigurationPtr query(input);
  ISetupConfiguration2Ptr configuration2(query);
  if (configuration2.GetInterfacePtr() != expected_configuration2) {
    // Do not call Release through an incorrectly typed interface in this
    // failing process. A failed witness makes no cleanup-success claim.
    configuration2.Detach();
    return E_UNEXPECTED;
  }
  IEnumSetupInstancesPtr instances;
  HRESULT result = configuration2->EnumAllInstances(&instances);
  if (FAILED(result)) return result;
  ISetupHelperPtr helper(query);
  if (helper.GetInterfacePtr() != expected_helper) {
    helper.Detach();
    return E_UNEXPECTED;
  }
  return helper->ParseVersion(version, parsed);
}
"""
        harness = r"""
// Appended to the pinned-header/common preamble. This oracle does not use
// __uuidof to initialize its expected values or identify supported interfaces.
#include <stdio.h>
#include <string.h>

extern "C" const GUID *host_setup_guid(unsigned);
extern "C" const GUID *private_setup_guid(unsigned);
extern "C" HRESULT host_setup_run(ISetupConfiguration *, ISetupConfiguration2 *,
                                   ISetupHelper *, BSTR, ULONGLONG *);
extern "C" HRESULT private_setup_run(ISetupConfiguration *, ISetupConfiguration2 *,
                                      ISetupHelper *, BSTR, ULONGLONG *);

static const GUID Expected[5] = {
    {0x00000000, 0x0000, 0x0000, {0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}},
    {0x177f0c4a, 0x1cd3, 0x4de7, {0xa3,0x2c,0x71,0xdb,0xbb,0x9f,0xa3,0x6d}},
    {0x42843719, 0xdb4c, 0x46c2, {0x8e,0x7c,0x64,0xf1,0x81,0x6e,0xfd,0x5b}},
    {0x26aab78c, 0x4a60, 0x49d6, {0xaf,0x3b,0x3c,0x35,0xbc,0x93,0x36,0x5d}},
    {0x42b21b78, 0x6192, 0x463e, {0x87,0xbf,0xd5,0x77,0x83,0x8f,0x1d,0x5c}},
};
static const GUID EnumIID =
    {0x6380bcff, 0x41d3, 0x4b2e, {0x8b,0x2e,0xbf,0x8a,0x68,0x10,0xc8,0x48}};
static const unsigned InterfaceSlot[4] = {0, 2, 3, 4};
static const ULONGLONG ParsedValue = 0x0001000200030004ULL;
static const HRESULT EnumFailure = static_cast<HRESULT>(0x80040201UL);
static const HRESULT ParseFailure = static_cast<HRESULT>(0x80040202UL);

static bool same_guid(REFGUID a, REFGUID b) {
  if (a.Data1 != b.Data1 || a.Data2 != b.Data2 || a.Data3 != b.Data3) return false;
  for (unsigned i = 0; i != 8; ++i) if (a.Data4[i] != b.Data4[i]) return false;
  return true;
}
static void guid_text(const GUID &value, char text[37]) {
  snprintf(text, 37, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           static_cast<unsigned long>(value.Data1), static_cast<unsigned>(value.Data2),
           static_cast<unsigned>(value.Data3), static_cast<unsigned>(value.Data4[0]),
           static_cast<unsigned>(value.Data4[1]), static_cast<unsigned>(value.Data4[2]),
           static_cast<unsigned>(value.Data4[3]), static_cast<unsigned>(value.Data4[4]),
           static_cast<unsigned>(value.Data4[5]), static_cast<unsigned>(value.Data4[6]),
           static_cast<unsigned>(value.Data4[7]));
}
static int finish(int code, const char *reason) {
  // Every reason is a fixed string literal, never runner/user-controlled text.
  if (code == 41 || code == 42 || code == 43)
    printf("{\"event\":\"negative_control\",\"check_code\":%d,\"reason\":\"%s\"}\n", code, reason);
  printf("{\"event\":\"final\",\"check_code\":%d,\"reason\":\"%s\"}\n", code, reason);
  fflush(stdout);
  return code;
}
enum class Fault { None, Address, WrongHelper, NoAddRef };
enum class MethodMode { Success, EnumFailure, ParseFailure };
struct Counts {
  ULONG live = 0, adds = 0, releases = 0, created = 0, destroyed = 0, underflows = 0;
};
struct Stats {
  Counts config, enumerator;
  ULONG queries = 0, enum_calls = 0, parse_calls = 0, bad_calls = 0;
  ULONG iid_queries[4] = {}, unsupported_queries = 0;
};

class FakeEnum final : public IEnumSetupInstances {
  Stats &s;
public:
  explicit FakeEnum(Stats &stats) : s(stats) { ++s.enumerator.created; ++s.enumerator.live; }
  ~FakeEnum() { ++s.enumerator.destroyed; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!same_guid(iid, Expected[0]) && !same_guid(iid, EnumIID)) return E_NOINTERFACE;
    *out = static_cast<IEnumSetupInstances *>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { ++s.enumerator.adds; return ++s.enumerator.live; }
  ULONG STDMETHODCALLTYPE Release() override {
    ++s.enumerator.releases;
    if (!s.enumerator.live) { ++s.enumerator.underflows; return 0; }
    ULONG left = --s.enumerator.live;
    if (!left) delete this;
    return left;
  }
  HRESULT STDMETHODCALLTYPE Next(ULONG count, ISetupInstance **items, ULONG *fetched) override {
    ++s.bad_calls;
    if (fetched) *fetched = 0;
    if (count && !items) return E_POINTER;
    for (ULONG i = 0; i < count; ++i) items[i] = nullptr;
    return S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE Skip(ULONG) override { ++s.bad_calls; return S_FALSE; }
  HRESULT STDMETHODCALLTYPE Reset() override { ++s.bad_calls; return S_OK; }
  HRESULT STDMETHODCALLTYPE Clone(IEnumSetupInstances **out) override {
    ++s.bad_calls;
    if (!out) return E_POINTER;
    *out = nullptr;
    return E_NOTIMPL;
  }
};

class FakeSetup final : public ISetupConfiguration2, public ISetupHelper {
  Stats &s;
  Fault fault;
  MethodMode method;
public:
  FakeSetup(Stats &stats, Fault f = Fault::None, MethodMode m = MethodMode::Success)
      : s(stats), fault(f), method(m) { ++s.config.created; ++s.config.live; }
  ~FakeSetup() { ++s.config.destroyed; }
  void *face(unsigned which) {
    switch (which) {
    case 0: return static_cast<IUnknown *>(static_cast<ISetupConfiguration *>(this));
    case 1: return static_cast<ISetupConfiguration *>(this);
    case 2: return static_cast<ISetupConfiguration2 *>(this);
    default: return static_cast<ISetupHelper *>(this);
    }
  }
  IUnknown *view(unsigned which) {
    switch (which) {
    case 0: return static_cast<IUnknown *>(static_cast<ISetupConfiguration *>(this));
    case 1: return static_cast<ISetupConfiguration *>(this);
    case 2: return static_cast<ISetupConfiguration2 *>(this);
    default: return static_cast<ISetupHelper *>(this);
    }
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
    ++s.queries;
    if (!out) return E_POINTER;
    *out = nullptr;
    for (unsigned k = 0; k != 4; ++k) {
      const GUID &wanted = Expected[InterfaceSlot[k]];
      if (!same_guid(iid, wanted)) continue;
      ++s.iid_queries[k];
      if (fault == Fault::Address && &iid != &wanted) return E_NOINTERFACE;
      *out = fault == Fault::WrongHelper && k == 3 ? face(0) : face(k);
      if (fault != Fault::NoAddRef) AddRef();
      return S_OK;
    }
    ++s.unsupported_queries;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { ++s.config.adds; return ++s.config.live; }
  ULONG STDMETHODCALLTYPE Release() override {
    ++s.config.releases;
    if (!s.config.live) { ++s.config.underflows; return 0; }
    ULONG left = --s.config.live;
    if (!left) delete this;
    return left;
  }
  HRESULT STDMETHODCALLTYPE EnumInstances(IEnumSetupInstances **out) override {
    ++s.bad_calls;
    if (!out) return E_POINTER;
    *out = nullptr;
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE GetInstanceForCurrentProcess(ISetupInstance **out) override {
    ++s.bad_calls;
    if (!out) return E_POINTER;
    *out = nullptr;
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE GetInstanceForPath(LPCWSTR, ISetupInstance **out) override {
    return GetInstanceForCurrentProcess(out);
  }
  HRESULT STDMETHODCALLTYPE EnumAllInstances(IEnumSetupInstances **out) override {
    ++s.enum_calls;
    if (!out) return E_POINTER;
    *out = nullptr;
    if (method == MethodMode::EnumFailure) return EnumFailure;
    *out = new FakeEnum(s); // Return exactly one owned reference to the SDK smart pointer.
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE ParseVersion(LPCOLESTR text, PULONGLONG parsed) override {
    ++s.parse_calls;
    if (!text || !parsed) return E_POINTER;
    const wchar_t wanted[] = L"1.2.3.4";
    for (unsigned k = 0; k != sizeof(wanted) / sizeof(wanted[0]); ++k) {
      if (text[k] != wanted[k]) { ++s.bad_calls; return E_INVALIDARG; }
    }
    if (method == MethodMode::ParseFailure) return ParseFailure;
    *parsed = ParsedValue;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE ParseVersionRange(LPCOLESTR, PULONGLONG low, PULONGLONG high) override {
    ++s.bad_calls;
    if (low) *low = 0;
    if (high) *high = 0;
    return E_NOTIMPL;
  }
};

static IUnknown *returned_view(void *out, unsigned which) {
  switch (which) {
  case 0: return static_cast<IUnknown *>(out);
  case 1: return static_cast<ISetupConfiguration *>(out);
  case 2: return static_cast<ISetupConfiguration2 *>(out);
  default: return static_cast<ISetupHelper *>(out);
  }
}

static int check_interfaces(Fault fault) {
  Stats s;
  FakeSetup *object = new FakeSetup(s, fault);
  void *canonical = object->face(0);
  if (object->face(3) == canonical) return finish(20, "helper_subobject_not_distinct");
  for (unsigned source = 0; source != 4; ++source) {
    for (unsigned target = 0; target != 4; ++target) {
      ULONG before = s.config.live, adds = s.config.adds;
      void *out = nullptr;
      HRESULT hr = object->view(source)->QueryInterface(Expected[InterfaceSlot[target]], &out);
      printf("{\"event\":\"qi\",\"source\":%u,\"target\":%u,\"hr\":%lu,\"pointer\":\"%p\",\"before\":%lu,\"after\":%lu}\n",
             source, target, static_cast<unsigned long>(hr), out, before, s.config.live);
      if (hr != S_OK) return finish(21, "qi_hresult");
      // Check the pointer before any cast or virtual call through its value.
      if (out != object->face(target))
        return finish(fault == Fault::WrongHelper && target == 3 ? 42 : 22, "returned_interface");
      if (s.config.live != before + 1 || s.config.adds != adds + 1)
        return finish(fault == Fault::NoAddRef ? 43 : 23, "qi_addref");
      IUnknown *returned = returned_view(out, target);
      void *identity = nullptr;
      adds = s.config.adds;
      hr = returned->QueryInterface(Expected[0], &identity);
      if (hr != S_OK || identity != canonical) return finish(24, "canonical_iunknown");
      if (s.config.live != before + 2 || s.config.adds != adds + 1)
        return finish(25, "canonical_addref");
      ULONG identity_left = static_cast<IUnknown *>(identity)->Release();
      ULONG returned_left = returned->Release();
      if (identity_left != before + 1 || returned_left != before || s.config.live != before)
        return finish(26, "qi_release_balance");
      printf("{\"event\":\"canonical\",\"source\":%u,\"target\":%u,\"pointer\":\"%p\",\"refs\":%lu}\n",
             source, target, identity, s.config.live);
    }
  }
  for (unsigned target = 0; target != 4; ++target) {
    GUID copy = Expected[InterfaceSlot[target]];
    if (&copy == &Expected[InterfaceSlot[target]]) return finish(35, "copied_iid_address");
    ULONG before = s.config.live, adds = s.config.adds;
    void *out = nullptr;
    HRESULT hr = object->view(0)->QueryInterface(copy, &out);
    printf("{\"event\":\"copied_iid\",\"target\":%u,\"hr\":%lu,\"iid_address\":\"%p\",\"expected_address\":\"%p\"}\n",
           target, static_cast<unsigned long>(hr), static_cast<void *>(&copy),
           static_cast<const void *>(&Expected[InterfaceSlot[target]]));
    if (hr != S_OK) return finish(fault == Fault::Address && hr == E_NOINTERFACE ? 41 : 27, "copied_iid_value");
    if (out != object->face(target)) return finish(28, "copied_iid_pointer");
    if (s.config.live != before + 1 || s.config.adds != adds + 1) return finish(29, "copied_iid_addref");
    if (returned_view(out, target)->Release() != before || s.config.live != before)
      return finish(30, "copied_iid_release");
  }
  GUID unsupported = Expected[0];
  unsupported.Data1 ^= 1;
  void *out = object->face(3); // A valid, nonnull sentinel that must be cleared.
  ULONG before = s.config.live, adds = s.config.adds;
  HRESULT hr = object->view(0)->QueryInterface(unsupported, &out);
  printf("{\"event\":\"unsupported_iid\",\"hr\":%lu,\"output_null\":%u,\"before\":%lu,\"after\":%lu}\n",
         static_cast<unsigned long>(hr), out ? 0U : 1U, before, s.config.live);
  if (hr != E_NOINTERFACE || out || s.config.live != before || s.config.adds != adds)
    return finish(31, "unsupported_iid_contract");
  hr = object->view(0)->QueryInterface(Expected[0], nullptr);
  printf("{\"event\":\"null_output\",\"hr\":%lu,\"before\":%lu,\"after\":%lu}\n",
         static_cast<unsigned long>(hr), before, s.config.live);
  if (hr != E_POINTER || s.config.live != before || s.config.adds != adds)
    return finish(32, "null_output_contract");
  if (fault != Fault::None) return finish(50, "negative_not_detected");
  if (s.queries != 38 || s.iid_queries[0] != 21 || s.iid_queries[1] != 5 ||
      s.iid_queries[2] != 5 || s.iid_queries[3] != 5 || s.unsupported_queries != 1)
    return finish(34, "qi_coverage_count");
  if (object->Release() != 0 || s.config.live || s.config.created != 1 ||
      s.config.destroyed != 1 || s.config.underflows || s.config.releases != s.config.adds + 1)
    return finish(33, "matrix_lifecycle");
  printf("{\"event\":\"qi_coverage\",\"matrix\":16,\"canonical\":16,\"copied\":4,\"unsupported\":1,\"null_output\":1,\"queries\":%lu,\"destroyed\":%lu}\n",
         s.queries, s.config.destroyed);
  return 0;
}

static int check_provider_runs() {
  typedef HRESULT (*Run)(ISetupConfiguration *, ISetupConfiguration2 *,
                        ISetupHelper *, BSTR, ULONGLONG *);
  const Run providers[2] = {host_setup_run, private_setup_run};
  for (unsigned side = 0; side != 2; ++side) {
    for (unsigned scenario = 0; scenario != 3; ++scenario) {
      Stats s;
      MethodMode mode = scenario == 0 ? MethodMode::Success :
          scenario == 1 ? MethodMode::EnumFailure : MethodMode::ParseFailure;
      FakeSetup *object = new FakeSetup(s, Fault::None, mode);
      BSTR version = SysAllocString(L"1.2.3.4");
      if (!version) return finish(51, "bstr_allocation");
      const ULONGLONG sentinel = 0xfedcba9876543210ULL;
      ULONGLONG parsed = sentinel;
      // Only interface addresses are supplied as the pointer oracle. The real
      // SDK smart pointers still select every requested IID themselves.
      HRESULT hr = providers[side](static_cast<ISetupConfiguration *>(object),
                                   static_cast<ISetupConfiguration2 *>(object),
                                   static_cast<ISetupHelper *>(object), version, &parsed);
      SysFreeString(version);
      HRESULT wanted = scenario == 0 ? S_OK : scenario == 1 ? EnumFailure : ParseFailure;
      ULONG enums = scenario == 1 ? 0 : 1;
      printf("{\"event\":\"methods\",\"side\":%u,\"scenario\":%u,\"hr\":%lu,\"parsed\":%llu,\"enum_calls\":%lu,\"parse_calls\":%lu,\"refs\":%lu}\n",
             side, scenario, static_cast<unsigned long>(hr), static_cast<unsigned long long>(parsed),
             s.enum_calls, s.parse_calls, s.config.live);
      printf("{\"event\":\"provider_iids\",\"side\":%u,\"scenario\":%u,\"unknown\":%lu,\"configuration\":%lu,\"configuration2\":%lu,\"helper\":%lu,\"unsupported\":%lu}\n",
             side, scenario, s.iid_queries[0], s.iid_queries[1],
             s.iid_queries[2], s.iid_queries[3], s.unsupported_queries);
      if (hr != wanted || parsed != (scenario == 0 ? ParsedValue : sentinel))
        return finish(52, "method_result");
      if (s.enum_calls != 1 || s.parse_calls != enums || s.bad_calls)
        return finish(53, "method_dispatch");
      // A wrong IID can return the same Configuration/Configuration2 address
      // and vtable: method success alone is therefore not an IID oracle.
      if (!s.iid_queries[2] || (enums ? !s.iid_queries[3] : s.iid_queries[3] != 0) ||
          s.unsupported_queries)
        return finish(56, "provider_iid_dispatch");
      // Do not require a fixed SDK temporary/copy count, only balanced ownership.
      if (s.config.live != 1 || s.config.adds != s.config.releases || s.config.destroyed ||
          s.config.underflows || s.enumerator.live || s.enumerator.underflows ||
          s.enumerator.created != enums || s.enumerator.destroyed != enums ||
          s.enumerator.releases != s.enumerator.adds + enums)
        return finish(54, "smart_pointer_balance");
      if (object->Release() != 0 || s.config.live || s.config.destroyed != 1 ||
          s.config.releases != s.config.adds + 1 || s.config.underflows)
        return finish(55, "provider_lifecycle");
      printf("{\"event\":\"lifecycle\",\"side\":%u,\"scenario\":%u,\"config_created\":%lu,\"config_destroyed\":%lu,\"enum_created\":%lu,\"enum_destroyed\":%lu}\n",
             side, scenario, s.config.created, s.config.destroyed,
             s.enumerator.created, s.enumerator.destroyed);
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2) return finish(10, "mode_argument");
  Fault fault;
  if (!strcmp(argv[1], "normal")) fault = Fault::None;
  else if (!strcmp(argv[1], "address")) fault = Fault::Address;
  else if (!strcmp(argv[1], "wrong-helper")) fault = Fault::WrongHelper;
  else if (!strcmp(argv[1], "no-addref")) fault = Fault::NoAddRef;
  else return finish(11, "unknown_mode");
  for (unsigned slot = 0; slot != 5; ++slot) {
    const GUID *host = host_setup_guid(slot), *private_value = private_setup_guid(slot);
    if (!host || !private_value) {
      printf("{\"event\":\"guid_null\",\"slot\":%u,\"host\":\"%p\",\"private\":\"%p\"}\n",
             slot, static_cast<const void *>(host), static_cast<const void *>(private_value));
      return finish(12, "guid_null");
    }
    char host_text[37], private_text[37];
    guid_text(*host, host_text);
    guid_text(*private_value, private_text);
    // Observe cross-provider identity; COM does not require shared GUID storage.
    printf("{\"event\":\"guid\",\"slot\":%u,\"host\":\"%p\",\"private\":\"%p\",\"same_address\":%u,\"host_value\":\"%s\",\"private_value\":\"%s\"}\n",
           slot, static_cast<const void *>(host), static_cast<const void *>(private_value),
           host == private_value ? 1U : 0U, host_text, private_text);
    if (!same_guid(*host, Expected[slot]) || !same_guid(*private_value, Expected[slot]))
      return finish(12, "guid_fields");
    if (host != host_setup_guid(slot) || private_value != private_setup_guid(slot))
      return finish(13, "guid_address_stability");
  }
  int code = check_interfaces(fault);
  if (code) return code;
  code = check_provider_runs();
  if (code) return code;
  printf("{\"event\":\"coverage\",\"guid_slots\":5,\"provider_sides\":2,\"provider_scenarios\":3,\"provider_runs\":6}\n");
  return finish(0, "ok");
}
"""
        library_dirs = [Path(value.strip().strip('"'))
                        for value in os.environ.get("LIB", "").split(";")
                        if value.strip()]
        self.assertTrue(library_dirs, "The native MSVC LIB environment is required")
        library_dirs = [path.resolve() for path in library_dirs if path.is_dir()]
        libraries = ("libcmt.lib", "libucrt.lib", "libvcruntime.lib", "oldnames.lib",
                     "kernel32.lib", "ole32.lib", "oleaut32.lib", "comsuppw.lib")
        self.setup_report["native_libraries"] = {}
        for name in libraries:
            found = next((path / name for path in library_dirs if (path / name).is_file()), None)
            self.assertIsNotNone(found, f"The native CRT/SDK must provide {name}")
            self.setup_report["native_libraries"][name] = str(found)
        linker = self.llvm_root / "bin/lld-link.exe"
        for name, path in (("clang", self.clang), ("msvc", Path(self.msvc)),
                           ("librarian", self.librarian), ("nm", self.nm),
                           ("readobj", self.readobj), ("lld-link", linker)):
            path = path.resolve(strict=True)
            self.assertTrue(path.is_file(), f"Required witness tool missing: {path}")
            digest = hashlib.sha256()
            with path.open("rb") as stream:
                while chunk := stream.read(1024 * 1024):
                    self.check_setup_budget()
                    digest.update(chunk)
            self.check_setup_budget()
            self.setup_report["tools"][name] = {
                "path": str(path), "sha256": digest.hexdigest()}
        self.write_setup_report()
        machine = "x64" if self.target.startswith("x86_64-") else "arm64"
        cases = (("normal", 0, "ok"), ("address", 41, "copied_iid_value"),
                 ("wrong-helper", 42, "returned_interface"),
                 ("no-addref", 43, "qi_addref"))
        normal_counts = {
            "guid": 5, "qi": 16, "canonical": 16, "copied_iid": 4,
            "unsupported_iid": 1, "null_output": 1, "qi_coverage": 1,
            "methods": 6, "provider_iids": 6, "lifecycle": 6,
            "coverage": 1, "final": 1,
        }
        for msvc in (False, True):
            compiler = "msvc" if msvc else "clang"
            directory = self.root / "setup-runtime" / compiler
            directory.mkdir(parents=True)
            objects = {}
            for unit in ("host", "private", "harness"):
                source = directory / (unit + ".cpp")
                obj = directory / (unit + ".obj")
                source.write_text(common + (harness if unit == "harness" else provider)
                                  + (ENTRY if unit == "private" else ""),
                                  encoding="utf-8")
                if msvc:
                    command = [self.msvc, "/nologo", "/std:c++17", "/c", "/Od",
                               "/GL-", "/GR-", "/EHsc", "/MT", "/Zc:wchar_t",
                               "/showIncludes",
                               "/I" + str(include_root), "/Fo" + str(obj),
                               *([] if unit == "harness" else ["/DSIDE=" + unit]), source]
                else:
                    command = [self.clang, "--target=" + self.target, "-std=c++17",
                               "-O2", "-fno-lto", "-fms-extensions", "-fno-exceptions",
                               "-fno-rtti", "-fms-runtime-lib=static", "-I", include_root,
                               "-Xclang", "--show-includes", "-Xclang", "-sys-header-deps",
                               *([] if unit == "harness" else ["-DSIDE=" + unit]),
                               "-c", source, "-o", obj]
                environment = os.environ.copy()
                if msvc:
                    environment["VSLANG"] = "1033"
                compiled = self.setup_command(command, env=environment)
                output = compiled.stdout + compiled.stderr
                self.assertEqual(compiled.returncode, 0, output)
                consumed = {}
                for line in output.splitlines():
                    prefix = "Note: including file:"
                    if not line.startswith(prefix):
                        continue
                    path = Path(line[len(prefix):].lstrip(" "))
                    name = path.name.lower()
                    if name not in self.setup_report["headers"]:
                        continue
                    self.assertTrue(path.is_absolute(), str(path))
                    path = path.resolve(strict=True)
                    evidence = {"path": str(path),
                                "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                    self.assertEqual(evidence, self.setup_report["headers"][name],
                                     "Runtime providers must consume the captured SDK headers")
                    consumed[name] = evidence
                self.assertIn("comdef.h", consumed)
                self.assertIn("msvcsetupapi.h", consumed)
                self.setup_report["runtime_headers"][compiler + "/" + unit] = consumed
                self.write_setup_report()
                self.require_success([self.nm, "--extern-only", "--format=posix", obj])
                self.require_success([self.readobj, "--file-headers", "--symbols",
                                      "--sections", "--section-data", "--relocations", obj])
                objects[unit] = obj
            archives = {}
            for side in ("host", "private"):
                archive = directory / (side + ".lib")
                self.require_success([self.librarian, "/nologo", "/out:" + str(archive),
                                      objects[side]])
                archives[side] = archive
            variants = [
                    ("baseline", "host-first", ("host", "private")),
                    ("baseline", "private-first", ("private", "host"))]
            if self.setup_contract:
                isolated_private = self.setup_rewrite_archive(
                    archives["private"], compiler, "runtime")
                private_names = tuple(self.setup_guid_renames[name] for name in (
                    "_GUID_00000000_0000_0000_c000_000000000046",
                    "_GUID_177f0c4a_1cd3_4de7_a32c_71dbbb9fa36d",
                    "_GUID_42843719_db4c_46c2_8e7c_64f1816efd5b",
                    "_GUID_26aab78c_4a60_49d6_af3b_3c35bc93365d",
                    "_GUID_42b21b78_6192_463e_87bf_d577838f1d5c"))
                variants.extend([
                    ("isolated", "host-first", ("host", "private")),
                    ("isolated", "private-first", ("private", "host"))])
            for variant, order, sides in variants:
                stem = variant + "-" + order
                executable = directory / (stem + ".exe")
                link_map = directory / (stem + ".map")
                inputs = [isolated_private if side == "private" and variant == "isolated"
                          else archives[side] for side in sides]
                options = ["/OPT:NOICF", "/MAP:" + str(link_map),
                           "/VERBOSE:LIB" if msvc else "/verbose",
                           *("/libpath:" + str(path) for path in library_dirs),
                           "oleaut32.lib", "ole32.lib", "comsuppw.lib"]
                if msvc:
                    command = [self.msvc, "/nologo", objects["harness"], *inputs,
                               "/Fe" + str(executable), "/link", *options]
                else:
                    command = [linker, "/nologo", "/out:" + str(executable),
                               "/machine:" + machine, "/subsystem:console",
                               "/defaultlib:libcmt", "/defaultlib:oldnames",
                               objects["harness"], *inputs, *options]
                self.require_success(command)
                self.assertTrue(link_map.is_file(), "The final link must produce a map")
                map_text = link_map.read_text(encoding="utf-8", errors="replace")
                (self.report_dir / (compiler + "-" + stem + ".map.txt")).write_text(
                    map_text, encoding="utf-8")
                for side in sides:
                    for suffix in ("_setup_guid", "_setup_run"):
                        self.assertIn(side + suffix, map_text,
                                      "Both provider members must enter the final link")
                if variant == "isolated":
                    for name in private_names:
                        definitions = [line for line in map_text.splitlines()
                                       if re.search(r"\s" + re.escape(name) + r"\s", line)]
                        self.assertEqual(len(definitions), 1, name + "\n" + map_text)
                        self.assertRegex(definitions[0], r"\bprivate-isolated:private\.obj\s*$")
                    if msvc:
                        for interface, name in (("ISetupConfiguration2", private_names[3]),
                                                ("ISetupHelper", private_names[4])):
                            needle = "?GetIID@?$_com_IIID@U" + interface + "@@$1?" + name + "@@"
                            definitions = [line for line in map_text.splitlines() if needle in line]
                            self.assertEqual(len(definitions), 1, needle + "\n" + map_text)
                            self.assertRegex(definitions[0], r"\bprivate-isolated:private\.obj\s*$")
                self.require_success([self.readobj, "--file-headers", "--coff-imports",
                                      executable])
                for mode, expected_code, reason in cases:
                    observed = {"compiler": compiler, "variant": variant,
                                "order": order, "mode": mode,
                                "expected_code": expected_code, "status": "started"}
                    self.setup_report["runtime_cases"].append(observed)
                    self.write_setup_report()
                    result = self.setup_command([executable, mode], timeout=15)
                    observed["returncode"] = result.returncode
                    observed["command"] = len(self.setup_report["commands"])
                    self.write_setup_report()
                    # Never truncate Windows exception codes or accept arbitrary
                    # nonzero exits as a detected negative control.
                    self.assertEqual(result.returncode, expected_code,
                                     f"{compiler}/{order}/{mode}: {result.stdout}\n{result.stderr}")
                    events = [json.loads(line) for line in result.stdout.splitlines() if line]
                    self.assertTrue(events, "Runtime witness output is required")
                    self.assertTrue(all(isinstance(event, dict) for event in events))
                    guid_events = [event for event in events if event.get("event") == "guid"]
                    self.assertEqual(len(guid_events), 5)
                    if variant == "isolated":
                        self.assertTrue(all(event["same_address"] == 0 for event in guid_events),
                                        "With /OPT:NOICF, private GUID storage must stay separate")
                    self.assertEqual(events[-1], {
                        "event": "final", "check_code": expected_code, "reason": reason})
                    if mode == "normal":
                        counts = {name: sum(event.get("event") == name for event in events)
                                  for name in normal_counts}
                        self.assertEqual(counts, normal_counts)
                        self.assertEqual(len(events), sum(normal_counts.values()))
                        coverage = next(event for event in events if event["event"] == "coverage")
                        self.assertEqual(coverage, {"event": "coverage", "guid_slots": 5,
                                                   "provider_sides": 2, "provider_scenarios": 3,
                                                   "provider_runs": 6})
                    else:
                        self.assertEqual([event for event in events
                                          if event.get("event") == "negative_control"],
                                         [{"event": "negative_control", "check_code": expected_code,
                                           "reason": reason}])
                    observed.update(status="passed", events=events)
                    self.write_setup_report()
        self.assertEqual(len(self.setup_report["runtime_cases"]),
                         32 if self.setup_contract else 16)
        self.assertTrue(all(case["status"] == "passed"
                            for case in self.setup_report["runtime_cases"]))

    def test_setup_sdk_inputs_and_object_provenance(self):
        # Preserve the separate non-LTO object inventories, then exercise a
        # linked baseline with real SDK smart pointers and deterministic COM.
        # The explicit post-build mode also tests the production byte writer.
        self.setup_started = time.monotonic()
        self.setup_deadline = self.setup_started + 600
        self.assertFalse(self.report_dir.is_relative_to(self.root),
                         "Reports must survive temporary-directory cleanup")
        self.setup_report["setup_status"] = "started"
        self.write_setup_report()
        repository = Path(__file__).resolve().parents[2]
        checkout = self.require_success(["git", "-C", repository, "rev-parse", "HEAD"]).strip()
        self.assertRegex(checkout, r"\A[0-9a-f]{40}\Z")
        self.assertEqual(checkout, self.setup_report["source_sha"],
                         "The witness must run the workflow's exact checkout")
        self.setup_report["verified_checkout"] = checkout
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

        if self.setup_contract:
            # The private sub-build has already verified and extracted the pin.
            # Hash its actual header again; do not download another toolchain.
            header_bytes = self.pinned_setup_header.read_bytes()
            self.setup_report["source_archive"] = {
                "url": archive_url, "sha256": archive_sha,
                "mode": "verified-private-subbuild", "header_path": str(self.pinned_setup_header)}
            self.setup_writer_tools = {"nm": self.private_nm, "readobj": self.private_readobj}
            self.setup_report["private_readers"] = {}
            for role, tool in self.setup_writer_tools.items():
                self.assertTrue(tool.is_file(), str(tool))
                self.assertNotEqual(tool.resolve(), getattr(self, role).resolve(),
                                    "The private reader must not fall back to the host toolchain")
                version = self.require_success([tool, "--version"])
                self.assertRegex(version, r"\bLLVM version 20\.1\.8(?:\s|$)")
                self.setup_report["private_readers"][role] = {
                    "path": str(tool), "sha256": hashlib.sha256(tool.read_bytes()).hexdigest(),
                    "bytes": tool.stat().st_size,
                    "version_command": len(self.setup_report["commands"])}
            sys.path.insert(0, str(self.audit.parent))
            try:
                from SetupGuidSymbols import GUID_RENAMES
            finally:
                sys.path.pop(0)
            self.setup_guid_renames = GUID_RENAMES
            self.write_setup_report()
        else:
            # This runs only in the existing Windows GitHub toolchain jobs, before
            # the main ExternalProject has extracted its source. Download the same
            # content-addressed release here; never substitute the host LLVM header.
            archive_path = self.root / "llvm-project-20.1.8.src.tar.xz"
            digest = hashlib.sha256()
            downloaded = 0
            started = time.monotonic()
            with urllib.request.urlopen(archive_url, timeout=min(30, self.check_setup_budget())) as response:
                self.assertEqual(response.status, 200)
                with archive_path.open("wb") as output:
                    while chunk := response.read(1024 * 1024):
                        self.check_setup_budget()
                        downloaded += len(chunk)
                        self.assertLessEqual(downloaded, archive_size)
                        self.assertLess(time.monotonic() - started, 300,
                                        "Pinned source download exceeded five minutes")
                        digest.update(chunk)
                        output.write(chunk)
            self.check_setup_budget()
            self.assertEqual(downloaded, archive_size)
            self.assertEqual(digest.hexdigest(), archive_sha)
            self.setup_report["source_archive"] = {
                "url": archive_url, "bytes": downloaded, "sha256": digest.hexdigest()}
            self.write_setup_report()
            print(f"SETUP source archive: url={archive_url} bytes={downloaded} "
                  f"sha256={digest.hexdigest()} pin_file_sha256="
                  f"{hashlib.sha256(pin.read_bytes()).hexdigest()}", flush=True)

            headers = []
            self.check_setup_budget()
            with tarfile.open(archive_path, mode="r|xz") as archive:
                for member in archive:
                    self.check_setup_budget()
                    if member.name != member_name:
                        continue
                    self.assertTrue(member.isfile(), member.name)
                    self.assertEqual(member.size, header_size)
                    with archive.extractfile(member) as source:
                        headers.append(source.read(header_size + 1))
            self.check_setup_budget()
            self.assertEqual(len(headers), 1, "Expected one exact release header member")
            header_bytes = headers[0]
        self.assertEqual(len(header_bytes), header_size)
        self.assertEqual(hashlib.sha256(header_bytes).hexdigest(), header_sha)
        self.setup_report["pinned_header"] = {
            "member": member_name, "bytes": header_size, "sha256": header_sha}
        self.write_setup_report()
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
        self.setup_guid_names = expected_guids
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
                    source.write_text(common + ENTRY + body, encoding="utf-8")
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
                    result = self.setup_command(command, env=environment)
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
                        self.setup_report["headers"][name] = {
                            "path": evidence[0], "sha256": evidence[1]}
                        self.write_setup_report()
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
                    self.setup_report["object_probes"].append(compiler + "/" + unit)
                    self.write_setup_report()
                    if self.setup_contract:
                        probe_archive = directory / "probe.lib"
                        self.require_success([self.librarian, "/nologo",
                                              "/out:" + str(probe_archive), obj])
                        self.setup_rewrite_archive(probe_archive, compiler, unit)
        self.assertEqual(set(self.setup_report["object_probes"]), {
            compiler + "/" + unit for compiler in ("clang", "msvc")
            for unit in ("explicit-values", "smart-pointers")})
        self.run_setup_runtime_witness(common, include_root)
        if self.setup_contract:
            self.run_setup_closure_edges(common, values, include_root)
        self.check_setup_budget()
        self.setup_report["setup_elapsed_seconds"] = time.monotonic() - self.setup_started
        self.setup_report["setup_status"] = "passed"
        self.write_setup_report()
        print("SETUP " + ("isolation contract and " if self.setup_contract else "") +
              "SDK runtime witness passed; "
              "COM activation, VS discovery, and production LTO equivalence remain untested.",
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


REPORT_FILE_LIMIT = 8 * 1024 * 1024
REPORT_TOTAL_LIMIT = 32 * 1024 * 1024
REPORT_COUNT_LIMIT = 512
REPORT_METADATA_RESERVE = 1024 * 1024
REPORT_SCHEMA = 2
REWRITE_SCHEMA = "neverc.setup-coff-rewrite.v2"
SETUP_COMPILERS = ("clang", "msvc")
SETUP_VARIANTS = ("baseline", "isolated")
SETUP_ORDERS = ("host-first", "private-first")
SETUP_MODES = {"normal": (0, "ok"), "address": (41, "copied_iid_value"),
               "wrong-helper": (42, "returned_interface"), "no-addref": (43, "qi_addref")}
SETUP_REWRITE_CASES = ("explicit-values", "smart-pointers", "runtime")
SETUP_EDGE_ACCEPT = ("d-only", "definition-first", "reference-first", "duplicate-definition")
SETUP_EDGE_REJECT = ("u-only", "missing-d", "old-u", "private-u", "host-fallback",
                     "host-fallback-coff-index")
SETUP_MAP_FILES = {f"{compiler}-{variant}-{order}.map.txt"
                   for compiler in SETUP_COMPILERS for variant in SETUP_VARIANTS
                   for order in SETUP_ORDERS}
SETUP_PROOF_FILES = {f"setup-rewrite-{compiler}-{case}.txt"
                     for compiler in SETUP_COMPILERS for case in SETUP_REWRITE_CASES}
SETUP_EDGE_FILES = ({f"setup-closure-{compiler}-{case}.txt"
                    for compiler in SETUP_COMPILERS for case in SETUP_EDGE_ACCEPT}
                   | {f"setup-negative-{compiler}-{case}.txt"
                      for compiler in SETUP_COMPILERS for case in ("u-only", "missing-d")})
SETUP_FIXED_FILES = ({"manifest.json", "production-stage.txt", "production-rewrite.txt",
                      "production-audit.txt", "production-members.txt"}
                     | SETUP_MAP_FILES | SETUP_PROOF_FILES | SETUP_EDGE_FILES)


def report_identity(target):
    return {"source_sha": os.environ.get("GITHUB_SHA", ""),
            "run_id": os.environ.get("GITHUB_RUN_ID", ""),
            "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT", ""),
            "target": target}


def report_require(condition, message):
    if not condition:
        raise ValueError(message)


def report_sha256(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def report_text(path):
    report_require(not path.is_symlink() and path.is_file(),
                   "Setup evidence is not a regular file: " + path.name)
    size = path.stat().st_size
    report_require(size <= REPORT_FILE_LIMIT, "Setup evidence file exceeds its byte limit: " + path.name)
    with path.open("rb") as stream:
        data = stream.read(REPORT_FILE_LIMIT + 1)
    report_require(len(data) == size and len(data) <= REPORT_FILE_LIMIT,
                   "Setup evidence changed or exceeded its byte limit: " + path.name)
    data.decode("utf-8", errors="strict")
    report_require(b"\0" not in data, "NUL in Setup text evidence: " + path.name)
    return data


def validate_rewrite_proof(proof, target):
    report_require(isinstance(proof, dict) and proof.get("schema") == REWRITE_SCHEMA
                   and proof.get("status") == "passed" and proof.get("published") is True,
                   "Missing completed v2 rewrite proof")
    before, after = proof["input"], proof["output"]
    for record in (before, after):
        report_require(isinstance(record, dict) and report_sha256(record.get("sha256"))
                       and type(record.get("size")) is int and record["size"] > 0
                       and isinstance(record.get("path"), str) and bool(record["path"]),
                       "Invalid rewrite input/output identity")
    byte_proof = proof["byte_proof"]
    report_require(isinstance(byte_proof, dict) and byte_proof.get("status") == "passed"
                   and byte_proof.get("compared_bytes") == before["size"] == after["size"],
                   "Missing complete rewrite byte proof")
    report_require(proof.get("machine") == ("AMD64" if target.startswith("x86_64-") else "ARM64"),
                   "Rewrite machine does not match the native runner")
    readers = proof["tools"]
    report_require(isinstance(readers, dict) and set(readers) == {"nm", "readobj"},
                   "Rewrite must use exactly the two private readers")
    for reader in readers.values():
        report_require(isinstance(reader, dict) and report_sha256(reader.get("sha256"))
                       and isinstance(reader.get("version"), str)
                       and re.search(r"\bLLVM version 20\.1\.8(?:\s|$)", reader["version"]),
                       "Rewrite reader is not the pinned LLVM 20.1.8")


def validate_setup_contract(files):
    def text_file(name):
        report_require(name in files, "Missing Setup evidence file: " + name)
        return files[name].decode("utf-8")

    def json_file(name):
        value = json.loads(text_file(name))
        report_require(isinstance(value, dict), "Expected an object in " + name)
        return value

    target = {"X64": "x86_64-pc-windows-msvc", "ARM64": "aarch64-pc-windows-msvc"}.get(
        os.environ.get("RUNNER_ARCH", "").upper())
    report_require(target is not None, "Unsupported or missing native RUNNER_ARCH")
    report = json_file("manifest.json")
    for key, value in report_identity(target).items():
        report_require(bool(value) and report.get(key) == value, "Setup report identity mismatch: " + key)
    report_require(report.get("schema") == REPORT_SCHEMA and report.get("mode") == "setup-contract"
                   and report.get("status") == "passed" and report.get("setup_status") == "passed"
                   and report.get("verified_checkout") == report["source_sha"],
                   "Setup contract did not complete successfully")
    suite = report["suite"]
    report_require(isinstance(suite, dict) and
                   tuple(suite.get(key) for key in ("tests_run", "failures", "errors", "skipped")) == (1, 0, 0, 0),
                   "Expected exactly one complete Setup contract test")
    commands = report["commands"]
    report_require(isinstance(commands, list) and bool(commands), "Missing Setup commands")
    required_files = set(SETUP_FIXED_FILES)
    for index, command in enumerate(commands, 1):
        report_require(isinstance(command, dict) and command.get("status") == "completed"
                       and type(command.get("returncode")) is int
                       and isinstance(command.get("argv"), list) and bool(command["argv"])
                       and all(isinstance(value, str) for value in command["argv"]),
                       "Incomplete Setup command: " + str(index))
        for stream in ("stdout", "stderr"):
            name = f"command-{index:03d}.{stream}.txt"
            report_require(command.get(stream) == name and name in files,
                           "Missing or mismatched Setup command output: " + name)
            required_files.add(name)

    def command_at(index):
        report_require(type(index) is int and 1 <= index <= len(commands), "Invalid Setup command index")
        return commands[index - 1]

    def audit_command(index, *, diagnostic=None, no_host=False):
        command = command_at(index)
        report_require(any(Path(value).name == "AuditArchive.py" for value in command["argv"]),
                       "Setup audit evidence references the wrong command")
        if no_host:
            report_require("--host-lib-dir" not in command["argv"], "Private closure audit borrowed a host")
        output = text_file(command["stdout"]) + text_file(command["stderr"])
        if diagnostic is None:
            report_require(command["returncode"] == 0
                           and "defined and undefined symbols use the private LLVM ABI" in output,
                           "Missing successful private closure audit")
        else:
            report_require(command["returncode"] != 0 and diagnostic in output,
                           "Missing expected private closure rejection")

    runtime = report["runtime_cases"]
    report_require(isinstance(runtime, list) and len(runtime) == 32
                   and all(isinstance(case, dict) and case.get("status") == "passed" for case in runtime),
                   "Missing Setup runtime cases")
    expected = {(compiler, variant, order, mode) for compiler in SETUP_COMPILERS
                for variant in SETUP_VARIANTS for order in SETUP_ORDERS for mode in SETUP_MODES}
    actual = {(case["compiler"], case["variant"], case["order"], case["mode"]) for case in runtime}
    report_require(actual == expected, "Setup runtime matrix is incomplete")
    runtime_commands = set()
    for case in runtime:
        code, reason = SETUP_MODES[case["mode"]]
        command = command_at(case["command"])
        report_require(case["command"] not in runtime_commands, "Setup runtime command was reused")
        runtime_commands.add(case["command"])
        report_require(case.get("expected_code") == code and case.get("returncode") == code
                       and command["returncode"] == code and len(command["argv"]) == 2
                       and command["argv"][-1] == case["mode"]
                       and Path(command["argv"][0]).name == f"{case['variant']}-{case['order']}.exe"
                       and Path(command["argv"][0]).parent.name == case["compiler"],
                       "Setup runtime mode or full return code differs from its evidence")
        events = [json.loads(line) for line in text_file(command["stdout"]).splitlines() if line]
        report_require(bool(events) and events == case.get("events")
                       and all(isinstance(event, dict) for event in events)
                       and events[-1] == {"event": "final", "check_code": code, "reason": reason},
                       "Setup runtime stdout differs from its recorded events")
        guid_events = [event for event in events if event.get("event") == "guid"]
        report_require(len(guid_events) == 5 and
                       (case["variant"] != "isolated" or
                        all(event.get("same_address") == 0 for event in guid_events)),
                       "Missing or shared private Setup GUID runtime evidence")
        if case["mode"] == "normal":
            counts = {"guid": 5, "qi": 16, "canonical": 16, "copied_iid": 4,
                      "unsupported_iid": 1, "null_output": 1, "qi_coverage": 1,
                      "methods": 6, "provider_iids": 6, "lifecycle": 6, "coverage": 1, "final": 1}
            report_require(len(events) == 64 and
                           {name: sum(event.get("event") == name for event in events) for name in counts} == counts,
                           "Setup normal runtime event coverage is incomplete")
        else:
            report_require([event for event in events if event.get("event") == "negative_control"] ==
                           [{"event": "negative_control", "check_code": code, "reason": reason}],
                           "Setup negative control lacks its exact marker")
    for name in SETUP_MAP_FILES:
        report_require(bool(text_file(name).strip()), "Empty Setup link map: " + name)

    closures = report["closure_cases"]
    report_require(isinstance(closures, list) and len(closures) == 6
                   and all(isinstance(case, dict) and case.get("status") == "passed" for case in closures),
                   "Missing real-object rewrite cases")
    report_require({(case["compiler"], case["case"]) for case in closures}
                   == {(compiler, case) for compiler in SETUP_COMPILERS for case in SETUP_REWRITE_CASES},
                   "Real-object rewrite identities are incomplete")
    for case in closures:
        name = f"setup-rewrite-{case['compiler']}-{case['case']}.txt"
        report_require(case.get("report") == name, "Wrong real-object rewrite report name")
        proof = json_file(name)
        validate_rewrite_proof(proof, target)
        report_require(proof == case.get("proof")
                       and case.get("original_sha256") == proof["input"]["sha256"]
                       and case.get("changed_sha256") == proof["output"]["sha256"],
                       "Real-object rewrite report or archive digest differs from its record")
        audit_command(case["original_rejected_command"], diagnostic="unisolated Setup GUID", no_host=True)
        audit_command(case["changed_accepted_command"], no_host=True)

    edges = report["closure_edges"]
    report_require(isinstance(edges, list) and len(edges) == 20
                   and all(isinstance(case, dict) and case.get("status") == "passed" for case in edges),
                   "Missing Setup definition/reference closure cases")
    report_require({(case["compiler"], case["case"]) for case in edges}
                   == {(compiler, case) for compiler in SETUP_COMPILERS
                       for case in (*SETUP_EDGE_ACCEPT, *SETUP_EDGE_REJECT)},
                   "Setup definition/reference closure identities are incomplete")
    edge_commands = set()
    for case in edges:
        accepted = case["case"] in SETUP_EDGE_ACCEPT
        report_require(case.get("expectation") == ("accept" if accepted else "reject"),
                       "Incorrect Setup closure expectation")
        command = command_at(case["command"])
        report_require(case["command"] not in edge_commands, "Setup closure command was reused")
        edge_commands.add(case["command"])
        if accepted or case["case"] in ("u-only", "missing-d"):
            prefix = "setup-closure" if accepted else "setup-negative"
            name = f"{prefix}-{case['compiler']}-{case['case']}.txt"
            report_require(case.get("report") == name
                           and any(Path(value).name == "RewriteSetupCoffSymbols.py" for value in command["argv"]),
                           "Setup closure references the wrong writer report or command")
            proof = json_file(name)
            report_require(proof == case.get("proof"), "Setup closure writer report differs from its record")
            if accepted:
                validate_rewrite_proof(proof, target)
                report_require(command["returncode"] == 0, "Accepted Setup rewrite command failed")
                audit_command(case["no_host_command"], no_host=True)
            else:
                diagnostic = case.get("negative_error")
                report_require(command["returncode"] != 0 and proof.get("schema") == REWRITE_SCHEMA
                               and proof.get("status") == "failed" and proof.get("published") is False
                               and isinstance(diagnostic, str) and bool(diagnostic)
                               and proof.get("error") == diagnostic
                               and "missing original Setup GUID data definition" in diagnostic,
                               "Missing expected unclosed Setup writer rejection")
        else:
            diagnostic = "unisolated Setup GUID" if case["case"] == "old-u" else "unresolved private dependency"
            report_require(case.get("negative_error") == diagnostic, "Incorrect Setup audit diagnostic")
            if case["case"].startswith("host-fallback"):
                arguments = command["argv"]
                for option in ("--host-lib-dir", "--host-format"):
                    report_require(arguments.count(option) == 1
                                   and arguments.index(option) + 1 < len(arguments)
                                   and bool(arguments[arguments.index(option) + 1]),
                                   "Missing explicit Setup host fallback argument: " + option)
                expected_format = "coff-index" if case["case"].endswith("coff-index") else "nm"
                report_require(arguments[arguments.index("--host-format") + 1] == expected_format,
                               "Setup host fallback reader format differs from its case")
            audit_command(case["command"], diagnostic=diagnostic,
                          no_host=case["case"] in ("old-u", "private-u"))

    stage = json_file("production-stage.txt")
    proof = json_file("production-rewrite.txt")
    validate_rewrite_proof(proof, target)
    report_require(stage.get("phase") == "published" and stage.get("stage") == "publish"
                   and stage.get("result") == "0", "Production archive was not successfully published")
    report_require(stage.get("input_sha256") == proof["input"]["sha256"]
                   and stage.get("output_sha256") == proof["output"]["sha256"],
                   "Production stage/rewrite digest mismatch")
    report_require("defined and undefined symbols use the private LLVM ABI" in text_file("production-audit.txt"),
                   "Missing successful production no-host audit output")
    member_count = proof.get("member_count")
    report_require(type(member_count) is int and member_count > 0 and
        (f"Builtin C++ frontend: checked {member_count} object members; "
         "excluded implementation members are absent") in text_file("production-members.txt").splitlines(),
        "Production archive-member inspection/count differs from the byte proof")
    report_require(set(files) == required_files, "Unexpected or incomplete Setup evidence file inventory")


def setup_upload_ready(ready):
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as stream:
            stream.write("upload_ready=" + ("true" if ready else "false") + "\n")


def verify_setup_contract_report(directory):
    # Export only bounded, validated text snapshots. A failed validation still
    # gets a fresh failure artifact; an unknown upload directory is never used.
    setup_upload_ready(False)
    failures, files, inventory = [], {}, []
    upload = directory / "upload"
    try:
        report_require(not directory.is_symlink() and directory.is_dir(),
                       "Missing or unsafe Setup evidence directory")
        report_require(not upload.exists() and not upload.is_symlink(),
                       "Setup upload directory already exists; refusing to overwrite or upload it")
        paths = []
        for path in directory.iterdir():
            if len(paths) >= REPORT_COUNT_LIMIT - 2:
                failures.append("Too many Setup evidence files")
                break
            paths.append(path)
        total = 0
        for path in sorted(paths):
            try:
                # An old index is checked like every other source file, but
                # never reused as the new export's verification result.
                allowed = (path.name in SETUP_FIXED_FILES or path.name == "evidence-index.txt"
                           or re.fullmatch(r"command-[0-9]{3}\.(stdout|stderr)\.txt", path.name))
                report_require(allowed, "Unexpected Setup evidence file: " + path.name)
                data = report_text(path)
                report_require(total + len(data) <= REPORT_TOTAL_LIMIT - REPORT_METADATA_RESERVE,
                               "Setup evidence exceeds its total byte limit")
                total += len(data)
                if path.name == "evidence-index.txt":
                    continue
                files[path.name] = data
                inventory.append({"file": path.name, "bytes": len(data),
                                  "sha256": hashlib.sha256(data).hexdigest()})
            except (OSError, ValueError) as error:
                if len(failures) < 32:
                    failures.append(str(error)[:1024])
        try:
            validate_setup_contract(files)
        except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
            failures.append(str(error)[:1024])
        status = "failed" if failures else "passed"
        # A numeric filename alone is not proof that a process produced it.
        # Keep only streams declared by the manifest's sequential records,
        # including started records whose partial output helps diagnose failure.
        declared_streams = set()
        try:
            source_report = json.loads(files.get("manifest.json", b"{}"))
            source_commands = source_report.get("commands", []) if isinstance(source_report, dict) else []
            if isinstance(source_commands, list):
                for index, command in enumerate(source_commands, 1):
                    if not isinstance(command, dict):
                        continue
                    for stream in ("stdout", "stderr"):
                        name = f"command-{index:03d}.{stream}.txt"
                        if command.get(stream) == name:
                            declared_streams.add(name)
        except (ValueError, TypeError):
            pass
        payloads = {name: data for name, data in files.items()
                    if not name.startswith("command-") or name in declared_streams}
        if failures:
            # Preserve the original manifest in the source directory. The
            # exported manifest must never suggest that incomplete evidence passed.
            source_manifest_file = None
            if "manifest.json" in files:
                source_manifest_file = "source-manifest.txt"
                payloads[source_manifest_file] = files["manifest.json"]
            payloads["manifest.json"] = (json.dumps({
                "schema": REPORT_SCHEMA, "mode": "setup-contract", "status": "failed",
                "setup_status": "evidence-validation-failed", "failures": failures,
                **report_identity({"X64": "x86_64-pc-windows-msvc", "ARM64": "aarch64-pc-windows-msvc"}.get(
                    os.environ.get("RUNNER_ARCH", "").upper(), "unknown")),
                "source_manifest_file": source_manifest_file,
                "source_manifest": next((item for item in inventory if item["file"] == "manifest.json"), None),
            }, indent=2) + "\n").encode("utf-8")
        exported = [{"file": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                    for name, data in sorted(payloads.items())]
        payloads["evidence-index.txt"] = (json.dumps({
            "status": status, "failures": failures, "source_files": inventory, "files": exported,
        }, indent=2) + "\n").encode("utf-8")
        report_require(len(payloads) <= REPORT_COUNT_LIMIT
                       and all(len(data) <= REPORT_FILE_LIMIT for data in payloads.values())
                       and sum(map(len, payloads.values())) <= REPORT_TOTAL_LIMIT,
                       "Setup export metadata exceeds its byte limit")
        upload.mkdir(exist_ok=False)
        for name, data in sorted(payloads.items()):
            with (upload / name).open("xb") as stream:
                stream.write(data)
        setup_upload_ready(True)
    except (OSError, ValueError) as error:
        failures.append(str(error)[:1024])
    print("Setup contract evidence: " + ("failed" if failures else "passed") +
          (": " + "; ".join(failures) if failures else ""), flush=True)
    return 1 if failures else 0


class SetupContractReportTests(unittest.TestCase):
    # These are report fixtures, not generated compiler evidence. Keep their
    # expected identities and event layout independent of validator constants.
    RUNTIME_ROWS = """
clang baseline host-first normal 0 ok
clang baseline host-first address 41 copied_iid_value
clang baseline host-first wrong-helper 42 returned_interface
clang baseline host-first no-addref 43 qi_addref
clang baseline private-first normal 0 ok
clang baseline private-first address 41 copied_iid_value
clang baseline private-first wrong-helper 42 returned_interface
clang baseline private-first no-addref 43 qi_addref
clang isolated host-first normal 0 ok
clang isolated host-first address 41 copied_iid_value
clang isolated host-first wrong-helper 42 returned_interface
clang isolated host-first no-addref 43 qi_addref
clang isolated private-first normal 0 ok
clang isolated private-first address 41 copied_iid_value
clang isolated private-first wrong-helper 42 returned_interface
clang isolated private-first no-addref 43 qi_addref
msvc baseline host-first normal 0 ok
msvc baseline host-first address 41 copied_iid_value
msvc baseline host-first wrong-helper 42 returned_interface
msvc baseline host-first no-addref 43 qi_addref
msvc baseline private-first normal 0 ok
msvc baseline private-first address 41 copied_iid_value
msvc baseline private-first wrong-helper 42 returned_interface
msvc baseline private-first no-addref 43 qi_addref
msvc isolated host-first normal 0 ok
msvc isolated host-first address 41 copied_iid_value
msvc isolated host-first wrong-helper 42 returned_interface
msvc isolated host-first no-addref 43 qi_addref
msvc isolated private-first normal 0 ok
msvc isolated private-first address 41 copied_iid_value
msvc isolated private-first wrong-helper 42 returned_interface
msvc isolated private-first no-addref 43 qi_addref
""".strip().splitlines()
    NORMAL_EVENT_NAMES = """
guid guid guid guid guid
qi qi qi qi qi qi qi qi qi qi qi qi qi qi qi qi
canonical canonical canonical canonical canonical canonical canonical canonical
canonical canonical canonical canonical canonical canonical canonical canonical
copied_iid copied_iid copied_iid copied_iid unsupported_iid null_output qi_coverage
methods methods methods methods methods methods
provider_iids provider_iids provider_iids provider_iids provider_iids provider_iids
lifecycle lifecycle lifecycle lifecycle lifecycle lifecycle coverage final
""".split()

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="neverc-report-fixture-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.output = self.root / "step-output.txt"
        self.case_number = 0
        environment = mock.patch.dict(os.environ, {
            "GITHUB_SHA": "0123456789abcdef0123456789abcdef01234567",
            "GITHUB_RUN_ID": "987654321", "GITHUB_RUN_ATTEMPT": "2", "RUNNER_ARCH": "X64",
            "GITHUB_OUTPUT": str(self.output),
        }, clear=True)
        environment.start()
        self.addCleanup(environment.stop)
        # An accidental compiler, Git or network dependency must fail here,
        # rather than acquiring a runner toolchain for these pure fixtures.
        for owner, name in ((subprocess, "run"), (subprocess, "Popen"), (urllib.request, "urlopen")):
            guard = mock.patch.object(owner, name, side_effect=AssertionError("Report fixtures must not invoke tools or network"))
            guard.start()
            self.addCleanup(guard.stop)

    @staticmethod
    def json_bytes(value):
        return (json.dumps(value, indent=2) + "\n").encode("utf-8")

    @staticmethod
    def proof():
        return {
            "schema": "neverc.setup-coff-rewrite.v2", "status": "passed", "published": True,
            "input": {"path": "/fixture/original.lib", "sha256": "1" * 64, "size": 128},
            "output": {"path": "/fixture/isolated.lib", "sha256": "2" * 64, "size": 128},
            "machine": "AMD64", "member_count": 2,
            "byte_proof": {"status": "passed", "compared_bytes": 128,
                           "patch_intervals": 1, "changed_bytes": 42},
            "tools": {
                "nm": {"path": "/private/llvm-nm.exe", "sha256": "3" * 64, "version": "LLVM version 20.1.8"},
                "readobj": {"path": "/private/llvm-readobj.exe", "sha256": "4" * 64, "version": "LLVM version 20.1.8"},
            },
        }

    def fixture(self):
        files = {}
        report = {
            "schema": 2, "mode": "setup-contract", "status": "passed", "setup_status": "passed",
            "source_sha": "0123456789abcdef0123456789abcdef01234567",
            "verified_checkout": "0123456789abcdef0123456789abcdef01234567",
            "run_id": "987654321", "run_attempt": "2", "target": "x86_64-pc-windows-msvc",
            "suite": {"tests_run": 1, "failures": 0, "errors": 0, "skipped": 0},
            "commands": [], "runtime_cases": [], "closure_cases": [], "closure_edges": [],
        }

        def command(arguments, code=0, stdout=""):
            index = len(report["commands"]) + 1
            out, err = f"command-{index:03d}.stdout.txt", f"command-{index:03d}.stderr.txt"
            report["commands"].append({"argv": arguments, "status": "completed", "returncode": code,
                                       "stdout": out, "stderr": err})
            files[out], files[err] = stdout.encode("utf-8"), b""
            return index

        def audit(diagnostic=None, host_format=None):
            arguments = ["python", "/fixture/AuditArchive.py", "--archive", "/fixture/private.lib"]
            if host_format is not None:
                arguments += ["--host-lib-dir", "/fixture/host", "--host-format", host_format]
            output = diagnostic or "defined and undefined symbols use the private LLVM ABI"
            return command(arguments, 1 if diagnostic else 0, output + "\n")

        self.assertEqual(len(self.RUNTIME_ROWS), 32)
        self.assertEqual(len(self.NORMAL_EVENT_NAMES), 64)
        for row in self.RUNTIME_ROWS:
            compiler, variant, order, mode, code, reason = row.split()
            code = int(code)
            if mode == "normal":
                events = [{"event": name} for name in self.NORMAL_EVENT_NAMES]
            else:
                events = [{"event": "guid"} for _ in range(5)]
                events += [{"event": "negative_control", "check_code": code, "reason": reason}, {"event": "final"}]
            for event in events:
                if event["event"] == "guid":
                    event["same_address"] = 0 if variant == "isolated" else 1
            events[-1] = {"event": "final", "check_code": code, "reason": reason}
            stdout = "".join(json.dumps(event) + "\n" for event in events)
            index = command([f"/fixture/{compiler}/{variant}-{order}.exe", mode], code, stdout)
            report["runtime_cases"].append({
                "compiler": compiler, "variant": variant, "order": order, "mode": mode,
                "expected_code": code, "returncode": code, "status": "passed", "command": index, "events": events,
            })
            files[f"{compiler}-{variant}-{order}.map.txt"] = b"Fixture link map\n"
        for compiler in ("clang", "msvc"):
            for label in ("explicit-values", "smart-pointers", "runtime"):
                name = f"setup-rewrite-{compiler}-{label}.txt"
                proof = self.proof()
                files[name] = self.json_bytes(proof)
                report["closure_cases"].append({
                    "compiler": compiler, "case": label, "status": "passed", "report": name, "proof": proof,
                    "original_sha256": "1" * 64, "changed_sha256": "2" * 64,
                    "original_rejected_command": audit("unisolated Setup GUID"),
                    "changed_accepted_command": audit(),
                })
            for label in ("d-only", "definition-first", "reference-first", "duplicate-definition"):
                name = f"setup-closure-{compiler}-{label}.txt"
                proof = self.proof()
                files[name] = self.json_bytes(proof)
                report["closure_edges"].append({
                    "compiler": compiler, "case": label, "status": "passed", "expectation": "accept",
                    "report": name, "proof": proof,
                    "command": command(["python", "/fixture/RewriteSetupCoffSymbols.py"]),
                    "no_host_command": audit(),
                })
            for label in ("u-only", "missing-d"):
                name = f"setup-negative-{compiler}-{label}.txt"
                error = "missing original Setup GUID data definition"
                proof = {"schema": "neverc.setup-coff-rewrite.v2", "status": "failed",
                         "published": False, "error": error}
                files[name] = self.json_bytes(proof)
                report["closure_edges"].append({
                    "compiler": compiler, "case": label, "status": "passed", "expectation": "reject",
                    "report": name, "proof": proof, "negative_error": error,
                    "command": command(["python", "/fixture/RewriteSetupCoffSymbols.py"], 1, error),
                })
            for label, diagnostic, host_format in (
                    ("old-u", "unisolated Setup GUID", None),
                    ("private-u", "unresolved private dependency", None),
                    ("host-fallback", "unresolved private dependency", "nm"),
                    ("host-fallback-coff-index", "unresolved private dependency", "coff-index")):
                report["closure_edges"].append({
                    "compiler": compiler, "case": label, "status": "passed", "expectation": "reject",
                    "negative_error": diagnostic, "command": audit(diagnostic, host_format),
                })
        files["production-stage.txt"] = self.json_bytes({
            "phase": "published", "stage": "publish", "result": "0",
            "input_sha256": "1" * 64, "output_sha256": "2" * 64,
        })
        files["production-rewrite.txt"] = self.json_bytes(self.proof())
        files["production-audit.txt"] = b"defined and undefined symbols use the private LLVM ABI\n"
        files["production-members.txt"] = (
            b"Builtin C++ frontend: checked 2 object members; excluded implementation members are absent\r\n")
        files["manifest.json"] = self.json_bytes(report)
        return files

    def stage(self, files):
        self.case_number += 1
        directory = self.root / ("report-" + str(self.case_number))
        directory.mkdir()
        for name, data in files.items():
            (directory / name).write_bytes(data)
        self.output.write_text("", encoding="utf-8")
        return directory

    def exported(self, files, code):
        directory = self.stage(files)
        self.assertEqual(verify_setup_contract_report(directory), code)
        self.assertEqual(self.output.read_text(encoding="utf-8").splitlines()[-1], "upload_ready=true")
        upload = directory / "upload"
        manifest = json.loads((upload / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["status"], "passed" if code == 0 else "failed")
        index = json.loads((upload / "evidence-index.txt").read_text(encoding="utf-8"))
        self.assertEqual(index["status"], manifest["status"])
        for record in index["files"]:
            data = (upload / record["file"]).read_bytes()
            self.assertEqual(len(data), record["bytes"])
            self.assertEqual(hashlib.sha256(data).hexdigest(), record["sha256"])
        return directory, manifest

    def test_complete_report_and_export(self):
        files = self.fixture()
        validate_setup_contract(files)
        directory, _ = self.exported(files, 0)
        self.assertEqual({path.name for path in (directory / "upload").iterdir()}, set(files) | {"evidence-index.txt"})
        for name, data in files.items():
            self.assertEqual((directory / name).read_bytes(), data)
            self.assertEqual((directory / "upload" / name).read_bytes(), data)

    def test_missing_or_empty_map_is_rejected(self):
        for empty in (False, True):
            with self.subTest(empty=empty):
                files = self.fixture()
                if empty:
                    files["clang-isolated-host-first.map.txt"] = b""
                else:
                    del files["clang-isolated-host-first.map.txt"]
                with self.assertRaisesRegex(ValueError, "map"):
                    validate_setup_contract(files)

    def test_truncated_or_inconsistent_rewrite_proof_is_rejected(self):
        for truncated in (False, True):
            with self.subTest(truncated=truncated):
                files = self.fixture()
                if truncated:
                    files["setup-rewrite-msvc-runtime.txt"] = b'{"schema":'
                else:
                    proof = json.loads(files["setup-rewrite-msvc-runtime.txt"])
                    proof["input"]["sha256"] = "a" * 64
                    files["setup-rewrite-msvc-runtime.txt"] = self.json_bytes(proof)
                with self.assertRaises(ValueError):
                    validate_setup_contract(files)

    def test_ci_identity_mismatches_are_rejected(self):
        for key, value in (("source_sha", "b" * 40), ("run_id", "different-run"),
                           ("run_attempt", "3"), ("target", "aarch64-pc-windows-msvc")):
            with self.subTest(field=key):
                files = self.fixture()
                report = json.loads(files["manifest.json"])
                report[key] = value
                files["manifest.json"] = self.json_bytes(report)
                with self.assertRaisesRegex(ValueError, "identity mismatch"):
                    validate_setup_contract(files)
        for arch in ("ARM64", "unknown", ""):
            with self.subTest(runner_arch=arch), mock.patch.dict(os.environ, {"RUNNER_ARCH": arch}):
                with self.assertRaises(ValueError):
                    validate_setup_contract(self.fixture())

    def test_unfinished_or_empty_commands_are_rejected(self):
        for empty in (False, True):
            with self.subTest(empty=empty):
                files = self.fixture()
                report = json.loads(files["manifest.json"])
                if empty:
                    report["commands"] = []
                else:
                    report["commands"][0]["status"] = "started"
                files["manifest.json"] = self.json_bytes(report)
                with self.assertRaisesRegex(ValueError, "command"):
                    validate_setup_contract(files)

    def test_runtime_stdout_or_full_exit_code_is_rejected(self):
        for field in ("stdout", "returncode"):
            with self.subTest(field=field):
                files = self.fixture()
                report = json.loads(files["manifest.json"])
                command = report["commands"][1]
                if field == "stdout":
                    files[command["stdout"]] = b'{"event":"different"}\n'
                else:
                    command["returncode"] = 0xC0000029
                    files["manifest.json"] = self.json_bytes(report)
                with self.assertRaisesRegex(ValueError, "runtime"):
                    validate_setup_contract(files)

    def test_runtime_matrix_and_event_coverage_are_required(self):
        for mutation in ("duplicate", "normal-event", "negative-marker"):
            with self.subTest(mutation=mutation):
                files = self.fixture()
                report = json.loads(files["manifest.json"])
                if mutation == "duplicate":
                    report["runtime_cases"][-1] = report["runtime_cases"][0]
                else:
                    case = report["runtime_cases"][0 if mutation == "normal-event" else 1]
                    name = "qi" if mutation == "normal-event" else "negative_control"
                    index = next(index for index, event in enumerate(case["events"]) if event["event"] == name)
                    del case["events"][index]
                    output = report["commands"][case["command"] - 1]["stdout"]
                    files[output] = "".join(json.dumps(event) + "\n" for event in case["events"]).encode("utf-8")
                files["manifest.json"] = self.json_bytes(report)
                with self.assertRaises(ValueError):
                    validate_setup_contract(files)

    def test_unknown_and_non_text_files_are_not_exported(self):
        for name, data in (("sdk-source.txt", b"unknown source text"),
                           ("fixture.cpp", b"int main() {}"),
                           ("command-999.stdout.txt", b"unreferenced command text"),
                           ("clang-isolated-host-first.map.txt", b"bad\0text"),
                           ("clang-isolated-host-first.map.txt", b"bad\xfftext")):
            with self.subTest(name=name, data=data):
                files = self.fixture()
                files[name] = data
                directory, _ = self.exported(files, 1)
                self.assertFalse((directory / "upload" / name).exists())
                self.assertEqual((directory / name).read_bytes(), data)

    def test_symlink_sources_and_root_are_rejected(self):
        # Mock only the filesystem's symlink predicate so this contract does
        # not depend on Windows symlink creation privileges. Real bytes and
        # directories still exercise the complete verifier/export path.
        for kind in ("root", "index", "upload"):
            with self.subTest(kind=kind):
                directory = self.stage(self.fixture())
                unsafe = (directory if kind == "root" else
                          directory / ("upload" if kind == "upload" else "evidence-index.txt"))
                if kind == "index":
                    unsafe.write_bytes(b"original index target")
                original = Path.is_symlink
                with mock.patch.object(Path, "is_symlink", lambda path: path == unsafe or original(path)):
                    self.assertEqual(verify_setup_contract_report(directory), 1)
                output = self.output.read_text(encoding="utf-8").splitlines()
                if kind != "index":
                    self.assertEqual(output[-1], "upload_ready=false")
                    self.assertFalse((directory / "upload").exists())
                else:
                    self.assertEqual(output[-1], "upload_ready=true")
                    self.assertEqual(unsafe.read_bytes(), b"original index target")
                    index = json.loads((directory / "upload/evidence-index.txt").read_text(encoding="utf-8"))
                    self.assertEqual(index["status"], "failed")

    def test_file_byte_limit_excludes_oversized_evidence(self):
        files = self.fixture()
        name = "clang-isolated-host-first.map.txt"
        files[name] = b"x" * (8 * 1024 * 1024 + 1)
        directory, manifest = self.exported(files, 1)
        self.assertTrue(any("byte limit" in error for error in manifest["failures"]))
        self.assertFalse((directory / "upload" / name).exists())
        self.assertEqual((directory / name).stat().st_size, 8 * 1024 * 1024 + 1)

    def test_total_byte_limit_produces_a_bounded_failure_export(self):
        files = self.fixture()
        for name in ("clang-baseline-host-first.map.txt", "clang-baseline-private-first.map.txt",
                     "clang-isolated-host-first.map.txt", "clang-isolated-private-first.map.txt",
                     "msvc-baseline-host-first.map.txt"):
            files[name] = b"x" * (7 * 1024 * 1024)
        directory, manifest = self.exported(files, 1)
        self.assertTrue(any("total byte limit" in error for error in manifest["failures"]))
        self.assertLessEqual(sum(path.stat().st_size for path in (directory / "upload").iterdir()), 32 * 1024 * 1024)

    def test_file_count_limit_is_enforced(self):
        files = self.fixture()
        for index in range(600, 1000):
            files[f"command-{index}.stdout.txt"] = b""
        directory, manifest = self.exported(files, 1)
        self.assertIn("Too many Setup evidence files", manifest["failures"])
        self.assertLessEqual(len(list((directory / "upload").iterdir())), 512)
        self.assertEqual(len(list(directory.iterdir())), len(files) + 1)

    def test_failure_export_preserves_source_manifest_bytes(self):
        files = self.fixture()
        report = json.loads(files["manifest.json"])
        report["status"] = "failed"
        report["suite"]["failures"] = 1
        report["suite"]["failure_details"] = [{"test": "fixture", "traceback": "first line\nassertion details\n"}]
        files["manifest.json"] = self.json_bytes(report)
        directory, manifest = self.exported(files, 1)
        self.assertEqual(manifest["source_manifest_file"], "source-manifest.txt")
        self.assertEqual((directory / "upload/source-manifest.txt").read_bytes(), files["manifest.json"])
        self.assertEqual((directory / "manifest.json").read_bytes(), files["manifest.json"])

    def test_existing_upload_is_never_overwritten(self):
        directory = self.stage(self.fixture())
        upload = directory / "upload"
        upload.mkdir()
        (upload / "sentinel.txt").write_bytes(b"existing data")
        self.assertEqual(verify_setup_contract_report(directory), 1)
        self.assertEqual(self.output.read_text(encoding="utf-8").splitlines()[-1], "upload_ready=false")
        self.assertEqual({path.name for path in upload.iterdir()}, {"sentinel.txt"})
        self.assertEqual((upload / "sentinel.txt").read_bytes(), b"existing data")

    def test_production_proof_stage_and_member_count_must_agree(self):
        for field in ("schema", "published", "byte_proof", "stage", "hash", "member_count"):
            with self.subTest(field=field):
                files = self.fixture()
                proof = json.loads(files["production-rewrite.txt"])
                stage = json.loads(files["production-stage.txt"])
                if field == "schema":
                    proof["schema"] = "obsolete"
                elif field == "published":
                    proof["published"] = False
                elif field == "byte_proof":
                    proof["byte_proof"]["compared_bytes"] = 127
                elif field == "stage":
                    stage["phase"] = "started"
                elif field == "hash":
                    stage["output_sha256"] = "5" * 64
                else:
                    proof["member_count"] = 3
                files["production-rewrite.txt"] = self.json_bytes(proof)
                files["production-stage.txt"] = self.json_bytes(stage)
                with self.assertRaises(ValueError):
                    validate_setup_contract(files)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-root", type=Path)
    parser.add_argument("--target", choices=(
        "x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc"))
    parser.add_argument("--report-dir", type=Path)
    parser.add_argument("--report-self-tests", action="store_true")
    parser.add_argument("--setup-contract", action="store_true")
    parser.add_argument("--private-nm", type=Path)
    parser.add_argument("--private-readobj", type=Path)
    parser.add_argument("--pinned-setup-header", type=Path)
    parser.add_argument("--pending-setup-contract-report-dir", type=Path)
    parser.add_argument("--verify-setup-contract-report", action="store_true")
    arguments = parser.parse_args()
    if arguments.report_self_tests:
        if (arguments.report_dir or arguments.setup_contract or arguments.llvm_root or arguments.target
                or arguments.private_nm or arguments.private_readobj or arguments.pinned_setup_header
                or arguments.pending_setup_contract_report_dir or arguments.verify_setup_contract_report):
            parser.error("Report self-tests accept no toolchain or report arguments")
        program = unittest.main(argv=[sys.argv[0], "SetupContractReportTests"], verbosity=2, exit=False)
        result = program.result
        sys.exit(0 if result.testsRun > 0 and result.wasSuccessful() and not result.skipped else 1)
    if arguments.report_dir is None:
        parser.error("--report-dir is required unless --report-self-tests is selected")
    if arguments.verify_setup_contract_report:
        if (arguments.setup_contract or arguments.llvm_root or arguments.target
                or arguments.private_nm or arguments.private_readobj or arguments.pinned_setup_header
                or arguments.pending_setup_contract_report_dir):
            parser.error("Evidence verification accepts only --report-dir")
        sys.exit(verify_setup_contract_report(arguments.report_dir.absolute()))
    if arguments.llvm_root is None or arguments.target is None:
        parser.error("--llvm-root and --target are required to run toolchain tests")
    if arguments.setup_contract and any(value is None for value in (
            arguments.private_nm, arguments.private_readobj, arguments.pinned_setup_header)):
        parser.error("Setup contract requires the private nm, readobj and pinned header")
    if not arguments.setup_contract and any(value is not None for value in (
            arguments.private_nm, arguments.private_readobj, arguments.pinned_setup_header)):
        parser.error("Private reader arguments require --setup-contract")
    if arguments.setup_contract and arguments.pending_setup_contract_report_dir:
        parser.error("The post-build contract cannot create its own pending record")
    identity = report_identity(arguments.target)
    if arguments.pending_setup_contract_report_dir:
        pending = arguments.pending_setup_contract_report_dir.resolve()
        pending.mkdir(parents=True, exist_ok=False)
        (pending / "manifest.json").write_text(json.dumps({
            "schema": REPORT_SCHEMA, "mode": "setup-contract", "status": "not-started",
            "setup_status": "not-started", **identity,
            "reason": "Waiting for private LLVM readers and bundle build"}, indent=2) + "\n",
            encoding="utf-8")
    CppFrontendToolchainTests.llvm_root = arguments.llvm_root.resolve()
    CppFrontendToolchainTests.target = arguments.target
    CppFrontendToolchainTests.setup_contract = arguments.setup_contract
    CppFrontendToolchainTests.report_dir = arguments.report_dir.absolute()
    if arguments.setup_contract:
        report_require(not CppFrontendToolchainTests.report_dir.is_symlink(), "Unsafe pending Setup directory")
        pending = json.loads(report_text(CppFrontendToolchainTests.report_dir / "manifest.json"))
        if (not isinstance(pending, dict) or pending.get("schema") != REPORT_SCHEMA
                or pending.get("status") != "not-started" or pending.get("mode") != "setup-contract"
                or any(pending.get(key) != value or not value for key, value in identity.items())):
            raise ValueError("Missing or mismatched pending Setup contract identity")
        CppFrontendToolchainTests.private_nm = arguments.private_nm.resolve()
        CppFrontendToolchainTests.private_readobj = arguments.private_readobj.resolve()
        CppFrontendToolchainTests.pinned_setup_header = arguments.pinned_setup_header.resolve()
    else:
        CppFrontendToolchainTests.report_dir.mkdir(parents=True, exist_ok=False)
    CppFrontendToolchainTests.setup_report = {
        "schema": REPORT_SCHEMA, "mode": "setup-contract" if arguments.setup_contract else "baseline",
        "status": "started", "setup_status": "not-started", **identity,
        "headers": {}, "runtime_headers": {}, "tools": {},
        "object_probes": [], "commands": [], "runtime_cases": [], "closure_cases": [], "closure_edges": [],
        "limits": {"setup_cooperative_seconds": 600, "command_seconds": 120,
                   "runtime_seconds": 15, "report_file_bytes": REPORT_FILE_LIMIT,
                   "report_total_bytes": REPORT_TOTAL_LIMIT, "report_files": REPORT_COUNT_LIMIT},
    }
    CppFrontendToolchainTests.write_setup_report()
    report = CppFrontendToolchainTests.setup_report
    passed = False
    try:
        selected = (["CppFrontendToolchainTests.test_setup_sdk_inputs_and_object_provenance"]
                    if arguments.setup_contract else ["CppFrontendToolchainTests"])
        program = unittest.main(argv=[sys.argv[0], *selected], verbosity=2, exit=False)
        result = program.result
        report["suite"] = {
            "tests_run": result.testsRun, "failures": len(result.failures),
            "errors": len(result.errors), "skipped": len(result.skipped),
            "failure_details": [{"test": str(test), "traceback": details}
                                for test, details in result.failures],
            "error_details": [{"test": str(test), "traceback": details}
                              for test, details in result.errors],
        }
        passed = (result.wasSuccessful() and not result.skipped
                  and result.testsRun == (1 if arguments.setup_contract else 22)
                  and report["setup_status"] == "passed")
    except BaseException as error:
        report["interrupted"] = {"type": type(error).__name__, "detail": str(error)}
        raise
    finally:
        report["status"] = "passed" if passed else "failed"
        CppFrontendToolchainTests.write_setup_report()
    sys.exit(0 if passed else 1)
