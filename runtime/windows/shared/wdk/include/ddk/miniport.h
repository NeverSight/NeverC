/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    miniport.h

Abstract:

    Type definitions for miniport drivers.

Revision History:

--*/

#ifndef _MINIPORT_
#define _MINIPORT_

#include "stddef.h"

#define ASSERT( exp )

#ifndef FAR
#define FAR
#endif

#if defined(_MSC_VER)
#if (_MSC_VER >= 1200)
#pragma warning(push)
#pragma warning(disable:4201) // nameless struct/union
#pragma warning(disable:4214) // bit field other than int
#endif
#endif


#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#ifndef OPTIONAL
#define OPTIONAL
#endif

#ifndef NOTHING
#define NOTHING
#endif

#ifndef CRITICAL
#define CRITICAL
#endif

#ifndef ANYSIZE_ARRAY
#define ANYSIZE_ARRAY 1       // winnt
#endif

// begin_winnt

//
// For compilers that don't support nameless unions/structs
//
#ifndef DUMMYUNIONNAME
#if defined(NONAMELESSUNION) || !defined(_MSC_EXTENSIONS)
#define DUMMYUNIONNAME   u
#define DUMMYUNIONNAME2  u2
#define DUMMYUNIONNAME3  u3
#define DUMMYUNIONNAME4  u4
#define DUMMYUNIONNAME5  u5
#define DUMMYUNIONNAME6  u6
#define DUMMYUNIONNAME7  u7
#define DUMMYUNIONNAME8  u8
#define DUMMYUNIONNAME9  u9
#else
#define DUMMYUNIONNAME
#define DUMMYUNIONNAME2
#define DUMMYUNIONNAME3
#define DUMMYUNIONNAME4
#define DUMMYUNIONNAME5
#define DUMMYUNIONNAME6
#define DUMMYUNIONNAME7
#define DUMMYUNIONNAME8
#define DUMMYUNIONNAME9
#endif
#endif // DUMMYUNIONNAME

#ifndef DUMMYSTRUCTNAME
#if defined(NONAMELESSUNION) || !defined(_MSC_EXTENSIONS)
#define DUMMYSTRUCTNAME  s
#define DUMMYSTRUCTNAME2 s2
#define DUMMYSTRUCTNAME3 s3
#define DUMMYSTRUCTNAME4 s4
#define DUMMYSTRUCTNAME5 s5
#else
#define DUMMYSTRUCTNAME
#define DUMMYSTRUCTNAME2
#define DUMMYSTRUCTNAME3
#define DUMMYSTRUCTNAME4
#define DUMMYSTRUCTNAME5
#endif
#endif // DUMMYSTRUCTNAME

#include <specstrings.h>
#include <kernelspecs.h>

#if defined(STRICT_GS_ENABLED)
#pragma strict_gs_check(push, on)
#endif

#if defined(_M_MRX000) && !(defined(MIDL_PASS) || defined(RC_INVOKED)) && defined(ENABLE_RESTRICTED)
#define RESTRICTED_POINTER __restrict
#else
#define RESTRICTED_POINTER
#endif

#if defined(_M_MRX000) || defined(_M_ALPHA) || defined(_M_PPC) || defined(_M_IA64) || defined(_M_AMD64)
#define ALIGNMENT_MACHINE
#define UNALIGNED __unaligned
#if defined(_WIN64)
#define UNALIGNED64 __unaligned
#else
#define UNALIGNED64
#endif
#else
#undef ALIGNMENT_MACHINE
#define UNALIGNED
#define UNALIGNED64
#endif


#if defined(_WIN64) || defined(_M_ALPHA)
#define MAX_NATURAL_ALIGNMENT sizeof(ULONGLONG)
#define MEMORY_ALLOCATION_ALIGNMENT 16
#else
#define MAX_NATURAL_ALIGNMENT sizeof(ULONG)
#define MEMORY_ALLOCATION_ALIGNMENT 8
#endif

//
// TYPE_ALIGNMENT will return the alignment requirements of a given type for
// the current platform.
//

#ifdef __cplusplus
#if _MSC_VER >= 1300
#define TYPE_ALIGNMENT( t ) __alignof(t)
#endif
#else
#define TYPE_ALIGNMENT( t ) \
    FIELD_OFFSET( struct { char x; t test; }, test )
#endif

#if defined(_WIN64)

#if defined(_AMD64_)
#define PROBE_ALIGNMENT( _s ) TYPE_ALIGNMENT( ULONG )
#elif defined(_IA64_)
#define PROBE_ALIGNMENT( _s ) (TYPE_ALIGNMENT( _s ) > TYPE_ALIGNMENT( ULONG ) ? \
                              TYPE_ALIGNMENT( _s ) : TYPE_ALIGNMENT( ULONG ))
#else
#error "No Target Architecture"
#endif

#define PROBE_ALIGNMENT32( _s ) TYPE_ALIGNMENT( ULONG )

#else

#define PROBE_ALIGNMENT( _s ) TYPE_ALIGNMENT( ULONG )

#endif

//
// C_ASSERT() can be used to perform many compile-time assertions:
//            type sizes, field offsets, etc.
//
// An assertion failure results in error C2118: negative subscript.
//

#ifndef SORTPP_PASS
#define C_ASSERT(e) typedef char __C_ASSERT__[(e)?1:-1]
#else
#define C_ASSERT(e) /* nothing */
#endif

#include <basetsd.h>

// end_winnt

#ifndef CONST
#define CONST               const
#endif

// begin_winnt

#if (defined(_M_IX86) || defined(_M_IA64) || defined(_M_AMD64)) && !defined(MIDL_PASS)
#define DECLSPEC_IMPORT __declspec(dllimport)
#else
#define DECLSPEC_IMPORT
#endif

#ifndef DECLSPEC_NORETURN
#if (_MSC_VER >= 1200) && !defined(MIDL_PASS)
#define DECLSPEC_NORETURN   __declspec(noreturn)
#else
#define DECLSPEC_NORETURN
#endif
#endif

#ifndef DECLSPEC_NOTHROW
#if (_MSC_VER >= 1200) && !defined(MIDL_PASS)
#define DECLSPEC_NOTHROW   __declspec(nothrow)
#else
#define DECLSPEC_NOTHROW
#endif
#endif

#ifndef DECLSPEC_ALIGN
#if (_MSC_VER >= 1300) && !defined(MIDL_PASS)
#define DECLSPEC_ALIGN(x)   __declspec(align(x))
#else
#define DECLSPEC_ALIGN(x)
#endif
#endif

#ifndef SYSTEM_CACHE_ALIGNMENT_SIZE
#if defined(_AMD64_) || defined(_X86_)
#define SYSTEM_CACHE_ALIGNMENT_SIZE 64
#else
#define SYSTEM_CACHE_ALIGNMENT_SIZE 128
#endif
#endif

#ifndef DECLSPEC_CACHEALIGN
#define DECLSPEC_CACHEALIGN DECLSPEC_ALIGN(SYSTEM_CACHE_ALIGNMENT_SIZE)
#endif

#ifndef DECLSPEC_UUID
#if (_MSC_VER >= 1100) && defined (__cplusplus)
#define DECLSPEC_UUID(x)    __declspec(uuid(x))
#else
#define DECLSPEC_UUID(x)
#endif
#endif

#ifndef DECLSPEC_NOVTABLE
#if (_MSC_VER >= 1100) && defined(__cplusplus)
#define DECLSPEC_NOVTABLE   __declspec(novtable)
#else
#define DECLSPEC_NOVTABLE
#endif
#endif

#ifndef DECLSPEC_SELECTANY
#if (_MSC_VER >= 1100)
#define DECLSPEC_SELECTANY  __declspec(selectany)
#else
#define DECLSPEC_SELECTANY
#endif
#endif

#ifndef NOP_FUNCTION
#if (_MSC_VER >= 1210)
#define NOP_FUNCTION __noop
#else
#define NOP_FUNCTION (void)0
#endif
#endif

#ifndef DECLSPEC_ADDRSAFE
#if (_MSC_VER >= 1200) && (defined(_M_ALPHA) || defined(_M_AXP64))
#define DECLSPEC_ADDRSAFE  __declspec(address_safe)
#else
#define DECLSPEC_ADDRSAFE
#endif
#endif

#ifndef DECLSPEC_NOINLINE
#if (_MSC_VER >= 1300)
#define DECLSPEC_NOINLINE  __declspec(noinline)
#else
#define DECLSPEC_NOINLINE
#endif
#endif

#ifndef FORCEINLINE
#if (_MSC_VER >= 1200)
#define FORCEINLINE __forceinline
#else
#define FORCEINLINE __inline
#endif
#endif

#ifndef DECLSPEC_DEPRECATED
#if (_MSC_VER >= 1300) && !defined(MIDL_PASS)
#define DECLSPEC_DEPRECATED   __declspec(deprecated)
#define DEPRECATE_SUPPORTED
#else
#define DECLSPEC_DEPRECATED
#undef  DEPRECATE_SUPPORTED
#endif
#endif

#ifdef DEPRECATE_DDK_FUNCTIONS
#ifdef _NTDDK_
#define DECLSPEC_DEPRECATED_DDK DECLSPEC_DEPRECATED
#ifdef DEPRECATE_SUPPORTED
#define PRAGMA_DEPRECATED_DDK 1
#endif
#else
#define DECLSPEC_DEPRECATED_DDK
#define PRAGMA_DEPRECATED_DDK 1
#endif
#else
#define DECLSPEC_DEPRECATED_DDK
#define PRAGMA_DEPRECATED_DDK 0
#endif

//
// Void
//

typedef void *PVOID;
typedef void * POINTER_64 PVOID64;

// end_winnt

#ifndef _MANAGED
#if defined(_M_IX86)
#define FASTCALL __fastcall
#else
#define FASTCALL
#endif
#else
#define FASTCALL NTAPI
#endif


//
// Basics
//

#ifndef VOID
#define VOID void
typedef char CHAR;
typedef short SHORT;
typedef long LONG;
#if !defined(MIDL_PASS)
typedef int INT;
#endif
#endif

//
// UNICODE (Wide Character) types
//

#ifndef _MAC
typedef wchar_t WCHAR;    // wc,   16-bit UNICODE character
#else
// some Macintosh compilers don't define wchar_t in a convenient location, or define it as a char
typedef unsigned short WCHAR;    // wc,   16-bit UNICODE character
#endif

typedef WCHAR *PWCHAR, *LPWCH, *PWCH;
typedef CONST WCHAR *LPCWCH, *PCWCH;

typedef __nullterminated WCHAR *NWPSTR, *LPWSTR, *PWSTR;
typedef __nullterminated PWSTR *PZPWSTR;
typedef __nullterminated CONST PWSTR *PCZPWSTR;
typedef __nullterminated WCHAR UNALIGNED *LPUWSTR, *PUWSTR;
typedef __nullterminated CONST WCHAR *LPCWSTR, *PCWSTR;
typedef __nullterminated PCWSTR *PZPCWSTR;
typedef __nullterminated CONST WCHAR UNALIGNED *LPCUWSTR, *PCUWSTR;

typedef __nullnullterminated WCHAR *PZZWSTR;
typedef __nullnullterminated CONST WCHAR *PCZZWSTR;
typedef __nullnullterminated WCHAR UNALIGNED *PUZZWSTR;
typedef __nullnullterminated CONST WCHAR UNALIGNED *PCUZZWSTR;

typedef __possibly_notnullterminated WCHAR *PNZWCH;
typedef __possibly_notnullterminated CONST WCHAR *PCNZWCH;
typedef __possibly_notnullterminated WCHAR UNALIGNED *PUNZWCH;
typedef __possibly_notnullterminated CONST WCHAR UNALIGNED *PCUNZWCH;

#if _WIN32_WINNT >= 0x0600 || (defined(__cplusplus) && defined(WINDOWS_ENABLE_CPLUSPLUS))

typedef CONST WCHAR *LPCWCHAR, *PCWCHAR;
typedef CONST WCHAR UNALIGNED *LPCUWCHAR, *PCUWCHAR;

//
//  UCS (Universal Character Set) types
//

typedef unsigned long UCSCHAR;

//
//  Even pre-Unicode agreement, UCS values are always in the
//  range U+00000000 to U+7FFFFFFF, so we'll pick an obvious
//  value.

#define UCSCHAR_INVALID_CHARACTER (0xffffffff)

#define MIN_UCSCHAR (0)

//
//  We'll assume here that the ISO-10646 / Unicode agreement
//  not to assign code points after U+0010FFFF holds so that
//  we do not have to have separate "UCSCHAR" and "UNICODECHAR"
//  types.
//

#define MAX_UCSCHAR (0x0010FFFF)

typedef UCSCHAR *PUCSCHAR;
typedef const UCSCHAR *PCUCSCHAR;

typedef UCSCHAR *PUCSSTR;
typedef UCSCHAR UNALIGNED *PUUCSSTR;

typedef const UCSCHAR *PCUCSSTR;
typedef const UCSCHAR UNALIGNED *PCUUCSSTR;

typedef UCSCHAR UNALIGNED *PUUCSCHAR;
typedef const UCSCHAR UNALIGNED *PCUUCSCHAR;

#endif // _WIN32_WINNT >= 0x0600


//
// ANSI (Multi-byte Character) types
//
typedef CHAR *PCHAR, *LPCH, *PCH;
typedef CONST CHAR *LPCCH, *PCCH;

typedef __nullterminated CHAR *NPSTR, *LPSTR, *PSTR;
typedef __nullterminated PSTR *PZPSTR;
typedef __nullterminated CONST PSTR *PCZPSTR;
typedef __nullterminated CONST CHAR *LPCSTR, *PCSTR;
typedef __nullterminated PCSTR *PZPCSTR;

typedef __nullnullterminated CHAR *PZZSTR;
typedef __nullnullterminated CONST CHAR *PCZZSTR;

typedef __possibly_notnullterminated CHAR *PNZCH;
typedef __possibly_notnullterminated CONST CHAR *PCNZCH;

//
// Neutral ANSI/UNICODE types and macros
//
#ifdef  UNICODE                     // r_winnt

#ifndef _TCHAR_DEFINED
typedef WCHAR TCHAR, *PTCHAR;
typedef WCHAR TUCHAR, *PTUCHAR;
#define _TCHAR_DEFINED
#endif /* !_TCHAR_DEFINED */

typedef LPWCH LPTCH, PTCH;
typedef LPCWCH LPCTCH, PCTCH;
typedef LPWSTR PTSTR, LPTSTR;
typedef LPCWSTR PCTSTR, LPCTSTR;
typedef LPUWSTR PUTSTR, LPUTSTR;
typedef LPCUWSTR PCUTSTR, LPCUTSTR;
typedef LPWSTR LP;
typedef PZZWSTR PZZTSTR;
typedef PCZZWSTR PCZZTSTR;
typedef PUZZWSTR PUZZTSTR;
typedef PCUZZWSTR PCUZZTSTR;
typedef PNZWCH PNZTCH;
typedef PCNZWCH PCNZTCH;
typedef PUNZWCH PUNZTCH;
typedef PCUNZWCH PCUNZTCH;
#define __TEXT(quote) L##quote      // r_winnt

#else   /* UNICODE */               // r_winnt

#ifndef _TCHAR_DEFINED
typedef char TCHAR, *PTCHAR;
typedef unsigned char TUCHAR, *PTUCHAR;
#define _TCHAR_DEFINED
#endif /* !_TCHAR_DEFINED */

typedef LPCH LPTCH, PTCH;
typedef LPCCH LPCTCH, PCTCH;
typedef LPSTR PTSTR, LPTSTR, PUTSTR, LPUTSTR;
typedef LPCSTR PCTSTR, LPCTSTR, PCUTSTR, LPCUTSTR;
typedef PZZSTR PZZTSTR, PUZZTSTR;
typedef PCZZSTR PCZZTSTR, PCUZZTSTR;
typedef PNZCH PNZTCH, PUNZTCH;
typedef PCNZCH PCNZTCH, PCUNZTCH;
#define __TEXT(quote) quote         // r_winnt

#endif /* UNICODE */                // r_winnt
#define TEXT(quote) __TEXT(quote)   // r_winnt


// end_winnt

//
// The type QUAD and UQUAD are intended to use when a 8 byte aligned structure
// is required, but it is not a floating point number.
//

typedef double DOUBLE;

typedef struct _QUAD {
    union {
        __int64 UseThisFieldToCopy;
        double  DoNotUseThisField;
    } DUMMYUNIONNAME;

} QUAD;

//
// Pointer to Basics
//

typedef SHORT *PSHORT;  // winnt
typedef LONG *PLONG;    // winnt
typedef QUAD *PQUAD;

//
// Unsigned Basics
//

// Tell windef.h that some types are already defined.
#define BASETYPES

typedef unsigned char UCHAR;
typedef unsigned short USHORT;
typedef unsigned long ULONG;
typedef QUAD UQUAD;

//
// Pointer to Unsigned Basics
//

typedef UCHAR *PUCHAR;
typedef USHORT *PUSHORT;
typedef ULONG *PULONG;
typedef UQUAD *PUQUAD;

#if _WIN32_WINNT >= 0x0600 || (defined(__cplusplus) && defined(WINDOWS_ENABLE_CPLUSPLUS))

//
// Pointer to Const Unsigned Basics
//

typedef CONST UCHAR *PCUCHAR;
typedef CONST USHORT *PCUSHORT;
typedef CONST ULONG *PCULONG;
typedef CONST UQUAD *PCUQUAD;

#endif // _WIN32_WINNT >= 0x0600

//
// Signed characters
//

typedef signed char SCHAR;
typedef SCHAR *PSCHAR;

#if _WIN32_WINNT >= 0x0600 || (defined(__cplusplus) && defined(WINDOWS_ENABLE_CPLUSPLUS))

typedef CONST SCHAR *PCSCHAR;

#endif // _WIN32_WINNT >= 0x0600

#ifndef NO_STRICT
#ifndef STRICT
#define STRICT 1
#endif
#endif

// begin_winnt

#define ALL_PROCESSOR_GROUPS        0xffff

//
// Structure to represent a system wide processor number. It contains a
// group number and relative processor number within the group.
//

typedef struct _PROCESSOR_NUMBER {
    USHORT Group;
    UCHAR Number;
    UCHAR Reserved;
} PROCESSOR_NUMBER, *PPROCESSOR_NUMBER;

//
// Structure to represent a group-specific affinity, such as that of a
// thread.  Specifies the group number and the affinity within that group.
//

typedef struct _GROUP_AFFINITY {
    KAFFINITY Mask;
    USHORT Group;
    USHORT Reserved[3];
} GROUP_AFFINITY, *PGROUP_AFFINITY;

//
// Handle to an Object
//

#ifdef STRICT
typedef void *HANDLE;
#if 0 && (_MSC_VER > 1000)
#define DECLARE_HANDLE(name) struct name##__; typedef struct name##__ *name
#else
#define DECLARE_HANDLE(name) struct name##__{int unused;}; typedef struct name##__ *name
#endif
#else
typedef PVOID HANDLE;
#define DECLARE_HANDLE(name) typedef HANDLE name
#endif
typedef HANDLE *PHANDLE;

//
// Flag (bit) fields
//

typedef UCHAR  FCHAR;
typedef USHORT FSHORT;
typedef ULONG  FLONG;

// Component Object Model defines, and macros

#ifndef _HRESULT_DEFINED
#define _HRESULT_DEFINED
#ifdef __midl
typedef LONG HRESULT;
#else
typedef __success(return >= 0) long HRESULT;
#endif // __midl
#endif // !_HRESULT_DEFINED

#ifdef __cplusplus
    #define EXTERN_C    extern "C"
#else
    #define EXTERN_C    extern
#endif

#if defined(_WIN32) || defined(_MPPC_)

// Win32 doesn't support __export

#ifdef _68K_
#define STDMETHODCALLTYPE       __cdecl
#else
#define STDMETHODCALLTYPE       __stdcall
#endif
#define STDMETHODVCALLTYPE      __cdecl

#define STDAPICALLTYPE          __stdcall
#define STDAPIVCALLTYPE         __cdecl

#else

#define STDMETHODCALLTYPE       __export __stdcall
#define STDMETHODVCALLTYPE      __export __cdecl

#define STDAPICALLTYPE          __export __stdcall
#define STDAPIVCALLTYPE         __export __cdecl

#endif


#define STDAPI                  EXTERN_C HRESULT STDAPICALLTYPE
#define STDAPI_(type)           EXTERN_C type STDAPICALLTYPE

#define STDMETHODIMP            HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(type)     type STDMETHODCALLTYPE

#define STDOVERRIDEMETHODIMP        __override STDMETHODIMP
#define STDOVERRIDEMETHODIMP_(type) __override STDMETHODIMP_(type)

#define IFACEMETHODIMP          __override STDMETHODIMP
#define IFACEMETHODIMP_(type)   __override STDMETHODIMP_(type)

// The 'V' versions allow Variable Argument lists.

#define STDAPIV                 EXTERN_C HRESULT STDAPIVCALLTYPE
#define STDAPIV_(type)          EXTERN_C type STDAPIVCALLTYPE

#define STDMETHODIMPV           HRESULT STDMETHODVCALLTYPE
#define STDMETHODIMPV_(type)    type STDMETHODVCALLTYPE

#define STDOVERRIDEMETHODIMPV        __override STDMETHODIMPV
#define STDOVERRIDEMETHODIMPV_(type) __override STDMETHODIMPV_(type)

#define IFACEMETHODIMPV          __override STDMETHODIMPV
#define IFACEMETHODIMPV_(type)   __override STDMETHODIMPV_(type)

// end_winnt


//
// Low order two bits of a handle are ignored by the system and available
// for use by application code as tag bits.  The remaining bits are opaque
// and used to store a serial number and table index.
//

#define OBJ_HANDLE_TAGBITS  0x00000003L

//
// Cardinal Data Types [0 - 2**N-2)
//

typedef char CCHAR;          // winnt
typedef short CSHORT;
typedef ULONG CLONG;

typedef CCHAR *PCCHAR;
typedef CSHORT *PCSHORT;
typedef CLONG *PCLONG;


//
// __int64 is only supported by 2.0 and later midl.
// __midl is set by the 2.0 midl and not by 1.0 midl.
//

#define _ULONGLONG_
#if (!defined (_MAC) && (!defined(MIDL_PASS) || defined(__midl)) && (!defined(_M_IX86) || (defined(_INTEGRAL_MAX_BITS) && _INTEGRAL_MAX_BITS >= 64)))
typedef __int64 LONGLONG;
typedef unsigned __int64 ULONGLONG;

#define MAXLONGLONG                         (0x7fffffffffffffff)


#else

#if defined(_MAC) && defined(_MAC_INT_64)
typedef __int64 LONGLONG;
typedef unsigned __int64 ULONGLONG;

#define MAXLONGLONG                      (0x7fffffffffffffff)


#else
typedef double LONGLONG;
typedef double ULONGLONG;
#endif //_MAC and int64

#endif

typedef LONGLONG *PLONGLONG;
typedef ULONGLONG *PULONGLONG;

// Update Sequence Number

typedef LONGLONG USN;

