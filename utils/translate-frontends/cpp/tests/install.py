#!/usr/bin/env python3
"""Installed core/project/math smoke with an external pinned LLVM/SDK runtime."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neverc", required=True, type=Path)
    parser.add_argument("--neverc-build", type=Path,
                        help="Install the neverc CMake component, including its configured bundled runtime")
    parser.add_argument("--helper-build", required=True, type=Path)
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--cpp-sdk", type=Path, help="Enable math smoke and install this descriptor beside the helper")
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    if prefix.exists():
        parser.error("--prefix must be a new directory owned by this smoke test")
    cmake = shutil.which("cmake")
    if not cmake:
        parser.error("cmake is required to install the helper")
    prefix.mkdir(parents=True)
    (prefix / "bin").mkdir()
    (prefix / "sources").mkdir()
    (prefix / "generated").mkdir()
    installed = prefix / "bin" / args.neverc.name
    source_neverc = args.neverc.resolve()
    source_sha256 = hashlib.sha256(source_neverc.read_bytes()).hexdigest()
    environment = os.environ.copy()
    environment.pop("NEVERC_CPP_FRONTEND", None)
    environment["PATH"] = "/usr/bin:/bin" if os.name != "nt" else os.environ.get("SystemRoot", "C:\\Windows") + "\\System32"
    evidence = {"status": "running", "prefix": str(prefix), "commands": [],
                "source_neverc_sha256": source_sha256,
                "neverc_installation": "cmake-component" if args.neverc_build else "executable-copy",
                "external_frontend_runtime": "Pinned LLVM/Clang 20.1.8 dynamic libraries remain installed externally."}

    def run(command, env=environment, expected=0):
        result = subprocess.run([str(x) for x in command], env=env, text=True,
                                capture_output=True, timeout=120)
        evidence["commands"].append({"arguments": [str(x) for x in command],
                                     "exit_code": result.returncode})
        if (expected == 0 and result.returncode) or (expected != 0 and not result.returncode):
            sys.stderr.write(result.stdout + result.stderr)
            raise RuntimeError(f"unexpected exit {result.returncode}: {command}")
        return result.stdout + result.stderr

    install_environment = os.environ.copy()
    install_environment.pop("DESTDIR", None)
    if args.neverc_build:
        run([cmake, "--install", args.neverc_build.resolve(), "--component", "neverc",
             "--prefix", prefix], env=install_environment)
        assert installed.is_file(), installed
        cache = args.neverc_build / "CMakeCache.txt"
        evidence["neverc_build_configuration"] = {
            line.split(":", 1)[0]: line.split("=", 1)[1]
            for line in cache.read_text().splitlines()
            if line.startswith(("NEVERC_ENABLE_MIMALLOC:BOOL=",
                                "NEVERC_ENABLE_PYTHON_PLUGINS:BOOL=",
                                "NEVERC_BUNDLE_PYTHON_RUNTIME:BOOL="))}
    else:
        shutil.copy2(source_neverc, installed)
    run([cmake, "--install", args.helper_build.resolve(), "--prefix", prefix], env=install_environment)
    helper = prefix / "bin" / ("neverc-cpp-frontend.exe" if os.name == "nt" else "neverc-cpp-frontend")
    assert helper.is_file()
    evidence["neverc"] = run([installed, "--version"])
    evidence["frontend"] = run([helper, "--version"])
    evidence["executable_sha256"] = {"neverc": hashlib.sha256(installed.read_bytes()).hexdigest(),
                                    "frontend": hashlib.sha256(helper.read_bytes()).hexdigest()}
    source_resources = Path(run([source_neverc, "-print-resource-dir"]).strip())
    installed_resources = Path(run([installed, "-print-resource-dir"]).strip())
    assert installed_resources.is_relative_to(prefix), installed_resources
    installed_resources.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source_resources, installed_resources)
    evidence["installed_neverc_resources"] = str(installed_resources)
    repository = Path(__file__).resolve().parents[4]
    program = prefix / "sources/program.cpp"
    program.write_bytes((repository / "examples/translate-cpp/input.cpp").read_bytes())
    module = prefix / "sources/module.cpp"
    module.write_text('extern "C" unsigned int install_probe(unsigned int x) { return x * 37u + 11u; }\n')
    harness = prefix / "sources/harness.c"
    harness.write_text("unsigned int install_probe(unsigned int);\nint main(void) { return install_probe(7u) == 270u ? 0 : 1; }\n")
    for name, source in (("program", program), ("module", module)):
        destination = prefix / "generated" / (name + ".nc")
        # No --frontend option or environment override: discovery must find the
        # adjacent installed executable while PATH excludes the development bin.
        run([installed, "translate", "--from", "cpp", source, "-o", destination,
             "--", "-std=c++17"])
        assert destination.is_file()
    project = prefix / "sources/project"
    project.mkdir()
    for name in ("compute.hpp", "first.cpp", "second.cpp", "main.cpp"):
        shutil.copyfile(repository / "tests/neverc/Inputs/translate/cpp/project" / name, project / name)
    database = project / "compile_commands.json"
    database.write_text(json.dumps([{"directory": str(project), "file": str(project / name),
        "arguments": ["clang++", "-std=c++17", "-O2", "-I", str(project), "-c", str(project / name)]}
        for name in ("first.cpp", "second.cpp", "main.cpp")]))
    project_generated = prefix / "generated/project"
    run([installed, "translate", "--from", "cpp", "--profile", "cpp-project-v1",
         "--project-root", project, "--compdb", database, project / "first.cpp", project / "second.cpp",
         project / "main.cpp", "--out-dir", project_generated])
    math_generated = None
    if args.cpp_sdk:
        architecture = run([installed, "-dumpmachine"]).strip().split("-")[0]
        assert architecture in ("arm64", "aarch64", "x86_64"), architecture
        math_target = ("arm64" if architecture == "aarch64" else architecture) + "-apple-macosx15.0.0"
        descriptor = json.loads(args.cpp_sdk.read_text())
        descriptor["roots"] = {key: str((args.cpp_sdk.resolve().parent / value).resolve())
                               for key, value in descriptor["roots"].items()}
        (prefix / "bin/neverc-cpp-sdk.json").write_text(json.dumps(descriptor, indent=2) + "\n")
        math_source = prefix / "sources/math.cpp"
        math_source.write_text('#include <cmath>\nextern "C" double install_absolute(double x){return std::fabs(x);}\nextern "C" double install_floor(double x){return std::floor(x);}\n')
        math_db = prefix / "sources/math-compile_commands.json"
        math_db.write_text(json.dumps([{"directory": str(math_source.parent), "file": str(math_source),
            "arguments": ["clang++", "-std=c++17", "-O2", "-c", str(math_source)]}]))
        math_generated = prefix / "generated/math"
        # Both helper and SDK descriptor use adjacent installed discovery.
        run([installed, "translate", "--from", "cpp", "--profile", "cpp-math-v1",
             "--target", math_target, "--project-root", math_source.parent, "--compdb", math_db, math_source, "--out-dir", math_generated])
        generated_text = (math_generated / "translated.nc").read_text()
        assert "neverc_math_abs" in generated_text and "neverc_math_floor" in generated_text, generated_text
        math_harness = prefix / "sources/math-harness.c"
        math_harness.write_text('''extern double install_absolute(double);
extern double install_floor(double);
typedef union { double value; unsigned long long bits; } Bits;
int main(void) {
  volatile double input = -2.5;
  if (install_absolute(input) != 2.5 || install_floor(input) != -3.0) return 1;
  Bits zero = {.bits = 0x8000000000000000ULL};
  Bits result = {.value = install_absolute(zero.value)};
  if (result.bits != 0ULL) return 2;
  result.value = install_floor(zero.value);
  if (result.bits != zero.bits) return 3;
  Bits tiny = {.bits = 0x8000000000000001ULL};
  if (install_floor(tiny.value) != -1.0) return 4;
  Bits infinity = {.bits = 0x7ff0000000000000ULL};
  result.value = install_floor(infinity.value);
  if (result.bits != infinity.bits) return 5;
  Bits nan = {.bits = 0x7ff8000000000123ULL};
  result.value = install_floor(nan.value);
  if (result.bits != nan.bits) return 6;
  return 0;
}
''')
    first_program = prefix / "bin/program-with-helper"
    run([installed, prefix / "generated/program.nc", "-o", first_program])
    run([first_program])
    # Only mutate the helper copy that this script installed in its new prefix.
    disabled = helper.with_name(helper.name + ".disabled")
    helper.rename(disabled)
    descriptor = prefix / "bin/neverc-cpp-sdk.json"
    if descriptor.exists(): descriptor.rename(descriptor.with_suffix(".json.disabled"))
    missing = run([installed, "translate", "--from", "cpp", program, "--check"], expected=1)
    assert "TR0101" in missing, missing
    generated = prefix / "generated"
    module_object = generated / "module.o"
    harness_object = generated / "harness.o"
    program_alone = prefix / "bin/program-without-helper"
    module_alone = prefix / "bin/module-without-helper"
    run([installed, "-O2", generated / "program.nc", "-o", program_alone])
    run([program_alone])
    run([installed, "-O2", "-c", generated / "module.nc", "-o", module_object])
    run([installed, "-O2", "-c", harness, "-o", harness_object])
    run([installed, module_object, harness_object, "-o", module_alone])
    run([module_alone])
    additional_artifacts = []
    for optimization in ("-O0", "-O2"):
        project_alone = prefix / "bin" / ("project-without-helper-" + optimization[1:])
        run([installed, optimization, project_generated / "translated.nc", "-o", project_alone])
        run([project_alone])
        additional_artifacts.append(project_alone)
        if math_generated:
            math_object = math_generated / ("math-" + optimization[1:] + ".o")
            client_object = math_generated / ("client-" + optimization[1:] + ".o")
            math_alone = prefix / "bin" / ("math-without-helper-" + optimization[1:])
            run([installed, "-target", math_target, optimization, "-c", math_generated / "translated.nc", "-o", math_object])
            run([installed, "-target", math_target, optimization, "-c", math_harness, "-o", client_object])
            run([installed, "-target", math_target, math_object, client_object, "-o", math_alone])
            run([math_alone])
            additional_artifacts.extend((math_object, math_alone))
    evidence["generated_binary_inspection"] = {}
    for artifact in (module_object, program_alone, module_alone, *additional_artifacts):
        text = ""
        if sys.platform == "darwin":
            text += run(["/usr/bin/nm", "-u", artifact])
            if artifact.suffix != ".o":
                text += run(["/usr/bin/otool", "-L", artifact])
        elif sys.platform.startswith("linux"):
            text += run(["/usr/bin/nm", "-u", artifact])
            if artifact.suffix != ".o":
                text += run(["/usr/bin/readelf", "-d", artifact])
        else:
            raise RuntimeError("symbol inspection is currently implemented for the measured Unix hosts")
        inspected = text
        if sys.platform == "darwin":
            # NeverC lowers mimalloc's C destructor attribute through Darwin's
            # libSystem __cxa_atexit. This platform registration function does
            # not imply a C++ standard-library/exception-runtime dependency.
            inspected = "\n".join(line for line in text.splitlines() if line.strip() != "___cxa_atexit")
            evidence["permitted_platform_symbols"] = ["___cxa_atexit"]
        assert not re.search(r"__cxa_|__gxx_|_Unwind_|\b_?Z[NTSV]|libc\+\+|libstdc\+\+|libclang|libLLVM|GLIBCXX", inspected), text
        evidence["generated_binary_inspection"][str(artifact)] = text
    evidence["artifact_sizes"] = {str(p): p.stat().st_size for p in (installed, disabled, module_object, program_alone, module_alone, *additional_artifacts)}
    assert hashlib.sha256(source_neverc.read_bytes()).hexdigest() == source_sha256, "source NeverC changed during installation smoke"
    evidence["status"] = "passed"
    evidence["adjacent_helper_discovery"] = True
    evidence["generated_program_and_c_module_run_without_helper"] = True
    evidence["project_runs_without_helper_o0_o2"] = True
    evidence["math_runs_without_helper_or_descriptor_o0_o2"] = bool(math_generated)
    report = prefix / "smoke-report.json"
    report.write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps({"status": "passed", "report": str(report),
                      "helper_renamed_to": str(disabled)}, indent=2))


if __name__ == "__main__":
    main()
