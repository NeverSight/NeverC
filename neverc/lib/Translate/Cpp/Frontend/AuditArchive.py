#!/usr/bin/env python3
"""Reject private LLVM definitions or references that could bind the host ABI."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True


STRDUP_SYMBOLS = frozenset(("strdup", "_strdup"))


def symbol_records(output):
    member_header = None
    for line in output.splitlines():
        if not line:
            continue
        if line.endswith(":"):
            member_header = line
            continue
        fields = line.split()
        if len(fields) < 2 or len(fields[1]) != 1:
            raise ValueError("Unexpected llvm-nm output during ABI isolation audit")
        yield fields[0], fields[1], member_header, line


def symbol_rows(output):
    for name, kind, _, _ in symbol_records(output):
        yield name, kind


def report_strdup_records(side, archive, records):
    for name, kind, member_header, raw in records:
        if name in STRDUP_SYMBOLS:
            print(f"strdup provenance: {side} nm archive={str(archive)!r} "
                  f"member_header={member_header!r} symbol={name!r} "
                  f"kind={kind!r} raw={raw!r}", flush=True)


def report_failure_nm_records(side, archive, records, selected):
    definitions, references = set(), set()
    for name, kind, member_header, raw in records:
        if name not in selected:
            continue
        print(f"ABI audit provenance: {side} nm archive={str(archive)!r} "
              f"member_header={member_header!r} symbol={name!r} "
              f"kind={kind!r} raw={raw!r}", flush=True)
        (references if is_undefined(kind) else definitions).add(name)
    return definitions, references


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


def microsoft_string_literal(name, demangled):
    # MicrosoftMangle.cpp::mangleStringLiteral encodes the character kind,
    # byte length, CRC and leading bytes in a COMDAT name. Require the entire
    # raw spelling as well as successful literal demangling: a quoted substring
    # in an ordinary function/type name must never hide its namespace.
    number = r"(?:[0-9]|[A-P]{1,16}@)"
    crc = r"(?:[0-9]|[A-P]{1,8}@)"
    byte = r"(?:[A-Za-z0-9_$]|\?[A-Za-z0-9]|\?\$[A-P]{2})"
    return bool(re.fullmatch(
        r"\?\?_C@_[01](?!A@)" + number + crc + byte + r"+@", name)
        and re.fullmatch(r'(?:L|u|U)?"(?:[^"\\\r\n]|\\[^\r\n])*"(?:\.\.\.)?',
                         demangled))


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
        # COFF's immutable string COMDATs intentionally coalesce by this exact
        # mangled identity (CodeGenModule.cpp::GetAddrOfConstantStringFromLiteral).
        # This exception is only applied to a name present in both inventories.
        if microsoft_string_literal(name, demangled):
            return True
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


def decoded_symbols(nm, paths, *options, output=None):
    # --no-sort preserves identical object/symbol order in the two invocations;
    # this also works for Microsoft names containing spaces when demangled.
    if output is None:
        def output(paths, *flags):
            return nm_output(nm, paths, *flags)
    outputs = []
    for decoding in ((), ("--demangle",)):
        outputs.append([line for line in output(
            paths, "--format=just-symbols", "--no-sort", *options, *decoding
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
    bad_private_names, bad_host_names = set(), set()
    host_evidence_batches = []
    private_decoded = decoded_symbols(nm, [args.archive])
    for name, declaration in private_decoded.items():
        if microsoft_string_literal(name, declaration):
            continue
        if (re.search(r"(?<![A-Za-z0-9_])llvm::", declaration)
                or re.search(r"(?<![A-Za-z0-9_:])(?:mangledNameForMallocFamily|isVPIntrinsic|deserializeSanitizerMetadata)\(", declaration)
                or re.search(r"(?<![A-Za-z0-9_])DebugInfoPerPass\b", declaration)):
            bad.append(name + " => " + declaration)
            bad_private_names.add(name)
    definitions, references = set(), set()
    for record in symbol_records(nm_output(
            nm, [args.archive], "--format=posix")):
        name, kind, _, _ = record
        report_strdup_records("private", args.archive, (record,))
        plain = name[1:] if name.startswith("_") else name
        if (re.match(r"^_?(?:LLVM[A-Z]|llvm_|UseNewDbgInfoFormat$)", name)
                or name in renamed or plain in renamed):
            bad.append(name)
            bad_private_names.add(name)
        (references if is_undefined(kind) else definitions).add(name)
    private_inventory = definitions | references
    def private_dependency(name):
        plain = name[1:] if name.startswith("_") else name
        return ("neverc_cpp_llvm" in name or "5clang" in name
                or "@clang@@" in name
                or name in renamed.values() or plain in renamed.values())

    resolved_aliases = set()
    coff_readobj = getattr(args, "coff_readobj", None)
    coff_readobj_file = getattr(args, "coff_readobj_file", None)
    if coff_readobj_file:
        coff_readobj = coff_readobj_file.read_text(encoding="utf-8").strip()
        if not coff_readobj or "\n" in coff_readobj or "\r" in coff_readobj:
            raise ValueError("Invalid private llvm-readobj path manifest")
    if coff_readobj:
        from CoffWeakAliases import read_resolved_aliases
        # Include W/V aliases as well as undefined references. A COFF alias can
        # appear defined to nm while its auxiliary fallback remains unresolved.
        private_names = {name for name in definitions | references
                         if private_dependency(name)}
        resolved_aliases = read_resolved_aliases(
            coff_readobj, args.archive, definitions, private_names)
    for name in sorted(references - definitions - resolved_aliases):
        if private_dependency(name):
            bad.append("unresolved private dependency: " + name)
            bad_private_names.add(name)
    if not {"neverc_cpp_frontend_main", "_neverc_cpp_frontend_main"} & definitions:
        bad.append("missing builtin C++ frontend C entry point definition")

    if args.host_lib_dir:
        archives = host_archives(args.host_lib_dir, args.archive)
        host_definitions = set()
        host_format = getattr(args, "host_format", "nm")
        if host_format == "coff-index":
            from HostCoffSymbols import read_defined_symbols
            for archive in archives:
                indexed = read_defined_symbols(archive)
                host_definitions.update(indexed)
                # Keep only symbol identities potentially needing failure
                # evidence, not the raw inventory of every host archive.
                selected = indexed & private_inventory
                selected.update(name for name in indexed if has_raw_clang_name(name))
                if selected:
                    host_evidence_batches.append(([archive], selected))
                for name in sorted(indexed & STRDUP_SYMBOLS):
                    print(f"strdup provenance: host coff-index archive={str(archive)!r} "
                          f"symbol={name!r} index-only; object kind, member and raw "
                          "nm row unavailable; strdup exception disabled", flush=True)
            for name in sorted(host_definitions):
                if has_raw_clang_name(name):
                    bad.append("unexpected host Clang definition: " + name)
                    bad_host_names.add(name)
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
                strdup_records = []
                batch_candidates = set()
                for record in symbol_records(host_output(batch, "--format=posix")):
                    name, kind, _, _ = record
                    if not is_undefined(kind):
                        host_definitions.add(name)
                        if name in private_inventory:
                            batch_candidates.add(name)
                    if name in STRDUP_SYMBOLS:
                        strdup_records.append(record)
                expected_strdup = sorted((name, kind) for name, kind, _, _ in strdup_records)
                if expected_strdup:
                    # Default POSIX output labels archive members, but LLVM can
                    # omit their parent archive name. Re-read only an affected
                    # batch one archive at a time to report exact provenance.
                    observed_strdup = []
                    for archive in batch:
                        selected = strdup_records if len(batch) == 1 else [
                            record for record in symbol_records(host_output([archive], "--format=posix"))
                            if record[0] in STRDUP_SYMBOLS]
                        report_strdup_records("host", archive, selected)
                        observed_strdup.extend((name, kind) for name, kind, _, _ in selected
                                               if name in STRDUP_SYMBOLS)
                    if sorted(observed_strdup) != expected_strdup:
                        for name, kind, member_header, raw in strdup_records:
                            print(f"strdup provenance: host nm batch_archives={list(map(str, batch))!r} "
                                  f"member_header={member_header!r} symbol={name!r} "
                                  f"kind={kind!r} raw={raw!r}", flush=True)
                        raise ValueError("Host strdup symbol inventory changed during provenance read")
                for name, declaration in decoded_symbols(
                        host_nm, batch, "--defined-only", output=host_output).items():
                    if (not microsoft_string_literal(name, declaration)
                            and re.search(r"(?<![A-Za-z0-9_])clang::", declaration)):
                        bad.append("unexpected host Clang definition: " + name +
                                   " => " + declaration)
                        bad_host_names.add(name)
                        batch_candidates.add(name)
                if batch_candidates:
                    host_evidence_batches.append((batch, batch_candidates))
        shared = (definitions | references) & host_definitions
        for name in sorted(shared):
            # Only a platform strdup reference may bind the host allocator.
            # A private strong OR weak definition is never exempted. The COFF
            # index cannot supply observed object kinds, so it grants no such
            # exception until equivalent evidence is available.
            if (host_format == "nm" and name in STRDUP_SYMBOLS
                    and name in references and name not in definitions):
                continue
            if not standard_shared_symbol(name, private_decoded.get(name, "")):
                bad.append("private/host symbol intersection: " + name +
                           f"; private_demangled={private_decoded.get(name, '')!r}" +
                           f"; private_definition={name in definitions}" +
                           f"; private_reference={name in references}")
                bad_private_names.add(name)
                bad_host_names.add(name)
    if bad:
        diagnostic = (f"Unisolated symbols in builtin C++ frontend (total={len(bad)}):\n" +
                      "\n".join(bad))
        # Failure evidence is read one archive at a time. LLVM's default POSIX
        # member header can omit the parent archive, so a batch row alone is
        # insufficient to attribute a collision. Normal successful audits do
        # not retain all raw rows or run these additional inspections.
        try:
            if bad_private_names:
                observed_definitions, observed_references = report_failure_nm_records(
                    "private", args.archive,
                    symbol_records(nm_output(nm, [args.archive], "--format=posix")),
                    bad_private_names)
                if (observed_definitions != definitions & bad_private_names or
                        observed_references != references & bad_private_names):
                    raise ValueError("Private symbol inventory changed during provenance read")
            for batch, candidates in host_evidence_batches:
                selected = candidates & bad_host_names
                if not selected:
                    continue
                observed_definitions = set()
                for archive in batch:
                    if host_format == "coff-index":
                        observed = read_defined_symbols(archive) & selected
                        for name in sorted(observed):
                            print(f"ABI audit provenance: host coff-index archive={str(archive)!r} "
                                  f"symbol={name!r} index-only; object kind, member and raw "
                                  "nm row unavailable", flush=True)
                    else:
                        observed, _ = report_failure_nm_records(
                            "host", archive,
                            symbol_records(host_output([archive], "--format=posix")),
                            selected)
                    observed_definitions.update(observed)
                if observed_definitions != selected:
                    raise ValueError("Host symbol inventory changed during provenance read")
        except (OSError, UnicodeError, ValueError, subprocess.CalledProcessError) as error:
            # Preserve every original finding even when its additional evidence
            # cannot be read. The archive remains rejected in either case.
            raise ValueError(diagnostic + "\nABI audit provenance collection failed: " +
                             str(error)) from error
        raise ValueError(diagnostic)
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
    coff_reader = parser.add_mutually_exclusive_group()
    coff_reader.add_argument("--coff-readobj")
    coff_reader.add_argument("--coff-readobj-file", type=Path)
    args = parser.parse_args()
    try:
        audit(args)
    except (OSError, UnicodeError, ValueError, subprocess.CalledProcessError) as error:
        # A failed inspection must not leave an apparently usable aggregate.
        args.archive.unlink(missing_ok=True)
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    main()
