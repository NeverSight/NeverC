#!/usr/bin/env python3
"""Configure real native CMake link rules without building the fixture target."""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


def cmake_literal(value):
    value = str(value)
    if any(character in value for character in ("\r", "\n", "\0")) or "]=]" in value:
        raise ValueError("Unsupported fixture path")
    return "[=[" + value + "]=]"


def ninja_text(path):
    return path.read_text(encoding="utf-8").replace("$\n", "")


def ninja_blocks(text):
    blocks = []
    current = None
    for line in text.splitlines():
        if line.startswith("build "):
            separator = re.search(r"(?<!\$):\s+", line)
            if separator is None:
                raise ValueError("Unrecognized generated Ninja build edge")
            current = {"kind": "build", "rule": line[separator.end():].split()[0],
                       "variables": {}}
            blocks.append(current)
        elif line.startswith("rule "):
            current = {"kind": "rule", "rule": line[5:], "variables": {}}
            blocks.append(current)
        elif line.startswith("  ") and current is not None:
            name, separator, value = line.strip().partition(" = ")
            if separator:
                current["variables"][name] = value
        elif line and not line.startswith("#"):
            current = None
    return blocks


def ninja_unescape(value):
    return re.sub(r"\$([ $:])", lambda match: match.group(1), value)


PROJECT = r'''
cmake_minimum_required(VERSION 3.20)
project(NeverCCppImportContract LANGUAGES CXX)
include(@MODULE@)
add_executable(contract_tool main.cpp)
set_target_properties(contract_tool PROPERTIES
  ENABLE_EXPORTS OFF
  DEFINE_SYMBOL ""
  WINDOWS_EXPORT_ALL_SYMBOLS OFF
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/host libraries")
if(CONTRACT_CUSTOM_OUTPUTS)
  set_target_properties(contract_tool PROPERTIES
    OUTPUT_NAME "renamed-executable"
    ARCHIVE_OUTPUT_NAME "unused-common-import"
    ARCHIVE_OUTPUT_NAME_DEBUG "debug-import"
    ARCHIVE_OUTPUT_NAME_RELEASE "release-import"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/import outputs/Debug"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/import outputs/Release"
    IMPORT_PREFIX "audit_"
    IMPORT_SUFFIX ".imports.lib"
    DEBUG_POSTFIX "_dbg")
endif()
add_library(LLVMCore STATIC IMPORTED)
set_target_properties(LLVMCore PROPERTIES IMPORTED_LOCATION
  "${CMAKE_BINARY_DIR}/host libraries/${CMAKE_STATIC_LIBRARY_PREFIX}LLVMCore${CMAKE_STATIC_LIBRARY_SUFFIX}")
add_library(nevercCppFrontend STATIC IMPORTED)
set_target_properties(nevercCppFrontend PROPERTIES
  IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/private input/${CMAKE_STATIC_LIBRARY_PREFIX}private${CMAKE_STATIC_LIBRARY_SUFFIX}"
  NEVERC_CPP_AUDIT_PYTHON @PYTHON@
  NEVERC_CPP_AUDIT_SCRIPT @AUDIT@
  NEVERC_CPP_AUDIT_NM_FILE @NM_FILE@
  NEVERC_CPP_AUDIT_PREFIX @PREFIX@
  NEVERC_CPP_AUDIT_HOST_ARGUMENTS ""
  NEVERC_CPP_AUDIT_PRIVATE_ARGUMENTS "")
# Match add_neverc_tool: register the runtime install before the audit hook.
install(TARGETS contract_tool EXPORT ContractTargets RUNTIME DESTINATION bin)
neverc_check_builtin_cpp_frontend(contract_tool)
export(TARGETS contract_tool FILE "${CMAKE_BINARY_DIR}/build-targets.cmake")
install(EXPORT ContractTargets FILE ContractTargets.cmake DESTINATION lib/cmake/contract)
set(_state [=[{
  "compiler_id": "@CMAKE_CXX_COMPILER_ID@",
  "compiler_version": "@CMAKE_CXX_COMPILER_VERSION@",
  "simulate_id": "@CMAKE_CXX_SIMULATE_ID@",
  "frontend_variant": "@CMAKE_CXX_COMPILER_FRONTEND_VARIANT@",
  "win32": "@WIN32@",
  "msvc": "@MSVC@",
  "exports": "$<TARGET_PROPERTY:contract_tool,ENABLE_EXPORTS>",
  "define_symbol": "$<TARGET_PROPERTY:contract_tool,DEFINE_SYMBOL>",
  "export_all": "$<TARGET_PROPERTY:contract_tool,WINDOWS_EXPORT_ALL_SYMBOLS>"
}]=])
string(CONFIGURE "${_state}" _state @ONLY)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/state-$<CONFIG>.json" CONTENT "${_state}\n")
'''


class BuiltinCppFrontendCMakeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cmake = shutil.which("cmake")
        cls.ninja = shutil.which("ninja")
        if not cls.cmake or not cls.ninja:
            raise RuntimeError("The configure contract requires installed CMake and Ninja")
        for tool in (cls.cmake, cls.ninja):
            result = subprocess.run([tool, "--version"], capture_output=True,
                                    text=True, encoding="utf-8", errors="replace", timeout=15)
            if result.returncode:
                raise RuntimeError("Cannot read configure tool version: " + result.stderr)
            print(result.stdout.strip(), flush=True)
        cls.frontend = Path(__file__).resolve().parent
        cls.module = cls.frontend.parents[3] / "cmake/modules/BuiltinCppFrontend.cmake"
        if not cls.module.is_file():
            raise RuntimeError("Missing production builtin C++ CMake module")
        if os.name == "nt":
            cl = shutil.which("cl")
            if not cl:
                raise RuntimeError("Windows configure tests require the x64 MSVC developer environment")
            candidates = ["C:/Program Files/LLVM/bin/clang++.exe",
                          "C:/LLVM/bin/clang++.exe", shutil.which("clang++")]
            clang = next((value for value in candidates if value and Path(value).is_file()), None)
            if clang is None:
                raise RuntimeError("Missing Windows clang++: the GNU-driver MSVC ABI contract must run")
            cls.compilers = (("msvc", cl), ("clang-gnu", clang))
        else:
            compiler = shutil.which("c++")
            if not compiler:
                raise RuntimeError("Native C++ compiler unavailable for configure contract")
            cls.compilers = (("native", compiler),)

    def configure(self, directory, compiler, generator, custom=False):
        source = directory / "source"
        build = directory / "build"
        source.mkdir(parents=True)
        (source / "main.cpp").write_text(
            "#ifdef _WIN32\n__declspec(dllexport) int contract_export() { return 1; }\n"
            "#endif\nint main() { return 0; }\n", encoding="utf-8")
        (source / "reader.txt").write_text("unused-reader\n", encoding="utf-8")
        (source / "prefix.h").write_text("", encoding="utf-8")
        values = {"MODULE": self.module.as_posix(), "PYTHON": Path(sys.executable).as_posix(),
                  "AUDIT": (self.frontend / "AuditArchive.py").as_posix(),
                  "NM_FILE": (source / "reader.txt").as_posix(),
                  "PREFIX": (source / "prefix.h").as_posix()}
        project = PROJECT
        for key, value in values.items():
            project = project.replace("@" + key + "@", cmake_literal(value))
        (source / "CMakeLists.txt").write_text(project, encoding="utf-8")
        command = [self.cmake, "-S", str(source), "-B", str(build), "-G", generator,
                   "-DCMAKE_MAKE_PROGRAM:FILEPATH=" + Path(self.ninja).as_posix(),
                   "-DCMAKE_CXX_COMPILER:FILEPATH=" + Path(compiler).as_posix(),
                   "-DCMAKE_BUILD_TYPE:STRING=Release",
                   "-DCONTRACT_CUSTOM_OUTPUTS:BOOL=" + ("ON" if custom else "OFF")]
        if os.name == "nt" and compiler == dict(self.compilers)["clang-gnu"]:
            command.append("-DCMAKE_CXX_COMPILER_TARGET:STRING=x86_64-pc-windows-msvc")
        if generator == "Ninja Multi-Config":
            command.append("-DCMAKE_CONFIGURATION_TYPES:STRING=Debug;Release")
        # At most five configure invocations on Windows, three elsewhere. Compiler
        # detection is allowed; no --build, --install or fixture executable runs.
        result = subprocess.run(command, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return build

    def check_link_contract(self, build, configuration, driver, custom=False, multi=False):
        state = json.loads((build / ("state-" + configuration + ".json")).read_text(encoding="utf-8"))
        print("CMake import contract: " + json.dumps(
            {"driver": driver, "config": configuration, "custom": custom, **state},
            sort_keys=True), flush=True)
        windows = os.name == "nt"
        truth = {"1", "ON", "TRUE", "YES"}
        self.assertEqual(state["win32"].upper() in truth, windows)
        if windows:
            self.assertTrue(state["msvc"].upper() in truth or state["simulate_id"] == "MSVC", state)
            if driver == "clang-gnu":
                self.assertEqual(state["compiler_id"], "Clang")
                self.assertEqual(state["simulate_id"], "MSVC")
                self.assertEqual(state["frontend_variant"], "GNU")
        self.assertEqual(state["define_symbol"], "")
        self.assertEqual(state["export_all"], "OFF")
        graph_path = build / "CMakeFiles" / ("impl-" + configuration + ".ninja") if multi else build / "build.ninja"
        graph = ninja_text(graph_path)
        edges = [block for block in ninja_blocks(graph)
                 if block["kind"] == "build" and
                 "AuditArchive.py" in block["variables"].get("PRE_LINK", "")]
        self.assertEqual(len(edges), 1, "Expected one real executable link edge")
        edge = edges[0]
        pre_link = ninja_unescape(edge["variables"]["PRE_LINK"])
        self.assertEqual(pre_link.count("--self-import-library"), int(windows), pre_link)
        arguments = re.findall(r'(?:^|\s)--self-import-library\s+(?:"([^"]*)"|([^\s&]+))', pre_link)
        self.assertNotIn("contract_tool_EXPORTS", graph)
        if not windows:
            self.assertEqual(arguments, [], pre_link)
            self.assertEqual(state["exports"], "OFF")
            return
        self.assertEqual(len(arguments), 1, pre_link)
        self.assertIn(state["exports"].upper(), truth)
        supplied = Path(arguments[0][0] or arguments[0][1])
        self.assertTrue(supplied.is_absolute(), pre_link)
        implib = ninja_unescape(edge["variables"]["TARGET_IMPLIB"]).strip('"')
        actual = Path(implib)
        if not actual.is_absolute():
            actual = build / actual
        rules = [block for block in ninja_blocks(ninja_text(build / "CMakeFiles/rules.ninja"))
                 if block["kind"] == "rule" and block["rule"] == edge["rule"]]
        self.assertEqual(len(rules), 1)
        self.assertIn("$TARGET_IMPLIB", rules[0]["variables"]["command"])
        self.assertIn("implib:", rules[0]["variables"]["command"].lower())
        self.assertEqual(supplied.resolve(), actual.resolve())
        if custom:
            filename = ("audit_debug-import_dbg.imports.lib" if configuration == "Debug"
                        else "audit_release-import.imports.lib")
            expected = build / "import outputs" / configuration / filename
        else:
            expected = build / "host libraries/contract_tool.lib"
        self.assertEqual(actual.resolve(), expected.resolve())

    def test_native_default_import_library_uses_the_real_link_output(self):
        for driver, compiler in self.compilers:
            with self.subTest(driver=driver), tempfile.TemporaryDirectory() as temporary:
                build = self.configure(Path(temporary), compiler, "Ninja")
                self.check_link_contract(build, "Release", driver)

    def test_native_custom_names_and_configuration_outputs(self):
        for driver, compiler in self.compilers:
            with self.subTest(driver=driver), tempfile.TemporaryDirectory() as temporary:
                build = self.configure(Path(temporary), compiler, "Ninja Multi-Config", custom=True)
                for configuration in ("Debug", "Release"):
                    with self.subTest(configuration=configuration):
                        self.check_link_contract(build, configuration, driver, custom=True, multi=True)

    def test_runtime_install_and_export_metadata_remain_consistent(self):
        driver, compiler = self.compilers[0]
        with tempfile.TemporaryDirectory() as temporary:
            build = self.configure(Path(temporary), compiler, "Ninja")
            self.check_link_contract(build, "Release", driver)
            build_export = (build / "build-targets.cmake").read_text(encoding="utf-8")
            installed = list((build / "CMakeFiles/Export").rglob("ContractTargets*.cmake"))
            self.assertTrue(installed, "Expected the real generated install export")
            install_export = "\n".join(path.read_text(encoding="utf-8") for path in installed)
            install_script = (build / "cmake_install.cmake").read_text(encoding="utf-8")
            self.assertNotIn("IMPORTED_IMPLIB", install_export)
            self.assertNotIn("contract_tool.lib", install_script)
            if os.name == "nt":
                self.assertIn("IMPORTED_IMPLIB_RELEASE", build_export)
                self.assertRegex(build_export, r"set_property\(TARGET contract_tool PROPERTY ENABLE_EXPORTS 1\)")
                self.assertRegex(install_export, r"set_property\(TARGET contract_tool PROPERTY ENABLE_EXPORTS 1\)")
            else:
                self.assertNotIn("IMPORTED_IMPLIB", build_export)
                self.assertNotIn("ENABLE_EXPORTS", build_export)
                self.assertNotIn("ENABLE_EXPORTS", install_export)


if __name__ == "__main__":
    unittest.main(verbosity=2)
