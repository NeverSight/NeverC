#!/usr/bin/env python3

"""Generate the NeverC plugin ABI layout manifest (pluginsdk/abi/plugin.json).

For each supported host ABI key ``{arch, endian, pointer_width,
calling_convention}`` this records, for every public struct/union in the ABI:

  * the overall size and alignment, and
  * the offset and size of every named field.

It also records the ABI/interface versions from the SDK manifest. Because the
ABI uses fixed-width types wrapped in the SDK's ``NEVERC_ABI_PACK_BEGIN/END``
(pack(8)) machinery, these layouts must be identical across hosts *and*
independent of the caller's ambient packing. ``gen-abi-manifest.py`` therefore
also measures every struct under an outer ``#pragma pack(1)`` and
``#pragma pack(16)`` and fails if any layout changes -- a struct whose size or
alignment depends on ambient packing is an ABI hazard.

Two measurement modes produce the same facts:

  * Native (the current host): compile *and run* a probe that prints
    sizeof/_Alignof/offsetof, so the entry reflects what the host compiler
    actually produces.
  * Cross (``--all-hosts``): fold the same quantities into a constant array and
    read them back out of ``-emit-llvm`` output for each supported target
    triple. The public headers only need freestanding ``<stddef.h>`` and
    ``<stdint.h>``, so no target SDK or emulator is required.

Cross measurement is what lets one machine populate every shipped ABI key; each
host's CI still re-derives its own key natively under ``--check``, so a host
whose real layout disagrees with the committed prediction fails loudly there.

``--check`` recomputes the current host's entry (including the pack-invariance
proof) and requires it to match the committed manifest exactly; within one ABI
key an existing field may not move or shrink. It also requires every supported
host key to be present, so a missing key is caught on any machine rather than
only on the host that happens to be absent from the manifest.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SINGLE_HEADER = ROOT / "pluginsdk/include/neverc/Plugin/NevercPluginAPI.h"
INCLUDE_DIR = ROOT / "pluginsdk/include"
OUTPUT = ROOT / "pluginsdk/abi/plugin.json"
SDK_MANIFEST = ROOT / "pluginsdk/manifest/plugin.json"

# Matches every "typedef struct/union { BODY } NevercName;" pair. Applied with
# findall so each BODY is bounded by its own braces (a targeted re.search for a
# single trailing name would greedily span the whole file).
STRUCT_TYPEDEF = re.compile(
    r"typedef\s+(?:struct|union)\b[^{;]*\{(.*?)\}\s*(Neverc\w+)\s*;", re.DOTALL
)

# Outer packings the ABI layout must be invariant under.
PACK_VARIANTS = (1, 16)

# Host ABI keys the SDK ships layouts for, each with a triple that reproduces
# that key's C ABI. macOS and Linux on the same architecture share a key: the
# key records the calling convention family, not the OS.
SUPPORTED_ABI_KEYS = {
    "x86_64-le-64-sysv": "x86_64-unknown-linux-gnu",
    "aarch64-le-64-sysv": "aarch64-unknown-linux-gnu",
    "x86_64-le-64-win": "x86_64-pc-windows-msvc",
    "aarch64-le-64-win": "aarch64-pc-windows-msvc",
}

# Symbol the cross probe folds its measurements into, and the constant array
# clang emits for it, e.g. "@NevercAbiProbe = constant [7 x i64] [i64 8, ...]".
PROBE_SYMBOL = "NevercAbiProbe"
PROBE_IR = re.compile(
    r"@" + PROBE_SYMBOL + r"\s*=[^\[]*\[\s*\d+\s+x\s+i64\s*\]\s*\[([^\]]*)\]"
)
PROBE_VALUE = re.compile(r"i64\s+(\d+)")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def member_name(decl: str) -> str | None:
    """Return the field name declared by ``decl``, or None if it is anonymous."""
    tail = decl[decl.rfind("}") + 1:] if "}" in decl else decl
    tail = re.sub(r"\[[^\]]*\]", "", tail)   # drop array subscripts
    tail = re.sub(r":\s*\d+\s*$", "", tail)  # drop bitfield width
    match = re.search(r"(\w+)\s*$", tail.strip())
    return match.group(1) if match else None


def top_level_fields(body: str) -> list[str]:
    """Extract the named top-level fields of a struct body (brace-aware)."""
    body = strip_comments(body)
    fields: list[str] = []
    depth = 0
    current = ""
    for char in body:
        if char == "{":
            depth += 1
            current += char
        elif char == "}":
            depth -= 1
            current += char
        elif char == ";" and depth == 0:
            name = member_name(current)
            if name is not None:
                fields.append(name)
            current = ""
        else:
            current += char
    return fields


def struct_layouts() -> list[tuple[str, list[str]]]:
    text = SINGLE_HEADER.read_text(encoding="utf-8")
    pairs = STRUCT_TYPEDEF.findall(text)
    names = [name for _body, name in pairs]
    unique = sorted(set(names))
    if len(unique) != len(names):
        raise ValueError("duplicate public struct typedef names")
    fields_by_name = {name: top_level_fields(body) for body, name in pairs}
    return [(name, fields_by_name[name]) for name in unique]


def find_compiler(explicit: str | None) -> str:
    if explicit:
        return explicit
    # "cl" last: MSVC can measure the native host but not cross targets, and a
    # Windows runner with the MSVC environment loaded may have nothing else.
    for name in ("cc", "clang", "gcc", "cl"):
        found = shutil.which(name)
        if found:
            return found
    raise ValueError("no C compiler found to measure ABI layout")


def is_msvc(compiler: str) -> bool:
    return Path(compiler).stem.lower() == "cl"


def build_probe(layouts: list[tuple[str, list[str]]], pack: int | None) -> str:
    lines = []
    if pack is not None:
        lines.append(f"#pragma pack({pack})")
    lines += ['#include "neverc/Plugin/NevercPluginAPI.h"', "#include <stddef.h>",
              "#include <stdio.h>", "int main(void){"]
    lines.append('  printf("__ptr %zu\\n", sizeof(void *));')
    for name, fields in layouts:
        lines.append(
            f'  printf("S {name} %zu %zu\\n", sizeof({name}), _Alignof({name}));'
        )
        if pack is None:
            for field in fields:
                lines.append(
                    f'  printf("F {name} {field} %zu %zu\\n", '
                    f"offsetof({name}, {field}), "
                    f"sizeof((({name} *)0)->{field}));"
                )
    lines.append("  return 0;")
    lines.append("}")
    return "\n".join(lines) + "\n"


def run_probe(compiler: str, program: str) -> str:
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        source = tmp_path / "abi_probe.c"
        binary = tmp_path / ("abi_probe.exe" if is_msvc(compiler) else "abi_probe")
        source.write_text(program, encoding="utf-8")
        if is_msvc(compiler):
            # Relative names, so the .obj lands in cwd without a /Fo argument
            # whose trailing separator Windows argument quoting would mangle.
            command = [compiler, "/nologo", "/std:c11", f"/I{INCLUDE_DIR}",
                       source.name, f"/Fe:{binary.name}"]
        else:
            command = [compiler, "-std=c11", "-I", str(INCLUDE_DIR), str(source),
                       "-o", str(binary)]
        compile_result = subprocess.run(
            command, cwd=str(tmp_path), capture_output=True, text=True,
        )
        if compile_result.returncode != 0:
            raise ValueError(f"ABI probe failed to compile:\n{compile_result.stderr}")
        run_result = subprocess.run([str(binary)], capture_output=True, text=True)
        if run_result.returncode != 0:
            raise ValueError(f"ABI probe failed to run:\n{run_result.stderr}")
        return run_result.stdout


def parse_probe(output: str) -> tuple[int, dict]:
    structs: dict = {}
    pointer_width = None
    for line in output.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "__ptr":
            pointer_width = int(parts[1]) * 8
        elif parts[0] == "S":
            name, size, align = parts[1], int(parts[2]), int(parts[3])
            entry = structs.setdefault(name, {})
            entry["size"] = size
            entry["align"] = align
            entry.setdefault("fields", {})
        elif parts[0] == "F":
            name, field, offset, size = parts[1], parts[2], int(parts[3]), int(parts[4])
            structs.setdefault(name, {}).setdefault("fields", {})[field] = {
                "offset": offset,
                "size": size,
            }
    if pointer_width is None:
        raise ValueError("ABI probe did not report pointer width")
    return pointer_width, structs


def size_align_view(structs: dict) -> dict:
    return {name: (data["size"], data["align"]) for name, data in structs.items()}


def measure(compiler: str, layouts: list[tuple[str, list[str]]]) -> dict:
    pointer_width, structs = parse_probe(
        run_probe(compiler, build_probe(layouts, None))
    )
    baseline = size_align_view(structs)
    # Prove the ABI layout is invariant under hostile ambient packing.
    for pack in PACK_VARIANTS:
        _, packed = parse_probe(run_probe(compiler, build_probe(layouts, pack)))
        packed_view = size_align_view(packed)
        for name, sa in baseline.items():
            if packed_view.get(name) != sa:
                raise ValueError(
                    f"struct {name} layout is not pack-invariant: "
                    f"default size/align {sa} but pack({pack}) gives "
                    f"{packed_view.get(name)}"
                )
    return {"pointer_width": pointer_width, "structs": structs}


def build_cross_probe(layouts: list[tuple[str, list[str]]], pack: int | None) -> str:
    """Fold the same quantities ``build_probe`` prints into a constant array.

    Constant folding happens for the requested target, so the array can be read
    back out of the IR without running anything on that target.
    """
    lines = []
    if pack is not None:
        lines.append(f"#pragma pack({pack})")
    lines += ['#include "neverc/Plugin/NevercPluginAPI.h"', "#include <stddef.h>"]
    values = ["sizeof(void *)"]
    for name, fields in layouts:
        values += [f"sizeof({name})", f"_Alignof({name})"]
        if pack is None:
            for field in fields:
                values += [f"offsetof({name}, {field})",
                           f"sizeof((({name} *)0)->{field})"]
    lines.append(f"const unsigned long long {PROBE_SYMBOL}[] = {{")
    lines += [f"  (unsigned long long)({value})," for value in values]
    lines.append("};")
    return "\n".join(lines) + "\n"


def run_cross_probe(compiler: str, triple: str, program: str) -> list[int]:
    if is_msvc(compiler):
        raise ValueError(
            "cross measurement needs a clang-style driver (-target/-emit-llvm); "
            f"{compiler} cannot retarget, so pass --cc clang"
        )
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "abi_cross_probe.c"
        source.write_text(program, encoding="utf-8")
        result = subprocess.run(
            [compiler, "-target", triple, "-ffreestanding", "-std=c11",
             "-I", str(INCLUDE_DIR), "-S", "-emit-llvm", "-o", "-", str(source)],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            raise ValueError(
                f"ABI cross probe failed for {triple}:\n{result.stderr}"
            )
    match = PROBE_IR.search(result.stdout)
    if match is None:
        raise ValueError(f"ABI cross probe for {triple} emitted no {PROBE_SYMBOL}")
    return [int(value) for value in PROBE_VALUE.findall(match.group(1))]


def parse_cross_probe(
    values: list[int], layouts: list[tuple[str, list[str]]], pack: int | None
) -> tuple[int, dict]:
    expected = 1 + sum(
        2 + (2 * len(fields) if pack is None else 0) for _name, fields in layouts
    )
    if len(values) != expected:
        raise ValueError(
            f"ABI cross probe returned {len(values)} values, expected {expected}"
        )
    stream = iter(values)
    pointer_width = next(stream) * 8
    structs: dict = {}
    for name, fields in layouts:
        entry = structs.setdefault(name, {})
        entry["size"] = next(stream)
        entry["align"] = next(stream)
        entry.setdefault("fields", {})
        if pack is None:
            for field in fields:
                offset = next(stream)
                entry["fields"][field] = {"offset": offset, "size": next(stream)}
    return pointer_width, structs


def cross_measure(
    compiler: str, triple: str, layouts: list[tuple[str, list[str]]]
) -> dict:
    pointer_width, structs = parse_cross_probe(
        run_cross_probe(compiler, triple, build_cross_probe(layouts, None)),
        layouts, None,
    )
    baseline = size_align_view(structs)
    for pack in PACK_VARIANTS:
        _, packed = parse_cross_probe(
            run_cross_probe(compiler, triple, build_cross_probe(layouts, pack)),
            layouts, pack,
        )
        packed_view = size_align_view(packed)
        for name, sa in baseline.items():
            if packed_view.get(name) != sa:
                raise ValueError(
                    f"struct {name} layout is not pack-invariant on {triple}: "
                    f"default size/align {sa} but pack({pack}) gives "
                    f"{packed_view.get(name)}"
                )
    return {"pointer_width": pointer_width, "structs": structs}


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("arm64", "aarch64"):
        return "aarch64"
    if machine in ("x86_64", "amd64"):
        return "x86_64"
    return machine


def calling_convention() -> str:
    return "win" if os.name == "nt" else "sysv"


def abi_key(arch: str, endian: str, pointer_width: int, cc: str) -> str:
    short = "le" if endian == "little" else "be"
    return f"{arch}-{short}-{pointer_width}-{cc}"


def abi_versions() -> dict:
    manifest = json.loads(SDK_MANIFEST.read_text(encoding="utf-8"))
    return {"abi": manifest["abi"], "modules": [
        {"name": m["name"], "interfaces": [
            {"name": i["name"], "major": i["major"], "minor": i["minor"]}
            for i in m["interfaces"]
        ]}
        for m in manifest["modules"]
    ]}


def build_entry(arch: str, endian: str, cc: str, measured: dict) -> dict:
    """Shape one ABI key's facts. Native and cross measurement must agree here."""
    field_count = sum(len(data.get("fields", {}))
                      for data in measured["structs"].values())
    return {
        "arch": arch,
        "endian": endian,
        "pointer_width": measured["pointer_width"],
        "calling_convention": cc,
        "pack_invariant_under": list(PACK_VARIANTS),
        "struct_count": len(measured["structs"]),
        "field_count": field_count,
        "structs": measured["structs"],
    }


