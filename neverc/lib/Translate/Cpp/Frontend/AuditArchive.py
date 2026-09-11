#!/usr/bin/env python3
"""Reject private LLVM definitions or references that could bind the host ABI."""

import argparse
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True

from MsvcRuntimeSymbols import (MSVC_DELETE_FALLBACK,
                                msvc_delete_weak_reference,
                                msvc_runtime_definition)
from SetupGuidSymbols import (contains_old_guid_name, is_private_guid_name,
                              is_private_guid_metadata_name)


STRDUP_SYMBOLS = frozenset(("strdup", "_strdup"))

# The UCRT stdio wrappers, option accessors and their storage share one final
# module's CRT configuration. Keep this subset separate from the wider COFF
# runtime policy and from diagnostic-only storage selection below.
MSVC_STDIO_SYMBOLS = frozenset((
    "__local_stdio_printf_options",
    "__local_stdio_scanf_options",
    "_snprintf",
    "fprintf",
    "snprintf",
    "sprintf_s",
    "sscanf",
    "?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA",
    "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@4_KA",
))

# UCRT defines this zero-initialized selectany fallback in the final module.
# Sharing it does not authorize the feature variable or its alternate-name edge.
MSVC_AVX2_FALLBACK = "_Avx2WmemEnabledWeakValue"

CRT_STORAGE_SYMBOLS = frozenset((
    "?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA",
    "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@4_KA",
    "_Avx2WmemEnabledWeakValue",
))


def crt_storage_failure_context(args, shared, bad_private_names, bad_host_names,
                                archives, nm, coff_readobj, host_nm):
    """Describe an existing rejection; never decide whether sharing is allowed."""
    report_root = os.environ.get("NEVERC_CPP_CRT_STORAGE_REPORT_DIR")
    if (not report_root or not args.host_lib_dir or
            getattr(args, "host_format", "nm") != "nm"):
        return None
    selected = CRT_STORAGE_SYMBOLS & shared & bad_private_names & bad_host_names
    if not selected:
        return None
    return report_root, {
        "schema": "neverc.crt-storage-request.v1",
        "selected_symbols": sorted(selected),
        "private_archive": str(args.archive),
        "host_archives": list(map(str, archives)),
        "private_nm": str(nm),
        "private_readobj": str(coff_readobj) if coff_readobj else None,
        "host_nm": str(host_nm),
        "audit_outcome": "rejected",
        "source_sha": os.environ.get("GITHUB_SHA", ""),
        "run_id": os.environ.get("GITHUB_RUN_ID", ""),
        "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT", ""),
        "target": os.environ.get("NEVERC_CPP_CRT_STORAGE_TARGET", ""),
    }


def collect_crt_storage_failure(context, report_root):
    # A missing adjacent helper must not fall back to a PYTHONPATH module.
    helper = Path(__file__).resolve().with_name("CollectCrtStorageEvidence.py")
    if helper.is_symlink() or not helper.is_file():
        raise ValueError("Missing adjacent CRT storage diagnostic helper")
    spec = importlib.util.spec_from_file_location("_neverc_crt_storage_evidence", helper)
    if spec is None or spec.loader is None:
        raise ValueError("Cannot load adjacent CRT storage diagnostic helper")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module.collect_failure(context, report_root)


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


def host_archives(directory, private_archive, self_import_library=None):
    def fail_walk(error):
        raise error

    result = []
    excluded_archives = {private_archive.resolve()}
    if self_import_library is not None:
        excluded_archives.add(self_import_library.resolve())
    for parent, directories, files in os.walk(
            directory, followlinks=False, onerror=fail_walk):
        directories[:] = sorted(name for name in directories
                                if name != "_deps" and not
                                (Path(parent) / name).is_symlink())
        for name in sorted(files):
            path = Path(parent) / name
            if (path.suffix.lower() in (".a", ".lib")
                    and path.resolve() not in excluded_archives):
                result.append(path)
    if not result:
        raise ValueError("No host archives found for the builtin C++ ABI audit")
    return result


MICROSOFT_DECLARATION_LIMIT = 65536
MICROSOFT_NESTING_LIMIT = 32
# The demangler appends template arguments directly to the operator spelling.
# Consume both '<' characters for a shift only before its parameter/template
# group or the absolute end of an extracted owner token. In operator<<int>,
# the second '<' instead opens the less-than operator's template arguments.
_MICROSOFT_OPERATOR = re.compile(
    r"\boperator(?:<=>|->\*?|<<=|<<(?=[<(]|\Z)|>>=?|<=?|>=?|==|!=|&&|\|\||\+\+|--|"
    r"\(\)|\[\]|,|[-+*/%&|^~!=]=?)")
