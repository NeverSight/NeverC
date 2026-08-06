#!/usr/bin/env python3

"""Bundle the running CPython beside an installed NeverC compiler."""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import sysconfig
from dataclasses import dataclass
from pathlib import Path


EXCLUDED_STDLIB_TREES = {
    "__pycache__",
    "ensurepip",
    "idlelib",
    "site-packages",
    "test",
    "tests",
    "tkinter",
    "turtledemo",
    "venv",
}
EXCLUDED_SUFFIXES = {".a", ".la", ".pc", ".pyc", ".pyo"}
EXCLUDED_EXTENSION_PREFIXES = (
    "_ctypes_test.", "_test", "_tkinter.", "_xxtestfuzz.", "xxlimited"
)
DEBIAN_DOC_ROOT = Path("/usr/share/doc")
LICENSE_FILENAMES = {"copyright", "license", "license.txt", "copying"}
BUNDLED_CPYTHON_LICENSE = (
    Path(__file__).resolve().parent / "licenses" / "CPython-LICENSE.txt"
)
LINUX_SYSTEM_LIBRARIES = {
    "libatomic.so.1", "libc.so.6", "libdl.so.2", "libgcc_s.so.1",
    "libm.so.6", "libpthread.so.0", "libresolv.so.2", "librt.so.1",
    "libstdc++.so.6", "libutil.so.1",
}
WINDOWS_SYSTEM_DLLS = {
    "advapi32.dll", "bcrypt.dll", "crypt32.dll", "dnsapi.dll",
    "cabinet.dll", "combase.dll", "dbghelp.dll", "gdi32.dll",
    "iphlpapi.dll", "kernel32.dll", "msvcrt.dll", "normaliz.dll",
    "ncrypt.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll",
    "rpcrt4.dll", "secur32.dll", "shell32.dll", "shlwapi.dll",
    "powrprof.dll", "psapi.dll", "setupapi.dll", "ucrtbase.dll",
    "user32.dll", "userenv.dll", "version.dll", "winhttp.dll", "winmm.dll",
    "wintrust.dll",
    "wldap32.dll", "ws2_32.dll", "wsock32.dll",
}


@dataclass(frozen=True)
class RuntimeLayout:
    platform: str
    base_prefix: Path
    stdlib: Path
    libdir: Path
    runtime_name: str
    version: str
    abi: str


def host_platform() -> str:
    if sys.platform == "win32":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    return "linux"


def discover_layout() -> RuntimeLayout:
    system = host_platform()
    base = Path(sys.base_prefix).resolve()
    stdlib = Path(sysconfig.get_path("stdlib")).resolve()
    version = platform.python_version()
    abi = str(sysconfig.get_config_var("SOABI") or f"cp{sys.version_info.major}{sys.version_info.minor}")
    if system == "windows":
        runtime = f"python{sys.version_info.major}{sys.version_info.minor}.dll"
        libdir = base
    else:
        runtime = str(sysconfig.get_config_var("LDLIBRARY") or "")
        libdir_value = sysconfig.get_config_var("LIBDIR")
        libdir = Path(libdir_value).resolve() if libdir_value else base / "lib"
        # Relocatable distributions can retain the build-time LIBDIR (for
        # example /install/lib) in sysconfig while rebasing every real file
        # below sys.base_prefix. Prefer the selected runtime beside that
        # rebased prefix when the configured location is stale.
        rebased_libdir = base / "lib"
        if (
            runtime
            and not (libdir / Path(runtime).name).is_file()
            and (rebased_libdir / Path(runtime).name).is_file()
        ):
            libdir = rebased_libdir.resolve()
    if not runtime:
        raise RuntimeError("the selected CPython does not expose a shared runtime")
    return RuntimeLayout(system, base, stdlib, libdir, Path(runtime).name, version, abi)


def ignored_stdlib_path(relative: Path) -> bool:
    return any(
        part in EXCLUDED_STDLIB_TREES or part.startswith("config-")
        for part in relative.parts
    ) or relative.name.startswith(EXCLUDED_EXTENSION_PREFIXES) or (
        relative.suffix in EXCLUDED_SUFFIXES
    )


def copy_stdlib(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise RuntimeError(f"CPython standard library not found: {source}")
    for item in source.rglob("*"):
        relative = item.relative_to(source)
        if ignored_stdlib_path(relative):
            continue
        target = destination / relative
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif item.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, target)


def copy_file(source: Path, destination: Path) -> Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source.resolve(), destination)
    return destination


