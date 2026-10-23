#!/usr/bin/env python3
"""Install/check native system and tile signatures in the pruned LLVM tables.

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
    "vmread_reg": ("STRUCT2", "I64", "I8", "I64", "I64"),
    "vmwrite_mem": ("I8", "I64", "PTR"),
    "hreset": ("Done", "I32"),
    "invlpgb": ("Done", "I64", "I32", "I32"),
    "invlpgb_addr32": ("Done", "I32", "I32", "I32"),
    "tlbsync": ("Done",),
    "lkgs_reg": ("Done", "I16"),
    "lkgs_mem": ("Done", "PTR"),
}
for operation in ("vmload", "vmsave"):
    DECLARATIONS[operation] = ("Done", "I64")
DECLARATIONS["invlpga"] = ("Done", "I64", "I32")
for operation in ("vmload", "vmsave"):
    DECLARATIONS[operation + "_addr32"] = ("Done", "I32")
DECLARATIONS["invlpga_addr32"] = ("Done", "I32", "I32")
DECLARATIONS["vmfunc_eptp_switch"] = ("Done", "I32")
for operation in ("stgi", "clgi", "swapgs", "clac", "stac", "hlt"):
    DECLARATIONS[operation] = ("Done",)
DECLARATIONS["lsl"] = ("I32", "I32")
DECLARATIONS["invd"] = ("Done",)
for operation in ("encls", "enclv"):
    DECLARATIONS[operation] = ("STRUCT5", "I64", "I64", "I64", "I64", "I8",
                               "I64", "I64", "I64", "I64")
for operation in ("verr", "verw", "lldt", "ltr", "lmsw"):
    result = "I8" if operation in ("verr", "verw") else "Done"
    DECLARATIONS[f"{operation}_reg"] = (result, "I16")
    DECLARATIONS[f"{operation}_mem"] = (result, "PTR")
for operation in ("sldt", "str", "smsw"):
    for bits in (16, 32, 64):
        DECLARATIONS[f"{operation}_reg{bits}"] = (f"I{bits}",)
    DECLARATIONS[f"{operation}_mem"] = ("Done", "PTR")
for operation in ("lar", "lsl"):
    for bits in (16, 32, 64):
        for source in ("reg", "mem"):
            name = f"{operation}_{source}{bits}"
            operand = "I16" if source == "reg" else "PTR"
            DECLARATIONS[name] = ("STRUCT2", f"I{bits}", "I8", operand, f"I{bits}")
PROTOTYPE = "llvm.x86.vmread"
REGISTER_SIGNATURES = {
    "readcr": ("I64", "I32"), "readdr": ("I64", "I32"),
    "writecr": ("Done", "I32", "I64"), "writedr": ("Done", "I32", "I64"),
}
AMX_SIGNATURES = {
    **{name: (("Done", "I8", "I8", "I8"), "llvm.x86.tdpbssd")
       for name in ("tdpbf8ps", "tdpbhf8ps", "tdphbf8ps", "tdphf8ps", "tmmultf32ps")},
    **{name: (("Done", "I8", "PTR", "I64"), "llvm.x86.tileloadd64")
       for name in ("tileloaddrs64", "tileloaddrst164")},
}
ROW_IMMEDIATES = set()
for suffix, result in (("tcvtrowd2ps", ("V16", "F32")),
                       ("tcvtrowps2bf16h", ("V32", "BF16")),
                       ("tcvtrowps2bf16l", ("V32", "BF16")),
                       ("tcvtrowps2phh", ("V32", "F16")),
                       ("tcvtrowps2phl", ("V32", "F16")),
                       ("tilemovrow", ("V16", "I32"))):
    for immediate in (False, True):
        name = suffix + ("i" if immediate else "")
        AMX_SIGNATURES[name] = (result + ("I8", "I32"), "llvm.x86.tileloadd64")
        if immediate:
            ROW_IMMEDIATES.add(name)


def immediate_pair_attribute(impl, attributes):
    # The existing tile-load recipe owns arg0's ImmArg and conservative
    # function effects. Row-immediate APIs additionally require arg1 constant.
    body = ("      AS[0] = {1, getIntrinsicArgAttributeSet(C, 0)};\n"
            "      AS[1] = {2, getIntrinsicArgAttributeSet(C, 0)};\n"
            "      AS[2] = {AttributeList::FunctionIndex, getIntrinsicFnAttributeSet(C, 7)};\n"
            "      NumAttrs = 3;\n      break;\n    }\n")
    existing = re.search(r"    case (\d+): \{\n" + re.escape(body), impl)
    if existing:
        return existing.group(1), impl
    index = max(int(value) for value, _ in attributes) + 1
    anchor = "    case 108: {\n"
    if impl.count(anchor) != 1:
        raise ValueError("expected one conservative immediate-argument recipe")
    return str(index), impl.replace(anchor, f"    case {index}: {{\n" + body + anchor, 1)


def encode_signature(signature, impl):
    codes = {name: int(value) for name, value in
             re.findall(r"\bIIT_(\w+) = (\d+),", impl)}
    values = [codes[name] for name in signature]
    if len(values) <= 7 and all(value < 16 for value in values):
        return hex(sum(value << (4 * i) for i, value in enumerate(values))), impl
    start = impl.index("static const unsigned char IIT_LongEncodingTable[] = {")
    end = impl.index("};", start)
    table = impl[impl.index("{", start) + 1:end]
    tokens = re.sub(r"/\*.*?\*/", "", table)
    data = [int(value) for value in tokens.split(",") if value.strip()]
    if not data or data[-1] != 255:
        raise ValueError("long IIT table lacks its sentinel")
    sequence = values + [codes["Done"]]
    for offset in range(len(data) - len(sequence)):
        if data[offset:offset + len(sequence)] == sequence:
            return f"(1U<<31) | {offset}", impl
    offset = len(data) - 1
    sentinel = re.search(r"\b255(?=\s*$)", table)
    if not sentinel:
        raise ValueError("long IIT table sentinel has an unexpected spelling")
    addition = f"/* {offset} */ " + ", ".join(map(str, sequence)) + ",\n  255"
    updated = table[:sentinel.start()] + addition + table[sentinel.end():]
    return f"(1U<<31) | {offset}", impl[:impl.index("{", start) + 1] + updated + impl[end:]


def encoding_value(text):
    long_encoding = re.fullmatch(r"\(1U<<31\)\s*\|\s*(\d+)", text)
    return (1 << 31) | int(long_encoding.group(1)) if long_encoding else int(text, 0)


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
    immediate_pair, updated_impl = immediate_pair_attribute(impl, attributes)
    declarations = {suffix: (signature, PROTOTYPE)
                    for suffix, signature in DECLARATIONS.items()}
    declarations.update(AMX_SIGNATURES)
    for suffix, (signature, prototype) in declarations.items():
        source = names.index(prototype)
        attribute = immediate_pair if suffix in ROW_IMMEDIATES else attributes[source][0]
        cpp, name = "x86_" + suffix, "llvm.x86." + suffix.replace("_", ".")
        encoding, updated_impl = encode_signature(signature, updated_impl)
        if name in names:
            index = names.index(name)
            if (cpp_names.get(cpp) != name or encoding_value(types[index]) != encoding_value(encoding) or
                    attributes[index][0] != attribute or overloads[index + 1]):
                raise ValueError(f"native signature metadata disagrees with its schema: {name}")
            continue
        if cpp in cpp_names:
            raise ValueError(f"intrinsic enum/name mismatch: {cpp}")
        additions.append({"cpp": cpp, "name": name, "iit": encoding,
                          "attr_val": attribute, "overloaded": False})
    # Like INT, CR/DR accesses have conservative effects and an immediate first
    # argument. Older NeverC metadata omitted the immediate restriction.
    register_attr = attributes[names.index("llvm.x86.int")][0]
    for suffix, signature in REGISTER_SIGNATURES.items():
        name = "llvm.x86." + suffix
        index = names.index(name)
        encoding, _ = encode_signature(signature, impl)
        if int(types[index], 0) != int(encoding, 0) or overloads[index + 1]:
            raise ValueError(f"invalid system register signature: {name}")
        updated_impl = re.sub(r"^    \d+, // " + re.escape(name) + r"$",
                              f"    {register_attr}, // {name}", updated_impl, flags=re.MULTILINE)
    if updated_impl != impl:
        if not write:
            raise ValueError("native signature or immediate attributes are stale; run with --write")
        SORTER.INC_PATH.write_text(updated_impl)
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