_MICROSOFT_CALLING_CONVENTIONS = frozenset((
    "__cdecl", "__thiscall", "__stdcall", "__fastcall", "__vectorcall", "__clrcall"))


def _microsoft_group_end(text, start):
    """Return the end of one balanced demangler group, or None.

    Local scopes nest Microsoft backtick/apostrophe groups, including inside
    template arguments. Treating quotes as a boolean loses the outer scope.
    Operator punctuation is a name, not a template or parameter delimiter.
    """
    closes = {"<": ">", "(": ")", "[": "]", "{": "}", "`": "'"}
    stack = []
    index = start
    while index < len(text):
        operator = _MICROSOFT_OPERATOR.match(text, index)
        if operator:
            index = operator.end()
            continue
        char = text[index]
        if char in closes:
            stack.append(closes[char])
            if len(stack) > MICROSOFT_NESTING_LIMIT:
                return None
        elif char in ">)]}'":
            if not stack or stack.pop() != char:
                return None
            if not stack:
                return index + 1
        index += 1
    return None


def _microsoft_qualified_std_owner(entity, nesting):
    if not entity or entity.endswith("::"):
        return False
    # Check scope separators outside templates and quoted enclosing functions.
    # A nonempty suffix alone is insufficient for a local such as ::`2'::::x.
    index = component_start = 0
    while index < len(entity):
        operator = _MICROSOFT_OPERATOR.match(entity, index)
        if operator:
            index = operator.end()
            continue
        if entity[index] in "<([`":
            end = _microsoft_group_end(entity, index)
            if end is None:
                return False
            index = end
            continue
        if entity.startswith("::", index):
            if index == component_start:
                return False
            index += 2
            component_start = index
            continue
        if entity[index] == ":":
            return False
        index += 1
    if entity.startswith("std::"):
        # A missing member is not a declaration. Groups have already been
        # checked by the declaration scanner; std in their types is irrelevant.
        return bool(re.match(r"std::[A-Za-z_$~`<]", entity))
    if not entity.startswith("`"):
        return False
    end = _microsoft_group_end(entity, 0)
    if end is None:
        return False
    parent = entity[1:end - 1]
    suffix = entity[end:]
    for wrapper in ("dynamic initializer for ", "dynamic atexit destructor for "):
        if parent.startswith(wrapper):
            payload = parent[len(wrapper):]
            if (suffix or not payload.startswith("`") or
                    _microsoft_group_end(payload, 0) != len(payload)):
                return False
            return _microsoft_declaration_std_owner(payload[1:-1], nesting + 1)
    # The quote introducing a local entity must contain its complete enclosing
    # function declaration. A quoted type or a std type in a Host signature
    # cannot supply ownership for that entity.
    if not suffix.startswith("::") or len(suffix) == 2:
        return False
    return _microsoft_declaration_std_owner(parent, nesting + 1, function_only=True)


def _microsoft_function_suffix(suffix):
    # MSVC member cv/ref qualifiers follow the complete parameter list. Do not
    # silently discard another declaration or arbitrary trailing text.
    # This is FunctionSignatureNode::outputPost's order in the pinned LLVM
    # demangler, including noexcept before the reference qualifier.
    return bool(re.fullmatch(
        r"(?:\s+const)?(?:\s+volatile)?(?:\s+__restrict)?"
        r"(?:\s+__unaligned)?(?:\s+noexcept)?(?:\s+&&|\s+&)?\s*", suffix))


def _microsoft_declaration_std_owner(declaration, nesting=0, function_only=False):
    if nesting >= MICROSOFT_NESTING_LIMIT:
        return False
    descriptor = " `RTTI Type Descriptor'"
    if declaration.endswith(descriptor):
        declaration = declaration[:-len(descriptor)]
    token, previous = "", ""
    conversion = False
    calling_convention = False
    index = 0
    while index < len(declaration):
        char = declaration[index]
        operator = _MICROSOFT_OPERATOR.match(declaration, index)
        if operator:
            token += operator.group()
            index = operator.end()
            continue
        if char in "<([`{":
            end = _microsoft_group_end(declaration, index)
            if end is None:
                return False
            if char == "(":
                # A parenthesis in a function-pointer return type precedes
                # the declared function. Its return type is not its owner.
                if not calling_convention or not token:
                    return False
                return (_microsoft_function_suffix(declaration[end:]) and
                        _microsoft_qualified_std_owner(token, nesting))
            if char == "{":
                # A vftable adjustment names a base after the actual entity.
                if (function_only or end != len(declaration) or
                        not declaration[index + 1:end - 1].startswith("for ")):
                    return False
                return _microsoft_qualified_std_owner(token, nesting)
            token += declaration[index:end]
            index = end
            continue
        if char in ">)]}'":
            return False
        if char.isspace():
            if token in _MICROSOFT_CALLING_CONVENTIONS:
                calling_convention = True
            if token.endswith("::operator"):
                conversion = True
            if conversion:
                token += " "
            elif token:
                previous, token = token, ""
        elif char in "*&" and not conversion:
            # A pointer/reference declarator need not have whitespace before
            # its name: e.g. const *std::_Facetptr<...>::_Psave.
            previous, token = token, ""
        else:
            token += char
        index += 1
    return (not function_only and
            _microsoft_qualified_std_owner(token or previous, nesting))


