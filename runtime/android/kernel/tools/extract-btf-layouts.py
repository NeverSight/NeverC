#!/usr/bin/env python3
"""Extract selected C structure layouts from an ELF .BTF section."""

import argparse
import json
import struct
import sys

from elftools.dwarf.descriptions import describe_form_class
from elftools.dwarf.dwarf_expr import DWARFExprParser
from elftools.elf.elffile import ELFFile


BTF_MAGIC = 0xEB9F
BTF_KIND_INT = 1
BTF_KIND_PTR = 2
BTF_KIND_ARRAY = 3
BTF_KIND_STRUCT = 4
BTF_KIND_UNION = 5
BTF_KIND_ENUM = 6
BTF_KIND_TYPEDEF = 8
BTF_KIND_VOLATILE = 9
BTF_KIND_CONST = 10
BTF_KIND_RESTRICT = 11
BTF_KIND_FUNC_PROTO = 13
BTF_KIND_VAR = 14
BTF_KIND_DATASEC = 15
BTF_KIND_DECL_TAG = 17
BTF_KIND_TYPE_TAG = 18
BTF_KIND_ENUM64 = 19


def string_at(strings, offset):
    if offset >= len(strings):
        raise ValueError(f"BTF string offset {offset} is out of range")
    end = strings.find(b"\0", offset)
    if end < 0:
        raise ValueError(f"BTF string at offset {offset} is unterminated")
    return strings[offset:end].decode("utf-8", errors="replace")


def extra_record_size(kind, count):
    if kind == BTF_KIND_INT:
        return 4
    if kind == BTF_KIND_ARRAY:
        return 12
    if kind in (BTF_KIND_STRUCT, BTF_KIND_UNION):
        return count * 12
    if kind == BTF_KIND_ENUM:
        return count * 8
    if kind == BTF_KIND_FUNC_PROTO:
        return count * 8
    if kind == BTF_KIND_VAR:
        return 4
    if kind == BTF_KIND_DATASEC:
        return count * 12
    if kind == BTF_KIND_DECL_TAG:
        return 4
    if kind == BTF_KIND_ENUM64:
        return count * 12
    return 0


def resolve_btf_record(records, type_id):
    seen = set()
    while type_id and type_id not in seen:
        seen.add(type_id)
        record = records[type_id]
        if record["kind"] not in (
            BTF_KIND_TYPEDEF,
            BTF_KIND_VOLATILE,
            BTF_KIND_CONST,
            BTF_KIND_RESTRICT,
            BTF_KIND_TYPE_TAG,
        ):
            return type_id, record
        type_id = record["size_or_type"]
    return 0, None


def btf_type_size(data, records, type_id, endian, pointer_size):
    _, record = resolve_btf_record(records, type_id)
    if record is None:
        return None
    if record["kind"] == BTF_KIND_PTR:
        return pointer_size
    if record["kind"] in (
        BTF_KIND_INT,
        BTF_KIND_STRUCT,
        BTF_KIND_UNION,
        BTF_KIND_ENUM,
        BTF_KIND_ENUM64,
    ):
        return record["size_or_type"]
    if record["kind"] == BTF_KIND_ARRAY:
        element_type, _index_type, count = struct.unpack_from(
            endian + "III", data, record["payload"]
        )
        element_size = btf_type_size(
            data, records, element_type, endian, pointer_size
        )
        return None if element_size is None else element_size * count
    return None


def structure_members(
    data,
    records,
    type_id,
    strings,
    endian,
    base_bit_offset=0,
    active=None,
    pointer_size=8,
):
    members = {}
    bitfields = {}
    member_sizes = {}
    if active is None:
        active = set()
    if type_id in active:
        return members, bitfields, member_sizes

    active.add(type_id)
    record = records[type_id]
    offset = record["payload"]
    count = record["count"]
    kind_flag = record["kind_flag"]

    for index in range(count):
        member_offset = offset + index * 12
        name_offset, member_type_id, encoded_offset = struct.unpack_from(
            endian + "III", data, member_offset
        )
        name = string_at(strings, name_offset)
        bit_offset = encoded_offset & 0x00FFFFFF if kind_flag else encoded_offset
        bit_size = encoded_offset >> 24 if kind_flag else 0
        absolute_bit_offset = base_bit_offset + bit_offset

        if not name:
            nested_type_id, nested = resolve_btf_record(records, member_type_id)
            if nested is not None and nested["kind"] in (
                BTF_KIND_STRUCT,
                BTF_KIND_UNION,
            ):
                nested_members, nested_bitfields, nested_member_sizes = structure_members(
                    data,
                    records,
                    nested_type_id,
                    strings,
                    endian,
                    absolute_bit_offset,
                    active,
                    pointer_size,
                )
                members.update(nested_members)
                bitfields.update(nested_bitfields)
                member_sizes.update(nested_member_sizes)
                continue

        if bit_size:
            bitfields[name] = {
                "bit_offset": absolute_bit_offset,
                "bit_size": bit_size,
            }
        elif absolute_bit_offset % 8 == 0:
            members[name] = absolute_bit_offset // 8
            size = btf_type_size(
                data, records, member_type_id, endian, pointer_size
            )
            if size is not None:
                member_sizes[name] = size
        else:
            bitfields[name] = {
                "bit_offset": absolute_bit_offset,
                "bit_size": None,
            }

    active.remove(type_id)
    return members, bitfields, member_sizes


