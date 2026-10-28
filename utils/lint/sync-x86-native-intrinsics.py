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
    "fnop": ("Done",),
    "wrmsrns": ("Done", "I32", "I64"),
    "rdmsrlist": ("I64", "PTR", "PTR", "I64"),
    "wrmsrlist": ("I64", "PTR", "PTR", "I64"),
    "vmread_reg": ("STRUCT2", "I64", "I8", "I64", "I64"),
    "vmwrite_mem": ("I8", "I64", "PTR"),
    "hreset": ("Done", "I32"),
    "invlpgb": ("Done", "I64", "I32", "I32"),
    "invlpgb_addr32": ("Done", "I32", "I32", "I32"),
    "tlbsync": ("Done",),
    "skinit": ("Done", "I32"),
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
# ADX value APIs share the generic addcarry attributes, with no memory effects.
VALUE_SIGNATURES = {
    f"{op}_{bits}": (("STRUCT2", "I8", f"I{bits}", "I8", f"I{bits}", f"I{bits}"),
                     f"llvm.x86.addcarry.{bits}")
    for op in ("adcx", "adox") for bits in (32, 64)
}
PROTOTYPE = "llvm.x86.vmread"
EXISTING_FIRST_IMMEDIATE_SIGNATURES = {
    "readcr": ("I64", "I32"), "readdr": ("I64", "I32"),
    "writecr": ("Done", "I32", "I64"), "writedr": ("Done", "I32", "I64"),
    "xabort": ("Done", "I8"),
}
FIRST_IMMEDIATE_SIGNATURES = {
    **{name + "_bounded_state": ("STRUCT5", "I64", "I64", "I64", "I64", "I16",
                                  "I64", "I64", "I64", "I64", "I16")
       for name in ("enclu", "getsec")},
    "getsec_bounded": ("STRUCT4", "I64", "I64", "I64", "I64",
                        "I64", "I64", "I64", "I64"),
    "enclu_bounded": ("STRUCT5", "I64", "I64", "I64", "I64", "I8",
                       "I64", "I64", "I64", "I64"),
}
ADDRESS_SIGNATURES = {
    "lkgs_address": (("Done", "I64", "I32", "I32"), (1, 2)),
    "lar_mem16_address": (("STRUCT2", "I16", "I8", "I64", "I32", "I32", "I16"), (1, 2)),
    "lar_mem32_address": (("STRUCT2", "I32", "I8", "I64", "I32", "I32", "I32"), (1, 2)),
    "lar_mem64_address": (("STRUCT2", "I64", "I8", "I64", "I32", "I32", "I64"), (1, 2)),
    "lsl_mem16_address": (("STRUCT2", "I16", "I8", "I64", "I32", "I32", "I16"), (1, 2)),
    "lsl_mem32_address": (("STRUCT2", "I32", "I8", "I64", "I32", "I32", "I32"), (1, 2)),
    "lsl_mem64_address": (("STRUCT2", "I64", "I8", "I64", "I32", "I32", "I64"), (1, 2)),
    **{name + "_address": (("I8", "I64", "I64", "I32", "I32"), (2, 3))
       for name in ("vmread", "vmwrite_mem", "invept", "invvpid")},
    **{name + "_address": (("I8", "I64", "I32", "I32"), (1, 2))
       for name in ("vmclear", "vmptrld", "vmxon")},
    "vmptrst_address": (("Done", "I64", "I32", "I32"), (1, 2)),
}
for operation in ("verr_mem", "verw_mem", "sldt_mem", "str_mem", "smsw_mem",
                  "lldt_mem", "ltr_mem", "lmsw_mem", "sgdt", "sidt", "lgdt",
                  "lidt", "invlpg"):
    result = "I8" if operation in ("verr_mem", "verw_mem") else "Done"
    ADDRESS_SIGNATURES[operation + "_address"] = ((result, "I64", "I32", "I32"), (1, 2))


# APX native atomic addresses have explicit width and segment controls.
for bits in (32, 64):
    word = f"I{bits}"
    for operation in ("aadd", "aand", "aor", "axor"):
        ADDRESS_SIGNATURES[f"{operation}{bits}_address"] = (("Done", "I64", word, "I32", "I32"), (2, 3))
    ADDRESS_SIGNATURES[f"cmpccxadd{bits}_address"] = ((word, "I64", word, word, "I32", "I32", "I32"), (3, 4, 5))

# Exact legacy/VEX transactions; 32-bit code mode has a natural-width offset.
for bits in (32, 64):
    for operation in ("aadd", "aand", "aor", "axor", "cmpccxadd"):
        ADDRESS_SIGNATURES[f"{operation}{bits}_legacy_address"] = ADDRESS_SIGNATURES[f"{operation}{bits}_address"]