def microsoft_std_entity(demangled):
    """Recognize the declared std owner under the existing shared-std policy.

    This identifies declarations, including their local scopes; it does not
    establish ABI equivalence or authorize another vendor runtime namespace.
    Unknown, malformed and excessively nested spellings fail closed.
    """
    if (not demangled or len(demangled) > MICROSOFT_DECLARATION_LIMIT or
            any(ord(char) < 32 or ord(char) == 127 for char in demangled)):
        return False
    return _microsoft_declaration_std_owner(demangled.strip())


def microsoft_std_eh_entity(name):
    """Recognize plain-class CTA/TI owners under the shared-std policy.

    Clang's MicrosoftMangle.cpp writes a decimal entry count followed by the
    thrown type. Qualified identifiers are encoded from inner to outer scope.
    This checks ownership, not record layout or ABI equivalence. Templates,
    backreferences, TI qualifiers and CT records need separate handling.
    """
    if len(name) > MICROSOFT_DECLARATION_LIMIT:
        return False
    match = re.fullmatch(r"_(?:CTA|TI)(0|[1-9][0-9]{0,9})\?AV(.+)@@", name)
    if match is None or int(match.group(1)) > 0xFFFFFFFF:
        return False
    components = match.group(2).split("@")
    return (2 <= len(components) <= MICROSOFT_NESTING_LIMIT
            and components[-1] == "std"
            and all(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", part)
                    for part in components))


def microsoft_std_catchable_type(name):
    """Recognize the observed plain-class CT self-copy constructor forms.

    RTTI and constructor manglings use independent name backreference tables.
    With two or three distinct identifiers, the constructor's class parameter
    refers back to its own name with 01 or 012 respectively. Require the exact
    same std-owned type in both halves.

    CT numeric fields have no separators. The bounded canonical decimal tail
    below is a conservative acceptance domain, not the full upstream grammar;
    it does not decode size/offset fields or establish layout equivalence.
    """
    if len(name) > MICROSOFT_DECLARATION_LIMIT:
        return False
    identifier = r"[A-Za-z_][A-Za-z0-9_]*"
    scope = identifier + r"(?:@" + identifier + r"){1,2}"
    match = re.fullmatch(
        r"_CT\?\?_R0\?AV(" + scope + r")@@@8\?\?0(" + scope +
        r")@@QEAA@AEBV(01|012)@@Z(0|[1-9][0-9]{0,9})", name)
    if match is None:
        return False
    type_scope, ctor_scope, backrefs, tail = match.groups()
    components = type_scope.split("@")
    return (type_scope == ctor_scope and components[-1] == "std"
            and len(set(components)) == len(components)
            and backrefs == ("01" if len(components) == 2 else "012")
            and int(tail) <= 0xFFFFFFFF)


def global_cpp_record_entity(symbol, declaration, record):
    """Recognize explicit global record identities, never a name substring.

    MSVC spells record types with a tag. Itanium omits tags, so only use
    unambiguous owners, RTTI names and simple function parameter lists there.
    A bare Itanium template argument can instead name a non-type argument.
    """
    if (not declaration or declaration == symbol or
            len(declaration) > MICROSOFT_DECLARATION_LIMIT or
            any(ord(char) < 32 or ord(char) == 127 for char in declaration)):
        return False
    microsoft = symbol.startswith("?")
    itanium = symbol.startswith(("_Z", "__Z"))
    if not (microsoft or itanium):
        return False
    name = re.escape(record)
    # Positive delimiters avoid guessing the full Unicode identifier alphabet
    # (including combining marks) or treating MSVC's '$' as a separator.
    boundary = r"(?:^|[\s(<,>*&])"
    if re.search(boundary + name + r"::", declaration):
        return True
    if microsoft:
        return bool(re.search(
            boundary + r"(?:struct|class) " + name +
            r"(?=$|[\s*&,)>:\[])", declaration))
    if re.fullmatch(r"(?:vtable|VTT|typeinfo|typeinfo name) for " + name,
                    declaration):
        return True
    if re.search(r"::operator " + name + r"(?: const)?\s*[*&]*\(\)",
                 declaration):
        return True
    # Function parameter names are absent from nm demangling. Do not interpret
    # a bare function/data name, a nested template expression, or a parameter's
    # identifier in a declaration as a record type.
    function = re.fullmatch(
        r"[^<>()]+\(([^<>()]*)\)(?: const| volatile| &| &&| noexcept)*",
        declaration)
    if not function:
        return False
    parameter = r"\s*(?:(?:const|volatile)\s+)*" + name + (
        r"(?:\s+(?:const|volatile)\b)*"
        r"(?:\s*\*\s*(?:(?:const|volatile)\b\s*)*)*"
        r"(?:\s*&{1,2})?\s*")
    return any(re.fullmatch(parameter, part)
               for part in function.group(1).split(","))