def current_entry(compiler: str) -> tuple[str, dict]:
    layouts = struct_layouts()
    measured = measure(compiler, layouts)
    arch = host_arch()
    endian = sys.byteorder
    cc = calling_convention()
    key = abi_key(arch, endian, measured["pointer_width"], cc)
    return key, build_entry(arch, endian, cc, measured)


def cross_entry(compiler: str, key: str, triple: str,
                layouts: list[tuple[str, list[str]]]) -> dict:
    arch, short, _width, cc = key.split("-")
    endian = "little" if short == "le" else "big"
    measured = cross_measure(compiler, triple, layouts)
    produced = abi_key(arch, endian, measured["pointer_width"], cc)
    if produced != key:
        raise ValueError(f"target {triple} measures as ABI key {produced}, not {key}")
    return build_entry(arch, endian, cc, measured)


def missing_supported_keys(manifest: dict) -> list[str]:
    """Supported host keys the manifest never recorded.

    A key the SDK claims to support but never recorded would otherwise only
    surface on that host, so this is checked from any machine.
    """
    recorded = manifest.get("abi_keys", {})
    return [name for name in sorted(SUPPORTED_ABI_KEYS) if name not in recorded]


def load_manifest() -> dict:
    if OUTPUT.exists():
        return json.loads(OUTPUT.read_text(encoding="utf-8"))
    return {
        "manifest_version": 2,
        "generator": "utils/plugin-api/gen-abi-manifest.py",
        "abi_keys": {},
    }