def structure_layout(data, records, type_id, strings, endian, pointer_size):
    record = records[type_id]
    members, bitfields, member_sizes = structure_members(
        data, records, type_id, strings, endian, pointer_size=pointer_size
    )
    layout = {
        "size": record["size_or_type"],
        "members": dict(sorted(members.items())),
    }
    if bitfields:
        layout["bitfields"] = dict(sorted(bitfields.items()))
    if member_sizes:
        layout["member_sizes"] = dict(sorted(member_sizes.items()))
    return layout


def enum_constants(data, offset, count, strings, endian, wide):
    constants = {}
    record_size = 12 if wide else 8
    for index in range(count):
        value_offset = offset + index * record_size
        name_offset = struct.unpack_from(endian + "I", data, value_offset)[0]
        name = string_at(strings, name_offset)
        if wide:
            low, high = struct.unpack_from(
                endian + "II", data, value_offset + 4
            )
            value = low | (high << 32)
        else:
            value = struct.unpack_from(endian + "i", data, value_offset + 4)[0]
        constants[name] = value
    return constants


def parse_btf(data, requested, constants_only=False, pointer_size=8):
    if len(data) < 24:
        raise ValueError("truncated BTF header")

    magic = struct.unpack_from("<H", data)[0]
    endian = "<"
    if magic != BTF_MAGIC:
        magic = struct.unpack_from(">H", data)[0]
        endian = ">"
    if magic != BTF_MAGIC:
        raise ValueError("invalid BTF magic")

    (
        _magic,
        version,
        _flags,
        header_length,
        type_offset,
        type_length,
        string_offset,
        string_length,
    ) = struct.unpack_from(endian + "HBBIIIII", data)
    if version != 1:
        raise ValueError(f"unsupported BTF version {version}")

    type_start = header_length + type_offset
    type_end = type_start + type_length
    string_start = header_length + string_offset
    string_end = string_start + string_length
    if type_end > len(data) or string_end > len(data):
        raise ValueError("BTF header references data outside the section")

    strings = data[string_start:string_end]
    records = [None]
    cursor = type_start
    while cursor < type_end:
        if cursor + 12 > type_end:
            raise ValueError("truncated BTF type record")
        name_offset, info, size_or_type = struct.unpack_from(
            endian + "III", data, cursor
        )
        count = info & 0xFFFF
        kind = (info >> 24) & 0x1F
        kind_flag = bool(info >> 31)
        payload = cursor + 12
        record_end = payload + extra_record_size(kind, count)
        if record_end > type_end:
            raise ValueError("truncated BTF type payload")

        records.append(
            {
                "count": count,
                "kind": kind,
                "kind_flag": kind_flag,
                "name_offset": name_offset,
                "payload": payload,
                "size_or_type": size_or_type,
            }
        )
        cursor = record_end

    remaining = set(requested)
    layouts = {}
    if constants_only:
        for record in records[1:]:
            kind = record["kind"]
            if kind not in (BTF_KIND_ENUM, BTF_KIND_ENUM64):
                continue
            values = enum_constants(
                data,
                record["payload"],
                record["count"],
                strings,
                endian,
                kind == BTF_KIND_ENUM64,
            )
            for name in remaining.intersection(values):
                layouts[name] = values[name]
                remaining.remove(name)
            if not remaining:
                break
    else:
        for type_id, record in enumerate(records[1:], start=1):
            if record["kind"] != BTF_KIND_STRUCT:
                continue
            name = string_at(strings, record["name_offset"])
            if name in remaining:
                layouts[name] = structure_layout(
                    data,
                    records,
                    type_id,
                    strings,
                    endian,
                    pointer_size,
                )
                remaining.remove(name)
                if not remaining:
                    break

    return layouts


