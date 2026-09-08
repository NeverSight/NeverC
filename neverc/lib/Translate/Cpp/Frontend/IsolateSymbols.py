#!/usr/bin/env python3
"""Isolate frontend globals in the hash-pinned LLVM release sources."""

import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser()
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()

# These global C++ identifiers need file-specific handling: the intrinsic
# helper shares a spelling with a class member, Debugify's type occurs in public
# signatures, and PointerBounds shares a spelling with unrelated analysis
# members and parameters. Patch only the extracted private release source.
def replace_once(path, before, after):
    text = path.read_text(encoding="utf-8")
    if after in text:
        return
    if text.count(before) != 1:
        raise SystemExit("Unexpected pinned LLVM source while isolating " + str(path))
    path.write_text(text.replace(before, after), encoding="utf-8")


def isolate_pointer_bounds(path):
    before = ("struct PointerBounds {\n"
              "  TrackingVH<Value> Start;\n"
              "  TrackingVH<Value> End;\n"
              "  Value *StrideToCheck;\n"
              "};")
    after = ("struct neverc_cpp_PointerBounds {\n"
             "  TrackingVH<Value> Start;\n"
             "  TrackingVH<Value> End;\n"
             "  Value *StrideToCheck;\n"
             "};\n"
             "using PointerBounds = neverc_cpp_PointerBounds;")
    text = path.read_text(encoding="utf-8")
    counts = (text.count(before), text.count(after))
    if counts == (1, 0):
        block = before
    elif counts == (0, 1):
        block = after
    else:
        raise SystemExit("Unexpected pinned LLVM PointerBounds declaration in " + str(path))

    # The pinned file has six uses after the record declaration. An alias keeps
    # those uses intact while changing the actual record's mangled identity.
    # Neither an additional declaration nor a partial previous rewrite is valid.
    remainder = text.replace(block, "", 1)
    if (len(re.findall(r"\bPointerBounds\b", remainder)) != 6
            or re.search(r"\b(?:struct|class|union)\b[^;{}]*\bPointerBounds\b", remainder)
            or re.search(r"\bneverc_cpp_PointerBounds\b", remainder)
            or re.search(r"\busing\s+PointerBounds\s*=", remainder)
            or re.search(r"^\s*#\s*(?:define|undef)\s+PointerBounds\b", remainder, re.M)):
        raise SystemExit("Unexpected pinned LLVM PointerBounds uses in " + str(path))
    if block == before:
        path.write_text(text.replace(before, after, 1), encoding="utf-8")


def isolate_math_calls(source_root):
    # The integer/mixed cmath templates convert their arguments to double
    # before calling the C math overload. Make those same conversions at the
    # six pinned call sites instead of emitting shared global MSVC templates.
    # Keep the original getter, negation and float/half conversion order.
    files = (
        ("llvm/lib/Support/Signals.cpp", (
            ("std::log10(Depth)", "std::log10(static_cast<double>(Depth))"),
        )),
        ("llvm/lib/Support/APFixedPoint.cpp", (
            ("std::pow(2, Sema.getLsbWeight())",
             "std::pow(2.0, static_cast<double>(Sema.getLsbWeight()))"),
            ("std::pow(2, -DstFXSema.getLsbWeight())",
             "std::pow(2.0, static_cast<double>(-DstFXSema.getLsbWeight()))"),
            ("std::pow(2, DstFXSema.getLsbWeight())",
             "std::pow(2.0, static_cast<double>(DstFXSema.getLsbWeight()))"),
        )),
        ("llvm/lib/Analysis/ConstantFolding.cpp", (
            ("std::pow(Op1V.convertToFloat(), Exp)",
             "std::pow(static_cast<double>(Op1V.convertToFloat()), "
             "static_cast<double>(Exp))"),
            ("std::pow(Op1V.convertToDouble(), Exp)",
             "std::pow(Op1V.convertToDouble(), static_cast<double>(Exp))"),
        )),
    )
    updates = []
    for relative, replacements in files:
        path = source_root / relative
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            raise SystemExit("Unexpected pinned LLVM math calls in " + str(path)) from error
        counts = [(text.count(before), text.count(after))
                  for before, after in replacements]
        if all(count == (0, 1) for count in counts):
            continue
        if not all(count == (1, 0) for count in counts):
            raise SystemExit("Unexpected pinned LLVM math calls in " + str(path))
        for before, after in replacements:
            text = text.replace(before, after, 1)
        updates.append((path, text))
    # Validate all three inputs before changing any math source. Each file is
    # either wholly original or wholly rewritten; partial edits fail closed.
    for path, text in updates:
        path.write_text(text, encoding="utf-8")


intrinsics = args.source / "llvm/lib/IR/IntrinsicInst.cpp"
for before, after in [
    ("constexpr bool isVPIntrinsic(", "constexpr bool neverc_cpp_isVPIntrinsic("),
    ("return ::isVPIntrinsic(", "return ::neverc_cpp_isVPIntrinsic("),
    ("if (::isVPIntrinsic(", "if (::neverc_cpp_isVPIntrinsic("),
]:
    replace_once(intrinsics, before, after)
