#!/usr/bin/env python3

"""Consume an installed NeverC plugin SDK from a clean directory.

Given only an install prefix, this verifies the packaged SDK is self-sufficient:
the expected files exist, a plugin fixture compiles against both the single
header and the modular headers with an independent C (and C++) compiler, and the
bundled CMake package config builds the minimal template. It never reads the
source tree, so it catches packaging gaps that in-tree builds would hide.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

FIXTURE_SINGLE = """#include "neverc/Plugin/NevercPluginAPI.h"
int probe_single(void) { return (int)sizeof(NevercABITableHeader); }
"""

FIXTURE_MODULAR = """#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginDynCode.h"
#include "neverc/Plugin/PluginLink.h"
int probe_modular(void) {
  return (int)(sizeof(NevercInterfaceID) + NEVERC_PLUGIN_ABI_MAJOR);
}
"""


def fail(message: str) -> None:
    print(f"test-installed-sdk: {message}", file=sys.stderr)


def find_compiler(explicit: str | None, candidates: list[str]) -> str | None:
    if explicit:
        return explicit if (os.path.isabs(explicit) or shutil.which(explicit)) else None
    for name in candidates:
        found = shutil.which(name)
        if found:
            return found
    return None


def check_layout(sdk: Path) -> int:
    required = [
        "include/neverc/Plugin/NevercPluginAPI.h",
        "include/neverc/Plugin/PluginCore.h",
        "include/neverc/Plugin/PluginDynCode.h",
        "include/neverc/Plugin/PluginLink.h",
        "include/neverc/Plugin/Schema/PluginPhaseSchema.inc",
        "manifest/plugin.json",
        "cmake/NevercPluginSDKConfig.cmake",
        "cmake/NevercPluginSDKConfigVersion.cmake",
        "pkgconfig/neverc-plugin.pc",
        "templates/minimal/CMakeLists.txt",
        "templates/minimal/Plugin.c",
        "examples/DynCodeTracePlugin.c",
        "examples/DynCodeEncoderPlugin.c",
    ]
    missing = [rel for rel in required if not (sdk / rel).is_file()]
    if missing:
        fail("installed SDK is missing files:\n  " + "\n  ".join(missing))
        return 1
    return 0


def compile_fixture(compiler: str, std_flag: str, include: Path, source: Path,
                    output: Path) -> int:
    result = subprocess.run(
        [compiler, std_flag, "-I", str(include), "-c", str(source),
         "-o", str(output)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        fail(f"{compiler} {std_flag} failed for {source.name}:\n{result.stderr}")
        return 1
    return 0


def build_template(sdk: Path, work: Path) -> int:
    cmake = shutil.which("cmake")
    if not cmake:
        print("test-installed-sdk: cmake not found; skipping template build")
        return 0
    template = sdk / "templates" / "minimal"
    build_dir = work / "template-build"
    config = subprocess.run(
        [cmake, "-S", str(template), "-B", str(build_dir),
         f"-DNevercPluginSDK_DIR={sdk / 'cmake'}",
         f"-DCMAKE_PREFIX_PATH={sdk / 'cmake'}"],
        capture_output=True, text=True,
    )
    if config.returncode != 0:
        fail(f"template configure failed:\n{config.stdout}\n{config.stderr}")
        return 1
    build = subprocess.run(
        [cmake, "--build", str(build_dir)],
        capture_output=True, text=True,
    )
    if build.returncode != 0:
        fail(f"template build failed:\n{build.stdout}\n{build.stderr}")
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", required=True, type=Path,
                        help="install prefix containing pluginsdk/")
    parser.add_argument("--cc", default=None, help="C compiler to use")
    parser.add_argument("--cxx", default=None, help="C++ compiler to use")
    parser.add_argument("--skip-template", action="store_true",
                        help="skip the CMake template build")
    arguments = parser.parse_args()

    prefix = arguments.prefix.resolve()
    sdk = prefix / "pluginsdk"
    if not sdk.is_dir():
        fail(f"no pluginsdk directory under {prefix}")
        return 1

    status = check_layout(sdk)
    include = sdk / "include"

    cc = find_compiler(arguments.cc, ["cc", "clang", "gcc"])
    cxx = find_compiler(arguments.cxx, ["c++", "clang++", "g++"])
    if not cc and not cxx:
        fail("no C or C++ compiler found to consume the SDK")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        single = work / "fixture_single.c"
        modular = work / "fixture_modular.c"
        single.write_text(FIXTURE_SINGLE, encoding="utf-8")
        modular.write_text(FIXTURE_MODULAR, encoding="utf-8")
        if cc:
            status |= compile_fixture(cc, "-std=c11", include, single,
                                      work / "single_c.o")
            status |= compile_fixture(cc, "-std=c11", include, modular,
                                      work / "modular_c.o")
        if cxx:
            single_cpp = work / "fixture_single.cpp"
            single_cpp.write_text(FIXTURE_SINGLE, encoding="utf-8")
            status |= compile_fixture(cxx, "-std=c++17", include, single_cpp,
                                      work / "single_cxx.o")
        if not arguments.skip_template:
            status |= build_template(sdk, work)

    if status == 0:
        print("test-installed-sdk: installed SDK consumed successfully")
    return 1 if status else 0


if __name__ == "__main__":
    raise SystemExit(main())