def find_license(layout: RuntimeLayout) -> Path:
    candidates = [
        layout.stdlib / "LICENSE.txt",
        layout.stdlib / "LICENSE",
        layout.base_prefix / "LICENSE.txt",
        layout.base_prefix / "LICENSE",
        BUNDLED_CPYTHON_LICENSE,
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RuntimeError("the selected CPython distribution has no LICENSE file")


def copy_distribution_licenses(layout: RuntimeLayout, destination: Path) -> list[str]:
    source = layout.base_prefix / "licenses"
    if not source.is_dir():
        return []
    copied: list[str] = []
    target_root = destination / "distribution"
    for candidate in sorted(source.iterdir(), key=lambda path: path.name.casefold()):
        if not candidate.is_file() or not candidate.name.casefold().startswith(
            ("license", "copying", "notice")
        ):
            continue
        target = copy_file(candidate, target_root / candidate.name)
        copied.append(f"licenses/distribution/{target.name}")
    return copied


def runtime_sources(layout: RuntimeLayout) -> list[Path]:
    if layout.platform == "windows":
        candidates = [layout.base_prefix / layout.runtime_name,
                      layout.base_prefix / "DLLs" / layout.runtime_name]
        result = [candidate for candidate in candidates if candidate.is_file()]
        for candidate in layout.base_prefix.glob("python*.dll"):
            if candidate.is_file() and candidate not in result:
                result.append(candidate)
    else:
        exact = layout.libdir / layout.runtime_name
        result = [exact] if exact.exists() else []
        major_minor = ".".join(layout.version.split(".")[:2])
        prefix = f"libpython{major_minor}"
        for candidate in layout.libdir.glob(prefix + "*"):
            if candidate.is_file() and candidate not in result:
                result.append(candidate)
    if not result:
        raise RuntimeError(
            f"CPython shared runtime {layout.runtime_name!r} not found under {layout.libdir}"
        )
    return result


def parse_ldd(output: str) -> tuple[dict[str, Path], list[str]]:
    dependencies: dict[str, Path] = {}
    missing: list[str] = []
    for raw in output.splitlines():
        line = raw.strip()
        match = re.match(r"(\S+)\s+=>\s+(\S+)", line)
        if match:
            name, value = match.groups()
            if value == "not":
                missing.append(name)
            elif value.startswith("/"):
                dependencies[name] = Path(value)
            continue
        match = re.match(r"(/\S+)\s+\(", line)
        if match:
            value = Path(match.group(1))
            dependencies[value.name] = value
    return dependencies, missing


def parse_otool(output: str) -> list[str]:
    result: list[str] = []
    for raw in output.splitlines()[1:]:
        line = raw.strip()
        if not line:
            continue
        result.append(line.split(" (", 1)[0])
    return result


def parse_pe_imports(output: str) -> list[str]:
    result: list[str] = []
    for raw in output.splitlines():
        line = raw.strip()
        match = re.search(r"(?:Name:\s*)?([A-Za-z0-9_.+-]+\.dll)\b", line, re.IGNORECASE)
        if match:
            name = match.group(1)
            if name.casefold() not in {item.casefold() for item in result}:
                result.append(name)
    return result


def is_system_dependency(system: str, value: str) -> bool:
    normalized = value.replace("\\", "/")
    if system == "linux":
        name = Path(normalized).name
        return (
            name in LINUX_SYSTEM_LIBRARIES
            or name.startswith("ld-linux-")
            or name.startswith("linux-vdso.")
        )
    if system == "macos":
        return normalized.startswith(("/System/Library/", "/usr/lib/"))
    name = Path(normalized).name
    lowered = name.casefold()
    if lowered in WINDOWS_SYSTEM_DLLS or lowered.startswith(
        ("api-ms-win-", "ext-ms-win-")
    ):
        return True
    system_root = os.environ.get("SystemRoot")
    return bool(system_root and (Path(system_root) / "System32" / name).is_file())


def run_tool(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, capture_output=True, text=True)
    if check and result.returncode != 0:
        raise RuntimeError(
            f"command failed ({' '.join(command)}):\n{result.stdout}{result.stderr}"
        )
    return result


def is_elf(path: Path) -> bool:
    try:
        with path.open("rb") as stream:
            return stream.read(4) == b"\x7fELF"
    except OSError:
        return False


def is_macho(path: Path) -> bool:
    try:
        with path.open("rb") as stream:
            magic = stream.read(4)
    except OSError:
        return False
    return magic in {
        b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
        b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
        b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca",
    }


def native_files(prefix: Path, neverc: Path) -> list[Path]:
    result = [neverc]
    for root in (prefix / "python", prefix / "bin"):
        if not root.exists():
            continue
        for candidate in root.rglob("*"):
            lowered = candidate.name.lower()
            if candidate.is_file() and (
                ".so" in lowered or lowered.endswith((".dylib", ".dll", ".pyd"))
                or (
                    candidate.parent == prefix / "python" / "lib"
                    and (is_elf(candidate) or is_macho(candidate))
                )
            ):
                result.append(candidate)
    return list(dict.fromkeys(path.resolve() for path in result))


def dependency_license(source: Path, layout: RuntimeLayout, licenses: Path) -> str:
    try:
        source.resolve().relative_to(layout.base_prefix.resolve())
        return "licenses/CPython-LICENSE.txt"
    except ValueError:
        pass
    if layout.platform == "linux" and shutil.which("dpkg-query"):
        for query_path in dict.fromkeys((source, source.resolve())):
            owner = run_tool(
                ["dpkg-query", "-S", str(query_path)], check=False
            )
            if owner.returncode != 0:
                continue
            for line in owner.stdout.splitlines():
                package = line.rsplit(": ", 1)[0]
                listed = run_tool(["dpkg-query", "-L", package], check=False)
                if listed.returncode != 0:
                    continue
                package_name = package.split(":", 1)[0]
                candidates = [Path(value) for value in listed.stdout.splitlines()]
                candidates.extend(
                    DEBIAN_DOC_ROOT / package_name / name
                    for name in sorted(LICENSE_FILENAMES)
                )
                for candidate in dict.fromkeys(candidates):
                    if (
                        candidate.is_file()
                        and candidate.name.casefold() in LICENSE_FILENAMES
                    ):
                        target_name = f"{source.name}-{package.replace(':', '_')}-{candidate.name}"
                        copy_file(candidate, licenses / target_name)
                        return f"licenses/{target_name}"
    current = source.resolve().parent
    candidates: list[Path] = []
    for _ in range(6):
        for pattern in ("LICENSE*", "COPYING*", "NOTICE*"):
            candidates.extend(path for path in current.glob(pattern) if path.is_file())
        if current.parent == current:
            break
        current = current.parent
    if not candidates:
        raise RuntimeError(f"no attributable license found for native dependency {source}")
    license_source = sorted(candidates, key=lambda path: (len(path.parts), path.name))[0]
    target_name = f"{source.stem}-{license_source.name}"
    copy_file(license_source, licenses / target_name)
    return f"licenses/{target_name}"


def windows_dependency_index(layout: RuntimeLayout) -> dict[str, Path]:
    result: dict[str, Path] = {}
    for candidate in layout.base_prefix.rglob("*.dll"):
        result.setdefault(candidate.name.casefold(), candidate)
    return result


def inspect_dependencies(path: Path, system: str) -> tuple[dict[str, Path | None], list[str]]:
    if system == "linux":
        result = run_tool(["ldd", str(path)], check=False)
        if result.returncode != 0:
            return {}, []
        dependencies, missing = parse_ldd(result.stdout + result.stderr)
        return {name: value for name, value in dependencies.items()}, missing
    if system == "macos":
        result = run_tool(["otool", "-L", str(path)], check=False)
        if result.returncode != 0:
            return {}, []
        refs = parse_otool(result.stdout)
        dependencies: dict[str, Path | None] = {}
        for ref in refs:
            dependencies[Path(ref).name] = Path(ref) if ref.startswith("/") else None
        return dependencies, []
    tool = shutil.which("dumpbin")
    command = [tool, "/dependents", str(path)] if tool else []
    if not command:
        tool = shutil.which("llvm-readobj")
        command = [tool, "--coff-imports", str(path)] if tool else []
    if not command:
        raise RuntimeError("dumpbin or llvm-readobj is required to inspect PE imports")
    result = run_tool(command, check=False)
    if result.returncode != 0:
        return {}, []
    return {name: None for name in parse_pe_imports(result.stdout + result.stderr)}, []


def collect_dependency_closure(
    prefix: Path, neverc: Path, layout: RuntimeLayout, licenses: Path
) -> tuple[list[dict[str, str]], list[str]]:
    destination = prefix / "bin" if layout.platform == "windows" else prefix / "python" / "lib"
    queue = native_files(prefix, neverc)
    bundled = {path.name.casefold(): path for path in queue}
    windows_index = windows_dependency_index(layout) if layout.platform == "windows" else {}
    copied: list[dict[str, str]] = []
    system_dependencies: set[str] = set()
    visited: set[Path] = set()
    while queue:
        binary = queue.pop(0)
        if binary in visited:
            continue
        visited.add(binary)
        dependencies, missing = inspect_dependencies(binary, layout.platform)
        unresolved = [name for name in missing if name.casefold() not in bundled]
        if unresolved:
            raise RuntimeError(f"unresolved dependencies for {binary}: {', '.join(unresolved)}")
        for name, resolved in dependencies.items():
            key = name.casefold()
            if key in bundled:
                continue
            source = resolved
            if source is None and layout.platform == "windows":
                source = windows_index.get(key)
                if source is None and is_system_dependency("windows", name):
                    system_dependencies.add(name)
                    continue
            elif resolved and is_system_dependency(layout.platform, str(resolved)):
                system_dependencies.add(str(resolved))
                continue
            if source is None:
                # @rpath/@loader_path references are valid when their basename is
                # already bundled; everything else is an incomplete package.
                raise RuntimeError(f"cannot resolve native dependency {name} required by {binary}")
            target = copy_file(source, destination / source.name)
            license_path = dependency_license(source, layout, licenses)
            record = {"name": target.name, "license": license_path}
            copied.append(record)
            bundled[key] = target.resolve()
            queue.append(target.resolve())
    return copied, sorted(system_dependencies)


def repair_linux(prefix: Path, neverc: Path) -> None:
    tool = shutil.which("patchelf")
    if not tool:
        raise RuntimeError("patchelf is required to bundle Python on Linux")
    runtime_lib = prefix / "python" / "lib"
    for binary in native_files(prefix, neverc):
        if not is_elf(binary):
            continue
        if binary == neverc.resolve():
            rpath = "$ORIGIN/../python/lib"
        else:
            relative = os.path.relpath(runtime_lib, binary.parent)
            rpath = "$ORIGIN" if relative == "." else f"$ORIGIN/{relative}"
        run_tool([tool, "--set-rpath", rpath, str(binary)])
        needed = run_tool([tool, "--print-needed", str(binary)]).stdout.splitlines()
        for value in needed:
            if value.startswith("/"):
                run_tool([tool, "--replace-needed", value, Path(value).name, str(binary)])


def macho_rpaths(path: Path) -> list[str]:
    output = run_tool(["otool", "-l", str(path)]).stdout.splitlines()
    result: list[str] = []
    for index, line in enumerate(output):
        if line.strip() == "cmd LC_RPATH":
            for candidate in output[index + 1:index + 6]:
                match = re.search(r"path (\S+) \(offset", candidate)
                if match:
                    result.append(match.group(1))
                    break
    return result


def repair_macos(prefix: Path, neverc: Path) -> None:
    runtime_lib = prefix / "python" / "lib"
    binaries = [path for path in native_files(prefix, neverc) if is_macho(path)]
    bundled_names = {path.name for path in binaries if path != neverc.resolve()}
    for binary in binaries:
        dependencies = parse_otool(run_tool(["otool", "-L", str(binary)]).stdout)
        needs_runtime_rpath = False
        for dependency in dependencies:
            name = Path(dependency).name
            if name in bundled_names and dependency != f"@rpath/{name}":
                run_tool([
                    "install_name_tool", "-change", dependency, f"@rpath/{name}", str(binary)
                ])
            # A dylib reports its own install name in `otool -L`, but that is
            # not a dependency and does not require an LC_RPATH.  Likewise,
            # extension modules that only use Apple system libraries need no
            # runtime search path.  Avoiding unnecessary load commands matters
            # for stripped CPython extensions that have no spare Mach-O header
            # padding (notably the x86_64 _crypt module).
            is_own_install_name = (
                binary.parent == runtime_lib.resolve() and name == binary.name
            )
            if name in bundled_names and not is_own_install_name:
                needs_runtime_rpath = True
        if binary.parent == runtime_lib.resolve() and binary != neverc.resolve():
            run_tool(["install_name_tool", "-id", f"@rpath/{binary.name}", str(binary)])
        if binary == neverc.resolve():
            rpath = "@executable_path/../python/lib"
        else:
            relative = os.path.relpath(runtime_lib, binary.parent)
            rpath = "@loader_path" if relative == "." else f"@loader_path/{relative}"
        if needs_runtime_rpath and rpath not in macho_rpaths(binary):
            run_tool(["install_name_tool", "-add_rpath", rpath, str(binary)])
        # install_name_tool invalidates the linker-generated ad-hoc signature.
        # Keep CI artifacts executable; release jobs replace this with
        # their Developer ID signature afterwards.
        run_tool(["codesign", "--force", "--sign", "-", str(binary)])


def repair_runtime(prefix: Path, neverc: Path, layout: RuntimeLayout) -> None:
    if layout.platform == "linux":
        repair_linux(prefix, neverc)
    elif layout.platform == "macos":
        repair_macos(prefix, neverc)


def copy_windows_dlls(layout: RuntimeLayout, destination: Path) -> None:
    source = layout.base_prefix / "DLLs"
    if source.is_dir():
        copy_stdlib(source, destination)


def bundle_runtime(
    prefix: Path,
    neverc: Path,
    *,
    layout: RuntimeLayout | None = None,
    repair: bool = True,
) -> dict[str, object]:
    prefix = prefix.resolve()
    neverc = neverc.resolve()
    layout = layout or discover_layout()
    if not neverc.is_file():
        raise RuntimeError(f"NeverC executable not found: {neverc}")
    try:
        neverc.relative_to(prefix)
    except ValueError as error:
        raise RuntimeError("--neverc must be inside --prefix") from error

    python_root = prefix / "python"
    existing_dependencies: list[dict[str, str]] = []
    existing_manifest = python_root / "runtime.json"
    if existing_manifest.is_file():
        try:
            previous = json.loads(existing_manifest.read_text(encoding="utf-8"))
            existing_dependencies = list(previous.get("native_dependencies", []))
        except (json.JSONDecodeError, TypeError, ValueError):
            existing_dependencies = []
    licenses = python_root / "licenses"
    licenses.mkdir(parents=True, exist_ok=True)
    if layout.platform == "windows":
        stdlib_destination = python_root / "Lib"
        runtime_destination = prefix / "bin"
    else:
        major_minor = ".".join(layout.version.split(".")[:2])
        stdlib_destination = python_root / "lib" / f"python{major_minor}"
        runtime_destination = python_root / "lib"
    copy_stdlib(layout.stdlib, stdlib_destination)
    if layout.platform == "windows":
        copy_windows_dlls(layout, python_root / "DLLs")

    cpython_license = find_license(layout)
    copy_file(cpython_license, python_root / "LICENSE.txt")
    copy_file(cpython_license, licenses / "CPython-LICENSE.txt")
    distribution_licenses = copy_distribution_licenses(layout, licenses)
    copied_runtime_names: list[str] = []
    for source in runtime_sources(layout):
        target = copy_file(source, runtime_destination / source.name)
        copied_runtime_names.append(target.name)

    copied_dependencies: list[dict[str, str]] = []
    system_dependencies: list[str] = []
    if repair:
        copied_dependencies, system_dependencies = collect_dependency_closure(
            prefix, neverc, layout, licenses
        )
        repair_runtime(prefix, neverc, layout)
        # Re-run discovery after repair so unresolved relative references fail
        # during packaging rather than on a user's machine.
        collect_dependency_closure(prefix, neverc, layout, licenses)

    dependency_records = {
        str(record.get("name")): record
        for record in existing_dependencies + copied_dependencies
        if isinstance(record, dict) and record.get("name")
    }

    manifest: dict[str, object] = {
        "format": 1,
        "version": layout.version,
        "abi": layout.abi,
        "platform": layout.platform,
        "architecture": platform.machine(),
        "source_layout": "selected-interpreter-base-prefix",
        "runtime": layout.runtime_name,
        "runtime_aliases": sorted(set(copied_runtime_names)),
        "stdlib": str(stdlib_destination.relative_to(python_root)),
        "excluded": sorted(EXCLUDED_STDLIB_TREES | {"config-*"}),
        "native_dependencies": [dependency_records[name]
                                for name in sorted(dependency_records)],
        "system_dependencies": system_dependencies,
        "licenses": {
            "cpython": "licenses/CPython-LICENSE.txt",
            "distribution": distribution_licenses,
        },
    }
    (python_root / "runtime.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--neverc", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        manifest = bundle_runtime(arguments.prefix, arguments.neverc)
    except (OSError, RuntimeError) as error:
        print(f"bundle-python-runtime: {error}", file=sys.stderr)
        return 1
    print(
        "bundle-python-runtime: bundled "
        f"CPython {manifest['version']} ({manifest['platform']})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
