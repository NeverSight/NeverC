#!/usr/bin/env python3
"""Reject private LLVM definitions or references that could bind the host ABI."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True


def symbol_rows(output):
    for line in output.splitlines():
        if not line or line.endswith(":"):
            continue
        fields = line.split()
        if len(fields) < 2 or len(fields[1]) != 1:
            raise ValueError("Unexpected llvm-nm output during ABI isolation audit")
        yield fields[0], fields[1]


def is_undefined(kind):
    # Lowercase w/v denote undefined weak symbols, unlike defined W/V.
    return kind in ("U", "w", "v")


def nm_output(nm, paths, *options):
    return subprocess.run(
        [nm, "--extern-only", *options, *map(str, paths)],
        text=True, encoding="utf-8", errors="strict", capture_output=True,
        check=True).stdout


def host_archives(directory, private_archive):
    def fail_walk(error):
        raise error

    result = []
    private_archive = private_archive.resolve()
    for parent, directories, files in os.walk(
            directory, followlinks=False, onerror=fail_walk):
        directories[:] = sorted(name for name in directories
                                if name != "_deps" and not
                                (Path(parent) / name).is_symlink())
        for name in sorted(files):
            path = Path(parent) / name
            if (path.suffix.lower() in (".a", ".lib")
                    and path.resolve() != private_archive):
                result.append(path)
    if not result:
        raise ValueError("No host archives found for the builtin C++ ABI audit")
    return result


def microsoft_std_entity(demangled):
    """Find the declared entity, ignoring its return and argument types.

    MSVC demangling prefixes a declaration with access/calling-convention and
    return-type tokens. The last token before its parameter list is the name;
    spaces inside template arguments and conversion operators belong to it.
    A free function merely taking/returning a std type therefore cannot pass.
    Unknown spellings are deliberately not exempted from the collision check.
    """
    # Compiler metadata suffixes describe the named class, not another entity.
    demangled = demangled.split("::`RTTI ", 1)[0]
    descriptor = " `RTTI Type Descriptor'"
    if demangled.endswith(descriptor):
        demangled = demangled[:-len(descriptor)]
    demangled = demangled.split("{for ", 1)[0].rstrip()
    depth = 0
    token = ""
    previous = ""
    conversion = False
    calling_convention = False
    quoted = False
    for char in demangled:
        # Microsoft special members have a single backtick/apostrophe name,
        # e.g. `scalar deleting destructor'. Its spaces are not token breaks.
        if char == "`":
            quoted = True
        if quoted:
            token += char
            if char == "'":
                quoted = False
            continue
        if char == "(" and depth == 0:
            # A function-pointer return type opens parentheses before the
            # function's name. Do not mistake its std return type for a scope.
            if not calling_convention:
                return False
            break
        if char.isspace() and depth == 0:
            if token in {"__cdecl", "__thiscall", "__stdcall", "__fastcall",
                         "__vectorcall", "__clrcall"}:
                calling_convention = True
            if token.endswith("::operator"):
                conversion = True
            if conversion:
                token += "_"
            elif token:
                previous, token = token, ""
            continue
        if char == "<":
            depth += 1
        elif char == ">" and depth:
            depth -= 1
        token += char
    entity = token or previous
    return entity.startswith("std::")


def standard_shared_symbol(name, demangled=""):
    # Mach-O adds one underscore to the Itanium mangling and C identifiers.
    normalized = name[1:] if name.startswith("__Z") else name
    if normalized.startswith("_Z"):
        # The *entity's* outer namespace must be std. Never look for std in a
        # function signature: foo(std::string) is still an unisolated foo.
        if re.match(r"^_Z(?:N[KVrRO]*St|St|(?:TV|TT|TI|TS)N?St|"
                    r"(?:GV|GR)?ZN[KVrRO]*St|(?:GV|GR)N[KVrRO]*St)",
                    normalized):
            return True
        # Standard global allocation/deallocation overloads, including sized,
        # nothrow and aligned forms. Do not exempt arbitrary operator symbols.
        return bool(re.fullmatch(
            r"_Z(?:n[aw][mjy](?:St11align_val_t)?(?:RKSt9nothrow_t)?|"
            r"d[al]Pv(?:[mjy])?(?:St11align_val_t)?(?:RKSt9nothrow_t)?)",
            normalized))
    if name.startswith("?"):
        if microsoft_std_entity(demangled):
            return True
        # Microsoft ABI global new/new[]/delete/delete[]. Their signatures
        # contain only standard allocation parameter types.
        if name.startswith(("??2@", "??3@", "??_U@", "??_V@")):
            return bool(re.fullmatch(
                r"(?:void\s*\*|void)\s+__cdecl operator (?:new|delete)"
                r"(?:\[\])?\((?:unsigned (?:int|__int64)|void \*)"
                r"(?:,(?:unsigned (?:int|__int64)|enum std::align_val_t|"
                r"struct std::nothrow_t const &))*\)", demangled))
        return False
    # ELF's hidden weak COMDAT points at the shared C++ EH personality.
    # Both the host plugin bridge and private lowering enable exceptions.
    if name == "DW.ref.__gxx_personality_v0":
        return True
    plain = name[1:] if name.startswith("_") else name
    # Host allocator interposition (e.g. mimalloc) is intentional and has the
    # platform C runtime ABI. No LLVM/Clang C API is in this allowlist.
    runtime_symbols = {
        "malloc", "calloc", "realloc", "free", "aligned_alloc",
        "posix_memalign", "_aligned_malloc", "_aligned_free",
        "__clang_call_terminate",
    }
    return name in runtime_symbols or plain in runtime_symbols


def decoded_symbols(nm, archive):
    # --no-sort preserves identical object/symbol order in the two invocations;
    # this also works for Microsoft names containing spaces when demangled.
    outputs = []
    for options in ((), ("--demangle",)):
        outputs.append([line for line in nm_output(
            nm, [archive], "--format=just-symbols", "--no-sort", *options
        ).splitlines() if line and not line.endswith(":")])
    if len(outputs[0]) != len(outputs[1]):
        raise ValueError("Inconsistent llvm-nm symbol inventory")
    return dict(zip(*outputs))


def has_raw_clang_name(name):
    # No host library may expose a Clang type/entity. Microsoft names delimit
    # the namespace explicitly; an Itanium source-name has the exact length 5.
    # Excluding a preceding digit avoids reading e.g. 15clangSomething as clang.
    return "@clang@@" in name or (
        name.startswith(("_Z", "__Z")) and
        bool(re.search(r"(?<![0-9])5clang", name)))


def audit(args):
    nm = args.nm
    if args.nm_file:
        nm = args.nm_file.read_text(encoding="utf-8").strip()
        if not nm or "\n" in nm or "\r" in nm:
            raise ValueError("Invalid private llvm-nm path manifest")
    renamed = {}
    if args.prefix_header:
        renamed = dict(re.findall(
            r"^#define (\w+) (neverc_cpp_\w+)$",
            args.prefix_header.read_text(encoding="utf-8"), re.M))
    for name in ("PrintBranchProbFuncName", "ScalePartialSampleProfileWorkingSetSize"):
        renamed[name] = "neverc_cpp_" + name
    bad = []
    for line in nm_output(nm, [args.archive], "--demangle").splitlines():
        if line.endswith(":"):
            continue
        if (re.search(r"(?<![A-Za-z0-9_])llvm::", line)
                or re.search(r"(?<![A-Za-z0-9_:])(?:mangledNameForMallocFamily|isVPIntrinsic|deserializeSanitizerMetadata)\(", line)
                or re.search(r"(?<![A-Za-z0-9_])DebugInfoPerPass\b", line)):
            bad.append(line)
    definitions, references = set(), set()
    for name, kind in symbol_rows(nm_output(
            nm, [args.archive], "--format=posix")):
        plain = name[1:] if name.startswith("_") else name
        if (re.match(r"^_?(?:LLVM[A-Z]|llvm_|UseNewDbgInfoFormat$)", name)
                or name in renamed or plain in renamed):
            bad.append(name)
        (references if is_undefined(kind) else definitions).add(name)
    for name in sorted(references - definitions):
        plain = name[1:] if name.startswith("_") else name
        if ("neverc_cpp_llvm" in name or "5clang" in name
                or "@clang@@" in name
                or name in renamed.values() or plain in renamed.values()):
            bad.append("unresolved private dependency: " + name)
    if not {"neverc_cpp_frontend_main", "_neverc_cpp_frontend_main"} & definitions:
        bad.append("missing builtin C++ frontend C entry point definition")

    if args.host_lib_dir:
        archives = host_archives(args.host_lib_dir, args.archive)
        host_definitions = set()
        if getattr(args, "host_format", "nm") == "coff-index":
            from HostCoffSymbols import read_defined_symbols
            for archive in archives:
                host_definitions.update(read_defined_symbols(archive))
            for name in sorted(host_definitions):
                if has_raw_clang_name(name):
                    bad.append("unexpected host Clang definition: " + name)
        else:
            # A separate tool must understand the *host compiler's* bitcode.
            # The private pinned reader remains responsible for its own ABI.
            host_nm = getattr(args, "host_nm", None) or nm

            def host_output(batch, *options):
                try:
                    return nm_output(host_nm, batch, *options)
                except (OSError, UnicodeError, subprocess.CalledProcessError) as error:
                    detail = getattr(error, "stderr", None) or str(error)
                    raise ValueError(
                        "Host archive symbol read failed. Set NEVERC_CPP_HOST_NM "
                        "to a reader compatible with the build compiler's "
                        "object/LTO format.\n" + detail) from error

            # Bound command line size; every discovered archive is read.
            for first in range(0, len(archives), 16):
                batch = archives[first:first + 16]
                for name, kind in symbol_rows(host_output(batch, "--format=posix")):
                    if not is_undefined(kind):
                        host_definitions.add(name)
                for line in host_output(batch, "--defined-only", "--demangle").splitlines():
                    if not line.endswith(":") and re.search(
                            r"(?<![A-Za-z0-9_])clang::", line):
                        bad.append("unexpected host Clang definition: " + line)
        shared = (definitions | references) & host_definitions
        microsoft = decoded_symbols(nm, args.archive) if any(
            name.startswith("?") for name in shared) else {}
        for name in sorted(shared):
            if not standard_shared_symbol(name, microsoft.get(name, "")):
                bad.append("private/host symbol intersection: " + name)
    if bad:
        raise ValueError("Unisolated symbols in builtin C++ frontend:\n" +
                         "\n".join(bad[:100]))
    print("Builtin C++ frontend: defined and undefined symbols use the private LLVM ABI")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    nm = parser.add_mutually_exclusive_group(required=True)
    nm.add_argument("--nm")
    nm.add_argument("--nm-file", type=Path)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--prefix-header", type=Path)
    parser.add_argument("--host-lib-dir", type=Path)
    parser.add_argument("--host-format", choices=("nm", "coff-index"), default="nm")
    parser.add_argument("--host-nm")
    args = parser.parse_args()
    try:
        audit(args)
    except (OSError, UnicodeError, ValueError, subprocess.CalledProcessError) as error:
        # A failed inspection must not leave an apparently usable aggregate.
        args.archive.unlink(missing_ok=True)
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    main()
