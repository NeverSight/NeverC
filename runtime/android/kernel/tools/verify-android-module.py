#!/usr/bin/env python3
"""Verify the Android kernel loader ABI of a final NeverC ``.ko`` file."""

import argparse
from pathlib import Path
import sys

from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_COMPRESSED = 0x800
MODVERSION_ENTRY_SIZE = 64


class ValidationError(RuntimeError):
    """The artifact does not satisfy the Android module loader contract."""


def _one(records, label):
    if len(records) != 1:
        raise ValidationError(f"expected exactly one {label}, found {len(records)}")
    return records[0]


def _valid_alignment(value, minimum):
    return isinstance(value, int) and value >= minimum and value & (value - 1) == 0


def validate_module_contract(record, *, require_empty_alloc_tags=False):
    """Validate a plain record and return a compact contract summary.

    Keeping this policy independent of pyelftools makes every malformed shape
    unit-testable without manufacturing corrupt ELF files.
    """

    if record.get("elf_class") != "ELFCLASS64":
        raise ValidationError("module must be ELF64")
    if record.get("data_encoding") != "ELFDATA2LSB":
        raise ValidationError("module must be little-endian ELF")
    if record.get("machine") != "EM_AARCH64":
        raise ValidationError("module must target AArch64")
    if record.get("type") != "ET_REL":
        raise ValidationError("module must be an ET_REL relocatable object")

    sections = record.get("sections", {})
    versions = _one(sections.get("__versions", []), "__versions section")
    if versions.get("type") != "SHT_PROGBITS":
        raise ValidationError("__versions must be SHT_PROGBITS")
    if not versions.get("flags", 0) & SHF_ALLOC:
        raise ValidationError("__versions must have SHF_ALLOC")
    if versions.get("flags", 0) & SHF_COMPRESSED:
        raise ValidationError("__versions must not be compressed")
    if not _valid_alignment(versions.get("alignment"), 8):
        raise ValidationError("__versions alignment must be a power of two >= 8")
    versions_size = versions.get("size")
    if not isinstance(versions_size, int) or versions_size < 0:
        raise ValidationError("__versions has an invalid size")
    if versions_size % MODVERSION_ENTRY_SIZE:
        raise ValidationError(
            f"__versions size must be a multiple of {MODVERSION_ENTRY_SIZE}"
        )

    if sections.get("alloc_tags"):
        raise ValidationError("uncollected alloc_tags input section remains in module")
    alloc_tags = _one(
        sections.get(".codetag.alloc_tags", []), ".codetag.alloc_tags section"
    )
    if alloc_tags.get("type") != "SHT_PROGBITS":
        raise ValidationError(".codetag.alloc_tags must be SHT_PROGBITS")
    required_flags = SHF_ALLOC | SHF_WRITE
    if alloc_tags.get("flags", 0) & required_flags != required_flags:
        raise ValidationError(".codetag.alloc_tags must have SHF_ALLOC | SHF_WRITE")
    if alloc_tags.get("flags", 0) & SHF_COMPRESSED:
        raise ValidationError(".codetag.alloc_tags must not be compressed")
    if not _valid_alignment(alloc_tags.get("alignment"), 8):
        raise ValidationError(
            ".codetag.alloc_tags alignment must be a power of two >= 8"
        )
    alloc_tags_size = alloc_tags.get("size")
    if not isinstance(alloc_tags_size, int) or alloc_tags_size < 0:
        raise ValidationError(".codetag.alloc_tags has an invalid size")

    symbols = record.get("symbols", {})
    start = _one(symbols.get("__start_alloc_tags", []), "__start_alloc_tags symbol")
    stop = _one(symbols.get("__stop_alloc_tags", []), "__stop_alloc_tags symbol")
    for name, symbol in (
        ("__start_alloc_tags", start),
        ("__stop_alloc_tags", stop),
    ):
        if symbol.get("binding") != "STB_GLOBAL":
            raise ValidationError(f"{name} must be global")
        if symbol.get("type") != "STT_NOTYPE":
            raise ValidationError(f"{name} must have STT_NOTYPE type")
        if symbol.get("section_index") != alloc_tags.get("index"):
            raise ValidationError(
                f"{name} must be defined in .codetag.alloc_tags, not undefined or elsewhere"
            )
        if not isinstance(symbol.get("value"), int):
            raise ValidationError(f"{name} has an invalid value")

    if start["value"] != 0:
        raise ValidationError("__start_alloc_tags must be at offset 0")
    if stop["value"] != alloc_tags_size:
        raise ValidationError(
            "__stop_alloc_tags must equal the .codetag.alloc_tags section size"
        )
    if require_empty_alloc_tags and alloc_tags_size != 0:
        raise ValidationError("smoke module must have an empty alloc_tags range")

    return {
        "versions_entries": versions_size // MODVERSION_ENTRY_SIZE,
        "alloc_tags_size": alloc_tags_size,
    }


def inspect_module(path):
    """Parse one ELF module into the plain record consumed above."""

    path = Path(path)
    try:
        with path.open("rb") as stream:
            elf = ELFFile(stream)
            record = {
                "elf_class": elf.header["e_ident"]["EI_CLASS"],
                "data_encoding": elf.header["e_ident"]["EI_DATA"],
                "machine": elf.header["e_machine"],
                "type": elf.header["e_type"],
                "sections": {},
                "symbols": {},
            }
            for index, section in enumerate(elf.iter_sections()):
                section_record = {
                    "index": index,
                    "type": section["sh_type"],
                    "flags": int(section["sh_flags"]),
                    "alignment": int(section["sh_addralign"]),
                    "size": int(section["sh_size"]),
                }
                record["sections"].setdefault(section.name, []).append(
                    section_record
                )
                if isinstance(section, SymbolTableSection):
                    for symbol in section.iter_symbols():
                        if not symbol.name:
                            continue
                        record["symbols"].setdefault(symbol.name, []).append(
                            {
                                "binding": symbol["st_info"]["bind"],
                                "type": symbol["st_info"]["type"],
                                "section_index": symbol["st_shndx"],
                                "value": int(symbol["st_value"]),
                            }
                        )
            return record
    except (ELFError, OSError, ValueError, KeyError) as error:
        raise ValidationError(f"cannot parse module {path}: {error}") from error


def verify_module(path, *, require_empty_alloc_tags=False):
    return validate_module_contract(
        inspect_module(path), require_empty_alloc_tags=require_empty_alloc_tags
    )


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="verify final NeverC Android kernel module loader sections"
    )
    parser.add_argument("artifact", type=Path)
    parser.add_argument(
        "--require-empty-alloc-tags",
        action="store_true",
        help="also require start == stop for the zero-import smoke fixture",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    try:
        details = verify_module(
            args.artifact,
            require_empty_alloc_tags=args.require_empty_alloc_tags,
        )
    except ValidationError as error:
        print(f"verify-android-module: error: {error}", file=sys.stderr)
        return 1
    print(
        "[loader-contract] PASS "
        f"versions_entries={details['versions_entries']} "
        f"alloc_tags_size={details['alloc_tags_size']} artifact={args.artifact}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
