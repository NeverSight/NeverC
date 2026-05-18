#!/usr/bin/env python3
"""Convert a binary file to a C header with an embedded byte array.

Usage:
    python3 bin2c.py <input.bc> -o <output.h> --name <varname>

Generates:
    static const unsigned char <varname>[] = { 0x..., ... };
    static const unsigned int <varname>_len = sizeof(<varname>);
"""

import argparse
import os
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="Input binary file")
    parser.add_argument("-o", "--output", required=True, help="Output .h file")
    parser.add_argument("--name", default="kStringRuntimeBitcode",
                        help="Variable name for the array")
    args = parser.parse_args()

    with open(args.input, "rb") as f:
        data = f.read()

    lines = []
    lines.append(f"/* Auto-generated from {os.path.basename(args.input)} "
                 f"({len(data)} bytes) — DO NOT EDIT. */")
    lines.append(f"static const unsigned char {args.name}[] = {{")

    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"  {hex_vals},")

    lines.append("};")
    lines.append(f"static const unsigned int {args.name}_len = "
                 f"sizeof({args.name});")
    lines.append("")

    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    with open(args.output, "w") as f:
        f.write("\n".join(lines))

    print(f"bin2c: {len(data)} bytes -> {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