def dwarf_name(die):
    attribute = die.attributes.get("DW_AT_name")
    if attribute is None:
        return ""
    value = attribute.value
    return value.decode("utf-8", errors="replace") if isinstance(
        value, bytes
    ) else str(value)


def dwarf_member_offset(attribute, expression_parser):
    if attribute is None:
        return None
    form_class = describe_form_class(attribute.form)
    if form_class == "constant":
        return int(attribute.value)
    if form_class != "exprloc":
        return None

    operations = expression_parser.parse_expr(attribute.value)
    if len(operations) == 1 and operations[0].op_name in (
        "DW_OP_constu",
        "DW_OP_plus_uconst",
    ):
        return int(operations[0].args[0])
    return None


def resolve_dwarf_die(die):
    seen = set()
    while die is not None and die.offset not in seen:
        seen.add(die.offset)
        if die.tag not in (
            "DW_TAG_const_type",
            "DW_TAG_restrict_type",
            "DW_TAG_typedef",
            "DW_TAG_volatile_type",
        ):
            return die
        die = die.get_DIE_from_attribute("DW_AT_type")
    return None


def dwarf_type_size(die, pointer_size, active=None):
    die = resolve_dwarf_die(die)
    if die is None:
        return None
    if active is None:
        active = set()
    if die.offset in active:
        return None
    size = die.attributes.get("DW_AT_byte_size")
    if size is not None:
        return int(size.value)
    if die.tag in (
        "DW_TAG_pointer_type",
        "DW_TAG_reference_type",
        "DW_TAG_rvalue_reference_type",
    ):
        return pointer_size
    if die.tag == "DW_TAG_array_type":
        element = die.get_DIE_from_attribute("DW_AT_type")
        active.add(die.offset)
        try:
            element_size = dwarf_type_size(element, pointer_size, active)
        finally:
            active.remove(die.offset)
        if element_size is None:
            return None
        count = 1
        dimensions = 0
        for child in die.iter_children():
            if child.tag != "DW_TAG_subrange_type":
                continue
            count_attribute = child.attributes.get("DW_AT_count")
            if count_attribute is not None:
                dimension = int(count_attribute.value)
            else:
                upper = child.attributes.get("DW_AT_upper_bound")
                if upper is None:
                    return None
                lower = child.attributes.get("DW_AT_lower_bound")
                lower_value = int(lower.value) if lower is not None else 0
                dimension = int(upper.value) - lower_value + 1
            if dimension < 0:
                return None
            count *= dimension
            dimensions += 1
        return element_size * count if dimensions else None
    return None


def dwarf_structure_members(
    die,
    expression_parser,
    base_offset=0,
    active=None,
    pointer_size=8,
):
    members = {}
    bitfields = {}
    member_sizes = {}
    if active is None:
        active = set()
    if die.offset in active:
        return members, bitfields, member_sizes

    active.add(die.offset)
    for child in die.iter_children():
        if child.tag != "DW_TAG_member":
            continue
        name = dwarf_name(child)
        location = dwarf_member_offset(
            child.attributes.get("DW_AT_data_member_location"),
            expression_parser,
        )
        if location is None and die.tag == "DW_TAG_union_type":
            location = 0

        if not name and location is not None:
            nested = resolve_dwarf_die(
                child.get_DIE_from_attribute("DW_AT_type")
            )
            if nested is not None and nested.tag in (
                "DW_TAG_structure_type",
                "DW_TAG_union_type",
            ):
                nested_members, nested_bitfields, nested_member_sizes = dwarf_structure_members(
                    nested,
                    expression_parser,
                    base_offset + location,
                    active,
                    pointer_size,
                )
                members.update(nested_members)
                bitfields.update(nested_bitfields)
                member_sizes.update(nested_member_sizes)
                continue

        data_bit_offset = child.attributes.get("DW_AT_data_bit_offset")
        bit_size = child.attributes.get("DW_AT_bit_size")
        if data_bit_offset is not None and bit_size is not None:
            bitfields[name] = {
                "bit_offset": base_offset * 8 + int(data_bit_offset.value),
                "bit_size": int(bit_size.value),
            }
        elif location is not None:
            members[name] = base_offset + location
            size = dwarf_type_size(
                child.get_DIE_from_attribute("DW_AT_type"), pointer_size
            )
            if size is not None:
                member_sizes[name] = size

    active.remove(die.offset)
    return members, bitfields, member_sizes


