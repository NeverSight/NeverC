#!/usr/bin/env python3
"""Narrow MSVC runtime identities shared by the two compiler implementations.

The full runtime definition policy applies only to the COFF host-index audit.
AuditArchive's nm path admits only its explicit ten-symbol stdio subset
and the exact AVX2 fallback definition.
Definition sharing requires the complete ABI spelling, decoded declaration,
observed private definition kind and absence of private references. It does
not authorize other stdext entities or arbitrary C entry points.
"""

import re


# The runtime includes header-defined stdext exception support, stdio options,
# compiler-generated iteration/delete helpers and standard EH descriptors.
# Keep mutable B/C/D storage distinct from immutable R constants.
_DEFINITIONS = {
    "??0bad_array_new_length@stdext@@QEAA@XZ":
        ("T", "public: __cdecl stdext::bad_array_new_length::bad_array_new_length(void)"),
    "??0exception@stdext@@QEAA@AEBV01@@Z":
        ("T", "public: __cdecl stdext::exception::exception(class stdext::exception const &)"),
    "??1bad_alloc@stdext@@UEAA@XZ":
        ("T", "public: virtual __cdecl stdext::bad_alloc::~bad_alloc(void)"),
    "??1exception@stdext@@UEAA@XZ":
        ("T", "public: virtual __cdecl stdext::exception::~exception(void)"),
    "??_7bad_alloc@stdext@@6B@":
        ("R", "const stdext::bad_alloc::`vftable'"),
    "??_7bad_array_new_length@stdext@@6B@":
        ("R", "const stdext::bad_array_new_length::`vftable'"),
    "??_7exception@stdext@@6B@":
        ("R", "const stdext::exception::`vftable'"),
    "??_Gbad_alloc@stdext@@UEAAPEAXI@Z":
        ("T", "public: virtual void * __cdecl stdext::bad_alloc::`scalar deleting dtor'(unsigned int)"),
    "??_Gbad_array_new_length@stdext@@UEAAPEAXI@Z":
        ("T", "public: virtual void * __cdecl stdext::bad_array_new_length::`scalar deleting dtor'(unsigned int)"),
    "??_Gexception@stdext@@UEAAPEAXI@Z":
        ("T", "public: virtual void * __cdecl stdext::exception::`scalar deleting dtor'(unsigned int)"),
    "??_H@YAXPEAX_K1P6APEAX0@Z@Z":
        ("T", "void __cdecl `vector ctor iterator'(void *, unsigned __int64, unsigned __int64, void * (__cdecl *)(void *))"),
    "??_I@YAXPEAX_K1P6AX0@Z@Z":
        ("T", "void __cdecl `vector dtor iterator'(void *, unsigned __int64, unsigned __int64, void (__cdecl *)(void *))"),
    "??_R0?AVexception@stdext@@@8":
        ("D", "class stdext::exception `RTTI Type Descriptor'"),
    "??__G@YAXPEAX0_K1P6APEAX00@Z@Z":
        ("T", "void __cdecl `vector copy ctor iterator'(void *, void *, unsigned __int64, unsigned __int64, void * (__cdecl *)(void *, void *))"),
    "?_Doraise@bad_alloc@stdext@@MEBAXXZ":
        ("T", "protected: virtual void __cdecl stdext::bad_alloc::_Doraise(void) const"),
    "?_Doraise@exception@stdext@@MEBAXXZ":
        ("T", "protected: virtual void __cdecl stdext::exception::_Doraise(void) const"),
    "?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA":
        ("B", "unsigned __int64 `extern \"C\" __local_stdio_printf_options'::`2'::_OptionsStorage"),
    "?_OptionsStorage@?1??__local_stdio_printf_options@@9@9":
        ("C", "extern \"C\" `extern \"C\" __local_stdio_printf_options'::`2'::_OptionsStorage"),
    "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@4_KA":
        ("B", "unsigned __int64 `extern \"C\" __local_stdio_scanf_options'::`2'::_OptionsStorage"),
    "?_Raise@exception@stdext@@QEBAXXZ":
        ("T", "public: void __cdecl stdext::exception::_Raise(void) const"),
    "?__empty_global_delete@@YAXPEAX@Z":
        ("T", "void __cdecl __empty_global_delete(void *)"),
    "?__empty_global_delete@@YAXPEAXW4align_val_t@std@@@Z":
        ("T", "void __cdecl __empty_global_delete(void *, enum std::align_val_t)"),
    "?__empty_global_delete@@YAXPEAX_K@Z":
        ("T", "void __cdecl __empty_global_delete(void *, unsigned __int64)"),
    "?__empty_global_delete@@YAXPEAX_KW4align_val_t@std@@@Z":
        ("T", "void __cdecl __empty_global_delete(void *, unsigned __int64, enum std::align_val_t)"),
    "?what@exception@stdext@@UEBAPEBDXZ":
        ("T", "public: virtual char const * __cdecl stdext::exception::what(void) const"),
    "_Avx2WmemEnabledWeakValue":
        ("B", "_Avx2WmemEnabledWeakValue"),
    "_CT??_R0?AVbad_alloc@std@@@8??0bad_alloc@std@@QEAA@AEBV01@@Z24":
        ("R", "_CT??_R0?AVbad_alloc@std@@@8??0bad_alloc@std@@QEAA@AEBV01@@Z24"),
    "_CT??_R0?AVbad_array_new_length@std@@@8??0bad_array_new_length@std@@QEAA@AEBV01@@Z24":
        ("R", "_CT??_R0?AVbad_array_new_length@std@@@8??0bad_array_new_length@std@@QEAA@AEBV01@@Z24"),
    "_CT??_R0?AVexception@std@@@8??0exception@std@@QEAA@AEBV01@@Z24":
        ("R", "_CT??_R0?AVexception@std@@@8??0exception@std@@QEAA@AEBV01@@Z24"),
    "_CT??_R0?AVexception@stdext@@@8??0exception@stdext@@QEAA@AEBV01@@Z16":
        ("R", "_CT??_R0?AVexception@stdext@@@8??0exception@stdext@@QEAA@AEBV01@@Z16"),
    "_CT??_R0?AVfuture_error@std@@@8??0future_error@std@@QEAA@AEBV01@@Z32":
        ("R", "_CT??_R0?AVfuture_error@std@@@8??0future_error@std@@QEAA@AEBV01@@Z32"),
    "_CT??_R0?AVlogic_error@std@@@8??0logic_error@std@@QEAA@AEBV01@@Z16":
        ("R", "_CT??_R0?AVlogic_error@std@@@8??0logic_error@std@@QEAA@AEBV01@@Z16"),
    "_CTA3?AVbad_array_new_length@std@@":
        ("R", "_CTA3?AVbad_array_new_length@std@@"),
    "_CTA3?AVfuture_error@std@@":
        ("R", "_CTA3?AVfuture_error@std@@"),
    "_TI3?AVbad_array_new_length@std@@":
        ("R", "_TI3?AVbad_array_new_length@std@@"),
    "_TI3?AVfuture_error@std@@":
        ("R", "_TI3?AVfuture_error@std@@"),
    "__isa_available_default":
        ("B", "__isa_available_default"),
    "__local_stdio_printf_options":
        ("T", "__local_stdio_printf_options"),
    "__local_stdio_scanf_options":
        ("T", "__local_stdio_scanf_options"),
    "_snprintf":
        ("T", "_snprintf"),
    "fprintf":
        ("T", "fprintf"),
    "printf":
        ("T", "printf"),
    "snprintf":
        ("T", "snprintf"),
    "sprintf_s":
        ("T", "sprintf_s"),
    "sscanf":
        ("T", "sscanf"),
}
_LITERAL = re.compile(
    r"(?:__real@(?:[0-9a-f]{8}|[0-9a-f]{16})|"
    r"__xmm@[0-9a-f]{32}|__ymm@[0-9a-f]{64}|"
    r"_GUID_[0-9a-f]{8}_[0-9a-f]{4}_[0-9a-f]{4}_"
    r"[0-9a-f]{4}_[0-9a-f]{12})")

