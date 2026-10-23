#!/usr/bin/env python3
"""Install/check native system signatures in the pruned LLVM tables.

These APIs have fixed signatures, no overload parameters, and conservative
memory effects. Keep existing intrinsic identities,
type encodings, overload bits, attributes and DAGISel references together when
inserting their new IDs. This generator does not create machine opcodes.
"""

import argparse
import importlib.util
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "sort_x86_intrinsics", Path(__file__).with_name("sort-x86-intrinsics.py"))
SORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SORTER)
ENUMS = ROOT / "llvm/include/llvm/IR/IntrinsicEnums.inc"
# Result followed by parameters, in LLVM's fixed IIT scalar/pointer vocabulary.
DECLARATIONS = {
    "invept": ("I8", "I64", "PTR"),
    "invvpid": ("I8", "I64", "PTR"),
    "clts": ("Done",),
    "wrmsrns": ("Done", "I32", "I64"),
    "rdmsrlist": ("I64", "PTR", "PTR", "I64"),
    "wrmsrlist": ("I64", "PTR", "PTR", "I64"),
}
PROTOTYPE = "llvm.x86.vmread"


def encode_signature(signature, impl):
    codes = {name: int(value) for name, value in
             re.findall(r"\bIIT_(\w+) = (\d+),", impl)}
    values = [codes[name] for name in signature]
    if len(values) > 7 or any(value >= 16 for value in values):
        raise ValueError("native signature needs a long IIT encoding")
    return hex(sum(value << (4 * i) for i, value in enumerate(values)))


def synchronize(write):
    enum_text = SORTER.ENUM_PATH.read_text()
    impl = SORTER.INC_PATH.read_text()
    enums = ENUMS.read_text()
    names = SORTER.parse_name_table(impl)
    types = SORTER.parse_iit_table(impl)
    attributes = SORTER.parse_attr_map(impl)
    overloads = SORTER.parse_overload_table(impl)
    enum_entries = SORTER.parse_enum(enum_text)
    current_count = int(re.search(r"num_intrinsics = (\d+)", enums).group(1))
    if current_count != len(names) + 1 or len(names) != len(types) or len(names) != len(attributes):
        raise ValueError("intrinsic metadata table lengths disagree")
    source = names.index(PROTOTYPE)
    if overloads[source + 1]:
        raise ValueError("the VMREAD signature must not be overloaded")
    additions = []
    cpp_names = dict(enum_entries)
    for suffix, signature in DECLARATIONS.items():
        cpp, name = "x86_" + suffix, "llvm.x86." + suffix
        encoding = encode_signature(signature, impl)
        if name in names:
            index = names.index(name)
            if (cpp_names.get(cpp) != name or int(types[index], 0) != int(encoding, 0) or
                    attributes[index][0] != attributes[source][0] or overloads[index + 1]):
                raise ValueError(f"native signature metadata disagrees with its schema: {name}")
            continue
        if cpp in cpp_names:
            raise ValueError(f"intrinsic enum/name mismatch: {cpp}")
        additions.append({"cpp": cpp, "name": name, "iit": encoding,
                          "attr_val": attributes[source][0], "overloaded": False})
    if not additions:
        return
    if not write:
        raise ValueError("native intrinsic declarations are missing; run with --write")

    new_enum, new_impl, _ = SORTER.sort_x86_intrinsics(additions)
    old_names = SORTER.parse_enum_names(enum_text)
    new_names = SORTER.parse_enum_names(new_enum)
    remap = SORTER._build_remap(old_names, new_names)
    dag, _ = SORTER._patch_dagisel(SORTER.DAGISEL_PATH.read_text(), remap)
    enums = re.sub(r"num_intrinsics = \d+",
                   f"num_intrinsics = {current_count + len(additions)}", enums, count=1)
    SORTER.ENUM_PATH.write_text(new_enum)
    SORTER.INC_PATH.write_text(new_impl)
    SORTER.DAGISEL_PATH.write_text(dag)
    ENUMS.write_text(enums)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    try:
        synchronize(args.write)
    except ValueError as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
