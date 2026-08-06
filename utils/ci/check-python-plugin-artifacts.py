#!/usr/bin/env python3

"""Enforce Python-plugin contracts for every NeverC artifact producer."""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[2]
WORKFLOWS = REPOSITORY / ".github" / "workflows"
SETUP_PYTHON_COMMIT = "a26af69be951a213d495a4c3e4e4022e16d87065"
OFFICIAL_CPYTHON_VERSION = "3.12.10"
EXPECTED_PRODUCERS = 12
REQUIRED_COMMON = (
    SETUP_PYTHON_COMMIT,
    f'python-version: "{OFFICIAL_CPYTHON_VERSION}"',
    "NEVERC_ENABLE_PYTHON_PLUGINS=ON",
    "NEVERC_BUNDLE_PYTHON_RUNTIME=ON",
    "test-python-plugin-package.py",
)


def is_producer(text: str) -> bool:
    installs = "cmake --install" in text or re.search(
        r"cmake --build[^\n]*--target install", text
    )
    archives_neverc = re.search(r"(?:7z a|zip -r)[^\n]*neverc[^\s]*\.zip", text)
    return "actions/upload-artifact@" in text and bool(installs and archives_neverc)


def compiler_archives(text: str) -> set[str]:
    archives: set[str] = set()
    for line in text.splitlines():
        if "7z a" not in line and "zip -r" not in line:
            continue
        for name in re.findall(r"[A-Za-z0-9_.-]*neverc[A-Za-z0-9_.-]*\.zip", line):
            lowered = name.casefold()
            if any(token in lowered for token in ("-runtime", "-pdb", "profdata", "-log")):
                continue
            archives.add(name)
    return archives


def verified_archives(text: str) -> set[str]:
    return set(
        re.findall(
            r"test-python-plugin-package\.py\s+(?:\\\s*)?--archive\s+[\"']?"
            r"([^\s\"']+\.zip)",
            text,
        )
    )


def check_producer(path: Path, text: str) -> list[str]:
    failures: list[str] = []
    for required in REQUIRED_COMMON:
        if required not in text:
            failures.append(f"missing {required}")
    if "Python3_EXECUTABLE" not in text:
        failures.append("missing exact CMake Python interpreter forwarding")
    if "steps.python.outputs.python-path" not in text:
        failures.append("does not bind CMake to setup-python's exact interpreter")
    if "linux" in path.name and "patchelf" not in text:
        failures.append("Linux bundling job does not install patchelf")
    if "bundle-python-runtime.py" in text:
        failures.append(
            "directly invokes the Python bundler instead of relying on CMake install"
        )
    expected = compiler_archives(text)
    actual = verified_archives(text)
    if not expected:
        failures.append("could not discover any compiler-containing archive")
    for archive in sorted(expected - actual):
        failures.append(f"archive is not relocation-tested: {archive}")
    for archive in sorted(actual - expected):
        failures.append(f"verifier references an unclassified archive: {archive}")
    return failures


def option_defaults_to_on(text: str, option: str) -> bool:
    return bool(
        re.search(
            rf"option\(\s*{re.escape(option)}\s+\"[^\"]*\"\s+ON\s*\)",
            text,
            re.DOTALL,
        )
    )


