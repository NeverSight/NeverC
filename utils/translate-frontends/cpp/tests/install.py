#!/usr/bin/env python3
"""Install NeverC and verify builtin C++ translation without external toolchains."""
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
    parser.add_argument("--neverc-build", required=True, type=Path,
                        help="Install this build's neverc CMake component and configured runtime")
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--deny-read", action="append", default=[], type=Path,
                        help="On macOS, deny this development/SDK path to all installed smoke children; repeatable")
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    if prefix.exists():
        parser.error("--prefix must be a new directory owned by this smoke test")
    cmake = shutil.which("cmake")
    if not cmake:
        parser.error("cmake is required to install the neverc component")
    denied = sorted({str(path.absolute()) for path in args.deny_read} |
                    {str(path.resolve()) for path in args.deny_read})
    for path in denied:
        if not Path(path).exists():
            parser.error(f"--deny-read must identify an existing path: {path}")
        if prefix == Path(path) or prefix.is_relative_to(path):
            parser.error("the new installation prefix must be outside denied paths")
    if denied and (sys.platform != "darwin" or not Path("/usr/bin/sandbox-exec").is_file()):
        parser.error("--deny-read requires macOS sandbox-exec; isolation is never silently skipped")
    for directory in ("bin", "sources", "generated"):
        (prefix / directory).mkdir(parents=True)
    installed = prefix / "bin" / args.neverc.name
    source_neverc = args.neverc.resolve()
    source_sha256 = hashlib.sha256(source_neverc.read_bytes()).hexdigest()
    environment = os.environ.copy()
    missing = prefix / "unavailable-external-toolchain"
    environment.update(NEVERC_CPP_FRONTEND=str(missing / "frontend"),
                       NEVERC_CPP_SDK=str(missing / "sdk.json"), SDKROOT=str(missing / "sdk"),
                       CPATH=str(missing / "include"), CPLUS_INCLUDE_PATH=str(missing / "include"))
    environment["PATH"] = ("/usr/bin:/bin" if os.name != "nt" else
                           os.environ.get("SystemRoot", "C:\\Windows") + "\\System32")
    evidence = {"status": "running", "commands": [], "source_neverc_sha256": source_sha256,
                "neverc_installation": "cmake-component", "denied_read_paths": denied}
    sandbox = []

    def run(command, env=environment, expected=0, isolated=True):
        command = [*(sandbox if isolated else []), *(str(x) for x in command)]
        result = subprocess.run(command, env=env, cwd=prefix, text=True,
                                capture_output=True, timeout=300)
        evidence["commands"].append({"arguments": command, "exit_code": result.returncode,
                                     "sandboxed": bool(sandbox) and isolated})
        if (expected == 0 and result.returncode) or (expected != 0 and not result.returncode):
            sys.stderr.write(result.stdout + result.stderr)
            raise RuntimeError(f"unexpected exit {result.returncode}: {command}")
        return result.stdout + result.stderr

    install_environment = os.environ.copy()
    install_environment.pop("DESTDIR", None)
    components = ["neverc", "neverc-resource-headers", "neverc-std"]
    if sys.platform == "darwin":
        # This existing NeverC runtime is universal despite its directory name.
        components.append("neverc-runtime-macos-arm64")
    elif sys.platform.startswith("linux"):
        target = run([source_neverc, "--no-default-config", "-dumpmachine"]).strip()
        architecture = "arm64" if target.startswith(("aarch64-", "arm64-")) else "x64"
        components.append("neverc-runtime-linux-" + architecture)
    for component in components:
        run([cmake, "--install", args.neverc_build.resolve(), "--component", component,
             "--prefix", prefix], env=install_environment)
    evidence["installed_components"] = list(components)
    assert installed.is_file(), installed
    cache = args.neverc_build / "CMakeCache.txt"
    evidence["neverc_build_configuration"] = {
        line.split(":", 1)[0]: line.split("=", 1)[1]
        for line in cache.read_text().splitlines()
        if line.startswith(("NEVERC_ENABLE_MIMALLOC:BOOL=", "NEVERC_ENABLE_PYTHON_PLUGINS:BOOL=",
                            "NEVERC_BUNDLE_PYTHON_RUNTIME:BOOL="))}
    installed_resources = Path(run([installed, "--no-default-config", "-print-resource-dir"]).strip())
    assert installed_resources.is_relative_to(prefix), installed_resources
    assert (installed_resources / "include/neverc/std/math.h").is_file()
    evidence["installed_neverc_resources"] = str(installed_resources)
    assert not list((prefix / "bin").glob("neverc-cpp-frontend*"))
    assert not list(prefix.rglob("neverc-cpp-sdk.json"))
    notices = prefix / "share/neverc/licenses"
    for name in ("LICENSE.TXT", "BLAKE3/LICENSE", "NeverCCppThirdPartyNotices.txt", "UPSTREAM-SOURCE.txt"):
        assert (notices / "llvm-project-20.1.8" / name).is_file(), name
    sdk_sources = notices / "cpp-sdk"
    for name in ("README.md", "provenance.json", "embed.py", "licenses/LLVM.txt",
                 "licenses/APSL-1.1.txt", "licenses/APSL-2.0.txt", "licenses/BSD-4-NOTICES.txt"):
        assert (sdk_sources / name).is_file(), name
    sdk_catalog = json.loads((sdk_sources / "catalog.json").read_text())
    assert sdk_catalog["distribution_id"] == "neverc-embedded-clang20.1.8-libcxx200100-macos15.5"
    assert len(sdk_catalog["headers"]) == 209
    for entry in sdk_catalog["headers"] + sdk_catalog["metadata"]:
        data = (sdk_sources / entry["root"] / entry["path"]).read_bytes()
        assert hashlib.sha256(data).hexdigest() == entry["sha256"], entry
    evidence["installed_license_and_source_snapshot"] = "verified"

    if denied:
        profile = prefix / "deny-development.sb"
        profile.write_text("(version 1)\n(allow default)\n" +
                           "".join("(deny file-read* (subpath " + json.dumps(path) + "))\n" for path in denied))
        sandbox = ["/usr/bin/sandbox-exec", "-f", str(profile)]
        run(["/bin/echo", "sandbox-active"])
        for path in denied:
            run(["/bin/ls", "-d", path], expected=1)
    evidence["neverc"] = run([installed, "--no-default-config", "--version"])
    evidence["frontend"] = run([installed, "__neverc_cpp_frontend", "--version"])
    evidence["installed_neverc_sha256"] = hashlib.sha256(installed.read_bytes()).hexdigest()

    def darwin_libraries(artifact):
        # Inspection tools belong to the test harness; /usr/bin/otool itself
        # is an Xcode shim. Only NeverC and generated programs must execute
        # with developer paths denied. Inspect their files outside that box.
        output = run(["/usr/bin/otool", "-L", artifact], isolated=False)
        return output, [line.strip().split(" (", 1)[0] for line in output.splitlines()[1:] if line.startswith("\t")]

    # Follow the compiler's non-platform dependency closure, including a bundled
    # Python runtime when configured. Every such dependency must be installed.
    evidence["compiler_dynamic_dependencies"] = {}
    pending = [(installed, [])]
    visited = set()
    while pending:
        artifact, inherited = pending.pop()
        artifact = artifact.resolve()
        if artifact in visited:
            continue
        visited.add(artifact)
        if sys.platform == "darwin":
            output, libraries = darwin_libraries(artifact)
            commands = run(["/usr/bin/otool", "-l", artifact], isolated=False)
            rpaths = re.findall(r"cmd LC_RPATH\n\s*cmdsize \d+\n\s*path (.*?) \(offset", commands)
            def expand(path):
                return path.replace("@loader_path", str(artifact.parent)).replace("@executable_path", str(installed.parent))
            search = [expand(path) for path in rpaths] + inherited
            for library in libraries:
                assert not re.search(r"libclang|libLLVM", library, re.IGNORECASE), library
                if library.startswith(("/usr/lib/", "/System/Library/")):
                    continue
                candidates = ([Path(path) / library[len("@rpath/"):] for path in search]
                              if library.startswith("@rpath/") else [Path(expand(library))])
                resolved = next((path.resolve() for path in candidates if path.is_file()), None)
                assert resolved is not None and resolved.is_relative_to(prefix), (artifact, library, candidates)
                pending.append((resolved, search))
            evidence["compiler_dynamic_dependencies"][str(artifact)] = output
        elif sys.platform.startswith("linux"):
            output = run(["/usr/bin/ldd", artifact], isolated=False)
            assert not re.search(r"libclang|libLLVM|not found", output, re.IGNORECASE), output
            evidence["compiler_dynamic_dependencies"][str(artifact)] = output
        else:
            raise RuntimeError("dependency inspection is currently implemented for measured Unix hosts")

    repository = Path(__file__).resolve().parents[4]
    program = prefix / "sources/program.cpp"
    program.write_bytes((repository / "examples/translate-cpp/input.cpp").read_bytes())
    module = prefix / "sources/module.cpp"
    module.write_text('extern "C" unsigned int install_probe(unsigned int x){return x*37u+11u;}\n')
    harness = prefix / "sources/harness.c"
    harness.write_text("unsigned int install_probe(unsigned int);\nint main(void){return install_probe(7u)==270u?0:1;}\n")
    for name, source in (("program", program), ("module", module)):
        destination = prefix / "generated" / (name + ".nc")
        run([installed, "translate", "--from", "cpp", source, "-o", destination, "--", "-std=c++17"])
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
    if sys.platform == "darwin":
        architecture = run([installed, "--no-default-config", "-dumpmachine"]).strip().split("-")[0]
        assert architecture in ("arm64", "aarch64", "x86_64"), architecture
        math_target = ("arm64" if architecture == "aarch64" else architecture) + "-apple-macosx15.0.0"
        math_source = prefix / "sources/math.cpp"
        math_source.write_text('#include <cmath>\nextern "C" double install_absolute(double x){return std::fabs(x);}\nextern "C" double install_floor(double x){return std::floor(x);}\n')
        math_db = prefix / "sources/math-compile_commands.json"
        math_db.write_text(json.dumps([{"directory": str(math_source.parent), "file": str(math_source),
            "arguments": ["clang++", "-std=c++17", "-O2", "-c", str(math_source)]}]))
        math_generated = prefix / "generated/math"
        run([installed, "translate", "--from", "cpp", "--profile", "cpp-math-v1",
             "--target", math_target, "--project-root", math_source.parent, "--compdb", math_db,
             math_source, "--out-dir", math_generated])
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
    artifacts = []
    for optimization in ("-O0", "-O2"):
        suffix = optimization[1:]
        program_binary = prefix / "bin" / ("program-" + suffix)
        module_object = prefix / "generated" / ("module-" + suffix + ".o")
        harness_object = prefix / "generated" / ("client-" + suffix + ".o")
        module_binary = prefix / "bin" / ("module-" + suffix)
        project_binary = prefix / "bin" / ("project-" + suffix)
        run([installed, "--no-default-config", optimization, prefix / "generated/program.nc", "-o", program_binary])
        run([program_binary])
        run([installed, "--no-default-config", optimization, "-c", prefix / "generated/module.nc", "-o", module_object])
        run([installed, "--no-default-config", optimization, "-c", harness, "-o", harness_object])
        run([installed, "--no-default-config", module_object, harness_object, "-o", module_binary])
        run([module_binary])
        run([installed, "--no-default-config", optimization, project_generated / "translated.nc", "-o", project_binary])
        run([project_binary])
        artifacts.extend((program_binary, module_object, module_binary, project_binary))
        if math_generated:
            math_object = math_generated / ("math-" + suffix + ".o")
            client_object = math_generated / ("client-" + suffix + ".o")
            math_binary = prefix / "bin" / ("math-" + suffix)
            run([installed, "--no-default-config", "-target", math_target, optimization, "-c", math_generated / "translated.nc", "-o", math_object])
            run([installed, "--no-default-config", "-target", math_target, optimization, "-c", math_harness, "-o", client_object])
            run([installed, "--no-default-config", "-target", math_target, math_object, client_object, "-o", math_binary])
            run([math_binary])
            artifacts.extend((math_object, math_binary))
    evidence["generated_binary_inspection"] = {}
    for artifact in artifacts:
        output = run(["/usr/bin/nm", "-u", artifact], isolated=False)
        if artifact.suffix != ".o":
            if sys.platform == "darwin":
                libraries_text, libraries = darwin_libraries(artifact)
                assert libraries == ["/usr/lib/libSystem.B.dylib"], (artifact, libraries)
                output += libraries_text
            else:
                output += run(["/usr/bin/readelf", "-d", artifact], isolated=False)
        # Darwin's libSystem cleanup registration is also used by C mimalloc.
        inspected = "\n".join(line for line in output.splitlines() if line.strip() != "___cxa_atexit")
        assert not re.search(r"__cxa_|__gxx_|_Unwind_|\b_?Z[NTSV]|libc\+\+|libstdc\+\+|libclang|libLLVM|GLIBCXX", inspected), output
        evidence["generated_binary_inspection"][str(artifact)] = output
    evidence["artifact_sizes"] = {str(path): path.stat().st_size for path in (installed, *artifacts)}
    assert hashlib.sha256(source_neverc.read_bytes()).hexdigest() == source_sha256, "source NeverC changed during installation smoke"
    evidence.update(status="passed", external_frontend_and_sdk_overrides_ignored=True,
                    builtin_translation=True, core_and_project_run_o0_o2=True,
                    math_runs_o0_o2=bool(math_generated), sandbox_enforced=bool(denied))
    report = prefix / "smoke-report.json"
    report.write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps({"status": "passed", "report": str(report)}, indent=2))


if __name__ == "__main__":
    main()