MSVC_DELETE_WRAPPER = "?__global_delete@@YAXPEAX_K@Z"
MSVC_DELETE_FALLBACK = "?__empty_global_delete@@YAXPEAX_K@Z"
MSVC_DELETE_DECLARATION = "void __cdecl __global_delete(void *, unsigned __int64)"


def _same_declaration(actual, expected):
    # LLVM demangler versions differ only in whether commas have one space.
    # Preserve all other whitespace and reject control characters.
    return (isinstance(actual, str) and
            not any(ord(char) < 32 or ord(char) == 127 for char in actual) and
            actual.replace(", ", ",") == expected.replace(", ", ","))


def msvc_runtime_definition(name, demangled, kinds):
    if _LITERAL.fullmatch(name):
        return set(kinds) == {"R"} and demangled == name
    expected = _DEFINITIONS.get(name)
    return bool(expected and set(kinds) == {expected[0]} and
                _same_declaration(demangled, expected[1]))


def msvc_delete_weak_reference(name, demangled, kinds):
    # This is only a candidate: the caller must also prove the exact COFF
    # fallback edge to a real private definition of MSVC_DELETE_FALLBACK.
    return (name == MSVC_DELETE_WRAPPER and set(kinds) == {"w"} and
            _same_declaration(demangled, MSVC_DELETE_DECLARATION))
