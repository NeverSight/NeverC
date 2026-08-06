#!/usr/bin/env python3

"""Enforce Python-plugin contracts for every NeverC artifact producer."""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[2]
WORKFLOWS = REPOSITORY / ".github" / "workflows"
SETUP_PYTHON_COMMIT = "a26af69be951a213d495a4c3e4e4022e16d87065"
EXPECTED_PRODUCERS = 12
REQUIRED_COMMON = (
    SETUP_PYTHON_COMMIT,
    'python-version: "3.12"',
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
            "@Python3_EXECUTABLE@",
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
        "default-source-install.zip",
        '"neverc/cmake/**"',
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
