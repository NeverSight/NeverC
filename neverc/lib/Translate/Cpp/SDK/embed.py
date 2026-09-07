#!/usr/bin/env python3
"""Embed the checksum-pinned C++ header snapshot without host filesystem paths."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath


def initializer(data):
    """Emit byte initializers without MSVC's final string-literal length limit."""
    # Adjacent short literals still form one string and can exceed C2026's
    # final limit. Numeric elements also preserve CRLF, NUL and high bytes.
    values = [f"0x{byte:02x}" if byte < 128 else f"static_cast<char>(0x{byte:02x})"
              for byte in data]
    values.append("0")  # Catalog callers use StringRef's NUL-terminated form.
    return "{\n" + "\n".join("  " + ", ".join(values[i:i + 24]) + ","
                               for i in range(0, len(values), 24)) + "\n}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    catalog_bytes = args.catalog.read_bytes()
    catalog = json.loads(catalog_bytes)
    if catalog.get("schema") != "neverc.cpp.sdk.catalog" or catalog.get("version") != 1:
        parser.error("unsupported built-in SDK catalog")
    output = ["// Generated from the approved source snapshot. Do not edit.",
              "#ifndef NEVERC_BUILTIN_CPP_SDK_DATA_H",
              "#define NEVERC_BUILTIN_CPP_SDK_DATA_H", "#include <cstddef>",
              "namespace neverc_cpp_sdk {",
              "struct File { const char *Root, *Path, *Contents;",
              "  std::size_t Size; const char *SHA256; bool Metadata; };",
              "static constexpr char CatalogJSON[] =",
              initializer(catalog_bytes) + ";",
              'static constexpr char CatalogSHA256[] = "' +
              hashlib.sha256(catalog_bytes).hexdigest() + '";']
    files = []
    seen = set()
    for kind in ("headers", "metadata"):
        for entry in catalog.get(kind, []):
            root, name = entry["root"], entry["path"]
            relative = PurePosixPath(name)
            if (root not in ("libcxx", "resource", "platform") or
                    not name or relative.is_absolute() or
                    any(part in ("", ".", "..") for part in name.split("/")) or
                    "\\" in name or ":" in name or (root, name) in seen):
                parser.error("invalid or duplicate snapshot path")
            seen.add((root, name))
            contents = (args.root / root / name).read_bytes()
            if hashlib.sha256(contents).hexdigest() != entry["sha256"]:
                parser.error(f"snapshot does not match its approved hash: {root}/{name}")
            symbol = f"Data{len(files)}"
            output.extend([f"static constexpr char {symbol}[] =", initializer(contents) + ";"])
            files.append("  {" + json.dumps(root) + ", " + json.dumps(name) +
                         f", {symbol}, {len(contents)}, " + json.dumps(entry["sha256"]) +
                         (", true}," if kind == "metadata" else ", false},"))
    output.extend(["static constexpr File Files[] = {", *files, "};",
                   "static constexpr std::size_t FileCount = sizeof(Files) / sizeof(Files[0]);",
                   "} // namespace neverc_cpp_sdk", "#endif", ""])
    generated = "\n".join(output)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != generated:
        args.output.write_text(generated)


if __name__ == "__main__":
    main()