for operation in ("aadd", "aand", "aor", "axor"):
    ADDRESS_SIGNATURES[f"{operation}32_legacy_address32"] = (("Done", "I32", "I32", "I32", "I32"), (2, 3))

# Native compact-memory vector carriers preserve all six opcode families.
for bits in (128, 256, 512):
    vector = (f"V{bits // 32}", "I32")
    args = ("PTR", "I64") + vector + ("I32", "I32")
    ADDRESS_SIGNATURES[f"avx512_compressstore{bits}_native"] = (("Done",) + args, (3, 4))
    ADDRESS_SIGNATURES[f"avx512_expandload{bits}_native"] = (vector + args + ("I32",), (3, 4, 5))

# Four-source arithmetic takes each group member as an independent SSA value.
for operation in ("v4fmaddps", "v4fnmaddps", "v4fmaddss", "v4fnmaddss", "vp4dpwssd", "vp4dpwssds"):
    vector = ("V4" if operation in ("v4fmaddss", "v4fnmaddss") else "V16", "I32")
    ADDRESS_SIGNATURES[f"avx512_{operation}_native"] = (vector + ("PTR",) + vector * 5 + ("I16", "I32", "I32"), (7, 8))

# MPX uses the same constant-argument machinery for bounds-bank indices and
# address encoding controls. Effects remain conservative for disabled/faulting
# native operations as well as successful bounds accesses.
ADDRESS_SIGNATURES.update({
    "bnd_branch": (("Done", "I32"), (0,)),
    "bndmov_reg": (("Done", "I32", "I32"), (0, 1)),
    **{name: (("Done", "I32", "I64"), (0,))
       for name in ("bndcl_reg", "bndcu_reg", "bndcn_reg")},
    **{name: (("Done", "I32", "I64", "I32", "I32"), (0, 2, 3))
       for name in ("bndmov_load", "bndmov_store", "bndmov_store_lock",
                    "bndcl_address", "bndcu_address", "bndcn_address")},
    **{name: (("Done", "I32", "I64", "I64", "I32", "I32"), (0, 3, 4))
       for name in ("bndmk", "bndldx", "bndstx")},
})

# Explicit AMX addressing and row-register class controls.
for operation in ("ldtilecfg", "sttilecfg"):
    ADDRESS_SIGNATURES[operation + "_address"] = (("Done", "I64", "I32", "I32", "I32"), (1, 2, 3))
for operation in ("tileloadd64", "tileloaddt164", "tileloaddrs64", "tileloaddrst164", "tilestored64"):
    ADDRESS_SIGNATURES[operation + "_address"] = (("Done", "I8", "I64", "I64", "I32", "I32", "I32"), (0, 3, 4, 5))
for operation, result in (("tcvtrowd2ps", ("V16", "F32")),
                          ("tcvtrowps2bf16h", ("V32", "BF16")),
                          ("tcvtrowps2bf16l", ("V32", "BF16")),
                          ("tcvtrowps2phh", ("V32", "F16")),
                          ("tcvtrowps2phl", ("V32", "F16")),
                          ("tilemovrow", ("V16", "I32"))):
    ADDRESS_SIGNATURES[operation + "_reg"] = (result + ("I8", "I32", "I32"), (0, 2))

# Complete natural-width REP state. Address widths are controls, not operand
# truncations; the original instruction owns partial-register completion.
for bits in (64, 32):
    word = f"I{bits}"
    for operation in ("movs", "stos", "lods", "cmps", "scas", "ins", "outs"):
        compare = operation in ("cmps", "scas")
        source = operation in ("movs", "lods", "cmps", "outs")
        port = operation in ("ins", "outs")
        words = 3 if operation in ("movs", "lods", "cmps") else 2
        results = (word,) * words + (("I16",) if compare else ())
        inputs = (word, word, "I16" if port else word, "I8")
        if compare:
            inputs += ("I16",)
        first = len(inputs)
        controls = 2 + int(source) + int(compare)
        inputs += ("I32",) * controls
        name = "rep_" + operation + "_state" + ("32" if bits == 32 else "")
        ADDRESS_SIGNATURES[name] = ((f"STRUCT{len(results)}",) + results + inputs,
                                    tuple(range(first, first + controls)))

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