def dwarf_structure_layout(die, expression_parser, pointer_size=8):
    size_attribute = die.attributes.get("DW_AT_byte_size")
    if size_attribute is None:
        return None

    members, bitfields, member_sizes = dwarf_structure_members(
        die, expression_parser, pointer_size=pointer_size
    )

    layout = {
        "size": int(size_attribute.value),
        "members": dict(sorted(members.items())),
    }
    if bitfields:
        layout["bitfields"] = dict(sorted(bitfields.items()))
    if member_sizes:
        layout["member_sizes"] = dict(sorted(member_sizes.items()))
    return layout


def extract_dwarf_cu_layouts(
    compilation_unit,
    expression_parser,
    remaining,
    layouts,
    constants_only,
    pointer_size,
):
    """Scan one CU so its DIE locals are dead before its cache is released."""
    for die in compilation_unit.iter_DIEs():
        if constants_only and die.tag == "DW_TAG_enumerator":
            name = dwarf_name(die)
            if name not in remaining:
                continue
            value = die.attributes.get("DW_AT_const_value")
            if value is not None:
                layouts[name] = int(value.value)
                remaining.remove(name)
        elif (
            not constants_only
            and die.tag == "DW_TAG_structure_type"
            and dwarf_name(die) in remaining
        ):
            name = dwarf_name(die)
            layout = dwarf_structure_layout(
                die, expression_parser, pointer_size
            )
            if layout is not None:
                layouts[name] = layout
                remaining.remove(name)
        if not remaining:
            return True
    return False


def release_dwarf_caches(dwarf, compilation_unit):
    """Bound pyelftools memory after a CU has produced plain Python data.

    pyelftools 0.32 retains every parsed DIE through both the CompileUnit and
    DWARFInfo caches.  A DWARF-only GKI vmlinux can otherwise expand hundreds
    of megabytes of .debug_info into more memory than a hosted CI runner has.
    ``iter_CUs`` computes its next offset before yielding, so clearing these
    caches after the CU scan does not disturb sequential iteration.
    """
    compilation_unit._dielist.clear()
    compilation_unit._diemap.clear()
    dwarf._cu_cache.clear()
    dwarf._cu_offsets_map.clear()


def extract_dwarf_layouts(elf, requested, constants_only=False):
    if not elf.has_dwarf_info():
        raise ValueError("ELF has neither .BTF nor DWARF layout information")

    dwarf = elf.get_dwarf_info()
    expression_parser = DWARFExprParser(dwarf.structs)
    remaining = set(requested)
    layouts = {}
    for compilation_unit in dwarf.iter_CUs():
        try:
            try:
                pointer_size = int(compilation_unit["address_size"])
            except (KeyError, TypeError):
                pointer_size = 8
            complete = extract_dwarf_cu_layouts(
                compilation_unit,
                expression_parser,
                remaining,
                layouts,
                constants_only,
                pointer_size,
            )
        finally:
            release_dwarf_caches(dwarf, compilation_unit)
        if complete:
            return layouts
    return layouts


def extract_layouts(path, requested, constants_only=False):
    with open(path, "rb") as stream:
        elf = ELFFile(stream)
        section = elf.get_section_by_name(".BTF")
        if section is not None:
            return parse_btf(
                section.data(), requested, constants_only,
                pointer_size=elf.elfclass // 8,
            )
        return extract_dwarf_layouts(elf, requested, constants_only)


def main():
    parser = argparse.ArgumentParser(
        description="extract selected structure sizes and member offsets from BTF"
    )
    parser.add_argument(
        "--sizes-only",
        action="store_true",
        help="print a compact structure-to-size mapping",
    )
    parser.add_argument(
        "--constants",
        action="store_true",
        help="extract named integer constants from BTF enums",
    )
    parser.add_argument("elf", help="ELF image with .BTF, such as vmlinux")
    parser.add_argument("structures", nargs="+", help="structure names without 'struct'")
    args = parser.parse_args()

    try:
        layouts = extract_layouts(args.elf, args.structures, args.constants)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1

    missing = sorted(set(args.structures) - set(layouts))
    output = layouts
    if args.sizes_only and args.constants:
        parser.error("--sizes-only and --constants cannot be combined")
    if args.sizes_only:
        output = {name: layout["size"] for name, layout in layouts.items()}
    print(json.dumps(dict(sorted(output.items())), indent=2, sort_keys=True))
    if missing:
        print("missing structures: " + ", ".join(missing), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
