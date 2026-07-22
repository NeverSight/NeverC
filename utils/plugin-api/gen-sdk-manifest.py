#!/usr/bin/env python3

"""Generate the NeverC plugin SDK manifest (pluginsdk/manifest/plugin.json).

The manifest is a machine-readable summary of the shipped ABI: the top-level
plugin ABI version, every public interface with its stable ID/version/stability,
the schema digests, the supported target keys, and the distributed
single-header source digest. It lets a consumer confirm a downloaded SDK matches
a host without compiling anything.

All inputs are the committed source-of-truth files, so ``--check`` fails on any
manual drift.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = Path(__file__).resolve().parent / "plugin-api-modules.json"
SCHEMA_DIR = ROOT / "neverc/include/neverc/Plugin/Schema"
TARGET_DIR = ROOT / "pluginsdk/schemas/targets"

INTERFACE_HIGH = re.compile(
    r"#define\s+NEVERC_INTERFACE_(\w+)_HIGH\s+UINT64_C\((0x[0-9a-fA-F]+)\)"
)
INTERFACE_LOW = re.compile(
    r"#define\s+NEVERC_INTERFACE_(\w+)_LOW\s+UINT64_C\((0x[0-9a-fA-F]+)\)"
)
API_MAJOR = re.compile(r"#define\s+NEVERC_(\w+)_API_MAJOR\s+UINT16_C\((\d+)\)")
API_MINOR = re.compile(r"#define\s+NEVERC_(\w+)_API_MINOR\s+UINT16_C\((\d+)\)")
STABILITY = re.compile(r"#define\s+NEVERC_(\w+)_INTERFACE_STABILITY\s+(\w+)")
DIGEST_LINE = re.compile(r"/\* generated-from-digest: ([0-9a-f]{64}) \*/")


def join_continuations(text: str) -> str:
    """Collapse backslash-continued preprocessor lines into single lines."""
    return re.sub(r"\\\n\s*", " ", text)


def parse_interfaces(text: str) -> list[dict]:
    text = join_continuations(text)
    highs = {name: value for name, value in INTERFACE_HIGH.findall(text)}
    lows = {name: value for name, value in INTERFACE_LOW.findall(text)}
    majors = {name: int(value) for name, value in API_MAJOR.findall(text)}
    minors = {name: int(value) for name, value in API_MINOR.findall(text)}
    stabilities = {name: value for name, value in STABILITY.findall(text)}
    interfaces = []
    for name in sorted(highs):
        if name not in lows:
            raise ValueError(f"interface {name} has a high ID but no low ID")
        interfaces.append(
            {
                "name": name,
                "id_high": _normalize(highs[name]),
                "id_low": _normalize(lows[name]),
                "major": majors.get(name),
                "minor": minors.get(name),
                "stability": stabilities.get(name),
            }
        )
    return interfaces


def _normalize(value: str) -> str:
    return f"0x{int(value, 16):016x}"


def read_abi_version(core_text: str) -> dict:
    core_text = join_continuations(core_text)
    major = re.search(r"#define\s+NEVERC_PLUGIN_ABI_MAJOR\s+UINT16_C\((\d+)\)", core_text)
    minor = re.search(r"#define\s+NEVERC_PLUGIN_ABI_MINOR\s+UINT16_C\((\d+)\)", core_text)
    entry = re.search(r'#define\s+NEVERC_PLUGIN_ENTRY_POINT\s+"([^"]+)"', core_text)
    if not (major and minor and entry):
        raise ValueError("PluginCore.h is missing ABI version macros")
    return {
        "major": int(major.group(1)),
        "minor": int(minor.group(1)),
        "entry_point": entry.group(1),
    }


def schema_digests() -> list[dict]:
    digests = []
    for path in sorted(SCHEMA_DIR.glob("*.json")):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        digests.append({"name": path.name, "digest": digest})
    return digests


def supported_targets() -> list[str]:
    names = []
    for path in sorted(TARGET_DIR.glob("*.json")):
        names.append(path.stem)
    return names


def single_header_digest(document: dict) -> str:
    path = ROOT / document["single_header"]
    match = DIGEST_LINE.search(path.read_text(encoding="utf-8"))
    if not match:
        raise ValueError(
            f"{path} has no source digest; run gen-single-header.py first"
        )
    return match.group(1)


def generate(document: dict) -> str:
    include_root = ROOT / document["include_root"]
    modules = []
    core_text = (include_root / "PluginCore.h").read_text(encoding="utf-8")
    for module in document["modules"]:
        text = (include_root / module["name"]).read_text(encoding="utf-8")
        modules.append(
            {
                "name": module["name"],
                "public": bool(module.get("public")),
                "interfaces": parse_interfaces(text),
            }
        )
    manifest = {
        "manifest_version": 1,
        "generator": "utils/plugin-api/gen-sdk-manifest.py",
        "abi": read_abi_version(core_text),
        "single_header": {
            "path": document["single_header"],
            "source_digest": single_header_digest(document),
        },
        "modules": modules,
        "schemas": schema_digests(),
        "targets": supported_targets(),
    }
    return json.dumps(manifest, indent=2, sort_keys=True) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if out of date")
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--output", type=Path, default=None)
    arguments = parser.parse_args()

    try:
        document = json.loads(arguments.manifest.read_text(encoding="utf-8"))
        output_path = arguments.output or (ROOT / "pluginsdk/manifest/plugin.json")
        generated = generate(document)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"SDK manifest generation error: {error}", file=sys.stderr)
        return 1

    if arguments.check:
        try:
            existing = output_path.read_text(encoding="utf-8")
        except OSError:
            existing = ""
        if existing != generated:
            print(
                f"{output_path} is out of date; run gen-sdk-manifest.py",
                file=sys.stderr,
            )
            return 1
        return 0

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(generated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
