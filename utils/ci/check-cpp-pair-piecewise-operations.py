#!/usr/bin/env python3
"""Run selected pair operations, comparisons, queries and piecewise construction in macOS CI."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import subprocess
import sys
import traceback
import xml.etree.ElementTree as ET
from pathlib import Path


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read(path: Path):
    return json.loads(path.read_text())


def write(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n")


class Gate:
    target = "arm64-apple-macosx15.0.0"
    catalog_digest = "1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167"

    def __init__(self, args):
        self.root = args.repo.resolve()
        self.build = args.build.resolve()
        self.output = args.output.resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.compiler = self.build / "bin/neverc"
        self.tests = self.build / "bin/neverc-translate-tests"
        # Preserve the clang++ driver name when it is a symlink to clang.
        self.clangxx = args.clangxx.absolute()
        self.clang = self.clangxx.with_name("clang")
        self.baseline_provenance = args.baseline_provenance.resolve()
        self.baseline = read(self.baseline_provenance)
        self.sdk = self.root / "neverc/lib/Translate/Cpp/SDK"
        self.catalog = read(self.sdk / "catalog.json")
        self.report = {"passed": False, "completed": False, "commands": [], "cases": []}
        self.report_path = self.output / "results.json"

    def save(self):
        write(self.report_path, self.report)

    def run(self, command, log: Path, timeout=180):
        command = [str(v) for v in command]
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open("w") as stream:
            result = subprocess.run(command, cwd=self.root, stdout=stream,
                                    stderr=subprocess.STDOUT, timeout=timeout)
        self.report["commands"].append({"command": command, "returncode": result.returncode,
                                        "log": str(log), "log_sha256": digest(log)})
        self.save()
        return result.returncode, log.read_text()

    def checked(self, command, log: Path, timeout=180):
        code, text = self.run(command, log, timeout)
        if code:
            raise RuntimeError(f"Command failed ({code}); see {log}\n{text[-6000:]}")
        return text

    def sdk_check(self):
        assert digest(self.sdk / "catalog.json") == self.catalog_digest
        assert self.catalog["distribution_id"] == "neverc-embedded-clang20.1.8-libcxx200100-macos15.5-r17"
        assert len(self.catalog["headers"]) == 664 and len(self.catalog["metadata"]) == 1
        for entry in self.catalog["headers"] + self.catalog["metadata"]:
            assert digest(self.sdk / entry["root"] / entry["path"]) == entry["sha256"]
        self.report["sdk_catalog_sha256"] = self.catalog_digest
        self.report["sdk_entries"] = 665
        self.save()

    def stable(self):
        assert digest(self.compiler) == self.report["compiler_sha256"]
        assert digest(self.tests) == self.report["test_binary_sha256"]
        for name, expected in self.report["source_sha256"].items():
            assert digest(self.root / name) == expected

    def programs(self, suite: str, expected: int):
        text = (self.root / "tests/neverc/TranslateTests.cpp").read_text()
        body = text.split(f"TEST_F(TranslateTest, {suite}) {{", 1)[1].split("\nTEST_F(", 1)[0]
        cases = re.findall(r'\{"([^"\n]+)", R"cpp\((.*?)\)cpp"\}', body, re.S)
        assert len(cases) == len({name for name, _ in cases}) == expected
        return cases

    def native(self, name: str, source: Path, directory: Path):
        for mode in ["-O0", "-O2"]:
            executable = directory / (name + "-native" + mode)
            self.checked(self.native_flags + [mode, source, "-o", executable],
                         directory / (name + "-native" + mode + "-build.log"))
            self.checked([executable], directory / (name + "-native" + mode + "-run.log"), 30)

    def managed(self, name: str, source: Path, directory: Path, expected: bool, sanitize=False):
        generated = directory / (name + ".nc")
        code, text = self.run([self.compiler, "translate", "--from", "cpp", source,
                               "--profile", "cpp-core-v2", "--target", self.target,
                               "-o", generated, "--", "-std=c++17"],
                              directory / (name + "-translate.log"), 120)
        if not expected:
            assert code > 0 and not list(directory.glob(generated.name + "*")), name
            assert ("TR0201" in text or "TR0203" in text), text
            assert "TR0101" not in text and "TR0102" not in text, text
            return
        assert code == 0, text
        assert "mapped_call" not in generated.read_text()
        manifest = read(Path(str(generated) + ".manifest.json"))
        assert manifest["target"]["triple"] == self.target
        assert manifest["sdk"]["catalog_sha256"] == self.catalog_digest
        assert read(Path(str(generated) + ".map.json"))["generated_sha256"] == digest(generated)
        modes = ["-O0", "-O2"] + (["asan-ubsan"] if sanitize else [])
        for mode in modes:
            executable = directory / (name + "-generated" + mode)
            if mode == "asan-ubsan":
                command = [self.clang, "-fuse-ld=lld", "-isysroot", self.sysroot,
                           "-x", "c", "-std=c23", "-O1", "-fno-fast-math", "-ffp-contract=off",
                           "-D__NEVERC_ATOMIC_INT_LOCK_FREE=__CLANG_ATOMIC_INT_LOCK_FREE",
                           "-fsanitize=address,undefined", "-fno-sanitize-recover=all", generated,
                           "-o", executable]
            else:
                command = [self.compiler, generated, mode, "-fno-lto", "-fno-builtin-mimalloc",
                           "-fno-fast-math", "-ffp-contract=off", "-o", executable]
            self.checked(command, directory / (name + mode + "-build.log"))
            self.checked([executable], directory / (name + mode + "-run.log"), 30)

    def cases(self, category: str, cases, expected: bool | None, sanitize=False, native_verified=None):
        for name, text, accepts in cases:
            directory = self.output / category / name
            directory.mkdir(parents=True, exist_ok=True)
            source = directory / (name + ".cpp")
            source.write_text(text)
            if native_verified is None:
                self.native(name, source, directory)
            else:
                assert native_verified[name] == digest(source)
            self.managed(name, source, directory, accepts if expected is None else expected, sanitize)
            self.report["cases"].append({"category": category, "name": name, "passed": True,
                                          "source": str(source), "source_sha256": digest(source)})
            self.stable()
            self.save()
            print(f"PASS {category}: {name}", flush=True)

    def cross(self, sources):
        targets = ["x86_64-apple-macosx15.0.0", self.target, "x86_64-unknown-linux-gnu",
                   "aarch64-unknown-linux-gnu", "i686-unknown-linux-gnu", "x86_64-pc-windows-msvc",
                   "aarch64-pc-windows-msvc", "i686-pc-windows-msvc"]
        protocol_count = object_count = 0
        for target in targets:
            for name, text in sources:
                directory = self.output / "cross" / target / name
                directory.mkdir(parents=True, exist_ok=True)
                source = directory / (name + ".cpp")
                source.write_text(text)
                request, response = directory / "request.json", directory / "response.json"
                write(request, {"protocol": 1, "profile": "cpp-core-v2", "root": str(directory),
                                "source": str(source), "target": target, "arguments": ["-std=c++17"],
                                "sdk": {"distribution_id": self.catalog["distribution_id"],
                                        "catalog_sha256": self.catalog_digest}})
                self.checked([self.compiler, "__neverc_cpp_frontend", "--request", request,
                              "--output", response], directory / "protocol.log", 60)
                module = read(response)
                assert module["diagnostics"] == [] and module["target"]["triple"] == target
                assert module["target"]["pointer_bits"] == (32 if target.startswith("i686-") else 64)
                assert module["sdk_catalog_sha256"] == self.catalog_digest
                for entry in module["sdk_dependencies"]:
                    assert digest(self.sdk / entry["root"] / entry["path"]) == entry["sha256"]
                protocol_count += 1
                if target.startswith("i686-"):
                    continue
                generated, obj = directory / "result.nc", directory / "result.o"
                self.checked([self.compiler, "translate", "--from", "cpp", source, "--profile", "cpp-core-v2",
                              "--target", target, "-o", generated, "--", "-std=c++17"],
                             directory / "translate.log", 120)
                assert read(Path(str(generated) + ".manifest.json"))["target"]["triple"] == target
                self.checked([self.compiler, generated, "--target=" + target, "-O2", "-c", "-fno-lto",
                              "-fno-fast-math", "-ffp-contract=off", "-o", obj], directory / "object.log")
                description = self.checked(["/usr/bin/file", obj], directory / "file.log", 10)
                assert ("Mach-O" if "apple" in target else "ELF" if "linux" in target else "COFF") in description
                assert obj.is_file() and obj.stat().st_size > 0
                object_count += 1
                self.stable()
        assert protocol_count == 88 and object_count == 66
        self.report.update(protocol_cases=protocol_count, cross_object_cases=object_count)
        self.save()

    def refusal_baseline(self, cases, category, expected):
        compiler = Path(self.baseline["compiler"])
        assert self.baseline["revision"] == "1617f18ead74a8fa719e3d1bd02f9c84a10de551"
        assert self.baseline["run_id"] == 39244936666
        assert digest(compiler) == self.baseline["compiler_sha256"]
        completed = []
        for name, text in cases:
            directory = self.output / category.replace("_", "-") / name
            directory.mkdir(parents=True)
            source, generated = directory / (name + ".cpp"), directory / (name + ".nc")
            source.write_text(text)
            self.native(name, source, directory)
            code, output = self.run([compiler, "translate", "--from", "cpp", source,
                "--profile", "cpp-core-v2", "--target", self.target, "-o", generated,
                "--", "-std=c++17"], directory / (name + "-translate.log"), 120)
            assert code > 0 and not list(directory.glob(generated.name + "*")), output
            assert "TR0201" in output or "TR0203" in output, output
            assert "TR0101" not in output and "TR0102" not in output, output
            completed.append({"name": name, "source_sha256": digest(source),
                              "native_passed": True, "prior_managed_rejected": True})
            assert digest(compiler) == self.baseline["compiler_sha256"]
            self.report[category] = {"provenance": self.baseline,
                                                   "cases": completed, "passed": len(completed) == expected}
            self.save()
        assert len(completed) == expected

    def formal(self):
        plan = read(self.root / "utils/ci/cpp-pair-piecewise-formal-tests.json")
        names = plan["new"] + plan["regression"]
        assert plan["expected_total"] == len(names) == len(set(names)) == 124
        directory = self.output / "formal"
        text = self.checked([self.tests, "--gtest_list_tests", "--gtest_filter=" + ":".join(names)],
                            directory / "inventory.log")
        actual, suite = [], ""
        for line in text.splitlines():
            if line.startswith("  "):
                actual.append(suite + line.strip().split("#", 1)[0].strip())
            elif line.endswith("."):
                suite = line
        assert set(actual) == set(names) and len(actual) == 124
        completed = []
        for number, name in enumerate(names, 1):
            xml = directory / (f"{number:03d}-" + name + ".xml")
            self.checked([self.tests, "--gtest_filter=" + name, "--gtest_output=xml:" + str(xml)],
                         xml.with_suffix(".log"), 1800)
            tree = ET.parse(xml).getroot()
            assert int(tree.attrib["tests"]) == 1
            assert int(tree.attrib["errors"]) == int(tree.attrib["failures"]) == 0
            assert int(tree.attrib.get("disabled", "0")) == 0 and not any(tree.iter("skipped"))
            cases = list(tree.iter("testcase"))
            assert [case.attrib["classname"] + "." + case.attrib["name"] for case in cases] == [name]
            assert all(case.attrib.get("status") == "run" and case.attrib.get("result") == "completed"
                       for case in cases)
            completed.append({"name": name, "passed": True, "xml": str(xml), "xml_sha256": digest(xml)})
            write(directory / "results.json", {"passed": len(completed) == 124, "expected_names": names,
                                                "cases": completed, "original_frontend_timeout_seconds": 60})
            self.stable()
            print(f"PASS formal {number}/124: {name}", flush=True)
        self.report["formal_suites"] = len(completed)
        self.save()

    def execute(self):
        assert platform.system() == "Darwin" and platform.machine() == "arm64"
        revision = self.checked(["git", "rev-parse", "HEAD"], self.output / "revision.log").strip()
        self.report.update(revision=revision, compiler_sha256=digest(self.compiler),
                           test_binary_sha256=digest(self.tests), source_sha256={
                               name: digest(self.root / name) for name in [
                                   "neverc/lib/Translate/Cpp/Frontend/Frontend.cpp",
                                   "neverc/lib/Translate/Cpp/Frontend/Frontend.h",
                                   "neverc/lib/Translate/Cpp/Frontend/MathSDK.cpp",
                                   "neverc/lib/Translate/Cpp/Frontend/Lowering.cpp",
                                   "tests/neverc/TranslateTests.cpp"]})
        self.sdk_check()
        version = self.checked([self.clangxx, "--version"], self.output / "native-clang-version.log")
        assert "clang version 20.1.8" in version
        self.sysroot = self.checked(["xcrun", "--show-sdk-path"], self.output / "sysroot.log").strip()
        self.native_flags = [self.clangxx, "--no-default-config", "-x", "c++", "-std=c++17",
                             "-nostdinc", "-nostdinc++", "-fno-ms-compatibility", "-fno-ms-extensions",
                             "-fno-delayed-template-parsing", "-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES",
                             "-D_NEVERC_CPP_CORE_V2_SDK", "-D_LIBCPP___ITERATOR_ISTREAM_ITERATOR_H",
                             "-D_LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H", "-D_LIBCPP___ITERATOR_OSTREAM_ITERATOR_H",
                             "-D_LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H", "-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS",
                             "-fno-fast-math", "-ffp-contract=off", "-resource-dir", self.sdk / "resource",
                             "-isystem", self.sdk / "libcxx", "-isystem", self.sdk / "resource/include",
                             "-target", self.target, "-isysroot", self.sysroot, "-fuse-ld=lld"]
        piecewise = self.programs("CoreV2PairPiecewiseConstructionRunsAtBothOptimizations", 14)
        piecewise_guards = self.programs("CoreV2PairPiecewiseConstructionRequiresExactSources", 10)
        self.refusal_baseline(piecewise, "piecewise_baseline", 14)
        piecewise_native = {case["name"]: case["source_sha256"]
                            for case in self.report["piecewise_baseline"]["cases"]}
        self.cases("piecewise-runtime", [(n, s, True) for n, s in piecewise], True,
                   sanitize=True, native_verified=piecewise_native)
        self.cases("piecewise-guards", [(n, s, False) for n, s in piecewise_guards], False)
        positives = self.programs("CoreV2PairSelectedAssignmentsAndSwapsRunAtBothOptimizations", 13)
        guards = self.programs("CoreV2PairSelectedAssignmentsAndSwapsRequireExactSources", 6)
        positives.sort(key=lambda case: case[0] != "pair-custom-owner-assignment-swap")
        self.cases("runtime", [(n, s, True) for n, s in positives], True, sanitize=True)
        self.cases("guards", [(n, s, False) for n, s in guards], False)
        comparisons = self.programs("CoreV2PairSelectedComparisonsRunAtBothOptimizations", 8)
        comparison_guards = self.programs("CoreV2PairSelectedComparisonsRequireExactSources", 10)
        queries = self.programs("CoreV2PairComparisonQueriesRunAtBothOptimizations", 3)
        query_guards = self.programs("CoreV2PairComparisonQueriesRequireExactSources", 1)
        self.refusal_baseline(comparisons, "comparison_baseline", 8)
        baseline_native = {case["name"]: case["source_sha256"]
                           for case in self.report["comparison_baseline"]["cases"]}
        self.cases("comparison-runtime", [(n, s, True) for n, s in comparisons], True,
                   sanitize=True, native_verified=baseline_native)
        self.cases("comparison-guards", [(n, s, False) for n, s in comparison_guards], False)
        self.cases("comparison-queries", [(n, s, True) for n, s in queries], True, sanitize=True)
        self.cases("comparison-query-guards", [(n, s, False) for n, s in query_guards], False)
        for name, _ in queries:
            generated = self.output / "comparison-queries" / name / (name + ".nc")
            text = generated.read_text()
            assert not re.search(r"\b(?:malloc|calloc|realloc|free|nct_string_copy|nct_delete)\s*\(", text)

        controls = read(self.root / "utils/ci/cpp-pair-selected-controls.json")
        assert controls["expected_cases"] == len(controls["cases"]) == 31
        for case in controls["cases"]:
            assert hashlib.sha256(case["source"].encode()).hexdigest() == case["source_sha256"]
        self.cases("controls", [(c["name"], c["source"], c["expected_accept"]) for c in controls["cases"]], None)
        cross_names = {"pair-string-copy-assignment", "pair-view-assignment-swap",
                       "pair-owner-move-assignment", "pair-array-owner-assignment-swap"}
        comparison_cross = {"pair-string-comparison-embedded-zero", "pair-view-comparison-live-buffers",
                            "pair-owner-comparison-pointer-order"}
        piecewise_cross = {"pair-piecewise-scalars", "pair-piecewise-source-records-order",
                           "pair-piecewise-owner-pointer", "pair-piecewise-reference-bindings"}
        self.cross([(n, s) for n, s in positives if n in cross_names] +
                   [(n, s) for n, s in comparisons if n in comparison_cross] +
                   [(n, s) for n, s in piecewise if n in piecewise_cross])
        for label in ["links", "facts"]:
            self.checked([sys.executable, self.root / f"utils/plugin-api/check-docs-{label}.py"],
                         self.output / ("docs-" + label + ".log"))
        self.formal()
        self.sdk_check()
        self.stable()
        self.report.update(completed=True, passed=True, runtime_programs=35, runtime_executions=105,
                           signature_query_programs=3, signature_query_executions=9,
                           comparison_baseline_programs=8, piecewise_baseline_programs=14,
                           native_valid_source_guards=27, prior_source_controls=31,
                           documentation_files=14, documentation_locales=11)
        self.save()
        print("PASS selected pair CI gate: 124 formal suites, 35 runtime programs/105 modes, 3 queries/9 modes, "
              "27 guards, 31 controls, 88 protocols/66 objects, SDK665 and multilingual docs", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--clangxx", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline-provenance", type=Path, required=True)
    gate = Gate(parser.parse_args())
    try:
        gate.execute()
    except BaseException:
        gate.report["failure"] = traceback.format_exc()
        gate.save()
        raise


if __name__ == "__main__":
    main()
