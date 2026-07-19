#!/usr/bin/env python3
"""Validate the unreleased prototype-to-first-ABI cutover."""

from __future__ import annotations

import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]

REMOVED_PATHS = (
    "neverc/include/neverc/Plugin/PluginLoader.h",
    "neverc/lib/Plugin/Core/PluginLoader.cpp",
    "neverc/lib/Plugin/Core/PluginPassAdaptor.cpp",
    "neverc/lib/Plugin/Core/PluginPassAdaptor.h",
    "neverc/lib/Plugin/Host/HostAPIBridge.cpp",
    "neverc/lib/Plugin/Host/HostAPIBridge.h",
    "neverc/lib/Plugin/Bridge/BridgeAnalysis.cpp",
    "neverc/lib/Plugin/Bridge/BridgeDataStructures.cpp",
    "neverc/lib/Plugin/Bridge/BridgeIR.cpp",
    "neverc/lib/Plugin/Bridge/BridgeIRBuilder.cpp",
    "neverc/lib/Plugin/Bridge/BridgeLinker.cpp",
    "neverc/lib/Plugin/Bridge/BridgeMIR.cpp",
    "neverc/lib/Plugin/Bridge/BridgeString.cpp",
)

FORBIDDEN_TOKENS = (
    "getGlobalPluginLoader",
    "pluginArgStorage",
    "NevercPluginInfo",
    "NevercValueRef",
)

AGGREGATE_HEADERS = (
    "PluginCore.h",
    "PluginDriver.h",
    "PluginSource.h",
    "PluginPrep.h",
    "PluginAST.h",
    "PluginSema.h",
    "PluginIR.h",
    "PluginMIR.h",
    "PluginPhaseSchema.h",
)

EXAMPLES = (
    "ExamplePlugin.c",
    "BenchPlugin.c",
    "CrtShimPlugin.c",
    "CustomCallConvPlugin.c",
)


def source_files(root: pathlib.Path):
    for suffix in ("*.h", "*.c", "*.cc", "*.cpp"):
        yield from root.rglob(suffix)


def main() -> int:
    errors: list[str] = []
    for relative in REMOVED_PATHS:
        if (ROOT / relative).exists():
            errors.append(f"removed prototype path still exists: {relative}")

    for directory in (ROOT / "neverc", ROOT / "pluginsdk"):
        for path in source_files(directory):
            text = path.read_text(encoding="utf-8", errors="replace")
            for token in FORBIDDEN_TOKENS:
                if token in text:
                    errors.append(
                        f"{path.relative_to(ROOT)} still references {token}"
                    )

    aggregate_path = (
        ROOT / "neverc/include/neverc/Plugin/NevercPluginAPI.h"
    )
    aggregate = aggregate_path.read_text(encoding="utf-8")
    for header in AGGREGATE_HEADERS:
        if f'"neverc/Plugin/{header}"' not in aggregate:
            errors.append(f"aggregate header does not include {header}")

    examples_dir = ROOT / "pluginsdk/examples"
    for name in EXAMPLES:
        text = (examples_dir / name).read_text(encoding="utf-8")
        if "neverc_plugin_entry" not in text:
            errors.append(f"{name} does not export neverc_plugin_entry")
        if "nevercGetPluginInfo" in text:
            errors.append(f"{name} still exports nevercGetPluginInfo")

    registry = (
        ROOT / "neverc/lib/Plugin/Core/PluginRegistry.cpp"
    ).read_text(encoding="utf-8")
    if 'getAddressOfSymbol("nevercGetPluginInfo")' not in registry:
        errors.append("the loader no longer diagnoses removed prototype binaries")

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