debugify = args.source / "llvm/include/llvm/Transforms/Utils/Debugify.h"
replace_once(debugify, "#define LLVM_TRANSFORMS_UTILS_DEBUGIFY_H",
             "#define LLVM_TRANSFORMS_UTILS_DEBUGIFY_H\n"
             "// Private NeverC frontend ABI: this upstream type is global.\n"
             "#define DebugInfoPerPass neverc_cpp_DebugInfoPerPass")
isolate_pointer_bounds(args.source / "llvm/lib/Transforms/Utils/LoopUtils.cpp")
isolate_math_calls(args.source)

symbols = set()
for header in sorted((args.source / "llvm/include/llvm-c").glob("*.h")):
    source = header.read_text(encoding="utf-8")
    symbols.update(re.findall(r"\b(LLVM[A-Z][A-Za-z0-9_]*)\s*\(", source))
    symbols.update(re.findall(r"\bllvm_blake3_[A-Za-z0-9_]+\b", source))

# BLAKE3's public and internal APIs include architecture-specific names. Scan
# the upstream prefix table so one inventory covers all supported build hosts.
blake = args.source / "llvm/lib/Support/BLAKE3/llvm_blake3_prefix.h"
symbols.update(re.findall(r"\bllvm_blake3_[A-Za-z0-9_]+\b", blake.read_text(encoding="utf-8")))
core = (args.source / "llvm/include/llvm-c/Core.h").read_text(encoding="utf-8")
classes = core.split("#define LLVM_FOR_EACH_VALUE_SUBCLASS(macro)", 1)[1].split("\n\n", 1)[0]
symbols.update("LLVMIsA" + name for name in re.findall(r"\bmacro\((\w+)\)", classes))
symbols.update({
    "llvm_regcomp", "llvm_regerror", "llvm_regexec", "llvm_regfree", "llvm_strlcpy",
    "UseNewDbgInfoFormat", "WriteNewDbgInfoFormat", "WriteNewDbgInfoFormatToBitcode",
    "WriteNewDbgInfoFormatToBitcode2", "PreserveInputDbgFormat", "UseDerefAtPointSemantics",
    "__crashreporter_info__",
    # Windows Signals.inc defines/registers this with C linkage inside llvm.
    # Renaming the namespace alone leaves its process-wide symbol unchanged.
    "HandleAbort",
    # UCRT's selectany default floating-point environment is a value object.
    # Prefix its header definition and FE_DFL_ENV references together; the CRT
    # fesetenv/feclearexcept/fetestexcept entry points retain their normal ABI.
    "_Fenv1",
    # shlguid.h defines these immutable GUID values with C linkage and selectany.
    # Keep the SDK layout/initializers and rewrite identifier uses, including
    # expansion of the SID_SUrlHistory alias, into the private frontend ABI.
    "CLSID_CUrlHistory", "CLSID_CUrlHistoryBoth",
})
if len(symbols) < 900:
    raise SystemExit("Unexpected LLVM 20.1.8 symbol inventory; review the pinned source")
text = [
    "// Generated for the SHA256-pinned LLVM 20.1.8 source. Do not edit.",
    "#ifndef NEVERC_CPP_PRIVATE_PREFIX_H",
    "#define NEVERC_CPP_PRIVATE_PREFIX_H",
    "#define llvm neverc_cpp_llvm",
]
text.extend(f"#define {name} neverc_cpp_{name}" for name in sorted(symbols))
text.append("#endif\n")
result = "\n".join(text)
args.output.parent.mkdir(parents=True, exist_ok=True)
if not args.output.exists() or args.output.read_text(encoding="utf-8") != result:
    args.output.write_text(result, encoding="utf-8")

# These upstream Support sources carry additional notices in their initial
# comment blocks. Preserve the exact text beside the project/BLAKE3 licenses.
notice_files = [
    "llvm/lib/Support/MD5.cpp", "llvm/lib/Support/xxhash.cpp",
    "llvm/lib/Support/UnicodeNameToCodepointGenerated.cpp",
    "llvm/lib/Support/ConvertUTF.cpp", "llvm/lib/Support/regex2.h",
    "llvm/lib/Support/regutils.h", "llvm/lib/Support/regex_impl.h",
    "llvm/lib/Support/regcomp.c", "llvm/lib/Support/regexec.c",
    "llvm/lib/Support/regerror.c", "llvm/lib/Support/regfree.c",
]
notices = ["Additional notices from the pinned LLVM 20.1.8 sources.\n"]
for name in notice_files:
    source = (args.source / name).read_text(encoding="utf-8")
    prefix = re.match(r"(?:\s+|/\*.*?\*/|//[^\n]*(?:\n|$))*", source, re.S)[0]
    if not prefix.strip():
        raise SystemExit("Missing expected upstream notice: " + name)
    notices.extend(["\n===== " + name + " =====\n", prefix])
notice_path = args.output.parent / "NeverCCppThirdPartyNotices.txt"
notice_text = "\n".join(notices)
if not notice_path.exists() or notice_path.read_text(encoding="utf-8") != notice_text:
    notice_path.write_text(notice_text, encoding="utf-8")
print(f"Isolated llvm namespace and {len(symbols)} C/global identifiers")