def microsoft_string_literal(name, demangled):
    # MSVC's empty narrow string observed in real archives has no encoded
    # payload. LLVM renders that exact spelling as a truncated empty literal.
    # Do not extend the general grammar to arbitrary zero-payload spellings.
    if name == "??_C@_00CNPNBAHC@@":
        return demangled == '""...'
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
                r"(?:, ?(?:unsigned (?:int|__int64)|enum std::align_val_t|"
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
    args._crt_storage_failure = None
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
    private_record_symbols = set()
    for name, declaration in private_decoded.items():
        if microsoft_string_literal(name, declaration):
            continue
        if global_cpp_record_entity(name, declaration, "neverc_cpp_PointerBounds"):
            private_record_symbols.add(name)
        if (re.search(r"(?<![A-Za-z0-9_])llvm::", declaration)
                or re.search(r"(?<![A-Za-z0-9_:])(?:mangledNameForMallocFamily|isVPIntrinsic|deserializeSanitizerMetadata)\(", declaration)
                or re.search(r"(?<![A-Za-z0-9_])DebugInfoPerPass\b", declaration)
                or global_cpp_record_entity(name, declaration, "PointerBounds")):
            bad.append(name + " => " + declaration)
            bad_private_names.add(name)
    definitions, references = set(), set()
    private_guid_symbols, private_guid_weak_symbols = set(), set()
    private_kinds = {}
    for record in symbol_records(nm_output(
            nm, [args.archive], "--format=posix")):
        name, kind, _, _ = record
        private_kinds.setdefault(name, set()).add(kind)
        report_strdup_records("private", args.archive, (record,))
        # This is a private-archive invariant, independent of whether a host
        # happens to expose the same GUID or SDK template COMDAT today.
        if contains_old_guid_name(name):
            bad.append("unisolated Setup GUID symbol: " + name)
            bad_private_names.add(name)
        try:
            if is_private_guid_name(name):
                if is_private_guid_metadata_name(name):
                    # nm_output always requests --extern-only. Metadata names
                    # are valid only on local COFF records, never as a public
                    # definition or an external closure obligation.
                    bad.append("Setup GUID metadata has external linkage: " + name)
                    bad_private_names.add(name)
                else:
                    private_guid_symbols.add(name)
                    if kind in ("W", "V", "w", "v"):
                        private_guid_weak_symbols.add(name)
        except ValueError as error:
            bad.append(str(error))
            bad_private_names.add(name)
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
                or name in private_guid_symbols
                or name in private_record_symbols
                or name in renamed.values() or plain in renamed.values())

    resolved_aliases = set()
    runtime_fallbacks = {}
    host_format = getattr(args, "host_format", "nm")
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
        # Only an observed weak MSVC wrapper with its precise private runtime
        # body can request an exception. Ordinary U references stay rejected.
        if (args.host_lib_dir and host_format == "coff-index" and
                msvc_runtime_definition(
                    MSVC_DELETE_FALLBACK,
                    private_decoded.get(MSVC_DELETE_FALLBACK, ""),
                    private_kinds.get(MSVC_DELETE_FALLBACK, set()))):
            runtime_fallbacks = {
                name: MSVC_DELETE_FALLBACK for name in references
                if msvc_delete_weak_reference(name, private_decoded.get(name, ""),
                                               private_kinds[name])}
        private_names.update(runtime_fallbacks)
        proof_arguments = ({"expected_fallbacks": runtime_fallbacks}
                           if runtime_fallbacks else {})
        resolved_aliases = read_resolved_aliases(
            coff_readobj, args.archive, definitions, private_names, **proof_arguments)
    elif private_guid_weak_symbols:
        # W/V can describe an alias, not an actual definition. Without COFF
        # auxiliary records its fallback could still borrow a host symbol.
        for name in sorted(private_guid_weak_symbols):
            bad.append("Setup GUID weak closure requires a COFF reader: " + name)
            bad_private_names.add(name)
    for name in sorted(references - definitions - resolved_aliases):
        if private_dependency(name):
            bad.append("unresolved private dependency: " + name)
            bad_private_names.add(name)
    if not {"neverc_cpp_frontend_main", "_neverc_cpp_frontend_main"} & definitions:
        bad.append("missing builtin C++ frontend C entry point definition")

    if args.host_lib_dir:
        archives = host_archives(
            args.host_lib_dir, args.archive,
            self_import_library=getattr(args, "self_import_library", None))
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
            # Runtime sharing never erases independent private-archive findings
            # above, including an original Setup GUID or invalid metadata.
            if host_format == "coff-index":
                if (name not in references and msvc_runtime_definition(
                        name, private_decoded.get(name, ""), private_kinds[name])):
                    continue
                if name in runtime_fallbacks and name in resolved_aliases:
                    continue
            # Only a platform strdup reference may bind the host allocator.
            # A private strong OR weak definition is never exempted. The COFF
            # index cannot supply observed object kinds, so it grants no such
            # exception until equivalent evidence is available.
            if (host_format == "nm" and name in STRDUP_SYMBOLS
                    and name in references and name not in definitions):
                continue
            # These exact stdio identities belong to the final module's CRT
            # configuration, including the accessors and their shared storage.
            # Require the observed private definition kind and declaration;
            # host nm W establishes a definition, not its text/storage kind.
            if (host_format == "nm" and name in MSVC_STDIO_SYMBOLS
                    and name not in references and msvc_runtime_definition(
                        name, private_decoded.get(name, ""), private_kinds[name])):
                continue
            # Share only the exact UCRT fallback definition. Its feature-variable
            # alias and all weak-reference closure checks retain their own rules.
            if (host_format == "nm" and name == MSVC_AVX2_FALLBACK
                    and name not in references and msvc_runtime_definition(
                        name, private_decoded.get(name, ""), private_kinds[name])):
                continue
            # Exception metadata retains its std type identity in both archives.
            # Only observed private read-only definitions may use this policy;
            # a host nm definition (often W) is not evidence of its object kind.
            if (host_format == "nm" and name not in references
                    and private_kinds[name] == {"R"}
                    and (microsoft_std_eh_entity(name)
                         or microsoft_std_catchable_type(name))):
                continue
            # A mismatched runtime declaration cannot fall through to the broader
            # std-owner policy, including a storage name paired with std text.
            if ((host_format == "nm" and (name in MSVC_STDIO_SYMBOLS
                                         or name == MSVC_AVX2_FALLBACK))
                    or not standard_shared_symbol(name, private_decoded.get(name, ""))):
                bad.append("private/host symbol intersection: " + name +
                           f"; private_demangled={private_decoded.get(name, '')!r}" +
                           f"; private_definition={name in definitions}" +
                           f"; private_reference={name in references}")
                bad_private_names.add(name)
                bad_host_names.add(name)
    if bad:
        if args.host_lib_dir and host_format == "nm":
            args._crt_storage_failure = crt_storage_failure_context(
                args, shared, bad_private_names, bad_host_names, archives,
                nm, coff_readobj, host_nm)
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
    parser.add_argument("--self-import-library", type=Path,
                        help="Exact import-library output of the executable being linked")
    parser.add_argument("--host-format", choices=("nm", "coff-index"), default="nm")
    parser.add_argument("--host-nm")
    coff_reader = parser.add_mutually_exclusive_group()
    coff_reader.add_argument("--coff-readobj")
    coff_reader.add_argument("--coff-readobj-file", type=Path)
    args = parser.parse_args()
    try:
        audit(args)
    except (OSError, UnicodeError, ValueError, subprocess.CalledProcessError) as error:
        # Diagnostics cannot replace the original rejection or retain an
        # apparently usable aggregate, including when their helper exits.
        failure = getattr(args, "_crt_storage_failure", None)
        try:
            if failure is not None:
                report_root, context = failure
                collect_crt_storage_failure(context, Path(report_root))
        except BaseException as diagnostic_error:
            try:
                print("CRT storage diagnostics unavailable: " +
                      type(diagnostic_error).__name__, file=sys.stderr)
            except BaseException:
                # Reporting a diagnostic failure is also subordinate to the
                # original rejection, even if the error stream is closed.
                pass
        finally:
            args.archive.unlink(missing_ok=True)
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    main()
