#!/usr/bin/env python3
"""Build a deterministic gzip-compressed newc initramfs for GKI QEMU."""

import argparse
import gzip
from pathlib import Path
import stat
import sys


NEWC_MAGIC = b"070701"


def padding(length):
    return b"\0" * ((-length) % 4)


def newc_entry(
    name,
    *,
    inode,
    mode,
    data=b"",
    rdevmajor=0,
    rdevminor=0,
):
    encoded_name = name.encode("utf-8") + b"\0"
    fields = (
        inode,
        mode,
        0,  # uid
        0,  # gid
        2 if stat.S_ISDIR(mode) else 1,
        0,  # mtime
        len(data),
        0,  # devmajor
        0,  # devminor
        rdevmajor,
        rdevminor,
        len(encoded_name),
        0,  # checksum (newc, not crc)
    )
    header = NEWC_MAGIC + b"".join(f"{value:08x}".encode("ascii") for value in fields)
    if len(header) != 110:
        raise AssertionError("invalid newc header length")
    record = header + encoded_name
    record += padding(len(record))
    record += data
    record += padding(len(data))
    return record


def build_archive(init_path, module_path, module_name="neverc-smoke.ko"):
    entries = [
        (".", stat.S_IFDIR | 0o755, b"", 0, 0),
        ("dev", stat.S_IFDIR | 0o755, b"", 0, 0),
        ("dev/console", stat.S_IFCHR | 0o600, b"", 5, 1),
        ("init", stat.S_IFREG | 0o755, Path(init_path).read_bytes(), 0, 0),
        (
            module_name,
            stat.S_IFREG | 0o644,
            Path(module_path).read_bytes(),
            0,
            0,
        ),
    ]
    payload = bytearray()
    for inode, (name, mode, data, rdevmajor, rdevminor) in enumerate(entries, 1):
        payload.extend(
            newc_entry(
                name,
                inode=inode,
                mode=mode,
                data=data,
                rdevmajor=rdevmajor,
                rdevminor=rdevminor,
            )
        )
    payload.extend(
        newc_entry("TRAILER!!!", inode=len(entries) + 1, mode=stat.S_IFREG, data=b"")
    )
    return bytes(payload)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="build the deterministic NeverC GKI QEMU initramfs"
    )
    parser.add_argument("--init", required=True, type=Path)
    parser.add_argument("--module", required=True, type=Path)
    parser.add_argument("--module-name", default="neverc-smoke.ko")
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    for label, path in (("init", args.init), ("module", args.module)):
        if not path.is_file():
            print(
                f"build-gki-initramfs: error: {label} is not a file: {path}",
                file=sys.stderr,
            )
            return 2
    if (
        not args.module_name
        or "/" in args.module_name
        or "\\" in args.module_name
        or args.module_name in {".", ".."}
    ):
        print(
            "build-gki-initramfs: error: --module-name must be a basename",
            file=sys.stderr,
        )
        return 2
    try:
        archive = build_archive(args.init, args.module, args.module_name)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("wb") as raw:
            with gzip.GzipFile(
                filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0
            ) as compressed:
                compressed.write(archive)
    except OSError as error:
        print(f"build-gki-initramfs: error: {error}", file=sys.stderr)
        return 1
    print(
        f"build-gki-initramfs: wrote {args.output} ({len(archive)} bytes uncompressed)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