def check_source_build_contract() -> list[str]:
    failures: list[str] = []
    cmake_path = REPOSITORY / "neverc" / "CMakeLists.txt"
    cmake = cmake_path.read_text(encoding="utf-8")
    for option in (
        "NEVERC_ENABLE_PYTHON_PLUGINS",
        "NEVERC_BUNDLE_PYTHON_RUNTIME",
    ):
        if not option_defaults_to_on(cmake, option):
            failures.append(f"{cmake_path.relative_to(REPOSITORY)}: {option} must default to ON")
    if "CMAKE_CROSSCOMPILING" not in cmake:
        failures.append(
            f"{cmake_path.relative_to(REPOSITORY)}: bundled host Python is not guarded during cross-compilation"
        )
    if "install-bundled-python.cmake" not in cmake or "install(SCRIPT" not in cmake:
        failures.append(
            f"{cmake_path.relative_to(REPOSITORY)}: CMake install does not own Python runtime bundling"
        )
    for token in (
        "ManagedCPython.cmake",
        "neverc_setup_managed_cpython()",
    ):
        if token not in cmake:
            failures.append(
                f"{cmake_path.relative_to(REPOSITORY)}: missing managed Python token {token}"
            )

    managed_module = REPOSITORY / "neverc" / "cmake" / "modules" / "ManagedCPython.cmake"
    if not managed_module.is_file():
        failures.append(
            f"{managed_module.relative_to(REPOSITORY)}: managed CPython module is missing"
        )
    else:
        managed_text = managed_module.read_text(encoding="utf-8")
        required_managed = (
            'set(NEVERC_MANAGED_CPYTHON_VERSION "3.12.10")',
            "neverc_managed_cpython_artifact",
            "EXPECTED_HASH",
            "NEVERC_MANAGED_PYTHON_ROOT",
            "NeverCPython::Python",
            "Py_InitializeFromConfig",
        )
        for token in required_managed:
            if token not in managed_text:
                failures.append(
                    f"{managed_module.relative_to(REPOSITORY)}: missing managed runtime contract {token}"
                )

    plugin_cmake_path = REPOSITORY / "neverc" / "lib" / "Plugin" / "CMakeLists.txt"
    plugin_cmake = plugin_cmake_path.read_text(encoding="utf-8")
    if "NeverCPython::Python" not in plugin_cmake or "Python3::Python" in plugin_cmake:
        failures.append(
            f"{plugin_cmake_path.relative_to(REPOSITORY)}: plugin bridge must link only managed CPython"
        )

    contract_test = REPOSITORY / "neverc" / "cmake" / "tests" / "ManagedCPythonContract.cmake"
    if not contract_test.is_file():
        failures.append(
            f"{contract_test.relative_to(REPOSITORY)}: managed CPython mapping test is missing"
        )
    else:
        contract_text = contract_test.read_text(encoding="utf-8")
        for token in (
            "Darwin|arm64",
            "Darwin|x86_64",
            "Linux|aarch64",
            "Linux|x86_64",
            "Windows|ARM64",
            "Windows|AMD64",
        ):
            if token not in contract_text:
                failures.append(
                    f"{contract_test.relative_to(REPOSITORY)}: missing host contract {token}"
                )

    install_script = REPOSITORY / "neverc" / "cmake" / "install-bundled-python.cmake.in"
    if not install_script.is_file():
        failures.append(
            f"{install_script.relative_to(REPOSITORY)}: install-time bundler script is missing"
        )
    else:
        install_text = install_script.read_text(encoding="utf-8")
        for token in (
            "$ENV{DESTDIR}",
            "${CMAKE_INSTALL_PREFIX}",
            "@NEVERC_MANAGED_PYTHON_EXECUTABLE@",
            "bundle-python-runtime.py",
        ):
            if token not in install_text:
                failures.append(
                    f"{install_script.relative_to(REPOSITORY)}: missing install contract token {token}"
                )

    workflow_path = WORKFLOWS / "python-plugin-bindings.yml"
    workflow = workflow_path.read_text(encoding="utf-8")
    required_workflow = (
        "NEVERC_ENABLE_PYTHON_PLUGINS:BOOL=ON",
        "NEVERC_BUNDLE_PYTHON_RUNTIME:BOOL=ON",
        "--component neverc",
        "--component neverc-pluginsdk",
        "DESTDIR=",
        "default-source-install-${{ matrix.host }}.zip",
        "RuntimeVersionPlugin.py",
        'data["version"] == "3.12.10"',
        "ubuntu-22.04-arm",
        "macos-15-intel",
        "macos-15",
        "windows-11-arm",
        "windows-latest",
        "NEVERC_MANAGED_PYTHON_ROOT",
        '"neverc/cmake/**"',
        "PluginPythonABIGeneration",
        "PluginPythonABILayout",
        "PluginPythonBridgeTest.*",
        "examples/ollvm/ollvm_plugin.py",
        "ollvm.fla.dispatch",
    )
    for token in required_workflow:
        if token not in workflow:
            failures.append(
                f"{workflow_path.relative_to(REPOSITORY)}: missing default source-build proof {token}"
            )

    tests_cmake_path = REPOSITORY / "tests" / "neverc" / "CMakeLists.txt"
    tests_cmake = tests_cmake_path.read_text(encoding="utf-8")
    required_test_tokens = (
        'string(REPLACE "\\\\" "/" _NEVERC_TEST_PYTHON_EXECUTABLE',
        'NEVERC_PYTHON="${_NEVERC_TEST_PYTHON_EXECUTABLE}"',
    )
    for token in required_test_tokens:
        if token not in tests_cmake:
            failures.append(
                f"{tests_cmake_path.relative_to(REPOSITORY)}: "
                "Windows Python test paths are not normalized before C++ compilation"
            )
            break
    return failures


def check_disabled_job() -> list[str]:
    path = WORKFLOWS / "python-plugin-bindings.yml"
    text = path.read_text(encoding="utf-8")
    required = (
        "python-disabled",
        f'python-version: "{OFFICIAL_CPYTHON_VERSION}"',
        "NEVERC_ENABLE_PYTHON_PLUGINS=OFF",
        "NEVERC_BUNDLE_PYTHON_RUNTIME=OFF",
        "NEVERC_ENABLE_PYTHON_PLUGINS=ON",
        "ldd",
        "libpython",
    )
    return [f"feature workflow missing OFF contract token: {value}"
            for value in required if value not in text]


def main() -> int:
    producers: list[tuple[Path, str]] = []
    for path in sorted(WORKFLOWS.glob("*.yml")):
        text = path.read_text(encoding="utf-8")
        if is_producer(text):
            producers.append((path, text))
    failures: list[str] = []
    if len(producers) != EXPECTED_PRODUCERS:
        failures.append(
            f"producer discovery changed: expected {EXPECTED_PRODUCERS}, found {len(producers)} "
            f"({', '.join(path.name for path, _ in producers)})"
        )
    for path, text in producers:
        for failure in check_producer(path, text):
            failures.append(f"{path.relative_to(REPOSITORY)}: {failure}")
    for path in sorted(WORKFLOWS.glob("*.yml")):
        if 'python-version: "3.12"' in path.read_text(encoding="utf-8"):
            failures.append(
                f"{path.relative_to(REPOSITORY)}: official CPython must use "
                f"the exact {OFFICIAL_CPYTHON_VERSION} patch version"
            )
    failures.extend(check_source_build_contract())
    failures.extend(check_disabled_job())
    if failures:
        print("check-python-plugin-artifacts: FAILED", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print(
        "check-python-plugin-artifacts: "
        f"{len(producers)} producers and all compiler archives are governed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