def render(manifest: dict) -> str:
    return json.dumps(manifest, indent=2, sort_keys=True) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if out of date")
    parser.add_argument("--all-hosts", action="store_true",
                        help="also cross-measure every supported host ABI key")
    parser.add_argument("--cc", default=None, help="C compiler to measure with")
    arguments = parser.parse_args()

    try:
        compiler = find_compiler(arguments.cc)
        key, entry = current_entry(compiler)
        versions = abi_versions()
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"ABI manifest error: {error}", file=sys.stderr)
        return 1

    if arguments.check:
        if not OUTPUT.exists():
            print(f"{OUTPUT} does not exist; run gen-abi-manifest.py", file=sys.stderr)
            return 1
        manifest = load_manifest()
        if key not in manifest.get("abi_keys", {}):
            fix = ("run gen-abi-manifest.py --all-hosts"
                   if key in SUPPORTED_ABI_KEYS
                   else "regenerate on this host and commit it")
            print(f"ABI manifest has no entry for host key {key}; {fix}",
                  file=sys.stderr)
            return 1
        expected = dict(manifest["abi_keys"][key])
        if expected != entry or manifest.get("versions") != versions:
            print(f"ABI manifest is out of date for host key {key}", file=sys.stderr)
            return 1
        missing = missing_supported_keys(manifest)
        if missing:
            print(
                "ABI manifest is missing supported host keys: "
                + ", ".join(missing)
                + "; run gen-abi-manifest.py --all-hosts",
                file=sys.stderr,
            )
            return 1
        return 0

    manifest = load_manifest()
    manifest["manifest_version"] = 2
    manifest["versions"] = versions
    manifest.setdefault("abi_keys", {})[key] = entry

    if arguments.all_hosts:
        try:
            layouts = struct_layouts()
            for name, triple in sorted(SUPPORTED_ABI_KEYS.items()):
                crossed = cross_entry(compiler, name, triple, layouts)
                if name == key:
                    # The current host is measured both ways. Requiring the two
                    # to agree is what makes the cross-measured entries for the
                    # other hosts trustworthy rather than merely plausible.
                    if crossed != entry:
                        print(
                            f"cross measurement of {name} via {triple} disagrees "
                            "with this host's native measurement",
                            file=sys.stderr,
                        )
                        return 1
                    continue
                manifest["abi_keys"][name] = crossed
        except (OSError, ValueError) as error:
            print(f"ABI manifest error: {error}", file=sys.stderr)
            return 1

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(render(manifest), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