#if defined(MIDL_PASS)
typedef struct _LARGE_INTEGER {
#else // MIDL_PASS
typedef union _LARGE_INTEGER {
    struct {
        ULONG LowPart;
        LONG HighPart;
    } DUMMYSTRUCTNAME;
    struct {
        ULONG LowPart;
        LONG HighPart;
    } u;
#endif //MIDL_PASS
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef LARGE_INTEGER *PLARGE_INTEGER;

#if defined(MIDL_PASS)
typedef struct _ULARGE_INTEGER {
#else // MIDL_PASS
typedef union _ULARGE_INTEGER {
    struct {
        ULONG LowPart;
        ULONG HighPart;
    } DUMMYSTRUCTNAME;
    struct {
        ULONG LowPart;
        ULONG HighPart;
    } u;
#endif //MIDL_PASS
    ULONGLONG QuadPart;
} ULARGE_INTEGER;

typedef ULARGE_INTEGER *PULARGE_INTEGER;


//
// Physical address.
//

typedef LARGE_INTEGER PHYSICAL_ADDRESS, *PPHYSICAL_ADDRESS;


//
// Boolean
//

typedef UCHAR BOOLEAN;           // winnt
typedef BOOLEAN *PBOOLEAN;       // winnt


//
// Constants
//

#define FALSE   0
#define TRUE    1

#ifndef NULL
#ifdef __cplusplus
#define NULL    0
#define NULL64  0
#else
#define NULL    ((void *)0)
#define NULL64  ((void * POINTER_64)0)
#endif
#endif // NULL

//
// Calculate the byte offset of a field in a structure of type type.
//

#define FIELD_OFFSET(type, field)    ((LONG)(LONG_PTR)&(((type *)0)->field))

//
// Calculate the size of a field in a structure of type type, without
// knowing or stating the type of the field.
//
#define RTL_FIELD_SIZE(type, field) (sizeof(((type *)0)->field))

//
// Calculate the size of a structure of type type up through and
// including a field.
//
#define RTL_SIZEOF_THROUGH_FIELD(type, field) \
    (FIELD_OFFSET(type, field) + RTL_FIELD_SIZE(type, field))

//
//  RTL_CONTAINS_FIELD usage:
//
//      if (RTL_CONTAINS_FIELD(pBlock, pBlock->cbSize, dwMumble)) { // safe to use pBlock->dwMumble
//
#define RTL_CONTAINS_FIELD(Struct, Size, Field) \
    ( (((PCHAR)(&(Struct)->Field)) + sizeof((Struct)->Field)) <= (((PCHAR)(Struct))+(Size)) )

//
// Return the number of elements in a statically sized array.
//   ULONG Buffer[100];
//   RTL_NUMBER_OF(Buffer) == 100
// This is also popularly known as: NUMBER_OF, ARRSIZE, _countof, NELEM, etc.
//
#define RTL_NUMBER_OF_V1(A) (sizeof(A)/sizeof((A)[0]))

#if defined(__cplusplus) && \
    !defined(MIDL_PASS) && \
    !defined(RC_INVOKED) && \
    !defined(_PREFAST_) && \
    (_MSC_FULL_VER >= 13009466) && \
    !defined(SORTPP_PASS)
//
// RtlpNumberOf is a function that takes a reference to an array of N Ts.
//
// typedef T array_of_T[N];
// typedef array_of_T &reference_to_array_of_T;
//
// RtlpNumberOf returns a pointer to an array of N chars.
// We could return a reference instead of a pointer but older compilers do not accept that.
//
// typedef char array_of_char[N];
// typedef array_of_char *pointer_to_array_of_char;
//
// sizeof(array_of_char) == N
// sizeof(*pointer_to_array_of_char) == N
//
// pointer_to_array_of_char RtlpNumberOf(reference_to_array_of_T);
//
// We never even call RtlpNumberOf, we just take the size of dereferencing its return type.
// We do not even implement RtlpNumberOf, we just decare it.
//
// Attempts to pass pointers instead of arrays to this macro result in compile time errors.
// That is the point.
//
extern "C++" // templates cannot be declared to have 'C' linkage
template <typename T, size_t N>
char (*RtlpNumberOf( UNALIGNED T (&)[N] ))[N];

#define RTL_NUMBER_OF_V2(A) (sizeof(*RtlpNumberOf(A)))

//
// This does not work with:
//
// void Foo()
// {
//    struct { int x; } y[2];
//    RTL_NUMBER_OF_V2(y); // illegal use of anonymous local type in template instantiation
// }
//
// You must instead do:
//
// struct Foo1 { int x; };
//
// void Foo()
// {
//    Foo1 y[2];
//    RTL_NUMBER_OF_V2(y); // ok
// }
//
// OR
//
// void Foo()
// {
//    struct { int x; } y[2];
//    RTL_NUMBER_OF_V1(y); // ok
// }
//
// OR
//
// void Foo()
// {
//    struct { int x; } y[2];
//    _ARRAYSIZE(y); // ok
// }
//

#else
#define RTL_NUMBER_OF_V2(A) RTL_NUMBER_OF_V1(A)
#endif

#ifdef ENABLE_RTL_NUMBER_OF_V2
#define RTL_NUMBER_OF(A) RTL_NUMBER_OF_V2(A)
#else
#define RTL_NUMBER_OF(A) RTL_NUMBER_OF_V1(A)
#endif

//
// ARRAYSIZE is more readable version of RTL_NUMBER_OF_V2, and uses
// it regardless of ENABLE_RTL_NUMBER_OF_V2
//
// _ARRAYSIZE is a version useful for anonymous types
//
#define ARRAYSIZE(A)    RTL_NUMBER_OF_V2(A)
#define _ARRAYSIZE(A)   RTL_NUMBER_OF_V1(A)

//
// An expression that yields the type of a field in a struct.
//
#define RTL_FIELD_TYPE(type, field) (((type*)0)->field)

// RTL_ to avoid collisions in the global namespace.
//
// Given typedef struct _FOO { BYTE Bar[123]; } FOO;
// RTL_NUMBER_OF_FIELD(FOO, Bar) == 123
//
#define RTL_NUMBER_OF_FIELD(type, field) (RTL_NUMBER_OF(RTL_FIELD_TYPE(type, field)))

//
// eg:
// typedef struct FOO {
//   ULONG Integer;
//   PVOID Pointer;
// } FOO;
//
// RTL_PADDING_BETWEEN_FIELDS(FOO, Integer, Pointer) == 0 for Win32, 4 for Win64
//
#define RTL_PADDING_BETWEEN_FIELDS(T, F1, F2) \
    ((FIELD_OFFSET(T, F2) > FIELD_OFFSET(T, F1)) \
        ? (FIELD_OFFSET(T, F2) - FIELD_OFFSET(T, F1) - RTL_FIELD_SIZE(T, F1)) \
        : (FIELD_OFFSET(T, F1) - FIELD_OFFSET(T, F2) - RTL_FIELD_SIZE(T, F2)))

// RTL_ to avoid collisions in the global namespace.
#if defined(__cplusplus)
#define RTL_CONST_CAST(type) const_cast<type>
#else
#define RTL_CONST_CAST(type) (type)
#endif

// end_winnt
//
// This works "generically" for Unicode and Ansi/Oem strings.
// Usage:
//   const static UNICODE_STRING FooU = RTL_CONSTANT_STRING(L"Foo");
//   const static         STRING Foo  = RTL_CONSTANT_STRING( "Foo");
// instead of the slower:
//   UNICODE_STRING FooU;
//           STRING Foo;
//   RtlInitUnicodeString(&FooU, L"Foo");
//          RtlInitString(&Foo ,  "Foo");
//
// Or:
//   const static char szFoo[] = "Foo";
//   const static STRING sFoo = RTL_CONSTANT_STRING(szFoo);
//
// This will compile without error or warning in C++. C will get a warning.
//
#ifdef __cplusplus
extern "C++"
{
char _RTL_CONSTANT_STRING_type_check(const char *s);
char _RTL_CONSTANT_STRING_type_check(const WCHAR *s);
// __typeof would be desirable here instead of sizeof.
template <size_t N> class _RTL_CONSTANT_STRING_remove_const_template_class;
template <> class _RTL_CONSTANT_STRING_remove_const_template_class<sizeof(char)>  {public: typedef  char T; };
template <> class _RTL_CONSTANT_STRING_remove_const_template_class<sizeof(WCHAR)> {public: typedef WCHAR T; };
#define _RTL_CONSTANT_STRING_remove_const_macro(s) \
    (const_cast<_RTL_CONSTANT_STRING_remove_const_template_class<sizeof((s)[0])>::T*>(s))
}
#else
char _RTL_CONSTANT_STRING_type_check(const void *s);
#define _RTL_CONSTANT_STRING_remove_const_macro(s) (s)
#endif
#define RTL_CONSTANT_STRING(s) \
{ \
    sizeof( s ) - sizeof( (s)[0] ), \
    sizeof( s ) / sizeof(_RTL_CONSTANT_STRING_type_check(s)), \
    _RTL_CONSTANT_STRING_remove_const_macro(s) \
}
// begin_winnt

// like sizeof
// usually this would be * CHAR_BIT, but we don't necessarily have #include <limits.h>
#define RTL_BITS_OF(sizeOfArg) (sizeof(sizeOfArg) * 8)

#define RTL_BITS_OF_FIELD(type, field) (RTL_BITS_OF(RTL_FIELD_TYPE(type, field)))

//
// Calculate the address of the base of the structure given its type, and an
// address of a field within the structure.
//

#define CONTAINING_RECORD(address, type, field) ((type *)( \
                                                  (PCHAR)(address) - \
                                                  (ULONG_PTR)(&((type *)0)->field)))


//
// Interrupt Request Level (IRQL)
//

typedef UCHAR KIRQL;

typedef KIRQL *PKIRQL;

#include <sdkddkver.h>

//
// Macros used to eliminate compiler warning generated when formal
// parameters or local variables are not declared.
//
// Use DBG_UNREFERENCED_PARAMETER() when a parameter is not yet
// referenced but will be once the module is completely developed.
//
// Use DBG_UNREFERENCED_LOCAL_VARIABLE() when a local variable is not yet
// referenced but will be once the module is completely developed.
//
// Use UNREFERENCED_PARAMETER() if a parameter will never be referenced.
//
// DBG_UNREFERENCED_PARAMETER and DBG_UNREFERENCED_LOCAL_VARIABLE will
// eventually be made into a null macro to help determine whether there
// is unfinished work.
//

#if ! defined(lint)
#define UNREFERENCED_PARAMETER(P)          (P)
#define DBG_UNREFERENCED_PARAMETER(P)      (P)
#define DBG_UNREFERENCED_LOCAL_VARIABLE(V) (V)

#else // lint

// Note: lint -e530 says don't complain about uninitialized variables for
// this varible.  Error 527 has to do with unreachable code.
// -restore restores checking to the -save state

#define UNREFERENCED_PARAMETER(P)          \
    /*lint -save -e527 -e530 */ \
    { \
        (P) = (P); \
    } \
    /*lint -restore */
#define DBG_UNREFERENCED_PARAMETER(P)      \
    /*lint -save -e527 -e530 */ \
    { \
        (P) = (P); \
    } \
    /*lint -restore */
#define DBG_UNREFERENCED_LOCAL_VARIABLE(V) \
    /*lint -save -e527 -e530 */ \
    { \
        (V) = (V); \
    } \
    /*lint -restore */

#endif // lint

//
// Macro used to eliminate compiler warning 4715 within a switch statement
// when all possible cases have already been accounted for.
//
// switch (a & 3) {
//     case 0: return 1;
//     case 1: return Foo();
//     case 2: return Bar();
//     case 3: return 1;
//     DEFAULT_UNREACHABLE;
//

#if (_MSC_VER > 1200)
#define DEFAULT_UNREACHABLE default: __assume(0)
#else

//
// Older compilers do not support __assume(), and there is no other free
// method of eliminating the warning.
//

#define DEFAULT_UNREACHABLE

#endif

#ifdef __cplusplus

// Define operator overloads to enable bit operations on enum values that are 
// used to define flags. Use DEFINE_ENUM_FLAG_OPERATORS(YOUR_TYPE) to enable these 
// operators on YOUR_TYPE.

// Moved here from objbase.w.

#define DEFINE_ENUM_FLAG_OPERATORS(ENUMTYPE) \
extern "C++" { \
inline ENUMTYPE operator | (ENUMTYPE a, ENUMTYPE b) { return ENUMTYPE(((int)a) | ((int)b)); } \
inline ENUMTYPE &operator |= (ENUMTYPE &a, ENUMTYPE b) { return (ENUMTYPE &)(((int &)a) |= ((int)b)); } \
inline ENUMTYPE operator & (ENUMTYPE a, ENUMTYPE b) { return ENUMTYPE(((int)a) & ((int)b)); } \
inline ENUMTYPE &operator &= (ENUMTYPE &a, ENUMTYPE b) { return (ENUMTYPE &)(((int &)a) &= ((int)b)); } \
inline ENUMTYPE operator ~ (ENUMTYPE a) { return ENUMTYPE(~((int)a)); } \
inline ENUMTYPE operator ^ (ENUMTYPE a, ENUMTYPE b) { return ENUMTYPE(((int)a) ^ ((int)b)); } \
inline ENUMTYPE &operator ^= (ENUMTYPE &a, ENUMTYPE b) { return (ENUMTYPE &)(((int &)a) ^= ((int)b)); } \
}
#else
#define DEFINE_ENUM_FLAG_OPERATORS(ENUMTYPE) // NOP, C allows these operators.
#endif

// Compile-time macros for initializing flag values in const data.
// 
// When using DEFINE_ENUM_FLAG_OPERATORS for enum values you should use the macros below
// when you need to initialize global const data.  Without these macros the inline operators 
// from DEFINE_ENUM_FLAG_OPERATORS force a runtime initialization rather than a
// compile time initialization.  This applies even if you have declared the data as const.
#define COMPILETIME_OR_2FLAGS(a,b)          ((UINT)(a)|(UINT)(b))
#define COMPILETIME_OR_3FLAGS(a,b,c)        ((UINT)(a)|(UINT)(b)|(UINT)(c))
#define COMPILETIME_OR_4FLAGS(a,b,c,d)      ((UINT)(a)|(UINT)(b)|(UINT)(c)|(UINT)(d))
#define COMPILETIME_OR_5FLAGS(a,b,c,d,e)    ((UINT)(a)|(UINT)(b)|(UINT)(c)|(UINT)(d)|(UINT)(e))

// end_winnt

//
//  Define standard min and max macros
//

#ifndef NOMINMAX

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

#endif  // NOMINMAX


#ifdef _AMD64_


#if defined(_M_AMD64) && !defined(RC_INVOKED) && !defined(MIDL_PASS)

//
// Define bit test intrinsics.
//

#ifdef __cplusplus
extern "C" {
#endif

#define BitTest _bittest
#define BitTestAndComplement _bittestandcomplement
#define BitTestAndSet _bittestandset
#define BitTestAndReset _bittestandreset
#define InterlockedBitTestAndSet _interlockedbittestandset
#define InterlockedBitTestAndReset _interlockedbittestandreset

#define BitTest64 _bittest64
#define BitTestAndComplement64 _bittestandcomplement64
#define BitTestAndSet64 _bittestandset64
#define BitTestAndReset64 _bittestandreset64
#define InterlockedBitTestAndSet64 _interlockedbittestandset64
#define InterlockedBitTestAndReset64 _interlockedbittestandreset64

__checkReturn
BOOLEAN
_bittest (
    __in_bcount((Offset+7)/8) LONG const *Base,
    __in LONG Offset
    );

BOOLEAN
_bittestandcomplement (
    __inout_bcount((Offset+7)/8) LONG *Base,
    __in LONG Offset
    );

BOOLEAN
_bittestandset (
    __inout_bcount((Offset+7)/8) LONG *Base,
    __in LONG Offset
    );

BOOLEAN
_bittestandreset (
    __inout_bcount((Offset+7)/8) LONG *Base,
    __in LONG Offset
    );

BOOLEAN
_interlockedbittestandset (
    __inout_bcount((Offset+7)/8) __drv_interlocked LONG volatile *Base,
    __in LONG Offset
    );

BOOLEAN
_interlockedbittestandreset (
    __inout_bcount((Offset+7)/8) __drv_interlocked LONG volatile *Base,
    __in LONG Offset
    );

BOOLEAN
_bittest64 (
    __in_bcount((Offset+7)/8) LONG64 const *Base,
    __in LONG64 Offset
    );

BOOLEAN
_bittestandcomplement64 (
    __inout_bcount((Offset+7)/8) LONG64 *Base,
    __in LONG64 Offset
    );

BOOLEAN
_bittestandset64 (
    __inout_bcount((Offset+7)/8) LONG64 *Base,
    __in LONG64 Offset
    );

BOOLEAN
_bittestandreset64 (
    __inout_bcount((Offset+7)/8) LONG64 *Base,
    __in LONG64 Offset
    );

BOOLEAN
_interlockedbittestandset64 (
    __inout_bcount((Offset+7)/8) __drv_interlocked LONG64 volatile *Base,
    __in LONG64 Offset
    );

BOOLEAN
_interlockedbittestandreset64 (
    __inout_bcount((Offset+7)/8) __drv_interlocked LONG64 volatile *Base,
    __in LONG64 Offset
    );

#pragma intrinsic(_bittest)
#pragma intrinsic(_bittestandcomplement)
#pragma intrinsic(_bittestandset)
#pragma intrinsic(_bittestandreset)
#pragma intrinsic(_interlockedbittestandset)
#pragma intrinsic(_interlockedbittestandreset)

#pragma intrinsic(_bittest64)
#pragma intrinsic(_bittestandcomplement64)
#pragma intrinsic(_bittestandset64)
#pragma intrinsic(_bittestandreset64)
#pragma intrinsic(_interlockedbittestandset64)
#pragma intrinsic(_interlockedbittestandreset64)

//
// Define bit scan intrinsics.
//

#define BitScanForward _BitScanForward
#define BitScanReverse _BitScanReverse
#define BitScanForward64 _BitScanForward64
#define BitScanReverse64 _BitScanReverse64

__success(return!=0)
BOOLEAN
_BitScanForward (
    __out ULONG *Index,
    __in ULONG Mask
    );

__success(return!=0)
BOOLEAN
_BitScanReverse (
    __out ULONG *Index,
    __in ULONG Mask
    );

__success(return!=0)
BOOLEAN
_BitScanForward64 (
    __out ULONG *Index,
    __in ULONG64 Mask
    );

__success(return!=0)
BOOLEAN
_BitScanReverse64 (
    __out ULONG *Index,
    __in ULONG64 Mask
    );

#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(_BitScanForward64)
#pragma intrinsic(_BitScanReverse64)

//
// Interlocked intrinsic functions.
//

#define InterlockedIncrement16 _InterlockedIncrement16
#define InterlockedDecrement16 _InterlockedDecrement16
#define InterlockedCompareExchange16 _InterlockedCompareExchange16

#define InterlockedAnd _InterlockedAnd
#define InterlockedAndAcquire _InterlockedAnd
#define InterlockedAndRelease _InterlockedAnd
#define InterlockedOr _InterlockedOr
#define InterlockedOrAcquire _InterlockedOr
#define InterlockedOrRelease _InterlockedOr
#define InterlockedXor _InterlockedXor
#define InterlockedIncrement _InterlockedIncrement
#define InterlockedIncrementAcquire InterlockedIncrement
#define InterlockedIncrementRelease InterlockedIncrement
#define InterlockedDecrement _InterlockedDecrement
#define InterlockedDecrementAcquire InterlockedDecrement
#define InterlockedDecrementRelease InterlockedDecrement
#define InterlockedAdd _InterlockedAdd
#define InterlockedExchange _InterlockedExchange
#define InterlockedExchangeAdd _InterlockedExchangeAdd
#define InterlockedCompareExchange _InterlockedCompareExchange
#define InterlockedCompareExchangeAcquire InterlockedCompareExchange
#define InterlockedCompareExchangeRelease InterlockedCompareExchange

#define InterlockedAnd64 _InterlockedAnd64
#define InterlockedAnd64Acquire _InterlockedAnd64
#define InterlockedAnd64Release _InterlockedAnd64
#define InterlockedAndAffinity InterlockedAnd64
#define InterlockedOr64 _InterlockedOr64
#define InterlockedOr64Acquire _InterlockedOr64
#define InterlockedOr64Release _InterlockedOr64
#define InterlockedOrAffinity InterlockedOr64
#define InterlockedXor64 _InterlockedXor64
#define InterlockedIncrement64 _InterlockedIncrement64
#define InterlockedDecrement64 _InterlockedDecrement64
#define InterlockedAdd64 _InterlockedAdd64
#define InterlockedExchange64 _InterlockedExchange64
#define InterlockedExchangeAcquire64 InterlockedExchange64
#define InterlockedExchangeAdd64 _InterlockedExchangeAdd64
#define InterlockedCompareExchange64 _InterlockedCompareExchange64
#define InterlockedCompareExchangeAcquire64 InterlockedCompareExchange64
#define InterlockedCompareExchangeRelease64 InterlockedCompareExchange64

#define InterlockedExchangePointer _InterlockedExchangePointer
#define InterlockedCompareExchangePointer _InterlockedCompareExchangePointer
#define InterlockedCompareExchangePointerAcquire _InterlockedCompareExchangePointer
#define InterlockedCompareExchangePointerRelease _InterlockedCompareExchangePointer

#define InterlockedExchangeAddSizeT(a, b) InterlockedExchangeAdd64((LONG64 *)a, b)
#define InterlockedIncrementSizeT(a) InterlockedIncrement64((LONG64 *)a)
#define InterlockedDecrementSizeT(a) InterlockedDecrement64((LONG64 *)a)

SHORT
InterlockedIncrement16 (
    __inout __drv_interlocked SHORT volatile *Addend
    );

SHORT
InterlockedDecrement16 (
    __inout __drv_interlocked SHORT volatile *Addend
    );

SHORT
InterlockedCompareExchange16 (
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT ExChange,
    __in SHORT Comperand
    );

LONG
InterlockedAnd (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG Value
    );

LONG
InterlockedOr (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG Value
    );

LONG
InterlockedXor (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG Value
    );

LONG64
InterlockedAnd64 (
    __inout __drv_interlocked LONG64 volatile *Destination,
    __in LONG64 Value
    );

LONG64
InterlockedOr64 (
    __inout __drv_interlocked LONG64 volatile *Destination,
    __in LONG64 Value
    );

LONG64
InterlockedXor64 (
    __inout __drv_interlocked LONG64 volatile *Destination,
    __in LONG64 Value
    );

LONG
InterlockedIncrement(
    __inout __drv_interlocked LONG volatile *Addend
    );

LONG
InterlockedDecrement(
    __inout __drv_interlocked LONG volatile *Addend
    );

LONG
InterlockedExchange(
    __inout __drv_interlocked LONG volatile *Target,
    __in LONG Value
    );

LONG
InterlockedExchangeAdd(
    __inout __drv_interlocked LONG volatile *Addend,
    __in LONG Value
    );

#if !defined(_X86AMD64_)

__forceinline
LONG
InterlockedAdd(
    __inout __drv_interlocked LONG volatile *Addend,
    __in LONG Value
    )

{
    return InterlockedExchangeAdd(Addend, Value) + Value;
}

#endif

LONG
InterlockedCompareExchange (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG ExChange,
    __in LONG Comperand
    );

LONG64
InterlockedIncrement64(
    __inout __drv_interlocked LONG64 volatile *Addend
    );

LONG64
InterlockedDecrement64(
    __inout __drv_interlocked LONG64 volatile *Addend
    );

LONG64
InterlockedExchange64(
    __inout __drv_interlocked LONG64 volatile *Target,
    __in LONG64 Value
    );

LONG64
InterlockedExchangeAdd64(
    __inout __drv_interlocked LONG64 volatile *Addend,
    __in LONG64 Value
    );

#if !defined(_X86AMD64_)

__forceinline
LONG64
InterlockedAdd64(
    __inout __drv_interlocked LONG64 volatile *Addend,
    __in LONG64 Value
    )

{
    return InterlockedExchangeAdd64(Addend, Value) + Value;
}

#endif

LONG64
InterlockedCompareExchange64 (
    __inout __drv_interlocked LONG64 volatile *Destination,
    __in LONG64 ExChange,
    __in LONG64 Comperand
    );

PVOID
InterlockedCompareExchangePointer (
    __inout __drv_interlocked PVOID volatile *Destination,
    __in_opt PVOID Exchange,
    __in_opt PVOID Comperand
    );

PVOID
InterlockedExchangePointer(
    __inout __drv_interlocked PVOID volatile *Target,
    __in_opt PVOID Value
    );

#pragma intrinsic(_InterlockedIncrement16)
#pragma intrinsic(_InterlockedDecrement16)
#pragma intrinsic(_InterlockedCompareExchange16)
#pragma intrinsic(_InterlockedAnd)
#pragma intrinsic(_InterlockedOr)
#pragma intrinsic(_InterlockedXor)
#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(_InterlockedDecrement)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedExchangeAdd)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedAnd64)
#pragma intrinsic(_InterlockedOr64)
#pragma intrinsic(_InterlockedXor64)
#pragma intrinsic(_InterlockedIncrement64)
#pragma intrinsic(_InterlockedDecrement64)
#pragma intrinsic(_InterlockedExchange64)
#pragma intrinsic(_InterlockedExchangeAdd64)
#pragma intrinsic(_InterlockedCompareExchange64)
#pragma intrinsic(_InterlockedExchangePointer)
#pragma intrinsic(_InterlockedCompareExchangePointer)

#if _MSC_FULL_VER >= 140041204

#define InterlockedAnd8 _InterlockedAnd8
#define InterlockedOr8 _InterlockedOr8
#define InterlockedXor8 _InterlockedXor8
#define InterlockedAnd16 _InterlockedAnd16
#define InterlockedOr16 _InterlockedOr16
#define InterlockedXor16 _InterlockedXor16

char
InterlockedAnd8 (
    __inout __drv_interlocked char volatile *Destination,
    __in char Value
    );

char
InterlockedOr8 (
    __inout __drv_interlocked char volatile *Destination,
    __in char Value
    );

char
InterlockedXor8 (
    __inout __drv_interlocked char volatile *Destination,
    __in char Value
    );

SHORT
InterlockedAnd16(
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

SHORT
InterlockedOr16(
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

SHORT
InterlockedXor16(
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

#pragma intrinsic (_InterlockedAnd8)
#pragma intrinsic (_InterlockedOr8)
#pragma intrinsic (_InterlockedXor8)
#pragma intrinsic (_InterlockedAnd16)
#pragma intrinsic (_InterlockedOr16)
#pragma intrinsic (_InterlockedXor16)

#endif

//
// Define function to flush a cache line.
//

#define CacheLineFlush(Address) _mm_clflush(Address)

VOID
_mm_clflush (
    __in VOID const *Address
    );

#pragma intrinsic(_mm_clflush)

VOID
_ReadWriteBarrier (
    VOID
    );

#pragma intrinsic(_ReadWriteBarrier)

//
// Define memory fence intrinsics
//

#define FastFence __faststorefence
#define LoadFence _mm_lfence
#define MemoryFence _mm_mfence
#define StoreFence _mm_sfence

VOID
__faststorefence (
    VOID
    );

VOID
_mm_lfence (
    VOID
    );

VOID
_mm_mfence (
    VOID
    );

VOID
_mm_sfence (
    VOID
    );

VOID
_mm_pause (
    VOID
    );

VOID
_mm_prefetch (
    __in CHAR CONST *a,
    __in int sel
    );

VOID
_m_prefetchw (
    __in volatile CONST VOID *Source
    );

//
// Define constants for use with _mm_prefetch.
//

#define _MM_HINT_T0     1
#define _MM_HINT_T1     2
#define _MM_HINT_T2     3
#define _MM_HINT_NTA    0

#pragma intrinsic(__faststorefence)
#pragma intrinsic(_mm_pause)
#pragma intrinsic(_mm_prefetch)
#pragma intrinsic(_mm_lfence)
#pragma intrinsic(_mm_mfence)
#pragma intrinsic(_mm_sfence)
#pragma intrinsic(_m_prefetchw)

#define YieldProcessor _mm_pause
#define MemoryBarrier __faststorefence
#define PreFetchCacheLine(l, a)  _mm_prefetch((CHAR CONST *) a, l)
#define PrefetchForWrite(p) _m_prefetchw(p)
#define ReadForWriteAccess(p) (_m_prefetchw(p), *(p))

//
// PreFetchCacheLine level defines.
//

#define PF_TEMPORAL_LEVEL_1 _MM_HINT_T0
#define PF_TEMPORAL_LEVEL_2 _MM_HINT_T1
#define PF_TEMPORAL_LEVEL_3 _MM_HINT_T2
#define PF_NON_TEMPORAL_LEVEL_ALL _MM_HINT_NTA

//
// Define get/set MXCSR intrinsics.
//

#define ReadMxCsr _mm_getcsr
#define WriteMxCsr _mm_setcsr

unsigned int
_mm_getcsr (
    VOID
    );

VOID
_mm_setcsr (
    __in unsigned int MxCsr
    );

#pragma intrinsic(_mm_getcsr)
#pragma intrinsic(_mm_setcsr)

//
// Assert exception.
//

VOID
__int2c (
    VOID
    );

#pragma intrinsic(__int2c)

__analysis_noreturn
FORCEINLINE
VOID
DbgRaiseAssertionFailure (
    VOID
    )

{
    __int2c();
}

//
// Define function to get the caller's EFLAGs value.
//

#define GetCallersEflags() __getcallerseflags()

unsigned __int32
__getcallerseflags (
    VOID
    );

#pragma intrinsic(__getcallerseflags)

//
// Define function to get segment limit.
//

#define GetSegmentLimit __segmentlimit

ULONG
__segmentlimit (
    __in ULONG Selector
    );

#pragma intrinsic(__segmentlimit)

//
// Define function to read the value of a performance counter.
//

#define ReadPMC __readpmc

ULONG64
__readpmc (
    __in ULONG Counter
    );

#pragma intrinsic(__readpmc)

//
// Define function to read the value of the time stamp counter
//

#define ReadTimeStampCounter() __rdtsc()

ULONG64
__rdtsc (
    VOID
    );

#pragma intrinsic(__rdtsc)

//
// Define functions to move strings as bytes, words, dwords, and qwords.
//

VOID
__movsb (
    __out_ecount_full(Count) PUCHAR Destination,
    __in_ecount(Count) UCHAR const *Source,
    __in SIZE_T Count
    );

VOID
__movsw (
    __out_ecount_full(Count) PUSHORT Destination,
    __in_ecount(Count) USHORT const *Source,
    __in SIZE_T Count
    );

VOID
__movsd (
    __out_ecount_full(Count) PULONG Destination,
    __in_ecount(Count) ULONG const *Source,
    __in SIZE_T Count
    );

VOID
__movsq (
    __out_ecount_full(Count) PULONG64 Destination,
    __in_ecount(Count) ULONG64 const *Source,
    __in SIZE_T Count
    );

#pragma intrinsic(__movsb)
#pragma intrinsic(__movsw)
#pragma intrinsic(__movsd)
#pragma intrinsic(__movsq)

//
// Define functions to store strings as bytes, words, dwords, and qwords.
//

VOID
__stosb (
    __out_ecount_full(Count) PUCHAR Destination,
    __in UCHAR Value,
    __in SIZE_T Count
    );

VOID
__stosw (
    __out_ecount_full(Count) PUSHORT Destination,
    __in USHORT Value,
    __in SIZE_T Count
    );

VOID
__stosd (
    __out_ecount_full(Count) PULONG Destination,
    __in ULONG Value,
    __in SIZE_T Count
    );

VOID
__stosq (
    __out_ecount_full(Count) PULONG64 Destination,
    __in ULONG64 Value,
    __in SIZE_T Count
    );

#pragma intrinsic(__stosb)
#pragma intrinsic(__stosw)
#pragma intrinsic(__stosd)
#pragma intrinsic(__stosq)

//
// Define functions to capture the high 64-bits of a 128-bit multiply.
//

#define MultiplyHigh __mulh
#define UnsignedMultiplyHigh __umulh

LONGLONG
MultiplyHigh (
    __in LONG64 Multiplier,
    __in LONG64 Multiplicand
    );

ULONGLONG
UnsignedMultiplyHigh (
    __in ULONG64 Multiplier,
    __in ULONG64 Multiplicand
    );

#pragma intrinsic(__mulh)
#pragma intrinsic(__umulh)

//
// Define functions to perform 128-bit shifts
//

#define ShiftLeft128 __shiftleft128
#define ShiftRight128 __shiftright128

ULONG64
ShiftLeft128 (
    __in ULONG64 LowPart,
    __in ULONG64 HighPart,
    __in UCHAR Shift
    );

ULONG64
ShiftRight128 (
    __in ULONG64 LowPart,
    __in ULONG64 HighPart,
    __in UCHAR Shift
    );

#pragma intrinsic(__shiftleft128)
#pragma intrinsic(__shiftright128)

//
// Define functions to perform 128-bit multiplies.
//

#define Multiply128 _mul128

LONG64
Multiply128 (
    __in LONG64 Multiplier,
    __in LONG64 Multiplicand,
    __out LONG64 *HighProduct
    );

#pragma intrinsic(_mul128)

#ifndef UnsignedMultiply128

#define UnsignedMultiply128 _umul128

ULONG64
UnsignedMultiply128 (
    __in ULONG64 Multiplier,
    __in ULONG64 Multiplicand,
    __out ULONG64 *HighProduct
    );

#pragma intrinsic(_umul128)

#endif

__forceinline
LONG64
MultiplyExtract128 (
    __in LONG64 Multiplier,
    __in LONG64 Multiplicand,
    __in UCHAR Shift
    )

{

    LONG64 extractedProduct;
    LONG64 highProduct;
    LONG64 lowProduct;
    BOOLEAN negate;
    ULONG64 uhighProduct;
    ULONG64 ulowProduct;

    lowProduct = Multiply128(Multiplier, Multiplicand, &highProduct);
    negate = FALSE;
    uhighProduct = (ULONG64)highProduct;
    ulowProduct = (ULONG64)lowProduct;
    if (highProduct < 0) {
        negate = TRUE;
        uhighProduct = (ULONG64)(-highProduct);
        ulowProduct = (ULONG64)(-lowProduct);
        if (ulowProduct != 0) {
            uhighProduct -= 1;
        }
    }

    extractedProduct = (LONG64)ShiftRight128(ulowProduct, uhighProduct, Shift);
    if (negate != FALSE) {
        extractedProduct = -extractedProduct;
    }

    return extractedProduct;
}

__forceinline
ULONG64
UnsignedMultiplyExtract128 (
    __in ULONG64 Multiplier,
    __in ULONG64 Multiplicand,
    __in UCHAR Shift
    )

{

    ULONG64 extractedProduct;
    ULONG64 highProduct;
    ULONG64 lowProduct;

    lowProduct = UnsignedMultiply128(Multiplier, Multiplicand, &highProduct);
    extractedProduct = ShiftRight128(lowProduct, highProduct, Shift);
    return extractedProduct;
}

//
// Define functions to read and write the uer TEB and the system PCR/PRCB.
//

UCHAR
__readgsbyte (
    __in ULONG Offset
    );

USHORT
__readgsword (
    __in ULONG Offset
    );

ULONG
__readgsdword (
    __in ULONG Offset
    );

ULONG64
__readgsqword (
    __in ULONG Offset
    );

VOID
__writegsbyte (
    __in ULONG Offset,
    __in UCHAR Data
    );

VOID
__writegsword (
    __in ULONG Offset,
    __in USHORT Data
    );

VOID
__writegsdword (
    __in ULONG Offset,
    __in ULONG Data
    );

VOID
__writegsqword (
    __in ULONG Offset,
    __in ULONG64 Data
    );

#pragma intrinsic(__readgsbyte)
#pragma intrinsic(__readgsword)
#pragma intrinsic(__readgsdword)
#pragma intrinsic(__readgsqword)
#pragma intrinsic(__writegsbyte)
#pragma intrinsic(__writegsword)
#pragma intrinsic(__writegsdword)
#pragma intrinsic(__writegsqword)

#if !defined(_MANAGED)

VOID
__incgsbyte (
    __in ULONG Offset
    );

VOID
__addgsbyte (
    __in ULONG Offset,
    __in UCHAR Value
    );

VOID
__incgsword (
    __in ULONG Offset
    );

VOID
__addgsword (
    __in ULONG Offset,
    __in USHORT Value
    );

VOID
__incgsdword (
    __in ULONG Offset
    );

VOID
__addgsdword (
    __in ULONG Offset,
    __in ULONG Value
    );

VOID
__incgsqword (
    __in ULONG Offset
    );

VOID
__addgsqword (
    __in ULONG Offset,
    __in ULONG64 Value
    );

#if 0
#pragma intrinsic(__incgsbyte)
#pragma intrinsic(__addgsbyte)
#pragma intrinsic(__incgsword)
#pragma intrinsic(__addgsword)
#pragma intrinsic(__incgsdword)
#pragma intrinsic(__addgsdword)
#pragma intrinsic(__incgsqword)
#pragma intrinsic(__addgsqword)
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif // defined(_M_AMD64) && !defined(RC_INVOKED) && !defined(MIDL_PASS)


#endif // _AMD64_


#ifdef _X86_

//
// Disable these two pragmas that evaluate to "sti" "cli" on x86 so that driver
// writers to not leave them inadvertantly in their code.
//

#if !defined(MIDL_PASS)
#if !defined(RC_INVOKED)

#if _MSC_VER >= 1200
#pragma warning(push)
#endif
#pragma warning(disable:4164)   // disable C4164 warning so that apps that
                                // build with /Od don't get weird errors !
#ifdef _M_IX86
#pragma function(_enable)
#pragma function(_disable)
#endif

#if _MSC_VER >= 1200
#pragma warning(pop)
#else
#pragma warning(default:4164)   // reenable C4164 warning
#endif

#endif
#endif

// end_ntddk end_nthal
#if defined(_M_IX86) && !defined(RC_INVOKED) && !defined(MIDL_PASS)

#ifdef __cplusplus
extern "C" {
#endif



#if (_MSC_FULL_VER >= 14000101)


//
// Define bit test intrinsics.
//

#define BitTest _bittest
#define BitTestAndComplement _bittestandcomplement
#define BitTestAndSet _bittestandset
#define BitTestAndReset _bittestandreset
#define InterlockedBitTestAndSet _interlockedbittestandset
#define InterlockedBitTestAndReset _interlockedbittestandreset

__checkReturn
BOOLEAN
_bittest (
    __in_bcount((Offset+7)/8) LONG const *Base,
    __in LONG Offset
    );

BOOLEAN
_bittestandcomplement (
    __inout_bcount((Offset+7)/8) LONG *Base,
    __in LONG Offset
    );

BOOLEAN
_bittestandset (
    __inout_bcount((Offset+7)/8) LONG *Base,
    __in LONG Offset
    );

BOOLEAN
_bittestandreset (
    __inout_bcount((Offset+7)/8) LONG *Base,
    __in LONG Offset
    );

BOOLEAN
_interlockedbittestandset (
    __inout_bcount((Offset+7)/8) __drv_interlocked LONG volatile *Base,
    __in LONG Offset
    );

BOOLEAN
_interlockedbittestandreset (
    __inout_bcount((Offset+7)/8) __drv_interlocked LONG volatile *Base,
    __in LONG Offset
    );

#pragma intrinsic(_bittest)
#pragma intrinsic(_bittestandcomplement)
#pragma intrinsic(_bittestandset)
#pragma intrinsic(_bittestandreset)
#pragma intrinsic(_interlockedbittestandset)
#pragma intrinsic(_interlockedbittestandreset)

//
// Define bit scan intrinsics.
//

#define BitScanForward _BitScanForward
#define BitScanReverse _BitScanReverse

__success(return != 0)
BOOLEAN
_BitScanForward (
    __out ULONG *Index,
    __in ULONG Mask
    );

__success(return != 0)
BOOLEAN
_BitScanReverse (
    __out ULONG *Index,
    __in ULONG Mask
    );

#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_BitScanReverse)

#else

#pragma warning(push)
#pragma warning(disable:4035 4793)

FORCEINLINE
BOOLEAN
InterlockedBitTestAndSet (
    __inout_bcount((Bit+7)/8) __drv_interlocked LONG volatile *Base,
    __in LONG Bit
    )
{
    __asm {
           mov eax, Bit
           mov ecx, Base
           lock bts [ecx], eax
           setc al
    };
}

FORCEINLINE
BOOLEAN
InterlockedBitTestAndReset (
    __inout_bcount((Bit+7)/8) __drv_interlocked LONG volatile *Base,
    __in LONG Bit
    )
{
    __asm {
           mov eax, Bit
           mov ecx, Base
           lock btr [ecx], eax
           setc al
    };
}
#pragma warning(pop)

#endif	/* _MSC_FULL_VER >= 14000101 */

//
// [pfx_parse] - guard against PREfix intrinsic error
//
#if (_MSC_FULL_VER >= 140040816) || (defined(_PREFAST_) && (_MSC_VER >= 1400))

#define InterlockedAnd16 _InterlockedAnd16
#define InterlockedCompareExchange16 _InterlockedCompareExchange16
#define InterlockedOr16 _InterlockedOr16

SHORT
_InterlockedAnd16 (
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

SHORT
_InterlockedCompareExchange16 (
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT ExChange,
    __in SHORT Comperand
    );

SHORT
_InterlockedOr16 (
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

#pragma intrinsic(_InterlockedAnd16)
#pragma intrinsic(_InterlockedCompareExchange16)
#pragma intrinsic(_InterlockedOr16)

#endif  /* _MSC_FULL_VER >= 140040816 */

#if !defined(_M_CEE_PURE)
#pragma warning(push)
#pragma warning(disable:4035 4793)

FORCEINLINE
BOOLEAN
InterlockedBitTestAndComplement (
    __inout_bcount((Bit+7)/8) __drv_interlocked LONG volatile *Base,
    __in LONG Bit
    )
{
    __asm {
           mov eax, Bit
           mov ecx, Base
           lock btc [ecx], eax
           setc al
    };
}
#pragma warning(pop)
#endif	/* _M_CEE_PURE */

//
// [pfx_parse]
// guard against __readfsbyte parsing error
//
#if (_MSC_FULL_VER >= 13012035) || defined(_PREFIX_) || defined(_PREFAST_)

//
// Define FS referencing intrinsics
//

UCHAR
__readfsbyte (
    __in ULONG Offset
    );

USHORT
__readfsword (
    __in ULONG Offset
    );

ULONG
__readfsdword (
    __in ULONG Offset
    );

VOID
__writefsbyte (
    __in ULONG Offset,
    __in UCHAR Data
    );

VOID
__writefsword (
    __in ULONG Offset,
    __in USHORT Data
    );

VOID
__writefsdword (
    __in ULONG Offset,
    __in ULONG Data
    );

#pragma intrinsic(__readfsbyte)
#pragma intrinsic(__readfsword)
#pragma intrinsic(__readfsdword)
#pragma intrinsic(__writefsbyte)
#pragma intrinsic(__writefsword)
#pragma intrinsic(__writefsdword)

#endif	/* _MSC_FULL_VER >= 13012035 */

#if (_MSC_FULL_VER >= 140050727) || defined(_PREFIX_) || defined(_PREFAST_)

#if !defined(_MANAGED)

VOID
__incfsbyte (
    __in ULONG Offset
    );

VOID
__addfsbyte (
    __in ULONG Offset,
    __in UCHAR Value
    );

VOID
__incfsword (
    __in ULONG Offset
    );

VOID
__addfsword (
    __in ULONG Offset,
    __in USHORT Value
    );

VOID
__incfsdword (
    __in ULONG Offset
    );

VOID
__addfsdword (
    __in ULONG Offset,
    __in ULONG Value
    );

#pragma intrinsic(__incfsbyte)
#pragma intrinsic(__addfsbyte)
#pragma intrinsic(__incfsword)
#pragma intrinsic(__addfsword)
#pragma intrinsic(__incfsdword)
#pragma intrinsic(__addfsdword)

#endif

#endif	/* _MSC_FULL_VER >= 140050727 */

#if (_MSC_FULL_VER >= 140041204) || defined(_PREFIX_) || defined(_PREFAST_)

VOID
_mm_pause (
    VOID
    );

#pragma intrinsic(_mm_pause)

#define YieldProcessor _mm_pause

#else

#if !defined(_M_CEE_PURE)
#define YieldProcessor() __asm { rep nop }
#endif  // !defined(_M_CEE_PURE)

#endif  // (_MSC_FULL_VER >= 140041204)

#ifdef __cplusplus
}
#endif

#endif  /* !defined(MIDL_PASS) || defined(_M_IX86) */

#endif //_X86_


#if defined(_M_IA64) && !defined(RC_INVOKED) && !defined(MIDL_PASS)

#ifdef __cplusplus
extern "C" {
#endif

//
// Define bit test intrinsics.
//

#define BitTest _bittest
#define BitTestAndComplement _bittestandcomplement
#define BitTestAndSet _bittestandset
#define BitTestAndReset _bittestandreset

#define BitTest64 _bittest64
#define BitTestAndComplement64 _bittestandcomplement64
#define BitTestAndSet64 _bittestandset64
#define BitTestAndReset64 _bittestandreset64

__checkReturn
BOOLEAN
_bittest (
    __in_bcount((Offset+7)/8) LONG const *Base,
    __in LONG Offset
    );

BOOLEAN
_bittestandcomplement (
    __inout_bcount((Offset+7)/8) LONG *Base,
    __in LONG Offset
    );

BOOLEAN
_bittestandset (
    __inout_bcount((Offset+7)/8) LONG *Base,
    __in LONG Offset
    );

BOOLEAN
_bittestandreset (
    __inout_bcount((Offset+7)/8) LONG *Base,
    __in LONG Offset
    );

__checkReturn
BOOLEAN
_bittest64 (
    __in_bcount((Offset+7)/8) LONG64 const *Base,
    __in LONG64 Offset
    );

BOOLEAN
_bittestandcomplement64 (
    __inout_bcount((Offset+7)/8) LONG64 *Base,
    __in LONG64 Offset
    );

BOOLEAN
_bittestandset64 (
    __inout_bcount((Offset+7)/8) LONG64 *Base,
    __in LONG64 Offset
    );

BOOLEAN
_bittestandreset64 (
    __inout_bcount((Offset+7)/8) LONG64 *Base,
    __in LONG64 Offset
    );

#pragma intrinsic(_bittest)
#pragma intrinsic(_bittestandcomplement)
#pragma intrinsic(_bittestandset)
#pragma intrinsic(_bittestandreset)

#pragma intrinsic(_bittest64)
#pragma intrinsic(_bittestandcomplement64)
#pragma intrinsic(_bittestandset64)
#pragma intrinsic(_bittestandreset64)

//
// Define bit scan intrinsics.
//

#define BitScanForward _BitScanForward
#define BitScanReverse _BitScanReverse
#define BitScanForward64 _BitScanForward64
#define BitScanReverse64 _BitScanReverse64

__success(return!=0)
BOOLEAN
_BitScanForward (
    __out ULONG *Index,
    __in ULONG Mask
    );

__success(return!=0)
BOOLEAN
_BitScanReverse (
    __out ULONG *Index,
    __in ULONG Mask
    );

__success(return!=0)
BOOLEAN
_BitScanForward64 (
    __out ULONG *Index,
    __in ULONG64 Mask
    );

__success(return!=0)
BOOLEAN
_BitScanReverse64 (
    __out ULONG *Index,
    __in ULONG64 Mask
    );

#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(_BitScanForward64)
#pragma intrinsic(_BitScanReverse64)

#define InterlockedCompareExchange16 _InterlockedCompareExchange16

SHORT
_InterlockedCompareExchange16 (
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT ExChange,
    __in SHORT Comperand
    );

#pragma intrinsic(_InterlockedCompareExchange16)

#ifdef __cplusplus
}
#endif

#define InterlockedAdd                  _InterlockedAdd
#define InterlockedAddAcquire           _InterlockedAdd_acq
#define InterlockedAddRelease           _InterlockedAdd_rel

#define InterlockedIncrement            _InterlockedIncrement
#define InterlockedIncrementAcquire     _InterlockedIncrement_acq
#define InterlockedIncrementRelease     _InterlockedIncrement_rel

#define InterlockedDecrement            _InterlockedDecrement
#define InterlockedDecrementAcquire     _InterlockedDecrement_acq
#define InterlockedDecrementRelease     _InterlockedDecrement_rel

#define InterlockedExchange             _InterlockedExchange
#define InterlockedExchangeAcquire      _InterlockedExchange_acq

#define InterlockedExchangeAdd          _InterlockedExchangeAdd
#define InterlockedExchangeAddAcquire   _InterlockedExchangeAdd_acq
#define InterlockedExchangeAddRelease   _InterlockedExchangeAdd_rel

#define InterlockedAdd64                _InterlockedAdd64
#define InterlockedAddAcquire64         _InterlockedAdd64_acq
#define InterlockedAddRelease64         _InterlockedAdd64_rel

#define InterlockedIncrement64          _InterlockedIncrement64
#define InterlockedIncrementAcquire64   _InterlockedIncrement64_acq
#define InterlockedIncrementRelease64   _InterlockedIncrement64_rel

#define InterlockedDecrement64          _InterlockedDecrement64
#define InterlockedDecrementAcquire64   _InterlockedDecrement64_acq
#define InterlockedDecrementRelease64   _InterlockedDecrement64_rel

#define InterlockedExchange64           _InterlockedExchange64
#define InterlockedExchangeAcquire64    _InterlockedExchange64_acq

#define InterlockedExchangeAdd64        _InterlockedExchangeAdd64
#define InterlockedExchangeAddAcquire64 _InterlockedExchangeAdd64_acq
#define InterlockedExchangeAddRelease64 _InterlockedExchangeAdd64_rel

#define InterlockedCompareExchange64        _InterlockedCompareExchange64
#define InterlockedCompareExchangeAcquire64 _InterlockedCompareExchange64_acq
#define InterlockedCompareExchangeRelease64 _InterlockedCompareExchange64_rel

#define InterlockedCompare64Exchange128         _InterlockedCompare64Exchange128
#define InterlockedCompare64ExchangeAcquire128  _InterlockedCompare64Exchange128_acq
#define InterlockedCompare64ExchangeRelease128  _InterlockedCompare64Exchange128_rel

#define InterlockedCompareExchange          _InterlockedCompareExchange
#define InterlockedCompareExchangeAcquire   _InterlockedCompareExchange_acq
#define InterlockedCompareExchangeRelease   _InterlockedCompareExchange_rel

#define InterlockedExchangePointer          _InterlockedExchangePointer
#define InterlockedExchangePointerAcquire   _InterlockedExchangePointer_acq

#define InterlockedCompareExchangePointer           _InterlockedCompareExchangePointer
#define InterlockedCompareExchangePointerRelease    _InterlockedCompareExchangePointer_rel
#define InterlockedCompareExchangePointerAcquire    _InterlockedCompareExchangePointer_acq


#define InterlockedExchangeAddSizeT(a, b) InterlockedExchangeAdd64((LONG64 *)a, b)
#define InterlockedIncrementSizeT(a) InterlockedIncrement64((LONG64 *)a)
#define InterlockedDecrementSizeT(a) InterlockedDecrement64((LONG64 *)a)

#define InterlockedOr       _InterlockedOr
#define InterlockedOrAcquire   _InterlockedOr_acq
#define InterlockedOrRelease   _InterlockedOr_rel
#define InterlockedOr8      _InterlockedOr8
#define InterlockedOr8Acquire  _InterlockedOr8_acq
#define InterlockedOr8Release  _InterlockedOr8_rel
#define InterlockedOr16     _InterlockedOr16
#define InterlockedOr16Acquire _InterlockedOr16_acq
#define InterlockedOr16Release _InterlockedOr16_rel
#define InterlockedOr64     _InterlockedOr64
#define InterlockedOr64Acquire _InterlockedOr64_acq
#define InterlockedOr64Release _InterlockedOr64_rel
#define InterlockedXor      _InterlockedXor
#define InterlockedXorAcquire  _InterlockedXor_acq
#define InterlockedXorRelease  _InterlockedXor_rel
#define InterlockedXor8     _InterlockedXor8
#define InterlockedXor8Acquire _InterlockedXor8_acq
#define InterlockedXor8Release _InterlockedXor8_rel
#define InterlockedXor16    _InterlockedXor16
#define InterlockedXor16Acquire _InterlockedXor16_acq
#define InterlockedXor16Release _InterlockedXor16_rel
#define InterlockedXor64     _InterlockedXor64
#define InterlockedXor64Acquire _InterlockedXor64_acq
#define InterlockedXor64Release _InterlockedXor64_rel
#define InterlockedAnd       _InterlockedAnd
#define InterlockedAndAcquire   _InterlockedAnd_acq
#define InterlockedAndRelease   _InterlockedAnd_rel
#define InterlockedAnd8      _InterlockedAnd8
#define InterlockedAnd8Acquire  _InterlockedAnd8_acq
#define InterlockedAnd8Release  _InterlockedAnd8_rel
#define InterlockedAnd16     _InterlockedAnd16
#define InterlockedAnd16Acquire _InterlockedAnd16_acq
#define InterlockedAnd16Release _InterlockedAnd16_rel
#define InterlockedAnd64     _InterlockedAnd64
#define InterlockedAnd64Acquire _InterlockedAnd64_acq
#define InterlockedAnd64Release _InterlockedAnd64_rel

#ifdef __cplusplus
extern "C" {
#endif

LONG
__cdecl
InterlockedAdd (
    __inout __drv_interlocked LONG volatile *Addend,
    __in LONG Value
    );

LONG
__cdecl
InterlockedAddAcquire (
    __inout __drv_interlocked LONG volatile *Addend,
    __in LONG Value
    );

LONG
__cdecl
InterlockedAddRelease (
    __inout __drv_interlocked LONG volatile *Addend,
    __in LONG Value
    );

LONGLONG
__cdecl
InterlockedAdd64 (
    __inout __drv_interlocked LONGLONG volatile *Addend,
    __in LONGLONG Value
    );

LONGLONG
__cdecl
InterlockedAddAcquire64 (
    __inout __drv_interlocked LONGLONG volatile *Addend,
    __in LONGLONG Value
    );


LONGLONG
__cdecl
InterlockedAddRelease64 (
    __inout __drv_interlocked LONGLONG volatile *Addend,
    __in LONGLONG Value
    );

LONG
__cdecl
InterlockedIncrement(
    __inout __drv_interlocked LONG volatile *Addend
    );

LONG
__cdecl
InterlockedDecrement(
    __inout __drv_interlocked LONG volatile *Addend
    );

LONG
__cdecl
InterlockedIncrementAcquire(
    __inout __drv_interlocked LONG volatile *Addend
    );

LONG
__cdecl
InterlockedDecrementAcquire(
    __inout __drv_interlocked LONG volatile *Addend
    );

LONG
__cdecl
InterlockedIncrementRelease(
    __inout __drv_interlocked LONG volatile *Addend
    );

LONG
__cdecl
InterlockedDecrementRelease(
    __inout __drv_interlocked LONG volatile *Addend
    );

LONG
__cdecl
InterlockedExchange(
    __inout __drv_interlocked LONG volatile *Target,
    __in LONG Value
    );

LONG
__cdecl
InterlockedExchangeAcquire(
    __inout __drv_interlocked LONG volatile *Target,
    __in LONG Value
    );

LONG
__cdecl
InterlockedExchangeAdd(
    __inout __drv_interlocked LONG volatile *Addend,
    __in LONG Value
    );

LONG
__cdecl
InterlockedExchangeAddAcquire(
    __inout __drv_interlocked LONG volatile *Addend,
    __in LONG Value
    );

LONG
__cdecl
InterlockedExchangeAddRelease(
    __inout __drv_interlocked LONG volatile *Addend,
    __in LONG Value
    );

LONG
__cdecl
InterlockedCompareExchange (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG ExChange,
    __in LONG Comperand
    );


LONG
__cdecl
InterlockedCompareExchangeRelease (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG ExChange,
    __in LONG Comperand
    );


LONG
__cdecl
InterlockedCompareExchangeAcquire (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG ExChange,
    __in LONG Comperand
    );


LONGLONG
__cdecl
InterlockedIncrement64(
    __inout __drv_interlocked LONGLONG volatile *Addend
    );

LONGLONG
__cdecl
InterlockedIncrementAcquire64(
    __inout __drv_interlocked LONGLONG volatile *Addend
    );

LONGLONG
__cdecl
InterlockedIncrementRelease64(
    __inout __drv_interlocked LONGLONG volatile *Addend
    );

LONGLONG
__cdecl
InterlockedDecrement64(
    __inout __drv_interlocked LONGLONG volatile *Addend
    );

LONGLONG
__cdecl
InterlockedDecrementAcquire64(
    __inout __drv_interlocked LONGLONG volatile *Addend
    );

LONGLONG
__cdecl
InterlockedDecrementRelease64(
    __inout __drv_interlocked LONGLONG volatile *Addend
    );

LONGLONG
__cdecl
InterlockedExchange64(
    __inout __drv_interlocked LONGLONG volatile *Target,
    __in LONGLONG Value
    );

LONGLONG
__cdecl
InterlockedExchangeAcquire64(
    __inout __drv_interlocked LONGLONG volatile *Target,
    __in LONGLONG Value
    );

LONGLONG
__cdecl
InterlockedExchangeAdd64(
    __inout __drv_interlocked LONGLONG volatile *Addend,
    __in LONGLONG Value
    );

LONGLONG
__cdecl
InterlockedExchangeAddAcquire64(
    __inout __drv_interlocked LONGLONG volatile *Addend,
    __in LONGLONG Value
    );

LONGLONG
__cdecl
InterlockedExchangeAddRelease64(
    __inout __drv_interlocked LONGLONG volatile *Addend,
    __in LONGLONG Value
    );

LONGLONG
__cdecl
InterlockedCompareExchange64 (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG ExChange,
    __in LONGLONG Comperand
    );

LONGLONG
__cdecl
InterlockedCompareExchangeAcquire64 (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG ExChange,
    __in LONGLONG Comperand
    );

LONGLONG
__cdecl
InterlockedCompareExchangeRelease64 (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG ExChange,
    __in LONGLONG Comperand
    );

LONG64
__cdecl
InterlockedCompare64Exchange128(
    __inout_bcount(16) __drv_interlocked LONG64 volatile *Destination,
    __in LONG64 ExchangeHigh,
    __in LONG64 ExchangeLow,
    __in LONG64 Comperand
    );

LONG64
__cdecl
InterlockedCompare64ExchangeAcquire128(
    __inout_bcount(16) __drv_interlocked LONG64 volatile *Destination,
    __in LONG64 ExchangeHigh,
    __in LONG64 ExchangeLow,
    __in LONG64 Comperand
    );

LONG64
__cdecl
InterlockedCompare64ExchangeRelease128(
    __inout_bcount(16) __drv_interlocked LONG64 volatile *Destination,
    __in LONG64 ExchangeHigh,
    __in LONG64 ExchangeLow,
    __in LONG64 Comperand
    );

PVOID
__cdecl
InterlockedCompareExchangePointer (
    __inout __drv_interlocked PVOID volatile *Destination,
    __in PVOID Exchange,
    __in PVOID Comperand
    );

PVOID
__cdecl
InterlockedCompareExchangePointerAcquire (
    __inout __drv_interlocked PVOID volatile *Destination,
    __in PVOID Exchange,
    __in PVOID Comperand
    );

PVOID
__cdecl
InterlockedCompareExchangePointerRelease (
    __inout __drv_interlocked PVOID volatile *Destination,
    __in PVOID Exchange,
    __in PVOID Comperand
    );

PVOID
__cdecl
InterlockedExchangePointer(
    __inout __drv_interlocked PVOID volatile *Target,
    __in PVOID Value
    );

PVOID
__cdecl
InterlockedExchangePointerAcquire(
    __inout __drv_interlocked PVOID volatile *Target,
    __in PVOID Value
    );

LONG
__cdecl
InterlockedOr (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG Value
    );

LONG
__cdecl
InterlockedOrAcquire (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG Value
    );

LONG
__cdecl
InterlockedOrRelease (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG Value
    );

CHAR
__cdecl
InterlockedOr8 (
    __inout __drv_interlocked CHAR volatile *Destination,
    __in CHAR Value
    );

CHAR
__cdecl
InterlockedOr8Acquire (
    __inout __drv_interlocked CHAR volatile *Destination,
    __in CHAR Value
    );

CHAR
__cdecl
InterlockedOr8Release (
    __inout __drv_interlocked CHAR volatile *Destination,
    __in CHAR Value
    );

SHORT
__cdecl
InterlockedOr16(
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

SHORT
__cdecl
InterlockedOr16Acquire (
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

SHORT
__cdecl
InterlockedOr16Release (
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

LONGLONG
__cdecl
InterlockedOr64 (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG Value
    );

LONGLONG
__cdecl
InterlockedOr64Acquire (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG Value
    );

LONGLONG
__cdecl
InterlockedOr64Release (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG Value
    );

LONG
__cdecl
InterlockedXor (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG Value
    );

LONG
__cdecl
InterlockedXorAcquire (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG Value
    );

LONG
__cdecl
InterlockedXorRelease (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG Value
    );

CHAR
__cdecl
InterlockedXor8 (
    __inout __drv_interlocked CHAR volatile *Destination,
    __in CHAR Value
    );

CHAR
__cdecl
InterlockedXor8Acquire (
    __inout __drv_interlocked CHAR volatile *Destination,
    __in CHAR Value
    );

CHAR
__cdecl
InterlockedXor8Release (
    __inout __drv_interlocked CHAR volatile *Destination,
    __in CHAR Value
    );

SHORT
__cdecl
InterlockedXor16(
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

SHORT
__cdecl
InterlockedXor16Acquire (
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

SHORT
__cdecl
InterlockedXor16Release (
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

LONGLONG
__cdecl
InterlockedXor64 (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG Value
    );

LONGLONG
__cdecl
InterlockedXor64Acquire (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG Value
    );

LONGLONG
__cdecl
InterlockedXor64Release (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG Value
    );

LONG
__cdecl
InterlockedAnd (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG Value
    );

LONG
__cdecl
InterlockedAndAcquire (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG Value
    );

LONG
__cdecl
InterlockedAndRelease (
    __inout __drv_interlocked LONG volatile *Destination,
    __in LONG Value
    );

CHAR
__cdecl
InterlockedAnd8 (
    __inout __drv_interlocked CHAR volatile *Destination,
    __in CHAR Value
    );

CHAR
__cdecl
InterlockedAnd8Acquire (
    __inout __drv_interlocked CHAR volatile *Destination,
    __in CHAR Value
    );

CHAR
__cdecl
InterlockedAnd8Release (
    __inout __drv_interlocked CHAR volatile *Destination,
    __in CHAR Value
    );

SHORT
__cdecl
InterlockedAnd16(
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

SHORT
__cdecl
InterlockedAnd16Acquire (
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

SHORT
__cdecl
InterlockedAnd16Release (
    __inout __drv_interlocked SHORT volatile *Destination,
    __in SHORT Value
    );

LONGLONG
__cdecl
InterlockedAnd64 (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG Value
    );

LONGLONG
__cdecl
InterlockedAnd64Acquire (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG Value
    );

LONGLONG
__cdecl
InterlockedAnd64Release (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG Value
    );

#pragma intrinsic(_InterlockedAdd)
#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(_InterlockedIncrement_acq)
#pragma intrinsic(_InterlockedIncrement_rel)
#pragma intrinsic(_InterlockedDecrement)
#pragma intrinsic(_InterlockedDecrement_acq)
#pragma intrinsic(_InterlockedDecrement_rel)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedCompareExchange_acq)
#pragma intrinsic(_InterlockedCompareExchange_rel)
#pragma intrinsic(_InterlockedExchangeAdd)
#pragma intrinsic(_InterlockedAdd64)
#pragma intrinsic(_InterlockedIncrement64)
#pragma intrinsic(_InterlockedDecrement64)
#pragma intrinsic(_InterlockedExchange64)
#pragma intrinsic(_InterlockedExchange64_acq)
#pragma intrinsic(_InterlockedCompareExchange64)
#pragma intrinsic(_InterlockedCompareExchange64_acq)
#pragma intrinsic(_InterlockedCompareExchange64_rel)
#pragma intrinsic(_InterlockedCompare64Exchange128)
#pragma intrinsic(_InterlockedCompare64Exchange128_acq)
#pragma intrinsic(_InterlockedCompare64Exchange128_rel)
#pragma intrinsic(_InterlockedExchangeAdd64)
#pragma intrinsic(_InterlockedExchangePointer)
#pragma intrinsic(_InterlockedCompareExchangePointer)
#pragma intrinsic(_InterlockedCompareExchangePointer_acq)
#pragma intrinsic(_InterlockedCompareExchangePointer_rel)
#pragma intrinsic(_InterlockedAdd_acq)
#pragma intrinsic(_InterlockedAdd_rel)
#pragma intrinsic(_InterlockedExchange_acq)
#pragma intrinsic(_InterlockedExchangeAdd_acq)
#pragma intrinsic(_InterlockedExchangeAdd_rel)
#pragma intrinsic(_InterlockedAdd64_acq)
#pragma intrinsic(_InterlockedAdd64_rel)
#pragma intrinsic(_InterlockedIncrement64_acq)
#pragma intrinsic(_InterlockedIncrement64_rel)
#pragma intrinsic(_InterlockedDecrement64_acq)
#pragma intrinsic(_InterlockedDecrement64_rel)
#pragma intrinsic(_InterlockedExchangeAdd64_acq)
#pragma intrinsic(_InterlockedExchangeAdd64_rel)
#pragma intrinsic(_InterlockedExchangePointer_acq)
#pragma intrinsic (_InterlockedOr)
#pragma intrinsic (_InterlockedOr_acq)
#pragma intrinsic (_InterlockedOr_rel)
#pragma intrinsic (_InterlockedOr8)
#pragma intrinsic (_InterlockedOr8_acq)
#pragma intrinsic (_InterlockedOr8_rel)
#pragma intrinsic (_InterlockedOr16)
#pragma intrinsic (_InterlockedOr16_acq)
#pragma intrinsic (_InterlockedOr16_rel)
#pragma intrinsic (_InterlockedOr64)
#pragma intrinsic (_InterlockedOr64_acq)
#pragma intrinsic (_InterlockedOr64_rel)
#pragma intrinsic (_InterlockedXor)
#pragma intrinsic (_InterlockedXor_acq)
#pragma intrinsic (_InterlockedXor_rel)
#pragma intrinsic (_InterlockedXor8)
#pragma intrinsic (_InterlockedXor8_acq)
#pragma intrinsic (_InterlockedXor8_rel)
#pragma intrinsic (_InterlockedXor16)
#pragma intrinsic (_InterlockedXor16_acq)
#pragma intrinsic (_InterlockedXor16_rel)
#pragma intrinsic (_InterlockedXor64)
#pragma intrinsic (_InterlockedXor64_acq)
#pragma intrinsic (_InterlockedXor64_rel)
#pragma intrinsic (_InterlockedAnd)
#pragma intrinsic (_InterlockedAnd_acq)
#pragma intrinsic (_InterlockedAnd_rel)
#pragma intrinsic (_InterlockedAnd8)
#pragma intrinsic (_InterlockedAnd8_acq)
#pragma intrinsic (_InterlockedAnd8_rel)
#pragma intrinsic (_InterlockedAnd16)
#pragma intrinsic (_InterlockedAnd16_acq)
#pragma intrinsic (_InterlockedAnd16_rel)
#pragma intrinsic (_InterlockedAnd64)
#pragma intrinsic (_InterlockedAnd64_acq)
#pragma intrinsic (_InterlockedAnd64_rel)

#if !defined (InterlockedAnd64)

#define InterlockedAnd64 InterlockedAnd64_Inline

LONGLONG
FORCEINLINE
InterlockedAnd64_Inline (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG Value
    )
{
    LONGLONG Old;

    do {
        Old = *Destination;
    } while (InterlockedCompareExchange64(Destination,
                                          Old & Value,
                                          Old) != Old);

    return Old;
}

#endif

#define InterlockedAndAffinity InterlockedAnd64

#if !defined (InterlockedOr64)

#define InterlockedOr64 InterlockedOr64_Inline

LONGLONG
FORCEINLINE
InterlockedOr64_Inline (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG Value
    )
{
    LONGLONG Old;

    do {
        Old = *Destination;
    } while (InterlockedCompareExchange64(Destination,
                                          Old | Value,
                                          Old) != Old);

    return Old;
}

#endif

#define InterlockedOrAffinity InterlockedOr64

#if !defined (InterlockedXor64)

#define InterlockedXor64 InterlockedXor64_Inline

LONGLONG
FORCEINLINE
InterlockedXor64_Inline (
    __inout __drv_interlocked LONGLONG volatile *Destination,
    __in LONGLONG Value
    )
{
    LONGLONG Old;

    do {
        Old = *Destination;
    } while (InterlockedCompareExchange64(Destination,
                                          Old ^ Value,
                                          Old) != Old);

    return Old;
}

#endif

#if !defined (InterlockedBitTestAndSet)

#define InterlockedBitTestAndSet InterlockedBitTestAndSet_Inline

BOOLEAN
FORCEINLINE
InterlockedBitTestAndSet_Inline (
    __inout __drv_interlocked LONG volatile *Base,
    __in LONG Bit
    )
{
    LONG tBit;

    tBit = 1<<(Bit & (sizeof (*Base)*8-1));
    return (BOOLEAN) ((InterlockedOr (&Base[Bit/(sizeof (*Base)*8)], tBit)&tBit) != 0);
}

#endif

#if !defined (InterlockedBitTestAndReset)

#define InterlockedBitTestAndReset InterlockedBitTestAndReset_Inline

BOOLEAN
FORCEINLINE
InterlockedBitTestAndReset_Inline (
    __inout __drv_interlocked LONG volatile *Base,
    __in LONG Bit
    )
{
    LONG tBit;

    tBit = 1<<(Bit & (sizeof (*Base)*8-1));
    return (BOOLEAN) ((InterlockedAnd (&Base[Bit/(sizeof (*Base)*8)], ~tBit)&tBit) != 0);
}

#endif

#if !defined (InterlockedBitTestAndSet64)

#define InterlockedBitTestAndSet64 InterlockedBitTestAndSet64_Inline

BOOLEAN
FORCEINLINE
InterlockedBitTestAndSet64_Inline (
    __inout __drv_interlocked LONG64 volatile *Base,
    __in LONG64 Bit
    )
{
    LONG64 tBit;

    tBit = 1i64<<(Bit & (sizeof (*Base)*8-1));
    return (BOOLEAN) ((InterlockedOr64 (&Base[Bit/(sizeof (*Base)*8)], tBit)&tBit) != 0);
}

#endif

#if !defined (InterlockedBitTestAndReset64)

#define InterlockedBitTestAndReset64 InterlockedBitTestAndReset64_Inline

BOOLEAN
FORCEINLINE
InterlockedBitTestAndReset64_Inline (
    __inout __drv_interlocked LONG64 volatile *Base,
    __in LONG64 Bit
    )
{
    LONG64 tBit;

    tBit = 1i64<<(Bit & (sizeof (*Base)*8-1));
    return (BOOLEAN) ((InterlockedAnd64 (&Base[Bit/(sizeof (*Base)*8)], ~tBit)&tBit) != 0);
}

#endif

#if !defined (InterlockedBitTestAndComplement)

#define InterlockedBitTestAndComplement InterlockedBitTestAndComplement_Inline

BOOLEAN
FORCEINLINE
InterlockedBitTestAndComplement_Inline (
    __inout __drv_interlocked LONG volatile *Base,
    __in LONG Bit
    )
{
    LONG tBit;

    tBit = 1<<(Bit & (sizeof (*Base)*8-1));
    return (BOOLEAN) ((InterlockedXor (&Base[Bit/(sizeof (*Base)*8)], tBit)&tBit) != 0);
}

#endif

#if !defined (InterlockedBitTestAndComplement64)

#define InterlockedBitTestAndComplement64 InterlockedBitTestAndComplement64_Inline

BOOLEAN
FORCEINLINE
InterlockedBitTestAndComplement64_Inline (
    __inout __drv_interlocked LONG64 volatile *Base,
    __in LONG64 Bit
    )
{
    LONG64 tBit;

    tBit = 1i64<<(Bit & (sizeof (*Base)*8-1));
    return (BOOLEAN) ((InterlockedXor64 (&Base[Bit/(sizeof (*Base)*8)], tBit)&tBit) != 0);
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* defined(_M_IA64) && !defined(RC_INVOKED) && !defined(MIDL_PASS) */


typedef enum _LOGICAL_PROCESSOR_RELATIONSHIP {
    RelationProcessorCore,
    RelationNumaNode,
    RelationCache,
    RelationProcessorPackage,
    RelationGroup,
    RelationAll = 0xffff
} LOGICAL_PROCESSOR_RELATIONSHIP;

#define LTP_PC_SMT 0x1

typedef enum _PROCESSOR_CACHE_TYPE {
    CacheUnified,
    CacheInstruction,
    CacheData,
    CacheTrace
} PROCESSOR_CACHE_TYPE;

#define CACHE_FULLY_ASSOCIATIVE 0xFF

typedef struct _CACHE_DESCRIPTOR {
    UCHAR  Level;
    UCHAR  Associativity;
    USHORT LineSize;
    ULONG  Size;
    PROCESSOR_CACHE_TYPE Type;
} CACHE_DESCRIPTOR, *PCACHE_DESCRIPTOR;

typedef struct _SYSTEM_LOGICAL_PROCESSOR_INFORMATION {
    ULONG_PTR   ProcessorMask;
    LOGICAL_PROCESSOR_RELATIONSHIP Relationship;
    union {
        struct {
            UCHAR Flags;
        } ProcessorCore;
        struct {
            ULONG NodeNumber;
        } NumaNode;
        CACHE_DESCRIPTOR Cache;
        ULONGLONG  Reserved[2];
    } DUMMYUNIONNAME;
} SYSTEM_LOGICAL_PROCESSOR_INFORMATION, *PSYSTEM_LOGICAL_PROCESSOR_INFORMATION;

typedef struct _PROCESSOR_RELATIONSHIP {
    UCHAR Flags;
    UCHAR Reserved[21];
    USHORT GroupCount;
    __field_ecount(GroupCount) GROUP_AFFINITY GroupMask[ANYSIZE_ARRAY];
} PROCESSOR_RELATIONSHIP, *PPROCESSOR_RELATIONSHIP;

typedef struct _NUMA_NODE_RELATIONSHIP {
    ULONG NodeNumber;
    UCHAR Reserved[20];
    GROUP_AFFINITY GroupMask;
} NUMA_NODE_RELATIONSHIP, *PNUMA_NODE_RELATIONSHIP;

typedef struct _CACHE_RELATIONSHIP {
    UCHAR Level;
    UCHAR Associativity;
    USHORT LineSize;
    ULONG CacheSize;
    PROCESSOR_CACHE_TYPE Type;
    UCHAR Reserved[20];
    GROUP_AFFINITY GroupMask;
} CACHE_RELATIONSHIP, *PCACHE_RELATIONSHIP;

typedef struct _PROCESSOR_GROUP_INFO {
    UCHAR MaximumProcessorCount;
    UCHAR ActiveProcessorCount;
    UCHAR Reserved[38];
    KAFFINITY ActiveProcessorMask;
} PROCESSOR_GROUP_INFO, *PPROCESSOR_GROUP_INFO;

typedef struct _GROUP_RELATIONSHIP {
    USHORT MaximumGroupCount;
    USHORT ActiveGroupCount;
    UCHAR Reserved[20];
    PROCESSOR_GROUP_INFO GroupInfo[ANYSIZE_ARRAY];
} GROUP_RELATIONSHIP, *PGROUP_RELATIONSHIP;

__struct_bcount(Size) struct _SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX {
    LOGICAL_PROCESSOR_RELATIONSHIP Relationship;
    ULONG Size;
    union {
        PROCESSOR_RELATIONSHIP Processor;
        NUMA_NODE_RELATIONSHIP NumaNode;
        CACHE_RELATIONSHIP Cache;
        GROUP_RELATIONSHIP Group;
    } DUMMYUNIONNAME;
};

typedef struct _SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, *PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX;

//
// Define the I/O bus interface types.
//

typedef enum _INTERFACE_TYPE {
    InterfaceTypeUndefined = -1,
    Internal,
    Isa,
    Eisa,
    MicroChannel,
    TurboChannel,
    PCIBus,
    VMEBus,
    NuBus,
    PCMCIABus,
    CBus,
    MPIBus,
    MPSABus,
    ProcessorInternal,
    InternalPowerBus,
    PNPISABus,
    PNPBus,
    Vmcs,
    MaximumInterfaceType
}INTERFACE_TYPE, *PINTERFACE_TYPE;

//
// Define the DMA transfer widths.
//

typedef enum _DMA_WIDTH {
    Width8Bits,
    Width16Bits,
    Width32Bits,
    MaximumDmaWidth
}DMA_WIDTH, *PDMA_WIDTH;

//
// Define DMA transfer speeds.
//

typedef enum _DMA_SPEED {
    Compatible,
    TypeA,
    TypeB,
    TypeC,
    TypeF,
    MaximumDmaSpeed
}DMA_SPEED, *PDMA_SPEED;

//
// Define Interface reference/dereference routines for
//  Interfaces exported by IRP_MN_QUERY_INTERFACE
//

typedef VOID (*PINTERFACE_REFERENCE)(PVOID Context);
typedef VOID (*PINTERFACE_DEREFERENCE)(PVOID Context);

// end_wdm
// begin_ntddk

//
// Define types of bus information.
//

typedef enum _BUS_DATA_TYPE {
    ConfigurationSpaceUndefined = -1,
    Cmos,
    EisaConfiguration,
    Pos,
    CbusConfiguration,
    PCIConfiguration,
    VMEConfiguration,
    NuBusConfiguration,
    PCMCIAConfiguration,
    MPIConfiguration,
    MPSAConfiguration,
    PNPISAConfiguration,
    SgiInternalConfiguration,
    MaximumBusDataType
} BUS_DATA_TYPE, *PBUS_DATA_TYPE;


#include <guiddef.h>


#if defined(_M_AMD64) && !defined(RC_INVOKED) && !defined(MIDL_PASS)

//
// Define intrinsic function to do in's and out's.
//

#ifdef __cplusplus
extern "C" {
#endif

UCHAR
__inbyte (
    __in USHORT Port
    );

USHORT
__inword (
    __in USHORT Port
    );

ULONG
__indword (
    __in USHORT Port
    );

VOID
__outbyte (
    __in USHORT Port,
    __in UCHAR Data
    );

VOID
__outword (
    __in USHORT Port,
    __in USHORT Data
    );

VOID
__outdword (
    __in USHORT Port,
    __in ULONG Data
    );

VOID
__inbytestring (
    __in USHORT Port,
    __out_ecount_full(Count) PUCHAR Buffer,
    __in ULONG Count
    );

VOID
__inwordstring (
    __in USHORT Port,
    __out_ecount_full(Count) PUSHORT Buffer,
    __in ULONG Count
    );

VOID
__indwordstring (
    __in USHORT Port,
    __out_ecount_full(Count) PULONG Buffer,
    __in ULONG Count
    );

VOID
__outbytestring (
    __in USHORT Port,
    __in_ecount(Count) PUCHAR Buffer,
    __in ULONG Count
    );

VOID
__outwordstring (
    __in USHORT Port,
    __in_ecount(Count) PUSHORT Buffer,
    __in ULONG Count
    );

VOID
__outdwordstring (
    __in USHORT Port,
    __in_ecount(Count) PULONG Buffer,
    __in ULONG Count
    );

#pragma intrinsic(__inbyte)
#pragma intrinsic(__inword)
#pragma intrinsic(__indword)
#pragma intrinsic(__outbyte)
#pragma intrinsic(__outword)
#pragma intrinsic(__outdword)
#pragma intrinsic(__inbytestring)
#pragma intrinsic(__inwordstring)
#pragma intrinsic(__indwordstring)
#pragma intrinsic(__outbytestring)
#pragma intrinsic(__outwordstring)
#pragma intrinsic(__outdwordstring)

#ifdef __cplusplus
}
#endif

#endif // defined(_M_AMD64) && !defined(RC_INVOKED) && !defined(MIDL_PASS)

#if defined(_AMD64_) && !defined(DSF_DRIVER)

//
// I/O space read and write macros.
//
//  The READ/WRITE_REGISTER_* calls manipulate I/O registers in MEMORY space.
//
//  The READ/WRITE_PORT_* calls manipulate I/O registers in PORT space.
//

#ifdef __cplusplus
extern "C" {
#endif

__forceinline
UCHAR
READ_REGISTER_UCHAR (
    __in __drv_nonConstant volatile UCHAR *Register
    )
{
    _ReadWriteBarrier();
    return *Register;
}

__forceinline
USHORT
READ_REGISTER_USHORT (
    __in __drv_nonConstant volatile USHORT *Register
    )
{
    _ReadWriteBarrier();
    return *Register;
}

__forceinline
ULONG
READ_REGISTER_ULONG (
    __in __drv_nonConstant volatile ULONG *Register
    )
{
    _ReadWriteBarrier();
    return *Register;
}

__forceinline
ULONG64
READ_REGISTER_ULONG64 (
    __in __drv_nonConstant volatile ULONG64 *Register
    )
{
    _ReadWriteBarrier();
    return *Register;
}

__forceinline
VOID
READ_REGISTER_BUFFER_UCHAR (
    __in __drv_nonConstant PUCHAR Register,
    __out_ecount_full(Count) PUCHAR Buffer,
    __in ULONG Count
    )
{
    _ReadWriteBarrier();
    __movsb(Buffer, Register, Count);
    return;
}

__forceinline
VOID
READ_REGISTER_BUFFER_USHORT (
    __in __drv_nonConstant PUSHORT Register,
    __out_ecount_full(Count) PUSHORT Buffer,
    __in ULONG Count
    )
{
    _ReadWriteBarrier();
    __movsw(Buffer, Register, Count);
    return;
}

__forceinline
VOID
READ_REGISTER_BUFFER_ULONG (
    __in __drv_nonConstant PULONG Register,
    __out_ecount_full(Count) PULONG Buffer,
    __in ULONG Count
    )
{
    _ReadWriteBarrier();
    __movsd(Buffer, Register, Count);
    return;
}

__forceinline
VOID
READ_REGISTER_BUFFER_ULONG64 (
    __in __drv_nonConstant PULONG64 Register,
    __out_ecount_full(Count) PULONG64 Buffer,
    __in ULONG Count
    )
{
    _ReadWriteBarrier();
    __movsq(Buffer, Register, Count);
    return;
}

__forceinline
VOID
WRITE_REGISTER_UCHAR (
    __in __drv_nonConstant volatile UCHAR *Register,
    __in UCHAR Value
    )
{

    *Register = Value;
    FastFence();
    return;
}

__forceinline
VOID
WRITE_REGISTER_USHORT (
    __in __drv_nonConstant volatile USHORT *Register,
    __in USHORT Value
    )
{

    *Register = Value;
    FastFence();
    return;
}

__forceinline
VOID
WRITE_REGISTER_ULONG (
    __in __drv_nonConstant volatile ULONG *Register,
    __in ULONG Value
    )
{

    *Register = Value;
    FastFence();
    return;
}

__forceinline
VOID
WRITE_REGISTER_ULONG64 (
    __in __drv_nonConstant volatile ULONG64 *Register,
    __in ULONG64 Value
    )
{

    *Register = Value;
    FastFence();
    return;
}

__forceinline
VOID
WRITE_REGISTER_BUFFER_UCHAR (
    __in __drv_nonConstant PUCHAR Register,
    __in_ecount(Count) PUCHAR Buffer,
    __in ULONG Count
    )
{

    __movsb(Register, Buffer, Count);
    FastFence();
    return;
}

__forceinline
VOID
WRITE_REGISTER_BUFFER_USHORT (
    __in __drv_nonConstant PUSHORT Register,
    __in_ecount(Count) PUSHORT Buffer,
    __in ULONG Count
    )
{

    __movsw(Register, Buffer, Count);
    FastFence();
    return;
}

__forceinline
VOID
WRITE_REGISTER_BUFFER_ULONG (
    __in __drv_nonConstant PULONG Register,
    __in_ecount(Count) PULONG Buffer,
    __in ULONG Count
    )
{

    __movsd(Register, Buffer, Count);
    FastFence();
    return;
}

__forceinline
VOID
WRITE_REGISTER_BUFFER_ULONG64 (
    __in __drv_nonConstant PULONG64 Register,
    __in_ecount(Count) PULONG64 Buffer,
    __in ULONG Count
    )
{

    __movsq(Register, Buffer, Count);
    FastFence();
    return;
}

__forceinline
UCHAR
READ_PORT_UCHAR (
    __in __drv_nonConstant PUCHAR Port
    )

{
    UCHAR Result;

    _ReadWriteBarrier();
    Result = __inbyte((USHORT)((ULONG_PTR)Port));
    _ReadWriteBarrier();
    return Result;
}

__forceinline
USHORT
READ_PORT_USHORT (
    __in __drv_nonConstant PUSHORT Port
    )

{
    USHORT Result;

    _ReadWriteBarrier();
    Result = __inword((USHORT)((ULONG_PTR)Port));
    _ReadWriteBarrier();
    return Result;
}

__forceinline
ULONG
READ_PORT_ULONG (
    __in __drv_nonConstant PULONG Port
    )

{
    ULONG Result;

    _ReadWriteBarrier();
    Result = __indword((USHORT)((ULONG_PTR)Port));
    _ReadWriteBarrier();
    return Result;
}


__forceinline
VOID
READ_PORT_BUFFER_UCHAR (
    __in __drv_nonConstant PUCHAR Port,
    __out_ecount_full(Count) PUCHAR Buffer,
    __in ULONG Count
    )

{
    _ReadWriteBarrier();
    __inbytestring((USHORT)((ULONG_PTR)Port), Buffer, Count);
    _ReadWriteBarrier();
    return;
}

__forceinline
VOID
READ_PORT_BUFFER_USHORT (
    __in __drv_nonConstant PUSHORT Port,
    __out_ecount_full(Count) PUSHORT Buffer,
    __in ULONG Count
    )

{
    _ReadWriteBarrier();
    __inwordstring((USHORT)((ULONG_PTR)Port), Buffer, Count);
    _ReadWriteBarrier();
    return;
}

__forceinline
VOID
READ_PORT_BUFFER_ULONG (
    __in __drv_nonConstant PULONG Port,
    __out_ecount_full(Count) PULONG Buffer,
    __in ULONG Count
    )

{
    _ReadWriteBarrier();
    __indwordstring((USHORT)((ULONG_PTR)Port), Buffer, Count);
    _ReadWriteBarrier();
    return;
}

__forceinline
VOID
WRITE_PORT_UCHAR (
    __in __drv_nonConstant PUCHAR Port,
    __in UCHAR Value
    )

{
    _ReadWriteBarrier();
    __outbyte((USHORT)((ULONG_PTR)Port), Value);
    _ReadWriteBarrier();
    return;
}

__forceinline
VOID
WRITE_PORT_USHORT (
    __in __drv_nonConstant PUSHORT Port,
    __in USHORT Value
    )

{
    _ReadWriteBarrier();
    __outword((USHORT)((ULONG_PTR)Port), Value);
    _ReadWriteBarrier();
    return;
}

__forceinline
VOID
WRITE_PORT_ULONG (
    __in __drv_nonConstant PULONG Port,
    __in ULONG Value
    )

{
    _ReadWriteBarrier();
    __outdword((USHORT)((ULONG_PTR)Port), Value);
    _ReadWriteBarrier();
    return;
}

__forceinline
VOID
WRITE_PORT_BUFFER_UCHAR (
    __in __drv_nonConstant PUCHAR Port,
    __in_ecount(Count) PUCHAR Buffer,
    __in ULONG Count
    )

{
    _ReadWriteBarrier();
    __outbytestring((USHORT)((ULONG_PTR)Port), Buffer, Count);
    _ReadWriteBarrier();
    return;
}

__forceinline
VOID
WRITE_PORT_BUFFER_USHORT (
    __in __drv_nonConstant PUSHORT Port,
    __in_ecount(Count) PUSHORT Buffer,
    __in ULONG Count
    )

{
    _ReadWriteBarrier();
    __outwordstring((USHORT)((ULONG_PTR)Port), Buffer, Count);
    _ReadWriteBarrier();
    return;
}

__forceinline
VOID
WRITE_PORT_BUFFER_ULONG (
    __in __drv_nonConstant PULONG Port,
    __in_ecount(Count) PULONG Buffer,
    __in ULONG Count
    )

{
    _ReadWriteBarrier();
    __outdwordstring((USHORT)((ULONG_PTR)Port), Buffer, Count);
    _ReadWriteBarrier();
    return;
}

#ifdef __cplusplus
}
#endif

#elif defined(_AMD64_) && defined(DSF_DRIVER)
#include <DsfHrmPorts.h>
#endif


//
// Interrupt modes.
//

typedef enum _KINTERRUPT_MODE {
    LevelSensitive,
    Latched
} KINTERRUPT_MODE;

typedef enum _KINTERRUPT_POLARITY {
    InterruptPolarityUnknown,
    InterruptActiveHigh,
    InterruptActiveLow
} KINTERRUPT_POLARITY, *PKINTERRUPT_POLARITY;


typedef enum _MEMORY_CACHING_TYPE_ORIG {
    MmFrameBufferCached = 2
} MEMORY_CACHING_TYPE_ORIG;

typedef enum _MEMORY_CACHING_TYPE {
    MmNonCached = FALSE,
    MmCached = TRUE,
    MmWriteCombined = MmFrameBufferCached,
    MmHardwareCoherentCached,
    MmNonCachedUnordered,       // IA64
    MmUSWCCached,
    MmMaximumCacheType
} MEMORY_CACHING_TYPE;


//
// Structures used by the kernel drivers to describe which ports must be
// hooked out directly from the V86 emulator to the driver.
//

typedef enum _EMULATOR_PORT_ACCESS_TYPE {
    Uchar,
    Ushort,
    Ulong
} EMULATOR_PORT_ACCESS_TYPE, *PEMULATOR_PORT_ACCESS_TYPE;

//
// Access Modes
//

#define EMULATOR_READ_ACCESS    0x01
#define EMULATOR_WRITE_ACCESS   0x02

typedef struct _EMULATOR_ACCESS_ENTRY {
    ULONG BasePort;
    ULONG NumConsecutivePorts;
    EMULATOR_PORT_ACCESS_TYPE AccessType;
    UCHAR AccessMode;
    UCHAR StringSupport;
    PVOID Routine;
} EMULATOR_ACCESS_ENTRY, *PEMULATOR_ACCESS_ENTRY;


typedef struct _PCI_SLOT_NUMBER {
    union {
        struct {
            ULONG   DeviceNumber:5;
            ULONG   FunctionNumber:3;
            ULONG   Reserved:24;
        } bits;
        ULONG   AsULONG;
    } u;
} PCI_SLOT_NUMBER, *PPCI_SLOT_NUMBER;


#define PCI_TYPE0_ADDRESSES             6
#define PCI_TYPE1_ADDRESSES             2
#define PCI_TYPE2_ADDRESSES             5

typedef struct _PCI_COMMON_HEADER {
    USHORT  VendorID;                   // (ro)
    USHORT  DeviceID;                   // (ro)
    USHORT  Command;                    // Device control
    USHORT  Status;
    UCHAR   RevisionID;                 // (ro)
    UCHAR   ProgIf;                     // (ro)
    UCHAR   SubClass;                   // (ro)
    UCHAR   BaseClass;                  // (ro)
    UCHAR   CacheLineSize;              // (ro+)
    UCHAR   LatencyTimer;               // (ro+)
    UCHAR   HeaderType;                 // (ro)
    UCHAR   BIST;                       // Built in self test

    union {
        struct _PCI_HEADER_TYPE_0 {
            ULONG   BaseAddresses[PCI_TYPE0_ADDRESSES];
            ULONG   CIS;
            USHORT  SubVendorID;
            USHORT  SubSystemID;
            ULONG   ROMBaseAddress;
            UCHAR   CapabilitiesPtr;
            UCHAR   Reserved1[3];
            ULONG   Reserved2;
            UCHAR   InterruptLine;      //
            UCHAR   InterruptPin;       // (ro)
            UCHAR   MinimumGrant;       // (ro)
            UCHAR   MaximumLatency;     // (ro)
        } type0;


    } u;

} PCI_COMMON_HEADER, *PPCI_COMMON_HEADER;

#ifdef __cplusplus

typedef struct _PCI_COMMON_CONFIG : PCI_COMMON_HEADER {
    UCHAR   DeviceSpecific[192];
} PCI_COMMON_CONFIG, *PPCI_COMMON_CONFIG;

#else

typedef struct _PCI_COMMON_CONFIG {
    PCI_COMMON_HEADER DUMMYSTRUCTNAME;
    UCHAR   DeviceSpecific[192];
} PCI_COMMON_CONFIG, *PPCI_COMMON_CONFIG;

#endif

#define PCI_COMMON_HDR_LENGTH (FIELD_OFFSET (PCI_COMMON_CONFIG, DeviceSpecific))
#define PCI_EXTENDED_CONFIG_LENGTH          0x1000

#define PCI_MAX_DEVICES                     32
#define PCI_MAX_FUNCTION                    8
#define PCI_MAX_BRIDGE_NUMBER               0xFF

#define PCI_INVALID_VENDORID                0xFFFF

//
// Bit encodings for  PCI_COMMON_CONFIG.HeaderType
//

#define PCI_MULTIFUNCTION                   0x80
#define PCI_DEVICE_TYPE                     0x00
#define PCI_BRIDGE_TYPE                     0x01
#define PCI_CARDBUS_BRIDGE_TYPE             0x02

#define PCI_CONFIGURATION_TYPE(PciData) \
    (((PPCI_COMMON_CONFIG)(PciData))->HeaderType & ~PCI_MULTIFUNCTION)

#define PCI_MULTIFUNCTION_DEVICE(PciData) \
    ((((PPCI_COMMON_CONFIG)(PciData))->HeaderType & PCI_MULTIFUNCTION) != 0)

//
// Bit encodings for PCI_COMMON_CONFIG.Command
//

#define PCI_ENABLE_IO_SPACE                 0x0001
#define PCI_ENABLE_MEMORY_SPACE             0x0002
#define PCI_ENABLE_BUS_MASTER               0x0004
#define PCI_ENABLE_SPECIAL_CYCLES           0x0008
#define PCI_ENABLE_WRITE_AND_INVALIDATE     0x0010
#define PCI_ENABLE_VGA_COMPATIBLE_PALETTE   0x0020
#define PCI_ENABLE_PARITY                   0x0040  // (ro+)
#define PCI_ENABLE_WAIT_CYCLE               0x0080  // (ro+)
#define PCI_ENABLE_SERR                     0x0100  // (ro+)
#define PCI_ENABLE_FAST_BACK_TO_BACK        0x0200  // (ro)
#define PCI_DISABLE_LEVEL_INTERRUPT         0x0400

//
// Bit encodings for PCI_COMMON_CONFIG.Status
//

#define PCI_STATUS_INTERRUPT_PENDING        0x0008
#define PCI_STATUS_CAPABILITIES_LIST        0x0010  // (ro)
#define PCI_STATUS_66MHZ_CAPABLE            0x0020  // (ro)
#define PCI_STATUS_UDF_SUPPORTED            0x0040  // (ro)
#define PCI_STATUS_FAST_BACK_TO_BACK        0x0080  // (ro)
#define PCI_STATUS_DATA_PARITY_DETECTED     0x0100
#define PCI_STATUS_DEVSEL                   0x0600  // 2 bits wide
#define PCI_STATUS_SIGNALED_TARGET_ABORT    0x0800
#define PCI_STATUS_RECEIVED_TARGET_ABORT    0x1000
#define PCI_STATUS_RECEIVED_MASTER_ABORT    0x2000
#define PCI_STATUS_SIGNALED_SYSTEM_ERROR    0x4000
#define PCI_STATUS_DETECTED_PARITY_ERROR    0x8000

//
// The NT PCI Driver uses a WhichSpace parameter on its CONFIG_READ/WRITE
// routines.   The following values are defined-
//

#define PCI_WHICHSPACE_CONFIG               0x0
#define PCI_WHICHSPACE_ROM                  0x52696350

//
// PCI Capability IDs
//

#define PCI_CAPABILITY_ID_POWER_MANAGEMENT  0x01
#define PCI_CAPABILITY_ID_AGP               0x02
#define PCI_CAPABILITY_ID_VPD               0x03
#define PCI_CAPABILITY_ID_SLOT_ID           0x04
#define PCI_CAPABILITY_ID_MSI               0x05
#define PCI_CAPABILITY_ID_CPCI_HOTSWAP      0x06
#define PCI_CAPABILITY_ID_PCIX              0x07
#define PCI_CAPABILITY_ID_HYPERTRANSPORT    0x08
#define PCI_CAPABILITY_ID_VENDOR_SPECIFIC   0x09
#define PCI_CAPABILITY_ID_DEBUG_PORT        0x0A
#define PCI_CAPABILITY_ID_CPCI_RES_CTRL     0x0B
#define PCI_CAPABILITY_ID_SHPC              0x0C
#define PCI_CAPABILITY_ID_P2P_SSID          0x0D
#define PCI_CAPABILITY_ID_AGP_TARGET        0x0E
#define PCI_CAPABILITY_ID_SECURE            0x0F
#define PCI_CAPABILITY_ID_PCI_EXPRESS       0x10
#define PCI_CAPABILITY_ID_MSIX              0x11

//
// All PCI Capability structures have the following header.
//
// CapabilityID is used to identify the type of the structure (is
// one of the PCI_CAPABILITY_ID values above.
//
// Next is the offset in PCI Configuration space (0x40 - 0xfc) of the
// next capability structure in the list, or 0x00 if there are no more
// entries.
//
typedef struct _PCI_CAPABILITIES_HEADER {
    UCHAR   CapabilityID;
    UCHAR   Next;
} PCI_CAPABILITIES_HEADER, *PPCI_CAPABILITIES_HEADER;

//
// Power Management Capability
//

typedef struct _PCI_PMC {
    UCHAR       Version:3;
    UCHAR       PMEClock:1;
    UCHAR       Rsvd1:1;
    UCHAR       DeviceSpecificInitialization:1;
    UCHAR       Rsvd2:2;
    struct _PM_SUPPORT {
        UCHAR   Rsvd2:1;
        UCHAR   D1:1;
        UCHAR   D2:1;
        UCHAR   PMED0:1;
        UCHAR   PMED1:1;
        UCHAR   PMED2:1;
        UCHAR   PMED3Hot:1;
        UCHAR   PMED3Cold:1;
    } Support;
} PCI_PMC, *PPCI_PMC;

typedef struct _PCI_PMCSR {
    USHORT      PowerState:2;
    USHORT      Rsvd1:6;
    USHORT      PMEEnable:1;
    USHORT      DataSelect:4;
    USHORT      DataScale:2;
    USHORT      PMEStatus:1;
} PCI_PMCSR, *PPCI_PMCSR;


typedef struct _PCI_PMCSR_BSE {
    UCHAR       Rsvd1:6;
    UCHAR       D3HotSupportsStopClock:1;       // B2_B3#
    UCHAR       BusPowerClockControlEnabled:1;  // BPCC_EN
} PCI_PMCSR_BSE, *PPCI_PMCSR_BSE;


typedef struct _PCI_PM_CAPABILITY {

    PCI_CAPABILITIES_HEADER Header;

    //
    // Power Management Capabilities (Offset = 2)
    //

    union {
        PCI_PMC         Capabilities;
        USHORT          AsUSHORT;
    } PMC;

    //
    // Power Management Control/Status (Offset = 4)
    //

    union {
        PCI_PMCSR       ControlStatus;
        USHORT          AsUSHORT;
    } PMCSR;

    //
    // PMCSR PCI-PCI Bridge Support Extensions
    //

    union {
        PCI_PMCSR_BSE   BridgeSupport;
        UCHAR           AsUCHAR;
    } PMCSR_BSE;

    //
    // Optional read only 8 bit Data register.  Contents controlled by
    // DataSelect and DataScale in ControlStatus.
    //

    UCHAR   Data;

} PCI_PM_CAPABILITY, *PPCI_PM_CAPABILITY;

// end_wdm
// begin_ntddk

//
// AGP Capabilities
//
typedef struct _PCI_AGP_CAPABILITY {

    PCI_CAPABILITIES_HEADER Header;

    USHORT  Minor:4;
    USHORT  Major:4;
    USHORT  Rsvd1:8;

    struct _PCI_AGP_STATUS {
        ULONG   Rate:3;
        ULONG   Agp3Mode:1;
        ULONG   FastWrite:1;
        ULONG   FourGB:1;
        ULONG   HostTransDisable:1;
        ULONG   Gart64:1;
        ULONG   ITA_Coherent:1;
        ULONG   SideBandAddressing:1;                   // SBA
        ULONG   CalibrationCycle:3;
        ULONG   AsyncRequestSize:3;
        ULONG   Rsvd1:1;
        ULONG   Isoch:1;
        ULONG   Rsvd2:6;
        ULONG   RequestQueueDepthMaximum:8;             // RQ
    } AGPStatus;

    struct _PCI_AGP_COMMAND {
        ULONG   Rate:3;
        ULONG   Rsvd1:1;
        ULONG   FastWriteEnable:1;
        ULONG   FourGBEnable:1;
        ULONG   Rsvd2:1;
        ULONG   Gart64:1;
        ULONG   AGPEnable:1;
        ULONG   SBAEnable:1;
        ULONG   CalibrationCycle:3;
        ULONG   AsyncReqSize:3;
        ULONG   Rsvd3:8;
        ULONG   RequestQueueDepth:8;
    } AGPCommand;

} PCI_AGP_CAPABILITY, *PPCI_AGP_CAPABILITY;

//
// An AGPv3 Target must have an extended capability,
// but it's only present for a Master when the Isoch
// bit is set in its status register
//
typedef enum _EXTENDED_AGP_REGISTER {
    IsochStatus,
    AgpControl,
    ApertureSize,
    AperturePageSize,
    GartLow,
    GartHigh,
    IsochCommand
} EXTENDED_AGP_REGISTER, *PEXTENDED_AGP_REGISTER;

typedef struct _PCI_AGP_ISOCH_STATUS {
    ULONG ErrorCode: 2;
    ULONG Rsvd1: 1;
    ULONG Isoch_L: 3;
    ULONG Isoch_Y: 2;
    ULONG Isoch_N: 8;
    ULONG Rsvd2: 16;
} PCI_AGP_ISOCH_STATUS, *PPCI_AGP_ISOCH_STATUS;

typedef struct _PCI_AGP_CONTROL {
    ULONG Rsvd1: 7;
    ULONG GTLB_Enable: 1;
    ULONG AP_Enable: 1;
    ULONG CAL_Disable: 1;
    ULONG Rsvd2: 22;
} PCI_AGP_CONTROL, *PPCI_AGP_CONTROL;

typedef struct _PCI_AGP_APERTURE_PAGE_SIZE {
    USHORT PageSizeMask: 11;
    USHORT Rsvd1: 1;
    USHORT PageSizeSelect: 4;
} PCI_AGP_APERTURE_PAGE_SIZE, *PPCI_AGP_APERTURE_PAGE_SIZE;

typedef struct _PCI_AGP_ISOCH_COMMAND {
    USHORT Rsvd1: 6;
    USHORT Isoch_Y: 2;
    USHORT Isoch_N: 8;
} PCI_AGP_ISOCH_COMMAND, *PPCI_AGP_ISOCH_COMMAND;

typedef struct PCI_AGP_EXTENDED_CAPABILITY {

    PCI_AGP_ISOCH_STATUS IsochStatus;

//
// Target only ----------------<<-begin->>
//
    PCI_AGP_CONTROL AgpControl;
    USHORT ApertureSize;
    PCI_AGP_APERTURE_PAGE_SIZE AperturePageSize;
    ULONG GartLow;
    ULONG GartHigh;
//
// ------------------------------<<-end->>
//

    PCI_AGP_ISOCH_COMMAND IsochCommand;

} PCI_AGP_EXTENDED_CAPABILITY, *PPCI_AGP_EXTENDED_CAPABILITY;


#define PCI_AGP_RATE_1X     0x1
#define PCI_AGP_RATE_2X     0x2
#define PCI_AGP_RATE_4X     0x4

// end_ntddk
// begin_wdm

//
// PCI-X Capability
//

typedef struct {

    PCI_CAPABILITIES_HEADER Header;

    union {
        struct {
            USHORT  DataParityErrorRecoveryEnable:1;
            USHORT  EnableRelaxedOrdering:1;
            USHORT  MaxMemoryReadByteCount:2;
            USHORT  MaxOutstandingSplitTransactions:3;
            USHORT  Reserved:9;
        } bits;
        USHORT  AsUSHORT;
    } Command;

    union {
        struct {
            ULONG   FunctionNumber:3;
            ULONG   DeviceNumber:5;
            ULONG   BusNumber:8;
            ULONG   Device64Bit:1;
            ULONG   Capable133MHz:1;
            ULONG   SplitCompletionDiscarded:1;
            ULONG   UnexpectedSplitCompletion:1;
            ULONG   DeviceComplexity:1;
            ULONG   DesignedMaxMemoryReadByteCount:2;
            ULONG   DesignedMaxOutstandingSplitTransactions:3;
            ULONG   DesignedMaxCumulativeReadSize:3;
            ULONG   ReceivedSplitCompletionErrorMessage:1;
            ULONG   CapablePCIX266:1;
            ULONG   CapablePCIX533:1;
        } bits;
        ULONG   AsULONG;
    } Status;
} PCI_X_CAPABILITY, *PPCI_X_CAPABILITY;

// end_wdm
// begin_ntddk

//
// PCI-X Bridge Capability
//

//
// Values for BusModeFrequency in the SecondaryStatus register
//
#define PCIX_MODE_CONVENTIONAL_PCI  0x0
#define PCIX_MODE1_66MHZ            0x1
#define PCIX_MODE1_100MHZ           0x2
#define PCIX_MODE1_133MHZ           0x3
#define PCIX_MODE2_266_66MHZ        0x9
#define PCIX_MODE2_266_100MHZ       0xA
#define PCIX_MODE2_266_133MHZ       0xB
#define PCIX_MODE2_533_66MHZ        0xD
#define PCIX_MODE2_533_100MHZ       0xE
#define PCIX_MODE2_533_133MHZ       0xF

//
// Values for the Version in the SecondaryStatus register
//
#define PCIX_VERSION_MODE1_ONLY     0x0
#define PCIX_VERSION_MODE2_ECC      0x1
#define PCIX_VERSION_DUAL_MODE_ECC  0x2

typedef struct _PCIX_BRIDGE_CAPABILITY {

    PCI_CAPABILITIES_HEADER Header;

    union {
        struct {
            USHORT Bus64Bit:1;
            USHORT Bus133MHzCapable:1;
            USHORT SplitCompletionDiscarded:1;
            USHORT UnexpectedSplitCompletion:1;
            USHORT SplitCompletionOverrun:1;
            USHORT SplitRequestDelayed:1;
            USHORT BusModeFrequency:4;  // PCIX_MODE_x
            USHORT Rsvd:2;
            USHORT Version:2;           // PCIX_VERSION_x
            USHORT Bus266MHzCapable:1;
            USHORT Bus533MHzCapable:1;
        } DUMMYSTRUCTNAME;
        USHORT AsUSHORT;
    } SecondaryStatus;

    union {
        struct {
            ULONG FunctionNumber:3;
            ULONG DeviceNumber:5;
            ULONG BusNumber:8;
            ULONG Device64Bit:1;
            ULONG Device133MHzCapable:1;
            ULONG SplitCompletionDiscarded:1;
            ULONG UnexpectedSplitCompletion:1;
            ULONG SplitCompletionOverrun:1;
            ULONG SplitRequestDelayed:1;
            ULONG Rsvd:7;
            ULONG DIMCapable:1;
            ULONG Device266MHzCapable:1;
            ULONG Device533MHzCapable:1;
        } DUMMYSTRUCTNAME;
        ULONG AsULONG;
    } BridgeStatus;

    USHORT UpstreamSplitTransactionCapacity;
    USHORT UpstreamSplitTransactionLimit;

    USHORT DownstreamSplitTransactionCapacity;
    USHORT DownstreamSplitTransactionLimit;

    union {
        struct {
            ULONG SelectSecondaryRegisters:1;
            ULONG ErrorPresentInOtherBank:1;
            ULONG AdditionalCorrectableError:1;
            ULONG AdditionalUncorrectableError:1;
            ULONG ErrorPhase:3;
            ULONG ErrorCorrected:1;
            ULONG Syndrome:8;
            ULONG ErrorFirstCommand:4;
            ULONG ErrorSecondCommand:4;
            ULONG ErrorUpperAttributes:4;
            ULONG ControlUpdateEnable:1;
            ULONG Rsvd:1;
            ULONG DisableSingleBitCorrection:1;
            ULONG EccMode:1;
        } DUMMYSTRUCTNAME;
        ULONG AsULONG;
    } EccControlStatus;

    ULONG EccFirstAddress;
    ULONG EccSecondAddress;
    ULONG EccAttribute;

} PCIX_BRIDGE_CAPABILITY, *PPCIX_BRIDGE_CAPABILITY;

//
// PCI to PCI Bridge Subsystem ID Capability
//
typedef struct _PCI_SUBSYSTEM_IDS_CAPABILITY {

    PCI_CAPABILITIES_HEADER Header;
    USHORT Reserved;
    USHORT SubVendorID;
    USHORT SubSystemID;

} PCI_SUBSYSTEM_IDS_CAPABILITY, *PPCI_SUBSYSTEM_IDS_CAPABILITY;

//
// _OSC is used by OSPM to query the capabilities of a device and to
// communicate the features supported by the device driver to the platform.
// The _OSC interface for PCI host bridge devices that originate PCI, PCI-X or
// PCI Express hierarchies is identified by a UUID of {33db4d5b-1ff7-401c-9657-
// 7441c03dd766}. A revision ID of 1 indicates that the capabilities buffer is
// composed of 3 DWORDs.
// The first DWORD is common across all OSC implementations and includes status
// and error information.
// The second DWORD (Support Field) provides information regarding OS supported
// features.
// The third DWORD (Control Field) is used to submit request for control of
// associated features. If any bits in the control field are returned cleared,
// then the respective feature is unsupported by the platform and must not be
// enabled.
// According to the PCI Firmware Specification a machine with multiple host
// bridge devices should report the same capabilities for all host bridges
// and also negotiate control of the features in the same way.
//

#define OSC_FIRMWARE_FAILURE                            0x02
#define OSC_UNRECOGNIZED_UUID                           0x04
#define OSC_UNRECOGNIZED_REVISION                       0x08
#define OSC_CAPABILITIES_MASKED                         0x10

#define PCI_ROOT_BUS_OSC_METHOD_CAPABILITY_REVISION     0x01

//
// The following declarations pertain to the second and third DWORD in
// evaluation of _OSC for PCI host bridge devices.
//

typedef struct _PCI_ROOT_BUS_OSC_SUPPORT_FIELD {
    union {
        struct {
            ULONG ExtendedConfigOpRegions:1;
            ULONG ActiveStatePowerManagement:1;
            ULONG ClockPowerManagement:1;
            ULONG SegmentGroups:1;
            ULONG MessageSignaledInterrupts:1;
            ULONG WindowsHardwareErrorArchitecture:1;
            ULONG Reserved:26;
        } DUMMYSTRUCTNAME;
        ULONG AsULONG;
    } u;
} PCI_ROOT_BUS_OSC_SUPPORT_FIELD, *PPCI_ROOT_BUS_OSC_SUPPORT_FIELD;

typedef struct _PCI_ROOT_BUS_OSC_CONTROL_FIELD {
    union {
        struct {
            ULONG ExpressNativeHotPlug:1;
            ULONG ShpcNativeHotPlug:1;
            ULONG ExpressNativePME:1;
            ULONG ExpressAdvancedErrorReporting:1;
            ULONG ExpressCapabilityStructure:1;
            ULONG Reserved:27;
        } DUMMYSTRUCTNAME;
        ULONG AsULONG;
    } u;
} PCI_ROOT_BUS_OSC_CONTROL_FIELD, *PPCI_ROOT_BUS_OSC_CONTROL_FIELD;

//
// An enumerator for the PCI physical and electrical interface.
//

typedef enum _PCI_HARDWARE_INTERFACE {

    PciConventional,
    PciXMode1,
    PciXMode2,
    PciExpress

} PCI_HARDWARE_INTERFACE, *PPCI_HARDWARE_INTERFACE;

typedef enum {

    BusWidth32Bits,
    BusWidth64Bits

} PCI_BUS_WIDTH;

typedef struct _PCI_ROOT_BUS_HARDWARE_CAPABILITY {

    //
    // Describes the secondary side of a PCI root bus.
    //

    PCI_HARDWARE_INTERFACE SecondaryInterface;

    //
    // These additional capabilities are available when each of the following
    // is true.
    // 1. The secondary side of a PCI root bus operates in conventional or
    //    PCI-X mode.
    // 2. The PCI root bus has a hardware ID or compatible ID of PNP0A03.
    // 3. A _DSM function 4 is defined for the root bus.
    //

    struct {

        //
        // This boolean indicates if the remaining fields describing the bus
        // capabilities are valid or not.
        //

        BOOLEAN BusCapabilitiesFound;


        //
        // Provides information on current and supported speeds/modes.
        //

        ULONG CurrentSpeedAndMode;
        ULONG SupportedSpeedsAndModes;

        //
        // Describes the root bus capability on forwarding of Device ID message
        // transactions.
        //

        BOOLEAN DeviceIDMessagingCapable;

        //
        // Provides the width for a PCI interface.
        //

        PCI_BUS_WIDTH SecondaryBusWidth;
    } DUMMYSTRUCTNAME;

    //
    // Fields describing features supported as well as control for them from
    // the bios.
    //

    PCI_ROOT_BUS_OSC_SUPPORT_FIELD OscFeatureSupport;
    PCI_ROOT_BUS_OSC_CONTROL_FIELD OscControlRequest;
    PCI_ROOT_BUS_OSC_CONTROL_FIELD OscControlGranted;

} PCI_ROOT_BUS_HARDWARE_CAPABILITY, *PPCI_ROOT_BUS_HARDWARE_CAPABILITY;

//
// PCI Express Capability
//

typedef union _PCI_EXPRESS_CAPABILITIES_REGISTER {

    struct {

        USHORT CapabilityVersion:4;
        USHORT DeviceType:4;               // PCI_EXPRESS_DEVICE_TYPE
        USHORT SlotImplemented:1;
        USHORT InterruptMessageNumber:5;
        USHORT Rsvd:2;
    } DUMMYSTRUCTNAME;

    USHORT AsUSHORT;

} PCI_EXPRESS_CAPABILITIES_REGISTER, *PPCI_EXPRESS_CAPABILITIES_REGISTER;

typedef union _PCI_EXPRESS_DEVICE_CAPABILITIES_REGISTER {

    struct {

        ULONG MaxPayloadSizeSupported:3;     // EXPRESS_MAX_PAYLOAD_SIZE
        ULONG PhantomFunctionsSupported:2;
        ULONG ExtendedTagSupported:1;
        ULONG L0sAcceptableLatency:3;        // EXPRESS_L0S_LATENCY
        ULONG L1AcceptableLatency:3;         // EXPRESS_L1_LATENCY
        ULONG Undefined:3;
        ULONG RoleBasedErrorReporting:1;
        ULONG Rsvd1:2;
        ULONG CapturedSlotPowerLimit:8;
        ULONG CapturedSlotPowerLimitScale:2;
        ULONG Rsvd2:4;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_DEVICE_CAPABILITIES_REGISTER, *PPCI_EXPRESS_DEVICE_CAPABILITIES_REGISTER;

//
// The low 3 bits of the PCI Express device control register dictate whether
// a device that implements AER routes error messages to the root complex.
// This mask is used when programming the AER bits in the device control
// register.
//

#define PCI_EXPRESS_AER_DEVICE_CONTROL_MASK 0x07;

typedef union _PCI_EXPRESS_DEVICE_CONTROL_REGISTER {

    struct {

        USHORT CorrectableErrorEnable:1;
        USHORT NonFatalErrorEnable:1;
        USHORT FatalErrorEnable:1;
        USHORT UnsupportedRequestErrorEnable:1;
        USHORT EnableRelaxedOrder:1;
        USHORT MaxPayloadSize:3;                 // EXPRESS_MAX_PAYLOAD_SIZE
        USHORT ExtendedTagEnable:1;
        USHORT PhantomFunctionsEnable:1;
        USHORT AuxPowerEnable:1;
        USHORT NoSnoopEnable:1;
        USHORT MaxReadRequestSize:3;             // EXPRESS_MAX_PAYLOAD_SIZE
        USHORT BridgeConfigRetryEnable:1;
    } DUMMYSTRUCTNAME;

    USHORT AsUSHORT;

} PCI_EXPRESS_DEVICE_CONTROL_REGISTER, *PPCI_EXPRESS_DEVICE_CONTROL_REGISTER;

//
// The low 4 bits of the PCI Express device status register hold AER device
// status. This mask is used when programming the AER bits in the device status
// register.
//

#define PCI_EXPRESS_AER_DEVICE_STATUS_MASK 0x0F;

typedef union _PCI_EXPRESS_DEVICE_STATUS_REGISTER {

    struct {

        USHORT CorrectableErrorDetected:1;
        USHORT NonFatalErrorDetected:1;
        USHORT FatalErrorDetected:1;
        USHORT UnsupportedRequestDetected:1;
        USHORT AuxPowerDetected:1;
        USHORT TransactionsPending:1;
        USHORT Rsvd:10;
    } DUMMYSTRUCTNAME;

    USHORT AsUSHORT;

} PCI_EXPRESS_DEVICE_STATUS_REGISTER, *PPCI_EXPRESS_DEVICE_STATUS_REGISTER;

typedef union _PCI_EXPRESS_LINK_CAPABILITIES_REGISTER {

    struct {

        ULONG MaximumLinkSpeed:4;
        ULONG MaximumLinkWidth:6;
        ULONG ActiveStatePMSupport:2;   // EXPRESS_ASPM_CONFIG
        ULONG L0sExitLatency:3;         // EXPRESS_L0S_LATENCY
        ULONG L1ExitLatency:3;          // EXPRESS_L1_LATENCY
        ULONG ClockPowerManagement:1;
        ULONG SurpriseDownErrorReportingCapable:1;
        ULONG DataLinkLayerActiveReportingCapable:1;
        ULONG Rsvd:3;
        ULONG PortNumber:8;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_LINK_CAPABILITIES_REGISTER, *PPCI_EXPRESS_LINK_CAPABILITIES_REGISTER;

typedef union _PCI_EXPRESS_LINK_CONTROL_REGISTER {

    struct {

        USHORT ActiveStatePMControl:2;    // EXPRESS_ASPM_CONFIG
        USHORT Rsvd1:1;
        USHORT ReadCompletionBoundary:1;  // EXPRESS_RCB
        USHORT LinkDisable:1;
        USHORT RetrainLink:1;
        USHORT CommonClockConfig:1;
        USHORT ExtendedSynch:1;
        USHORT EnableClockPowerManagement:1;
        USHORT Rsvd2:7;
    } DUMMYSTRUCTNAME;

    USHORT AsUSHORT;

} PCI_EXPRESS_LINK_CONTROL_REGISTER, *PPCI_EXPRESS_LINK_CONTROL_REGISTER;

typedef union _PCI_EXPRESS_LINK_STATUS_REGISTER {

    struct {

        USHORT LinkSpeed:4;
        USHORT LinkWidth:6;
        USHORT Undefined:1;
        USHORT LinkTraining:1;
        USHORT SlotClockConfig:1;
        USHORT DataLinkLayerActive:1;
        USHORT Rsvd:2;
    } DUMMYSTRUCTNAME;

    USHORT AsUSHORT;

} PCI_EXPRESS_LINK_STATUS_REGISTER, *PPCI_EXPRESS_LINK_STATUS_REGISTER;

typedef union _PCI_EXPRESS_SLOT_CAPABILITIES_REGISTER {

    struct {

        ULONG AttentionButtonPresent:1;
        ULONG PowerControllerPresent:1;
        ULONG MRLSensorPresent:1;
        ULONG AttentionIndicatorPresent:1;
        ULONG PowerIndicatorPresent:1;
        ULONG HotPlugSurprise:1;
        ULONG HotPlugCapable:1;
        ULONG SlotPowerLimit:8;
        ULONG SlotPowerLimitScale:2;
        ULONG ElectromechanicalLockPresent:1;
        ULONG NoCommandCompletedSupport:1;
        ULONG PhysicalSlotNumber:13;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_SLOT_CAPABILITIES_REGISTER, *PPCI_EXPRESS_SLOT_CAPABILITIES_REGISTER;

typedef union _PCI_EXPRESS_SLOT_CONTROL_REGISTER {

    struct {

        USHORT AttentionButtonEnable:1;
        USHORT PowerFaultDetectEnable:1;
        USHORT MRLSensorEnable:1;
        USHORT PresenceDetectEnable:1;
        USHORT CommandCompletedEnable:1;
        USHORT HotPlugInterruptEnable:1;
        USHORT AttentionIndicatorControl:2;  // EXPRESS_INDICATOR_STATE
        USHORT PowerIndicatorControl:2;      // EXPRESS_INDICATOR_STATE
        USHORT PowerControllerControl:1;     // EXPRESS_POWER_STATE
        USHORT ElectromechanicalLockControl:1;
        USHORT DataLinkStateChangeEnable:1;
        USHORT Rsvd:3;
    } DUMMYSTRUCTNAME;

    USHORT AsUSHORT;

} PCI_EXPRESS_SLOT_CONTROL_REGISTER, *PPCI_EXPRESS_SLOT_CONTROL_REGISTER;

typedef union _PCI_EXPRESS_SLOT_STATUS_REGISTER {

    struct {

        USHORT AttentionButtonPressed:1;
        USHORT PowerFaultDetected:1;
        USHORT MRLSensorChanged:1;
        USHORT PresenceDetectChanged:1;
        USHORT CommandCompleted:1;
        USHORT MRLSensorState:1;        // EXPRESS_MRL_STATE
        USHORT PresenceDetectState:1;   // EXPRESS_CARD_PRESENCE
        USHORT ElectromechanicalLockEngaged:1;
        USHORT DataLinkStateChanged:1;
        USHORT Rsvd:7;
    } DUMMYSTRUCTNAME;

    USHORT AsUSHORT;

} PCI_EXPRESS_SLOT_STATUS_REGISTER, *PPCI_EXPRESS_SLOT_STATUS_REGISTER;

typedef union _PCI_EXPRESS_ROOT_CONTROL_REGISTER {

    struct {

        USHORT CorrectableSerrEnable:1;
        USHORT NonFatalSerrEnable:1;
        USHORT FatalSerrEnable:1;
        USHORT PMEInterruptEnable:1;
        USHORT CRSSoftwareVisibilityEnable:1;
        USHORT Rsvd:11;
    } DUMMYSTRUCTNAME;

    USHORT AsUSHORT;

} PCI_EXPRESS_ROOT_CONTROL_REGISTER, *PPCI_EXPRESS_ROOT_CONTROL_REGISTER;

typedef union _PCI_EXPRESS_ROOT_CAPABILITIES_REGISTER {

    struct {

        USHORT CRSSoftwareVisibility:1;
        USHORT Rsvd:15;
    } DUMMYSTRUCTNAME;

    USHORT AsUSHORT;

} PCI_EXPRESS_ROOT_CAPABILITIES_REGISTER, *PPCI_EXPRESS_ROOT_CAPABILITIES_REGISTER;

typedef union _PCI_EXPRESS_ROOT_STATUS_REGISTER {

    struct {

        ULONG PMERequestorId:16;  // PCI_EXPRESS_REQUESTOR_ID
        ULONG PMEStatus:1;
        ULONG PMEPending:1;
        ULONG Rsvd:14;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_ROOT_STATUS_REGISTER, *PPCI_EXPRESS_ROOT_STATUS_REGISTER;

//
// PCI Express Capability
//

typedef struct _PCI_EXPRESS_CAPABILITY {

    PCI_CAPABILITIES_HEADER Header;
    PCI_EXPRESS_CAPABILITIES_REGISTER ExpressCapabilities;

    PCI_EXPRESS_DEVICE_CAPABILITIES_REGISTER DeviceCapabilities;

    PCI_EXPRESS_DEVICE_CONTROL_REGISTER DeviceControl;
    PCI_EXPRESS_DEVICE_STATUS_REGISTER DeviceStatus;

    PCI_EXPRESS_LINK_CAPABILITIES_REGISTER LinkCapabilities;

    PCI_EXPRESS_LINK_CONTROL_REGISTER LinkControl;
    PCI_EXPRESS_LINK_STATUS_REGISTER LinkStatus;

    PCI_EXPRESS_SLOT_CAPABILITIES_REGISTER SlotCapabilities;

    PCI_EXPRESS_SLOT_CONTROL_REGISTER SlotControl;
    PCI_EXPRESS_SLOT_STATUS_REGISTER SlotStatus;

    PCI_EXPRESS_ROOT_CONTROL_REGISTER RootControl;
    PCI_EXPRESS_ROOT_CAPABILITIES_REGISTER RootCapabilities;

    PCI_EXPRESS_ROOT_STATUS_REGISTER RootStatus;

} PCI_EXPRESS_CAPABILITY, *PPCI_EXPRESS_CAPABILITY;

typedef enum {

    MRLClosed = 0,
    MRLOpen

} PCI_EXPRESS_MRL_STATE;

typedef enum {

    SlotEmpty = 0,
    CardPresent

} PCI_EXPRESS_CARD_PRESENCE;

typedef enum {

    IndicatorOn = 1,
    IndicatorBlink,
    IndicatorOff

} PCI_EXPRESS_INDICATOR_STATE;

typedef enum {

    PowerOn = 0,
    PowerOff

} PCI_EXPRESS_POWER_STATE;

typedef enum {

    L0sEntrySupport = 1,
    L0sAndL1EntrySupport = 3

} PCI_EXPRESS_ASPM_SUPPORT;

typedef enum {

    L0sAndL1EntryDisabled,
    L0sEntryEnabled,
    L1EntryEnabled,
    L0sAndL1EntryEnabled

} PCI_EXPRESS_ASPM_CONTROL;

typedef enum {

    L0s_Below64ns = 0,
    L0s_64ns_128ns,
    L0s_128ns_256ns,
    L0s_256ns_512ns,
    L0s_512ns_1us,
    L0s_1us_2us,
    L0s_2us_4us,
    L0s_Above4us

} PCI_EXPRESS_L0s_EXIT_LATENCY;

typedef enum {

    L1_Below1us = 0,
    L1_1us_2us,
    L1_2us_4us,
    L1_4us_8us,
    L1_8us_16us,
    L1_16us_32us,
    L1_32us_64us,
    L1_Above64us

} PCI_EXPRESS_L1_EXIT_LATENCY;

typedef enum {

    PciExpressEndpoint = 0,
    PciExpressLegacyEndpoint,
    PciExpressRootPort = 4,
    PciExpressUpstreamSwitchPort,
    PciExpressDownstreamSwitchPort,
    PciExpressToPciXBridge,
    PciXToExpressBridge,
    PciExpressRootComplexIntegratedEndpoint,
    PciExpressRootComplexEventCollector

} PCI_EXPRESS_DEVICE_TYPE;

typedef enum {

    MaxPayload128Bytes = 0,
    MaxPayload256Bytes,
    MaxPayload512Bytes,
    MaxPayload1024Bytes,
    MaxPayload2048Bytes,
    MaxPayload4096Bytes

} PCI_EXPRESS_MAX_PAYLOAD_SIZE;

typedef union _PCI_EXPRESS_PME_REQUESTOR_ID {

    struct {

        USHORT FunctionNumber:3;
        USHORT DeviceNumber:5;
        USHORT BusNumber:8;
    } DUMMYSTRUCTNAME;

    USHORT AsUSHORT;

} PCI_EXPRESS_PME_REQUESTOR_ID, *PPCI_EXPRESS_PME_REQUESTOR_ID;

// end_ntddk
// begin_wdm

//
// PCI Express Extended Capabilities.
//

#define PCI_EXPRESS_ADVANCED_ERROR_REPORTING_CAP_ID                     0x0001
#define PCI_EXPRESS_VIRTUAL_CHANNEL_CAP_ID                              0x0002
#define PCI_EXPRESS_DEVICE_SERIAL_NUMBER_CAP_ID                         0x0003
#define PCI_EXPRESS_POWER_BUDGETING_CAP_ID                              0x0004
#define PCI_EXPRESS_RC_LINK_DECLARATION_CAP_ID                          0x0005
#define PCI_EXPRESS_RC_INTERNAL_LINK_CONTROL_CAP_ID                     0x0006
#define PCI_EXPRESS_RC_EVENT_COLLECTOR_ENDPOINT_ASSOCIATION_CAP_ID      0x0007
#define PCI_EXPRESS_MFVC_CAP_ID                                         0x0008
#define PCI_EXPRESS_VC_AND_MFVC_CAP_ID                                  0x0009
#define PCI_EXPRESS_RCRB_HEADER_CAP_ID                                  0x000A
#define PCI_EXPRESS_SINGLE_ROOT_IO_VIRTUALIZATION_CAP_ID                0x0010

//
// All Enhanced capabilities have the following header.
//

typedef struct _PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER {

    USHORT CapabilityID;
    USHORT Version:4;
    USHORT Next:12;

} PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER, *PPCI_EXPRESS_ENHANCED_CAPABILITY_HEADER;

//
// Serial Number Capability.
//

typedef struct _PCI_EXPRESS_SERIAL_NUMBER_CAPABILITY {

    PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;

    ULONG LowSerialNumber;
    ULONG HighSerialNumber;

} PCI_EXPRESS_SERIAL_NUMBER_CAPABILITY, *PPCI_EXPRESS_SERIAL_NUMBER_CAPABILITY;

//
// PCI Express Advanced Error Reporting structures.
//

typedef union _PCI_EXPRESS_UNCORRECTABLE_ERROR_STATUS {

    struct {
        ULONG Undefined:1;
        ULONG Reserved1:3;
        ULONG DataLinkProtocolError:1;
        ULONG SurpriseDownError:1;
        ULONG Reserved2:6;
        ULONG PoisonedTLP:1;
        ULONG FlowControlProtocolError:1;
        ULONG CompletionTimeout:1;
        ULONG CompleterAbort:1;
        ULONG UnexpectedCompletion:1;
        ULONG ReceiverOverflow:1;
        ULONG MalformedTLP:1;
        ULONG ECRCError:1;
        ULONG UnsupportedRequestError:1;
        ULONG Reserved3:11;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_UNCORRECTABLE_ERROR_STATUS, *PPCI_EXPRESS_UNCORRECTABLE_ERROR_STATUS;

typedef union _PCI_EXPRESS_UNCORRECTABLE_ERROR_MASK {

    struct {
        ULONG Undefined:1;
        ULONG Reserved1:3;
        ULONG DataLinkProtocolError:1;
        ULONG SurpriseDownError:1;
        ULONG Reserved2:6;
        ULONG PoisonedTLP:1;
        ULONG FlowControlProtocolError:1;
        ULONG CompletionTimeout:1;
        ULONG CompleterAbort:1;
        ULONG UnexpectedCompletion:1;
        ULONG ReceiverOverflow:1;
        ULONG MalformedTLP:1;
        ULONG ECRCError:1;
        ULONG UnsupportedRequestError:1;
        ULONG Reserved3:11;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_UNCORRECTABLE_ERROR_MASK, *PPCI_EXPRESS_UNCORRECTABLE_ERROR_MASK;

typedef union _PCI_EXPRESS_UNCORRECTABLE_ERROR_SEVERITY {

    struct {
        ULONG Undefined:1;
        ULONG Reserved1:3;
        ULONG DataLinkProtocolError:1;
        ULONG SurpriseDownError:1;
        ULONG Reserved2:6;
        ULONG PoisonedTLP:1;
        ULONG FlowControlProtocolError:1;
        ULONG CompletionTimeout:1;
        ULONG CompleterAbort:1;
        ULONG UnexpectedCompletion:1;
        ULONG ReceiverOverflow:1;
        ULONG MalformedTLP:1;
        ULONG ECRCError:1;
        ULONG UnsupportedRequestError:1;
        ULONG Reserved3:11;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_UNCORRECTABLE_ERROR_SEVERITY, *PPCI_EXPRESS_UNCORRECTABLE_ERROR_SEVERITY;

typedef union _PCI_EXPRESS_CORRECTABLE_ERROR_STATUS {

    struct {
        ULONG ReceiverError:1;
        ULONG Reserved1:5;
        ULONG BadTLP:1;
        ULONG BadDLLP:1;
        ULONG ReplayNumRollover:1;
        ULONG Reserved2:3;
        ULONG ReplayTimerTimeout:1;
        ULONG AdvisoryNonFatalError:1;
        ULONG Reserved3:18;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_CORRECTABLE_ERROR_STATUS, *PPCI_CORRECTABLE_ERROR_STATUS;

typedef union _PCI_EXPRESS_CORRECTABLE_ERROR_MASK {

    struct {
        ULONG ReceiverError:1;
        ULONG Reserved1:5;
        ULONG BadTLP:1;
        ULONG BadDLLP:1;
        ULONG ReplayNumRollover:1;
        ULONG Reserved2:3;
        ULONG ReplayTimerTimeout:1;
        ULONG AdvisoryNonFatalError:1;
        ULONG Reserved3:18;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_CORRECTABLE_ERROR_MASK, *PPCI_CORRECTABLE_ERROR_MASK;

typedef union _PCI_EXPRESS_AER_CAPABILITIES {

    struct {
        ULONG FirstErrorPointer:5;
        ULONG ECRCGenerationCapable:1;
        ULONG ECRCGenerationEnable:1;
        ULONG ECRCCheckCapable:1;
        ULONG ECRCCheckEnable:1;
        ULONG Reserved:23;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_AER_CAPABILITIES, *PPCI_EXPRESS_AER_CAPABILITIES;

typedef union _PCI_EXPRESS_ROOT_ERROR_COMMAND {

    struct {
        ULONG CorrectableErrorReportingEnable:1;
        ULONG NonFatalErrorReportingEnable:1;
        ULONG FatalErrorReportingEnable:1;
        ULONG Reserved:29;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_ROOT_ERROR_COMMAND, *PPCI_EXPRESS_ROOT_ERROR_COMMAND;

typedef union _PCI_EXPRESS_ROOT_ERROR_STATUS {

    struct {
        ULONG CorrectableErrorReceived:1;
        ULONG MultipleCorrectableErrorsReceived:1;
        ULONG UncorrectableErrorReceived:1;
        ULONG MultipleUncorrectableErrorsReceived:1;
        ULONG FirstUncorrectableFatal:1;
        ULONG NonFatalErrorMessagesReceived:1;
        ULONG FatalErrorMessagesReceived:1;
        ULONG Reserved:20;
        ULONG AdvancedErrorInterruptMessageNumber:5;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_ROOT_ERROR_STATUS, *PPCI_EXPRESS_ROOT_ERROR_STATUS;

typedef union _PCI_EXPRESS_ERROR_SOURCE_ID {

    struct {
        USHORT CorrectableSourceIdFun:3;
        USHORT CorrectableSourceIdDev:5;
        USHORT CorrectableSourceIdBus:8;
        USHORT UncorrectableSourceIdFun:3;
        USHORT UncorrectableSourceIdDev:5;
        USHORT UncorrectableSourceIdBus:8;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_ERROR_SOURCE_ID, *PPCI_EXPRESS_ERROR_SOURCE_ID;

typedef union _PCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_STATUS {

    struct {
        ULONG TargetAbortOnSplitCompletion:1;
        ULONG MasterAbortOnSplitCompletion:1;
        ULONG ReceivedTargetAbort:1;
        ULONG ReceivedMasterAbort:1;
        ULONG RsvdZ:1;
        ULONG UnexpectedSplitCompletionError:1;
        ULONG UncorrectableSplitCompletion:1;
        ULONG UncorrectableDataError:1;
        ULONG UncorrectableAttributeError:1;
        ULONG UncorrectableAddressError:1;
        ULONG DelayedTransactionDiscardTimerExpired:1;
        ULONG PERRAsserted:1;
        ULONG SERRAsserted:1;
        ULONG InternalBridgeError:1;
        ULONG Reserved:18;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_STATUS,
  *PPCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_STATUS;

typedef union _PCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_MASK {

    struct {
        ULONG TargetAbortOnSplitCompletion:1;
        ULONG MasterAbortOnSplitCompletion:1;
        ULONG ReceivedTargetAbort:1;
        ULONG ReceivedMasterAbort:1;
        ULONG RsvdZ:1;
        ULONG UnexpectedSplitCompletionError:1;
        ULONG UncorrectableSplitCompletion:1;
        ULONG UncorrectableDataError:1;
        ULONG UncorrectableAttributeError:1;
        ULONG UncorrectableAddressError:1;
        ULONG DelayedTransactionDiscardTimerExpired:1;
        ULONG PERRAsserted:1;
        ULONG SERRAsserted:1;
        ULONG InternalBridgeError:1;
        ULONG Reserved:18;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_MASK,
  *PPCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_MASK;

typedef union _PCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_SEVERITY {

    struct {
        ULONG TargetAbortOnSplitCompletion:1;
        ULONG MasterAbortOnSplitCompletion:1;
        ULONG ReceivedTargetAbort:1;
        ULONG ReceivedMasterAbort:1;
        ULONG RsvdZ:1;
        ULONG UnexpectedSplitCompletionError:1;
        ULONG UncorrectableSplitCompletion:1;
        ULONG UncorrectableDataError:1;
        ULONG UncorrectableAttributeError:1;
        ULONG UncorrectableAddressError:1;
        ULONG DelayedTransactionDiscardTimerExpired:1;
        ULONG PERRAsserted:1;
        ULONG SERRAsserted:1;
        ULONG InternalBridgeError:1;
        ULONG Reserved:18;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_SEVERITY,
  *PPCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_SEVERITY;

typedef union _PCI_EXPRESS_SEC_AER_CAPABILITIES {

    struct {
        ULONG SecondaryUncorrectableFirstErrorPtr:5;
        ULONG Reserved:27;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_SEC_AER_CAPABILITIES, *PPCI_EXPRESS_SEC_AER_CAPABILITIES;

#define ROOT_CMD_ENABLE_CORRECTABLE_ERROR_REPORTING  0x00000001
#define ROOT_CMD_ENABLE_NONFATAL_ERROR_REPORTING     0x00000002
#define ROOT_CMD_ENABLE_FATAL_ERROR_REPORTING        0x00000004

#define ROOT_CMD_ERROR_REPORTING_ENABLE_MASK \
    (ROOT_CMD_ENABLE_FATAL_ERROR_REPORTING | \
     ROOT_CMD_ENABLE_NONFATAL_ERROR_REPORTING | \
     ROOT_CMD_ENABLE_CORRECTABLE_ERROR_REPORTING)

//
// Advanced Error Reporting Capability structure.
//

typedef struct _PCI_EXPRESS_AER_CAPABILITY {

    PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;

    PCI_EXPRESS_UNCORRECTABLE_ERROR_STATUS UncorrectableErrorStatus;
    PCI_EXPRESS_UNCORRECTABLE_ERROR_MASK UncorrectableErrorMask;
    PCI_EXPRESS_UNCORRECTABLE_ERROR_SEVERITY UncorrectableErrorSeverity;
    PCI_EXPRESS_CORRECTABLE_ERROR_STATUS CorrectableErrorStatus;
    PCI_EXPRESS_CORRECTABLE_ERROR_MASK CorrectableErrorMask;
    PCI_EXPRESS_AER_CAPABILITIES CapabilitiesAndControl;
    ULONG HeaderLog[4];
    PCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_STATUS SecUncorrectableErrorStatus;
    PCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_MASK SecUncorrectableErrorMask;
    PCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_SEVERITY SecUncorrectableErrorSeverity;
    PCI_EXPRESS_SEC_AER_CAPABILITIES SecCapabilitiesAndControl;
    ULONG SecHeaderLog[4];

} PCI_EXPRESS_AER_CAPABILITY, *PPCI_EXPRESS_AER_CAPABILITY;

//
// Advanced Error Reporting Capability structure for root port.
//

typedef struct _PCI_EXPRESS_ROOTPORT_AER_CAPABILITY {

    PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;

    PCI_EXPRESS_UNCORRECTABLE_ERROR_STATUS UncorrectableErrorStatus;
    PCI_EXPRESS_UNCORRECTABLE_ERROR_MASK UncorrectableErrorMask;
    PCI_EXPRESS_UNCORRECTABLE_ERROR_SEVERITY UncorrectableErrorSeverity;
    PCI_EXPRESS_CORRECTABLE_ERROR_STATUS CorrectableErrorStatus;
    PCI_EXPRESS_CORRECTABLE_ERROR_MASK CorrectableErrorMask;
    PCI_EXPRESS_AER_CAPABILITIES CapabilitiesAndControl;
    ULONG HeaderLog[4];
    PCI_EXPRESS_ROOT_ERROR_COMMAND RootErrorCommand;
    PCI_EXPRESS_ROOT_ERROR_STATUS RootErrorStatus;
    PCI_EXPRESS_ERROR_SOURCE_ID ErrorSourceId;

} PCI_EXPRESS_ROOTPORT_AER_CAPABILITY, *PPCI_EXPRESS_ROOTPORT_AER_CAPABILITY;

//
// Advanced Error Reporting Capability structure for root port.
//

typedef struct _PCI_EXPRESS_BRIDGE_AER_CAPABILITY {

    PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER Header;

    PCI_EXPRESS_UNCORRECTABLE_ERROR_STATUS UncorrectableErrorStatus;
    PCI_EXPRESS_UNCORRECTABLE_ERROR_MASK UncorrectableErrorMask;
    PCI_EXPRESS_UNCORRECTABLE_ERROR_SEVERITY UncorrectableErrorSeverity;
    PCI_EXPRESS_CORRECTABLE_ERROR_STATUS CorrectableErrorStatus;
    PCI_EXPRESS_CORRECTABLE_ERROR_MASK CorrectableErrorMask;
    PCI_EXPRESS_AER_CAPABILITIES CapabilitiesAndControl;
    ULONG HeaderLog[4];
    PCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_STATUS SecUncorrectableErrorStatus;
    PCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_MASK SecUncorrectableErrorMask;
    PCI_EXPRESS_SEC_UNCORRECTABLE_ERROR_SEVERITY SecUncorrectableErrorSeverity;
    PCI_EXPRESS_SEC_AER_CAPABILITIES SecCapabilitiesAndControl;
    ULONG SecHeaderLog[4];

} PCI_EXPRESS_BRIDGE_AER_CAPABILITY, *PPCI_EXPRESS_BRIDGE_AER_CAPABILITY;

//
// Single-Root I/O Virtualization Capability structure for endpoints
// 

typedef union _PCI_EXPRESS_SRIOV_CAPS {

    struct {
        ULONG VFMigrationCapable:1;
        ULONG Reserved1:20;
        ULONG VFMigrationInterruptNumber:11;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_SRIOV_CAPS, *PPCI_EXPRESS_SRIOV_CAPS;

typedef union _PCI_EXPRESS_SRIOV_CONTROL {

    struct {
        USHORT VFEnable:1;
        USHORT VFMigrationEnable:1;
        USHORT VFMigrationInterruptEnable:1;
        USHORT VFMemorySpaceEnable:1;
        USHORT ARICapableHierarchy:1;
        USHORT Reserved1:11;
    } DUMMYSTRUCTNAME;

    USHORT AsUSHORT;

} PCI_EXPRESS_SRIOV_CONTROL, *PPCI_EXPRESS_SRIOV_CONTROL;

typedef union _PCI_EXPRESS_SRIOV_STATUS {

    struct {
        USHORT VFMigrationStatus:1;
        USHORT Reserved1:15;
    } DUMMYSTRUCTNAME;

    USHORT AsUSHORT;

} PCI_EXPRESS_SRIOV_STATUS, *PPCI_EXPRESS_SRIOV_STATUS;

typedef union _PCI_EXPRESS_SRIOV_MIGRATION_STATE_ARRAY {

    struct {
        ULONG VFMigrationStateBIR:3;
        ULONG VFMigrationStateOffset:29;
    } DUMMYSTRUCTNAME;

    ULONG AsULONG;

} PCI_EXPRESS_SRIOV_MIGRATION_STATE_ARRAY, *PPCI_EXPRESS_SRIOV_MIGRATION_STATE_ARRAY;

typedef struct _PCI_EXPRESS_SRIOV_CAPABILITY {

    PCI_EXPRESS_ENHANCED_CAPABILITY_HEADER  Header;

    PCI_EXPRESS_SRIOV_CAPS                  SRIOVCapabilities;
    PCI_EXPRESS_SRIOV_CONTROL               SRIOVControl;
    PCI_EXPRESS_SRIOV_STATUS                SRIOVStatus;
    USHORT                                  InitialVFs;
    USHORT                                  TotalVFs;
    USHORT                                  NumVFs;
    UCHAR                                   FunctionDependencyLink;
    UCHAR                                   RsvdP1;
    USHORT                                  FirstVFOffset;
    USHORT                                  VFStride;
    USHORT                                  RsvdP2;
    USHORT                                  VFDeviceId;
    ULONG                                   SupportedPageSizes;
    ULONG                                   SystemPageSize;
    ULONG                                   BaseAddresses[PCI_TYPE0_ADDRESSES];
    PCI_EXPRESS_SRIOV_MIGRATION_STATE_ARRAY VFMigrationStateArrayOffset;

} PCI_EXPRESS_SRIOV_CAPABILITY, *PPCI_EXPRESS_SRIOV_CAPABILITY;

//
// Base Class Code encodings for Base Class (from PCI spec rev 2.1).
//

#define PCI_CLASS_PRE_20                    0x00
#define PCI_CLASS_MASS_STORAGE_CTLR         0x01
#define PCI_CLASS_NETWORK_CTLR              0x02
#define PCI_CLASS_DISPLAY_CTLR              0x03
#define PCI_CLASS_MULTIMEDIA_DEV            0x04
#define PCI_CLASS_MEMORY_CTLR               0x05
#define PCI_CLASS_BRIDGE_DEV                0x06
#define PCI_CLASS_SIMPLE_COMMS_CTLR         0x07
#define PCI_CLASS_BASE_SYSTEM_DEV           0x08
#define PCI_CLASS_INPUT_DEV                 0x09
#define PCI_CLASS_DOCKING_STATION           0x0a
#define PCI_CLASS_PROCESSOR                 0x0b
#define PCI_CLASS_SERIAL_BUS_CTLR           0x0c
#define PCI_CLASS_WIRELESS_CTLR             0x0d
#define PCI_CLASS_INTELLIGENT_IO_CTLR       0x0e
#define PCI_CLASS_SATELLITE_COMMS_CTLR      0x0f
#define PCI_CLASS_ENCRYPTION_DECRYPTION     0x10
#define PCI_CLASS_DATA_ACQ_SIGNAL_PROC      0x11

// 0d thru fe reserved

#define PCI_CLASS_NOT_DEFINED               0xff

//
// Sub Class Code encodings (PCI rev 2.1).
//

// Class 00 - PCI_CLASS_PRE_20

#define PCI_SUBCLASS_PRE_20_NON_VGA         0x00
#define PCI_SUBCLASS_PRE_20_VGA             0x01

// Class 01 - PCI_CLASS_MASS_STORAGE_CTLR

#define PCI_SUBCLASS_MSC_SCSI_BUS_CTLR      0x00
#define PCI_SUBCLASS_MSC_IDE_CTLR           0x01
#define PCI_SUBCLASS_MSC_FLOPPY_CTLR        0x02
#define PCI_SUBCLASS_MSC_IPI_CTLR           0x03
#define PCI_SUBCLASS_MSC_RAID_CTLR          0x04
#define PCI_SUBCLASS_MSC_OTHER              0x80

// Class 02 - PCI_CLASS_NETWORK_CTLR

#define PCI_SUBCLASS_NET_ETHERNET_CTLR      0x00
#define PCI_SUBCLASS_NET_TOKEN_RING_CTLR    0x01
#define PCI_SUBCLASS_NET_FDDI_CTLR          0x02
#define PCI_SUBCLASS_NET_ATM_CTLR           0x03
#define PCI_SUBCLASS_NET_ISDN_CTLR          0x04
#define PCI_SUBCLASS_NET_OTHER              0x80

// Class 03 - PCI_CLASS_DISPLAY_CTLR

// N.B. Sub Class 00 could be VGA or 8514 depending on Interface byte

#define PCI_SUBCLASS_VID_VGA_CTLR           0x00
#define PCI_SUBCLASS_VID_XGA_CTLR           0x01
#define PCI_SUBLCASS_VID_3D_CTLR            0x02
#define PCI_SUBCLASS_VID_OTHER              0x80

// Class 04 - PCI_CLASS_MULTIMEDIA_DEV

#define PCI_SUBCLASS_MM_VIDEO_DEV           0x00
#define PCI_SUBCLASS_MM_AUDIO_DEV           0x01
#define PCI_SUBCLASS_MM_TELEPHONY_DEV       0x02
#define PCI_SUBCLASS_MM_OTHER               0x80

// Class 05 - PCI_CLASS_MEMORY_CTLR

#define PCI_SUBCLASS_MEM_RAM                0x00
#define PCI_SUBCLASS_MEM_FLASH              0x01
#define PCI_SUBCLASS_MEM_OTHER              0x80

// Class 06 - PCI_CLASS_BRIDGE_DEV

#define PCI_SUBCLASS_BR_HOST                0x00
#define PCI_SUBCLASS_BR_ISA                 0x01
#define PCI_SUBCLASS_BR_EISA                0x02
#define PCI_SUBCLASS_BR_MCA                 0x03
#define PCI_SUBCLASS_BR_PCI_TO_PCI          0x04
#define PCI_SUBCLASS_BR_PCMCIA              0x05
#define PCI_SUBCLASS_BR_NUBUS               0x06
#define PCI_SUBCLASS_BR_CARDBUS             0x07
#define PCI_SUBCLASS_BR_RACEWAY             0x08
#define PCI_SUBCLASS_BR_OTHER               0x80

// Class 07 - PCI_CLASS_SIMPLE_COMMS_CTLR

// N.B. Sub Class 00 and 01 additional info in Interface byte

#define PCI_SUBCLASS_COM_SERIAL             0x00
#define PCI_SUBCLASS_COM_PARALLEL           0x01
#define PCI_SUBCLASS_COM_MULTIPORT          0x02
#define PCI_SUBCLASS_COM_MODEM              0x03
#define PCI_SUBCLASS_COM_OTHER              0x80

// Class 08 - PCI_CLASS_BASE_SYSTEM_DEV

// N.B. See Interface byte for additional info.

#define PCI_SUBCLASS_SYS_INTERRUPT_CTLR     0x00
#define PCI_SUBCLASS_SYS_DMA_CTLR           0x01
#define PCI_SUBCLASS_SYS_SYSTEM_TIMER       0x02
#define PCI_SUBCLASS_SYS_REAL_TIME_CLOCK    0x03
#define PCI_SUBCLASS_SYS_GEN_HOTPLUG_CTLR   0x04
#define PCI_SUBCLASS_SYS_SDIO_CTRL          0x05
#define PCI_SUBCLASS_SYS_OTHER              0x80

// Class 09 - PCI_CLASS_INPUT_DEV

#define PCI_SUBCLASS_INP_KEYBOARD           0x00
#define PCI_SUBCLASS_INP_DIGITIZER          0x01
#define PCI_SUBCLASS_INP_MOUSE              0x02
#define PCI_SUBCLASS_INP_SCANNER            0x03
#define PCI_SUBCLASS_INP_GAMEPORT           0x04
#define PCI_SUBCLASS_INP_OTHER              0x80

// Class 0a - PCI_CLASS_DOCKING_STATION

#define PCI_SUBCLASS_DOC_GENERIC            0x00
#define PCI_SUBCLASS_DOC_OTHER              0x80

// Class 0b - PCI_CLASS_PROCESSOR

#define PCI_SUBCLASS_PROC_386               0x00
#define PCI_SUBCLASS_PROC_486               0x01
#define PCI_SUBCLASS_PROC_PENTIUM           0x02
#define PCI_SUBCLASS_PROC_ALPHA             0x10
#define PCI_SUBCLASS_PROC_POWERPC           0x20
#define PCI_SUBCLASS_PROC_COPROCESSOR       0x40

// Class 0c - PCI_CLASS_SERIAL_BUS_CTLR

#define PCI_SUBCLASS_SB_IEEE1394            0x00
#define PCI_SUBCLASS_SB_ACCESS              0x01
#define PCI_SUBCLASS_SB_SSA                 0x02
#define PCI_SUBCLASS_SB_USB                 0x03
#define PCI_SUBCLASS_SB_FIBRE_CHANNEL       0x04
#define PCI_SUBCLASS_SB_SMBUS               0x05

// Class 0d - PCI_CLASS_WIRELESS_CTLR

#define PCI_SUBCLASS_WIRELESS_IRDA          0x00
#define PCI_SUBCLASS_WIRELESS_CON_IR        0x01
#define PCI_SUBCLASS_WIRELESS_RF            0x10
#define PCI_SUBCLASS_WIRELESS_OTHER         0x80

// Class 0e - PCI_CLASS_INTELLIGENT_IO_CTLR

#define PCI_SUBCLASS_INTIO_I2O              0x00

// Class 0f - PCI_CLASS_SATELLITE_CTLR

#define PCI_SUBCLASS_SAT_TV                 0x01
#define PCI_SUBCLASS_SAT_AUDIO              0x02
#define PCI_SUBCLASS_SAT_VOICE              0x03
#define PCI_SUBCLASS_SAT_DATA               0x04

// Class 10 - PCI_CLASS_ENCRYPTION_DECRYPTION

#define PCI_SUBCLASS_CRYPTO_NET_COMP        0x00
#define PCI_SUBCLASS_CRYPTO_ENTERTAINMENT   0x10
#define PCI_SUBCLASS_CRYPTO_OTHER           0x80

// Class 11 - PCI_CLASS_DATA_ACQ_SIGNAL_PROC

#define PCI_SUBCLASS_DASP_DPIO              0x00
#define PCI_SUBCLASS_DASP_OTHER             0x80

// end_ntndis

//
// Bit encodes for PCI_COMMON_CONFIG.u.type0.BaseAddresses
//

#define PCI_ADDRESS_IO_SPACE                0x00000001  // (ro)
#define PCI_ADDRESS_MEMORY_TYPE_MASK        0x00000006  // (ro)
#define PCI_ADDRESS_MEMORY_PREFETCHABLE     0x00000008  // (ro)

#define PCI_ADDRESS_IO_ADDRESS_MASK         0xfffffffc
#define PCI_ADDRESS_MEMORY_ADDRESS_MASK     0xfffffff0
#define PCI_ADDRESS_ROM_ADDRESS_MASK        0xfffff800

#define PCI_TYPE_32BIT      0
#define PCI_TYPE_20BIT      2
#define PCI_TYPE_64BIT      4

//
// Bit encodes for PCI_COMMON_CONFIG.u.type0.ROMBaseAddresses
//

#define PCI_ROMADDRESS_ENABLED              0x00000001


//
// Reference notes for PCI configuration fields:
//
// ro   these field are read only.  changes to these fields are ignored
//
// ro+  these field are intended to be read only and should be initialized
//      by the system to their proper values.  However, driver may change
//      these settings.
//
// ---
//
//      All resources comsumed by a PCI device start as unitialized
//      under NT.  An uninitialized memory or I/O base address can be
//      determined by checking it's corrisponding enabled bit in the
//      PCI_COMMON_CONFIG.Command value.  An InterruptLine is unitialized
//      if it contains the value of -1.
//


typedef ULONG NODE_REQUIREMENT;

#define MM_ANY_NODE_OK          0x80000000


//
// Graphics support routines.
//

typedef
VOID
(*PBANKED_SECTION_ROUTINE) (
    __in ULONG ReadBank,
    __in ULONG WriteBank,
    __in PVOID Context
    );

//
// WMI minor function codes under IRP_MJ_SYSTEM_CONTROL
//

#define IRP_MN_QUERY_ALL_DATA               0x00
#define IRP_MN_QUERY_SINGLE_INSTANCE        0x01
#define IRP_MN_CHANGE_SINGLE_INSTANCE       0x02
#define IRP_MN_CHANGE_SINGLE_ITEM           0x03
#define IRP_MN_ENABLE_EVENTS                0x04
#define IRP_MN_DISABLE_EVENTS               0x05
#define IRP_MN_ENABLE_COLLECTION            0x06
#define IRP_MN_DISABLE_COLLECTION           0x07
#define IRP_MN_REGINFO                      0x08
#define IRP_MN_EXECUTE_METHOD               0x09
// Minor code 0x0a is reserved
#define IRP_MN_REGINFO_EX                   0x0b
// Minor code 0x0c is reserved


// workaround overloaded definition (rpc generated headers all define INTERFACE
// to match the class name).
#undef INTERFACE

typedef struct _INTERFACE {
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    // interface specific entries go here
} INTERFACE, *PINTERFACE;


//
// Defines the Type in the RESOURCE_DESCRIPTOR
//
// NOTE:  For all CM_RESOURCE_TYPE values, there must be a
// corresponding ResType value in the 32-bit ConfigMgr headerfile
// (cfgmgr32.h).  Values in the range [0x6,0x80) use the same values
// as their ConfigMgr counterparts.  CM_RESOURCE_TYPE values with
// the high bit set (i.e., in the range [0x80,0xFF]), are
// non-arbitrated resources.  These correspond to the same values
// in cfgmgr32.h that have their high bit set (however, since
// cfgmgr32.h uses 16 bits for ResType values, these values are in
// the range [0x8000,0x807F).  Note that ConfigMgr ResType values
// cannot be in the range [0x8080,0xFFFF), because they would not
// be able to map into CM_RESOURCE_TYPE values.  (0xFFFF itself is
// a special value, because it maps to CmResourceTypeDeviceSpecific.)
//

typedef int CM_RESOURCE_TYPE;

// CmResourceTypeNull is reserved

#define CmResourceTypeNull                0   // ResType_All or ResType_None (0x0000)
#define CmResourceTypePort                1   // ResType_IO (0x0002)
#define CmResourceTypeInterrupt           2   // ResType_IRQ (0x0004)
#define CmResourceTypeMemory              3   // ResType_Mem (0x0001)
#define CmResourceTypeDma                 4   // ResType_DMA (0x0003)
#define CmResourceTypeDeviceSpecific      5   // ResType_ClassSpecific (0xFFFF)
#define CmResourceTypeBusNumber           6   // ResType_BusNumber (0x0006)
#define CmResourceTypeMemoryLarge         7   // ResType_MemLarge (0x0007)
// end_wdm
// begin_ntddk
#define CmResourceTypeMaximum             8
// end_ntddk
// begin_wdm
#define CmResourceTypeNonArbitrated     128   // Not arbitrated if 0x80 bit set
#define CmResourceTypeConfigData        128   // ResType_Reserved (0x8000)
#define CmResourceTypeDevicePrivate     129   // ResType_DevicePrivate (0x8001)
#define CmResourceTypePcCardConfig      130   // ResType_PcCardConfig (0x8002)
#define CmResourceTypeMfCardConfig      131   // ResType_MfCardConfig (0x8003)

//
// Defines the ShareDisposition in the RESOURCE_DESCRIPTOR
//

typedef enum _CM_SHARE_DISPOSITION {
    CmResourceShareUndetermined = 0,    // Reserved
    CmResourceShareDeviceExclusive,
    CmResourceShareDriverExclusive,
    CmResourceShareShared
} CM_SHARE_DISPOSITION;

//
// Define the bit masks for Flags when type is CmResourceTypeInterrupt
//

#define CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE           0
#define CM_RESOURCE_INTERRUPT_LATCHED                   1
#define CM_RESOURCE_INTERRUPT_MESSAGE                   2
#define CM_RESOURCE_INTERRUPT_POLICY_INCLUDED           4
// end_wdm
#define CM_RESOURCE_INTERRUPT_ALLOW_RESERVED_IDT        8
// begin_wdm

//
// A bitmask defining the bits in a resource or requirements descriptor
// flags field that corresponds to the latch mode or a level triggered
// interrupt.
//

#define CM_RESOURCE_INTERRUPT_LEVEL_LATCHED_BITS 0x0001

//
// Define the token value used for an interrupt vector to mean that the vector
// is message signaled.  This value is used in the MaximumVector field.
//

#define CM_RESOURCE_INTERRUPT_MESSAGE_TOKEN   ((ULONG)-2)

//
// Define the bit masks for Flags when type is CmResourceTypeMemory
// or CmResourceTypeMemoryLarge
//

#define CM_RESOURCE_MEMORY_READ_WRITE                       0x0000
#define CM_RESOURCE_MEMORY_READ_ONLY                        0x0001
#define CM_RESOURCE_MEMORY_WRITE_ONLY                       0x0002
#define CM_RESOURCE_MEMORY_WRITEABILITY_MASK                0x0003
#define CM_RESOURCE_MEMORY_PREFETCHABLE                     0x0004

#define CM_RESOURCE_MEMORY_COMBINEDWRITE                    0x0008
#define CM_RESOURCE_MEMORY_24                               0x0010
#define CM_RESOURCE_MEMORY_CACHEABLE                        0x0020
#define CM_RESOURCE_MEMORY_WINDOW_DECODE                    0x0040
#define CM_RESOURCE_MEMORY_BAR                              0x0080

#define CM_RESOURCE_MEMORY_COMPAT_FOR_INACCESSIBLE_RANGE    0x0100

//
// Define the bit masks exclusive to type CmResourceTypeMemoryLarge.
//

#define CM_RESOURCE_MEMORY_LARGE                            0x0E00
#define CM_RESOURCE_MEMORY_LARGE_40                         0x0200
#define CM_RESOURCE_MEMORY_LARGE_48                         0x0400
#define CM_RESOURCE_MEMORY_LARGE_64                         0x0800

//
// Define limits for large memory resources
//

#define CM_RESOURCE_MEMORY_LARGE_40_MAXLEN          0x000000FFFFFFFF00
#define CM_RESOURCE_MEMORY_LARGE_48_MAXLEN          0x0000FFFFFFFF0000
#define CM_RESOURCE_MEMORY_LARGE_64_MAXLEN          0xFFFFFFFF00000000

//
// Define the bit masks for Flags when type is CmResourceTypePort
//

#define CM_RESOURCE_PORT_MEMORY                             0x0000
#define CM_RESOURCE_PORT_IO                                 0x0001
#define CM_RESOURCE_PORT_10_BIT_DECODE                      0x0004
#define CM_RESOURCE_PORT_12_BIT_DECODE                      0x0008
#define CM_RESOURCE_PORT_16_BIT_DECODE                      0x0010
#define CM_RESOURCE_PORT_POSITIVE_DECODE                    0x0020
#define CM_RESOURCE_PORT_PASSIVE_DECODE                     0x0040
#define CM_RESOURCE_PORT_WINDOW_DECODE                      0x0080
#define CM_RESOURCE_PORT_BAR                                0x0100

//
// Define the bit masks for Flags when type is CmResourceTypeDma
//

#define CM_RESOURCE_DMA_8                   0x0000
#define CM_RESOURCE_DMA_16                  0x0001
#define CM_RESOURCE_DMA_32                  0x0002
#define CM_RESOURCE_DMA_8_AND_16            0x0004
#define CM_RESOURCE_DMA_BUS_MASTER          0x0008
#define CM_RESOURCE_DMA_TYPE_A              0x0010
#define CM_RESOURCE_DMA_TYPE_B              0x0020
#define CM_RESOURCE_DMA_TYPE_F              0x0040


#include "pshpack1.h"


//
// Define Mca POS data block for slot
//

typedef struct _CM_MCA_POS_DATA {
    USHORT AdapterId;
    UCHAR PosData1;
    UCHAR PosData2;
    UCHAR PosData3;
    UCHAR PosData4;
} CM_MCA_POS_DATA, *PCM_MCA_POS_DATA;

//
// Memory configuration of eisa data block structure
//

typedef struct _EISA_MEMORY_TYPE {
    UCHAR ReadWrite: 1;
    UCHAR Cached : 1;
    UCHAR Reserved0 :1;
    UCHAR Type:2;
    UCHAR Shared:1;
    UCHAR Reserved1 :1;
    UCHAR MoreEntries : 1;
} EISA_MEMORY_TYPE, *PEISA_MEMORY_TYPE;

typedef struct _EISA_MEMORY_CONFIGURATION {
    EISA_MEMORY_TYPE ConfigurationByte;
    UCHAR DataSize;
    USHORT AddressLowWord;
    UCHAR AddressHighByte;
    USHORT MemorySize;
} EISA_MEMORY_CONFIGURATION, *PEISA_MEMORY_CONFIGURATION;


//
// Interrupt configurationn of eisa data block structure
//

typedef struct _EISA_IRQ_DESCRIPTOR {
    UCHAR Interrupt : 4;
    UCHAR Reserved :1;
    UCHAR LevelTriggered :1;
    UCHAR Shared : 1;
    UCHAR MoreEntries : 1;
} EISA_IRQ_DESCRIPTOR, *PEISA_IRQ_DESCRIPTOR;

typedef struct _EISA_IRQ_CONFIGURATION {
    EISA_IRQ_DESCRIPTOR ConfigurationByte;
    UCHAR Reserved;
} EISA_IRQ_CONFIGURATION, *PEISA_IRQ_CONFIGURATION;


//
// DMA description of eisa data block structure
//

typedef struct _DMA_CONFIGURATION_BYTE0 {
    UCHAR Channel : 3;
    UCHAR Reserved : 3;
    UCHAR Shared :1;
    UCHAR MoreEntries :1;
} DMA_CONFIGURATION_BYTE0;

typedef struct _DMA_CONFIGURATION_BYTE1 {
    UCHAR Reserved0 : 2;
    UCHAR TransferSize : 2;
    UCHAR Timing : 2;
    UCHAR Reserved1 : 2;
} DMA_CONFIGURATION_BYTE1;

typedef struct _EISA_DMA_CONFIGURATION {
    DMA_CONFIGURATION_BYTE0 ConfigurationByte0;
    DMA_CONFIGURATION_BYTE1 ConfigurationByte1;
} EISA_DMA_CONFIGURATION, *PEISA_DMA_CONFIGURATION;


//
// Port description of eisa data block structure
//

typedef struct _EISA_PORT_DESCRIPTOR {
    UCHAR NumberPorts : 5;
    UCHAR Reserved :1;
    UCHAR Shared :1;
    UCHAR MoreEntries : 1;
} EISA_PORT_DESCRIPTOR, *PEISA_PORT_DESCRIPTOR;

typedef struct _EISA_PORT_CONFIGURATION {
    EISA_PORT_DESCRIPTOR Configuration;
    USHORT PortAddress;
} EISA_PORT_CONFIGURATION, *PEISA_PORT_CONFIGURATION;


//
// Eisa slot information definition
// N.B. This structure is different from the one defined
//      in ARC eisa addendum.
//

typedef struct _CM_EISA_SLOT_INFORMATION {
    UCHAR ReturnCode;
    UCHAR ReturnFlags;
    UCHAR MajorRevision;
    UCHAR MinorRevision;
    USHORT Checksum;
    UCHAR NumberFunctions;
    UCHAR FunctionInformation;
    ULONG CompressedId;
} CM_EISA_SLOT_INFORMATION, *PCM_EISA_SLOT_INFORMATION;


//
// Eisa function information definition
//

typedef struct _CM_EISA_FUNCTION_INFORMATION {
    ULONG CompressedId;
    UCHAR IdSlotFlags1;
    UCHAR IdSlotFlags2;
    UCHAR MinorRevision;
    UCHAR MajorRevision;
    UCHAR Selections[26];
    UCHAR FunctionFlags;
    UCHAR TypeString[80];
    EISA_MEMORY_CONFIGURATION EisaMemory[9];
    EISA_IRQ_CONFIGURATION EisaIrq[7];
    EISA_DMA_CONFIGURATION EisaDma[4];
    EISA_PORT_CONFIGURATION EisaPort[20];
    UCHAR InitializationData[60];
} CM_EISA_FUNCTION_INFORMATION, *PCM_EISA_FUNCTION_INFORMATION;

//
// The following defines the way pnp bios information is stored in
// the registry \\HKEY_LOCAL_MACHINE\HARDWARE\Description\System\MultifunctionAdapter\x
// key, where x is an integer number indicating adapter instance. The
// "Identifier" of the key must equal to "PNP BIOS" and the
// "ConfigurationData" is organized as follow:
//
//      CM_PNP_BIOS_INSTALLATION_CHECK        +
//      CM_PNP_BIOS_DEVICE_NODE for device 1  +
//      CM_PNP_BIOS_DEVICE_NODE for device 2  +
//                ...
//      CM_PNP_BIOS_DEVICE_NODE for device n
//

//
// Pnp BIOS device node structure
//

typedef struct _CM_PNP_BIOS_DEVICE_NODE {
    USHORT Size;
    UCHAR Node;
    ULONG ProductId;
    UCHAR DeviceType[3];
    USHORT DeviceAttributes;
    // followed by AllocatedResourceBlock, PossibleResourceBlock
    // and CompatibleDeviceId
} CM_PNP_BIOS_DEVICE_NODE,*PCM_PNP_BIOS_DEVICE_NODE;

//
// Pnp BIOS Installation check
//

typedef struct _CM_PNP_BIOS_INSTALLATION_CHECK {
    UCHAR Signature[4];             // $PnP (ascii)
    UCHAR Revision;
    UCHAR Length;
    USHORT ControlField;
    UCHAR Checksum;
    ULONG EventFlagAddress;         // Physical address
    USHORT RealModeEntryOffset;
    USHORT RealModeEntrySegment;
    USHORT ProtectedModeEntryOffset;
    ULONG ProtectedModeCodeBaseAddress;
    ULONG OemDeviceId;
    USHORT RealModeDataBaseAddress;
    ULONG ProtectedModeDataBaseAddress;
} CM_PNP_BIOS_INSTALLATION_CHECK, *PCM_PNP_BIOS_INSTALLATION_CHECK;

#include "poppack.h"

//
// Masks for EISA function information
//

#define EISA_FUNCTION_ENABLED                   0x80
#define EISA_FREE_FORM_DATA                     0x40
#define EISA_HAS_PORT_INIT_ENTRY                0x20
#define EISA_HAS_PORT_RANGE                     0x10
#define EISA_HAS_DMA_ENTRY                      0x08
#define EISA_HAS_IRQ_ENTRY                      0x04
#define EISA_HAS_MEMORY_ENTRY                   0x02
#define EISA_HAS_TYPE_ENTRY                     0x01
#define EISA_HAS_INFORMATION                    EISA_HAS_PORT_RANGE + \
                                                EISA_HAS_DMA_ENTRY + \
                                                EISA_HAS_IRQ_ENTRY + \
                                                EISA_HAS_MEMORY_ENTRY + \
                                                EISA_HAS_TYPE_ENTRY

//
// Masks for EISA memory configuration
//

#define EISA_MORE_ENTRIES                       0x80
#define EISA_SYSTEM_MEMORY                      0x00
#define EISA_MEMORY_TYPE_RAM                    0x01

//
// Returned error code for EISA bios call
//

#define EISA_INVALID_SLOT                       0x80
#define EISA_INVALID_FUNCTION                   0x81
#define EISA_INVALID_CONFIGURATION              0x82
#define EISA_EMPTY_SLOT                         0x83
#define EISA_INVALID_BIOS_CALL                  0x86


//
// Define the bitmasks for resource options
//

#define IO_RESOURCE_PREFERRED       0x01
#define IO_RESOURCE_DEFAULT         0x02
#define IO_RESOURCE_ALTERNATIVE     0x08

//
// Define interrupt affinity policy values
//

#if defined(NT_PROCESSOR_GROUPS)

typedef USHORT IRQ_DEVICE_POLICY, *PIRQ_DEVICE_POLICY;
typedef enum _IRQ_DEVICE_POLICY_USHORT {
    IrqPolicyMachineDefault = 0,
    IrqPolicyAllCloseProcessors = 1,
    IrqPolicyOneCloseProcessor = 2,
    IrqPolicyAllProcessorsInMachine = 3,
    IrqPolicyAllProcessorsInGroup = 3,
    IrqPolicySpecifiedProcessors = 4,
    IrqPolicySpreadMessagesAcrossAllProcessors = 5
};

#else

typedef enum _IRQ_DEVICE_POLICY {
    IrqPolicyMachineDefault = 0,
    IrqPolicyAllCloseProcessors,
    IrqPolicyOneCloseProcessor,
    IrqPolicyAllProcessorsInMachine,
    IrqPolicySpecifiedProcessors,
    IrqPolicySpreadMessagesAcrossAllProcessors
} IRQ_DEVICE_POLICY, *PIRQ_DEVICE_POLICY;

#endif

//
// Define interrupt priority policy values
//

typedef enum _IRQ_PRIORITY {
    IrqPriorityUndefined = 0,
    IrqPriorityLow,
    IrqPriorityNormal,
    IrqPriorityHigh
} IRQ_PRIORITY, *PIRQ_PRIORITY;

//
// Define interrupt group affinity policy
//

typedef enum _IRQ_GROUP_POLICY {
    GroupAffinityAllGroupZero = 0,
    GroupAffinityDontCare
} IRQ_GROUP_POLICY, *PIRQ_GROUP_POLICY;

//
// This structure defines one type of resource requested by the driver
//

typedef struct _IO_RESOURCE_DESCRIPTOR {
    UCHAR Option;
    UCHAR Type;                         // use CM_RESOURCE_TYPE
    UCHAR ShareDisposition;             // use CM_SHARE_DISPOSITION
    UCHAR Spare1;
    USHORT Flags;                       // use CM resource flag defines
    USHORT Spare2;                      // align

    union {
        struct {
            ULONG Length;
            ULONG Alignment;
            PHYSICAL_ADDRESS MinimumAddress;
            PHYSICAL_ADDRESS MaximumAddress;
        } Port;

        struct {
            ULONG Length;
            ULONG Alignment;
            PHYSICAL_ADDRESS MinimumAddress;
            PHYSICAL_ADDRESS MaximumAddress;
        } Memory;

        struct {
            ULONG MinimumVector;
            ULONG MaximumVector;
#if defined(NT_PROCESSOR_GROUPS)
            IRQ_DEVICE_POLICY AffinityPolicy;
            USHORT Group;
#else
            IRQ_DEVICE_POLICY AffinityPolicy;
#endif
            IRQ_PRIORITY PriorityPolicy;
            KAFFINITY TargetedProcessors;
        } Interrupt;

        struct {
            ULONG MinimumChannel;
            ULONG MaximumChannel;
        } Dma;

        struct {
            ULONG Length;
            ULONG Alignment;
            PHYSICAL_ADDRESS MinimumAddress;
            PHYSICAL_ADDRESS MaximumAddress;
        } Generic;

        struct {
            ULONG Data[3];
        } DevicePrivate;

        //
        // Bus Number information.
        //

        struct {
            ULONG Length;
            ULONG MinBusNumber;
            ULONG MaxBusNumber;
            ULONG Reserved;
        } BusNumber;

        struct {
            ULONG Priority;   // use LCPRI_Xxx values in cfg.h
            ULONG Reserved1;
            ULONG Reserved2;
        } ConfigData;

        //
        // The following structures provide descriptions
        // for memory resource requirement greater than MAXULONG
        //

        struct {
            ULONG Length40;
            ULONG Alignment40;
            PHYSICAL_ADDRESS MinimumAddress;
            PHYSICAL_ADDRESS MaximumAddress;
        } Memory40;

        struct {
            ULONG Length48;
            ULONG Alignment48;
            PHYSICAL_ADDRESS MinimumAddress;
            PHYSICAL_ADDRESS MaximumAddress;
        } Memory48;

        struct {
            ULONG Length64;
            ULONG Alignment64;
            PHYSICAL_ADDRESS MinimumAddress;
            PHYSICAL_ADDRESS MaximumAddress;
        } Memory64;


    } u;

} IO_RESOURCE_DESCRIPTOR, *PIO_RESOURCE_DESCRIPTOR;


#if defined(_MSC_VER)
#if (_MSC_VER >= 1200)
#pragma warning(pop)
#endif
#endif

#endif /* _MINIPORT_ */