def immediate_arguments_attribute(impl, indices):
    # Preserve conservative instruction effects and add only these ImmArg
    # positions. AttributeList uses one-based parameter indices.
    body = "".join(f"      AS[{i}] = {{{arg + 1}, getIntrinsicArgAttributeSet(C, 0)}};\n"
                   for i, arg in enumerate(indices))
    body += (f"      AS[{len(indices)}] = {{AttributeList::FunctionIndex, "
             "getIntrinsicFnAttributeSet(C, 7)};\n"
             f"      NumAttrs = {len(indices) + 1};\n      break;\n    }}\n")
    start = impl.index("switch(IntrinsicsToAttributesMap[id - 1])")
    end = impl.index("return AttributeList::get", start)
    recipes = impl[start:end]
    existing = re.search(r"    case (\d+): \{\n" + re.escape(body), recipes)
    if existing:
        return existing.group(1), impl
    index = max(int(value) for value in re.findall(r"    case (\d+): \{", recipes)) + 1
    anchor = "    case 108: {\n"
    if impl.count(anchor) != 1:
        raise ValueError("expected one conservative immediate-argument recipe")
    return str(index), impl.replace(anchor, f"    case {index}: {{\n" + body + anchor, 1)


def secure_launch_attribute(impl):
    # The full fork's SKINIT contract has no default willreturn/nosync/nofree/
    # nocallback promises. Allocate the function and list recipes together.
    start = impl.index("static AttributeSet getIntrinsicFnAttributeSet")
    end = impl.index("AttributeList Intrinsic::getAttributes", start)
    functions = impl[start:end]
    body = "    return AttributeSet::get(C, {\n" + "".join(
        f"      Attribute::get(C, Attribute::{name}),\n"
        for name in ("NoUnwind", "NoReturn", "Convergent", "NoDuplicate")) + "    });\n"
    existing = re.search(r"  case (\d+):\n" + re.escape(body), functions)
    if existing:
        function = existing.group(1)
    else:
        function = str(max(int(v) for v in re.findall(r"  case (\d+):", functions)) + 1)
        closing = functions.rindex("  }\n}")
        functions = (functions[:closing] + f"  case {function}:\n" + body +
                     functions[closing:])
        impl = impl[:start] + functions + impl[end:]
    body = ("      AS[0] = {AttributeList::FunctionIndex, "
            f"getIntrinsicFnAttributeSet(C, {function})}};\n"
            "      NumAttrs = 1;\n      break;\n    }\n")
    start = impl.index("switch(IntrinsicsToAttributesMap[id - 1])")
    end = impl.index("return AttributeList::get", start)
    recipes = impl[start:end]
    existing = re.search(r"    case (\d+): \{\n" + re.escape(body), recipes)
    if existing:
        return existing.group(1), impl
    recipe = str(max(int(v) for v in re.findall(r"    case (\d+): \{", recipes)) + 1)
    impl = impl[:start] + recipes.replace(
        "    case 108: {\n", f"    case {recipe}: {{\n" + body + "    case 108: {\n", 1) + impl[end:]
    return recipe, impl


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
    immediate_pair, updated_impl = immediate_arguments_attribute(impl, (0, 1))
    launch_attribute, updated_impl = secure_launch_attribute(updated_impl)
    address_attributes = {}
    for indices in sorted({indices for _, indices in ADDRESS_SIGNATURES.values()}):
        attribute, updated_impl = immediate_arguments_attribute(updated_impl, indices)
        address_attributes[indices] = attribute
    declarations = {suffix: (signature, PROTOTYPE)
                    for suffix, signature in DECLARATIONS.items()}
    declarations.update(AMX_SIGNATURES)
    declarations.update(VALUE_SIGNATURES)
    declarations.update({suffix: (signature, PROTOTYPE)
                         for suffix, (signature, _) in ADDRESS_SIGNATURES.items()})
    declarations.update({suffix: (signature, "llvm.x86.int")
                         for suffix, signature in FIRST_IMMEDIATE_SIGNATURES.items()})
    for suffix, (signature, prototype) in declarations.items():
        source = names.index(prototype)
        attribute = immediate_pair if suffix in ROW_IMMEDIATES else attributes[source][0]
        if suffix in ADDRESS_SIGNATURES:
            attribute = address_attributes[ADDRESS_SIGNATURES[suffix][1]]
        if suffix == "skinit":
            attribute = launch_attribute
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
    # Like INT, CR/DR accesses and XABORT have conservative effects and an
    # immediate first argument. Older CR/DR metadata omitted that restriction.
    immediate_attr = attributes[names.index("llvm.x86.int")][0]
    for suffix, signature in EXISTING_FIRST_IMMEDIATE_SIGNATURES.items():
        name = "llvm.x86." + suffix
        index = names.index(name)
        encoding, _ = encode_signature(signature, impl)
        if int(types[index], 0) != int(encoding, 0) or overloads[index + 1]:
            raise ValueError(f"invalid existing immediate signature: {name}")
        updated_impl = re.sub(r"^    \d+, // " + re.escape(name) + r"$",
                              f"    {immediate_attr}, // {name}", updated_impl, flags=re.MULTILINE)
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
