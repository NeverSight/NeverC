/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    usermode_accessors.w

Abstract:

    Function definitions for interacting with user-mode virtual address space.

--*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

//
// Function signatures for internal helper functions.
//

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlCopyFromUser (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) const volatile VOID* Source,
    _In_ SIZE_T Length
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlCopyFromUserNonTemporal (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) const volatile VOID* Source,
    _In_ SIZE_T Length
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlCopyToUser (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) const volatile VOID* Source,
    _In_ SIZE_T Length
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlCopyToUserNonTemporal (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) const volatile VOID* Source,
    _In_ SIZE_T Length
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlCopyToUserFromUser (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) volatile const VOID* Source,
    _In_ SIZE_T Length
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlMoveToUserFromUser (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) volatile const VOID* Source,
    _In_ SIZE_T Length
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlSetUserMemory (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_ UCHAR Fill,
    _In_ SIZE_T Length
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
_Ret_range_(==, *Address)
UCHAR
RtlReadUCharFromUser (
    _In_ const volatile UCHAR* Address
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlWriteUCharToUser (
    _Out_ _Deref_out_range_(==, Value) volatile UCHAR* Address,
    _In_ UCHAR Value
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
_Ret_range_(==, *Address)
USHORT
RtlReadUShortFromUser (
    _In_ const volatile USHORT* Address
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlWriteUShortToUser (
    _Out_ _Deref_out_range_(==, Value) volatile USHORT* Address,
    _In_ USHORT Value
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
_Ret_range_(==, *Address)
ULONG
RtlReadULongFromUser (
    _In_ const volatile ULONG* Address
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlWriteULongToUser (
    _Out_ _Deref_out_range_(==, Value) volatile ULONG* Address,
    _In_ ULONG Value
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
_Ret_range_(==, *Address)
ULONG64
RtlReadULong64FromUser (
    _In_ const volatile ULONG64* Address
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlWriteULong64ToUser (
    _Out_ _Deref_out_range_(==, Value) volatile ULONG64* Address,
    _In_ ULONG64 Value
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
SIZE_T
RtlStringLengthFromUser (
    _In_z_ const volatile char* String
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
SIZE_T
RtlWideStringLengthFromUser (
    _In_z_ const volatile wchar_t* String
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
LONG
RtlInterlockedCompareExchange32ToUser (
    _Inout_ LONG volatile* Destination,
    _In_ LONG ExChange,
    _In_ LONG Comperand
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
LONG64
RtlInterlockedCompareExchange64ToUser (
    _Inout_ LONG64 volatile* Destination,
    _In_ LONG64 ExChange,
    _In_ LONG64 Comperand
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
LONG
RtlInterlockedOr32ToUser (
    _Inout_ LONG volatile* Destination,
    _In_ LONG Value
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
LONG64
RtlInterlockedOr64ToUser (
    _Inout_ LONG64 volatile* Destination,
    _In_ LONG64 Value
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
LONG
RtlInterlockedAnd32ToUser (
    _Inout_ LONG volatile* Destination,
    _In_ LONG Value
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
LONG64
RtlInterlockedAnd64ToUser (
    _Inout_ LONG64 volatile* Destination,
    _In_ LONG64 Value
    );

#if defined(_AMD64_) || defined(_X86_)

#define RtlReadUCharFromUserAcquire RtlReadUCharFromUser
#define RtlWriteUCharToUserRelease RtlWriteUCharToUser
#define RtlReadUShortFromUserAcquire RtlReadUShortFromUser
#define RtlWriteUShortToUserRelease RtlWriteUShortToUser
#define RtlReadULongFromUserAcquire RtlReadULongFromUser
#define RtlWriteULongToUserRelease RtlWriteULongToUser
#define RtlReadULong64FromUserAcquire RtlReadULong64FromUser
#define RtlWriteULong64ToUserRelease RtlWriteULong64ToUser

#elif defined(_ARM64_) || defined(_ARM_)

//
// Acquire/release accessors.
//

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
_Ret_range_(==, *Address)
UCHAR
RtlReadUCharFromUserAcquire (
    _In_ const volatile UCHAR* Address
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlWriteUCharToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile UCHAR* Address,
    _In_ UCHAR Value
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
_Ret_range_(==, *Address)
USHORT
RtlReadUShortFromUserAcquire (
    _In_ const volatile USHORT* Address
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlWriteUShortToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile USHORT* Address,
    _In_ USHORT Value
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
_Ret_range_(==, *Address)
ULONG
RtlReadULongFromUserAcquire (
    _In_ const volatile ULONG* Address
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlWriteULongToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile ULONG* Address,
    _In_ ULONG Value
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
_Ret_range_(==, *Address)
ULONG64
RtlReadULong64FromUserAcquire (
    _In_ const volatile ULONG64* Address
    );

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
VOID
RtlWriteULong64ToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile ULONG64* Address,
    _In_ ULONG64 Value
    );

#else

#error Unsupported architecture

#endif

#ifndef _EXCLUDE_EXTENDED_UMA_FUNCTIONS_

#if defined(_WIN64)

#define RtlReadULongPtrFromUser RtlReadULong64FromUser
#define RtlWriteULongPtrToUser RtlWriteULong64ToUser
#define RtlReadULongPtrFromUserAcquire RtlReadULong64FromUserAcquire
#define RtlWriteULongPtrToUserRelease RtlWriteULong64ToUserRelease

#else // defined(_WIN64)

#define RtlReadULongPtrFromUser RtlReadULongFromUser
#define RtlWriteULongPtrToUser RtlWriteULongToUser
#define RtlReadULongPtrFromUserAcquire RtlReadULongFromUserAcquire
#define RtlWriteULongPtrToUserRelease RtlWriteULongToUserRelease

#endif // defined(_WIN64)

#if DBG

#if defined(_LOCAL_FAST_FAIL_IF_USER_POINTER_)

#define RtlFastFailIfUserPointer _LOCAL_FAST_FAIL_IF_USER_POINTER_

#else

inline
void
RtlFastFailIfUserPointer (
    _In_ const void* Pointer
    )
{
    LOGICAL FastFail = 1;

    if (Pointer == NULL) {
        return;
    }

#if (NTDDI_VERSION >= NTDDI_WIN11_DT)

    FastFail = MmIsUserAddress(Pointer);

#else

    __try {
        ProbeForRead(Pointer, 1, 1);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        FastFail = 0;
    }

#endif // (NTDDI_VERSION >= NTDDI_WIN11_DT)

    if (FastFail) {
        __fastfail(FAST_FAIL_KERNEL_POINTER_EXPECTED);
    }
}

#endif //_LOCAL_FAST_FAIL_IF_USER_POINTER_

#else // DBG

#define RtlFastFailIfUserPointer

#endif // DBG

//
// Function definitions that developers should use.
//

//////////////////////////////////////////////
// AUTOGEN_START (created with GenerateUsermodeAccessors.ps1)
//////////////////////////////////////////////

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteBooleanToUser (
    _Out_ _Deref_out_range_(==, Value) volatile BOOLEAN* Destination,
    _In_ BOOLEAN Value
    )
{
    RtlWriteUCharToUser((volatile UCHAR*)Destination, (UCHAR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
BOOLEAN
ReadBooleanFromUser (
    _In_ const volatile BOOLEAN* Source
    )
{
    return (BOOLEAN)RtlReadUCharFromUser((volatile UCHAR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteBooleanToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile BOOLEAN* Destination,
    _In_ BOOLEAN Value
    )
{
    RtlWriteUCharToUserRelease((volatile UCHAR*)Destination, (UCHAR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
BOOLEAN
ReadBooleanFromUserAcquire (
    _In_ const volatile BOOLEAN* Source
    )
{
    return (BOOLEAN)RtlReadUCharFromUserAcquire((volatile UCHAR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteBooleanToMode (
    _Out_ _Deref_out_range_(==, Value) volatile BOOLEAN* Destination,
    _In_ BOOLEAN Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteBooleanToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteUCharNoFence((volatile UCHAR*)Destination, (UCHAR)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
BOOLEAN
ReadBooleanFromMode (
    _In_ const volatile BOOLEAN* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadBooleanFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (BOOLEAN)ReadUCharNoFence((volatile UCHAR*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUCharToUser (
    _Out_ _Deref_out_range_(==, Value) volatile UCHAR* Destination,
    _In_ UCHAR Value
    )
{
    RtlWriteUCharToUser(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UCHAR
ReadUCharFromUser (
    _In_ const volatile UCHAR* Source
    )
{
    return RtlReadUCharFromUser(Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUCharToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile UCHAR* Destination,
    _In_ UCHAR Value
    )
{
    RtlWriteUCharToUserRelease(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UCHAR
ReadUCharFromUserAcquire (
    _In_ const volatile UCHAR* Source
    )
{
    return RtlReadUCharFromUserAcquire(Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUCharToMode (
    _Out_ _Deref_out_range_(==, Value) volatile UCHAR* Destination,
    _In_ UCHAR Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteUCharToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteUCharNoFence(Destination, Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UCHAR
ReadUCharFromMode (
    _In_ const volatile UCHAR* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadUCharFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return ReadUCharNoFence(Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteCharToUser (
    _Out_ _Deref_out_range_(==, Value) volatile CHAR* Destination,
    _In_ CHAR Value
    )
{
    RtlWriteUCharToUser((volatile UCHAR*)Destination, (UCHAR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
CHAR
ReadCharFromUser (
    _In_ const volatile CHAR* Source
    )
{
    return (CHAR)RtlReadUCharFromUser((volatile UCHAR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteCharToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile CHAR* Destination,
    _In_ CHAR Value
    )
{
    RtlWriteUCharToUserRelease((volatile UCHAR*)Destination, (UCHAR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
CHAR
ReadCharFromUserAcquire (
    _In_ const volatile CHAR* Source
    )
{
    return (CHAR)RtlReadUCharFromUserAcquire((volatile UCHAR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteCharToMode (
    _Out_ _Deref_out_range_(==, Value) volatile CHAR* Destination,
    _In_ CHAR Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteCharToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteUCharNoFence((volatile UCHAR*)Destination, (UCHAR)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
CHAR
ReadCharFromMode (
    _In_ const volatile CHAR* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadCharFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (CHAR)ReadUCharNoFence((volatile UCHAR*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteWCharToUser (
    _Out_ _Deref_out_range_(==, Value) volatile WCHAR* Destination,
    _In_ WCHAR Value
    )
{
    RtlWriteUShortToUser((volatile USHORT*)Destination, (USHORT)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
WCHAR
ReadWCharFromUser (
    _In_ const volatile WCHAR* Source
    )
{
    return (WCHAR)RtlReadUShortFromUser((volatile USHORT*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteWCharToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile WCHAR* Destination,
    _In_ WCHAR Value
    )
{
    RtlWriteUShortToUserRelease((volatile USHORT*)Destination, (USHORT)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
WCHAR
ReadWCharFromUserAcquire (
    _In_ const volatile WCHAR* Source
    )
{
    return (WCHAR)RtlReadUShortFromUserAcquire((volatile USHORT*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteWCharToMode (
    _Out_ _Deref_out_range_(==, Value) volatile WCHAR* Destination,
    _In_ WCHAR Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteWCharToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteUShortNoFence((volatile USHORT*)Destination, (USHORT)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
WCHAR
ReadWCharFromMode (
    _In_ const volatile WCHAR* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadWCharFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (WCHAR)ReadUShortNoFence((volatile USHORT*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUShortToUser (
    _Out_ _Deref_out_range_(==, Value) volatile USHORT* Destination,
    _In_ USHORT Value
    )
{
    RtlWriteUShortToUser(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
USHORT
ReadUShortFromUser (
    _In_ const volatile USHORT* Source
    )
{
    return RtlReadUShortFromUser(Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUShortToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile USHORT* Destination,
    _In_ USHORT Value
    )
{
    RtlWriteUShortToUserRelease(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
USHORT
ReadUShortFromUserAcquire (
    _In_ const volatile USHORT* Source
    )
{
    return RtlReadUShortFromUserAcquire(Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUShortToMode (
    _Out_ _Deref_out_range_(==, Value) volatile USHORT* Destination,
    _In_ USHORT Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteUShortToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteUShortNoFence(Destination, Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
USHORT
ReadUShortFromMode (
    _In_ const volatile USHORT* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadUShortFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return ReadUShortNoFence(Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteShortToUser (
    _Out_ _Deref_out_range_(==, Value) volatile SHORT* Destination,
    _In_ SHORT Value
    )
{
    RtlWriteUShortToUser((volatile USHORT*)Destination, (USHORT)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
SHORT
ReadShortFromUser (
    _In_ const volatile SHORT* Source
    )
{
    return (SHORT)RtlReadUShortFromUser((volatile USHORT*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteShortToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile SHORT* Destination,
    _In_ SHORT Value
    )
{
    RtlWriteUShortToUserRelease((volatile USHORT*)Destination, (USHORT)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
SHORT
ReadShortFromUserAcquire (
    _In_ const volatile SHORT* Source
    )
{
    return (SHORT)RtlReadUShortFromUserAcquire((volatile USHORT*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteShortToMode (
    _Out_ _Deref_out_range_(==, Value) volatile SHORT* Destination,
    _In_ SHORT Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteShortToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteUShortNoFence((volatile USHORT*)Destination, (USHORT)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
SHORT
ReadShortFromMode (
    _In_ const volatile SHORT* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadShortFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (SHORT)ReadUShortNoFence((volatile USHORT*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULongToUser (
    _Out_ _Deref_out_range_(==, Value) volatile ULONG* Destination,
    _In_ ULONG Value
    )
{
    RtlWriteULongToUser(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULONG
ReadULongFromUser (
    _In_ const volatile ULONG* Source
    )
{
    return RtlReadULongFromUser(Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULongToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile ULONG* Destination,
    _In_ ULONG Value
    )
{
    RtlWriteULongToUserRelease(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULONG
ReadULongFromUserAcquire (
    _In_ const volatile ULONG* Source
    )
{
    return RtlReadULongFromUserAcquire(Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULongToMode (
    _Out_ _Deref_out_range_(==, Value) volatile ULONG* Destination,
    _In_ ULONG Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteULongToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULongNoFence(Destination, Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULONG
ReadULongFromMode (
    _In_ const volatile ULONG* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadULongFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return ReadULongNoFence(Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLongToUser (
    _Out_ _Deref_out_range_(==, Value) volatile LONG* Destination,
    _In_ LONG Value
    )
{
    RtlWriteULongToUser((volatile ULONG*)Destination, (ULONG)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LONG
ReadLongFromUser (
    _In_ const volatile LONG* Source
    )
{
    return (LONG)RtlReadULongFromUser((volatile ULONG*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLongToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile LONG* Destination,
    _In_ LONG Value
    )
{
    RtlWriteULongToUserRelease((volatile ULONG*)Destination, (ULONG)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LONG
ReadLongFromUserAcquire (
    _In_ const volatile LONG* Source
    )
{
    return (LONG)RtlReadULongFromUserAcquire((volatile ULONG*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLongToMode (
    _Out_ _Deref_out_range_(==, Value) volatile LONG* Destination,
    _In_ LONG Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteLongToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULongNoFence((volatile ULONG*)Destination, (ULONG)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LONG
ReadLongFromMode (
    _In_ const volatile LONG* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadLongFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (LONG)ReadULongNoFence((volatile ULONG*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULonglongToUser (
    _Out_ _Deref_out_range_(==, Value) volatile ULONGLONG* Destination,
    _In_ ULONGLONG Value
    )
{
    RtlWriteULong64ToUser((volatile ULONG64*)Destination, (ULONG64)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULONGLONG
ReadULonglongFromUser (
    _In_ const volatile ULONGLONG* Source
    )
{
    return (ULONGLONG)RtlReadULong64FromUser((volatile ULONG64*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULonglongToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile ULONGLONG* Destination,
    _In_ ULONGLONG Value
    )
{
    RtlWriteULong64ToUserRelease((volatile ULONG64*)Destination, (ULONG64)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULONGLONG
ReadULonglongFromUserAcquire (
    _In_ const volatile ULONGLONG* Source
    )
{
    return (ULONGLONG)RtlReadULong64FromUserAcquire((volatile ULONG64*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULonglongToMode (
    _Out_ _Deref_out_range_(==, Value) volatile ULONGLONG* Destination,
    _In_ ULONGLONG Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteULonglongToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULong64NoFence((volatile ULONG64*)Destination, (ULONG64)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULONGLONG
ReadULonglongFromMode (
    _In_ const volatile ULONGLONG* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadULonglongFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (ULONGLONG)ReadULong64NoFence((volatile ULONG64*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLonglongToUser (
    _Out_ _Deref_out_range_(==, Value) volatile LONGLONG* Destination,
    _In_ LONGLONG Value
    )
{
    RtlWriteULong64ToUser((volatile ULONG64*)Destination, (ULONG64)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LONGLONG
ReadLonglongFromUser (
    _In_ const volatile LONGLONG* Source
    )
{
    return (LONGLONG)RtlReadULong64FromUser((volatile ULONG64*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLonglongToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile LONGLONG* Destination,
    _In_ LONGLONG Value
    )
{
    RtlWriteULong64ToUserRelease((volatile ULONG64*)Destination, (ULONG64)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LONGLONG
ReadLonglongFromUserAcquire (
    _In_ const volatile LONGLONG* Source
    )
{
    return (LONGLONG)RtlReadULong64FromUserAcquire((volatile ULONG64*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLonglongToMode (
    _Out_ _Deref_out_range_(==, Value) volatile LONGLONG* Destination,
    _In_ LONGLONG Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteLonglongToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULong64NoFence((volatile ULONG64*)Destination, (ULONG64)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LONGLONG
ReadLonglongFromMode (
    _In_ const volatile LONGLONG* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadLonglongFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (LONGLONG)ReadULong64NoFence((volatile ULONG64*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULong64ToUser (
    _Out_ _Deref_out_range_(==, Value) volatile ULONG64* Destination,
    _In_ ULONG64 Value
    )
{
    RtlWriteULong64ToUser(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULONG64
ReadULong64FromUser (
    _In_ const volatile ULONG64* Source
    )
{
    return RtlReadULong64FromUser(Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULong64ToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile ULONG64* Destination,
    _In_ ULONG64 Value
    )
{
    RtlWriteULong64ToUserRelease(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULONG64
ReadULong64FromUserAcquire (
    _In_ const volatile ULONG64* Source
    )
{
    return RtlReadULong64FromUserAcquire(Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULong64ToMode (
    _Out_ _Deref_out_range_(==, Value) volatile ULONG64* Destination,
    _In_ ULONG64 Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteULong64ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULong64NoFence(Destination, Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULONG64
ReadULong64FromMode (
    _In_ const volatile ULONG64* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadULong64FromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return ReadULong64NoFence(Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLong64ToUser (
    _Out_ _Deref_out_range_(==, Value) volatile LONG64* Destination,
    _In_ LONG64 Value
    )
{
    RtlWriteULong64ToUser((volatile ULONG64*)Destination, (ULONG64)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LONG64
ReadLong64FromUser (
    _In_ const volatile LONG64* Source
    )
{
    return (LONG64)RtlReadULong64FromUser((volatile ULONG64*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLong64ToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile LONG64* Destination,
    _In_ LONG64 Value
    )
{
    RtlWriteULong64ToUserRelease((volatile ULONG64*)Destination, (ULONG64)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LONG64
ReadLong64FromUserAcquire (
    _In_ const volatile LONG64* Source
    )
{
    return (LONG64)RtlReadULong64FromUserAcquire((volatile ULONG64*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLong64ToMode (
    _Out_ _Deref_out_range_(==, Value) volatile LONG64* Destination,
    _In_ LONG64 Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteLong64ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULong64NoFence((volatile ULONG64*)Destination, (ULONG64)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LONG64
ReadLong64FromMode (
    _In_ const volatile LONG64* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadLong64FromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (LONG64)ReadULong64NoFence((volatile ULONG64*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULongPtrToUser (
    _Out_ _Deref_out_range_(==, Value) volatile ULONG_PTR* Destination,
    _In_ ULONG_PTR Value
    )
{
    RtlWriteULongPtrToUser(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULONG_PTR
ReadULongPtrFromUser (
    _In_ const volatile ULONG_PTR* Source
    )
{
    return RtlReadULongPtrFromUser(Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULongPtrToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile ULONG_PTR* Destination,
    _In_ ULONG_PTR Value
    )
{
    RtlWriteULongPtrToUserRelease(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULONG_PTR
ReadULongPtrFromUserAcquire (
    _In_ const volatile ULONG_PTR* Source
    )
{
    return RtlReadULongPtrFromUserAcquire(Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULongPtrToMode (
    _Out_ _Deref_out_range_(==, Value) volatile ULONG_PTR* Destination,
    _In_ ULONG_PTR Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteULongPtrToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULongPtrNoFence(Destination, Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULONG_PTR
ReadULongPtrFromMode (
    _In_ const volatile ULONG_PTR* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadULongPtrFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return ReadULongPtrNoFence(Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLongPtrToUser (
    _Out_ _Deref_out_range_(==, Value) volatile LONG_PTR* Destination,
    _In_ LONG_PTR Value
    )
{
    RtlWriteULongPtrToUser((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LONG_PTR
ReadLongPtrFromUser (
    _In_ const volatile LONG_PTR* Source
    )
{
    return (LONG_PTR)RtlReadULongPtrFromUser((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLongPtrToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile LONG_PTR* Destination,
    _In_ LONG_PTR Value
    )
{
    RtlWriteULongPtrToUserRelease((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LONG_PTR
ReadLongPtrFromUserAcquire (
    _In_ const volatile LONG_PTR* Source
    )
{
    return (LONG_PTR)RtlReadULongPtrFromUserAcquire((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLongPtrToMode (
    _Out_ _Deref_out_range_(==, Value) volatile LONG_PTR* Destination,
    _In_ LONG_PTR Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteLongPtrToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULongPtrNoFence((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LONG_PTR
ReadLongPtrFromMode (
    _In_ const volatile LONG_PTR* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadLongPtrFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (LONG_PTR)ReadULongPtrNoFence((volatile ULONG_PTR*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUInt8ToUser (
    _Out_ _Deref_out_range_(==, Value) volatile UINT8* Destination,
    _In_ UINT8 Value
    )
{
    RtlWriteUCharToUser((volatile UCHAR*)Destination, (UCHAR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT8
ReadUInt8FromUser (
    _In_ const volatile UINT8* Source
    )
{
    return (UINT8)RtlReadUCharFromUser((volatile UCHAR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUInt8ToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile UINT8* Destination,
    _In_ UINT8 Value
    )
{
    RtlWriteUCharToUserRelease((volatile UCHAR*)Destination, (UCHAR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT8
ReadUInt8FromUserAcquire (
    _In_ const volatile UINT8* Source
    )
{
    return (UINT8)RtlReadUCharFromUserAcquire((volatile UCHAR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUInt8ToMode (
    _Out_ _Deref_out_range_(==, Value) volatile UINT8* Destination,
    _In_ UINT8 Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteUInt8ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteUCharNoFence((volatile UCHAR*)Destination, (UCHAR)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT8
ReadUInt8FromMode (
    _In_ const volatile UINT8* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadUInt8FromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (UINT8)ReadUCharNoFence((volatile UCHAR*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteInt8ToUser (
    _Out_ _Deref_out_range_(==, Value) volatile INT8* Destination,
    _In_ INT8 Value
    )
{
    RtlWriteUCharToUser((volatile UCHAR*)Destination, (UCHAR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT8
ReadInt8FromUser (
    _In_ const volatile INT8* Source
    )
{
    return (INT8)RtlReadUCharFromUser((volatile UCHAR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteInt8ToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile INT8* Destination,
    _In_ INT8 Value
    )
{
    RtlWriteUCharToUserRelease((volatile UCHAR*)Destination, (UCHAR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT8
ReadInt8FromUserAcquire (
    _In_ const volatile INT8* Source
    )
{
    return (INT8)RtlReadUCharFromUserAcquire((volatile UCHAR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteInt8ToMode (
    _Out_ _Deref_out_range_(==, Value) volatile INT8* Destination,
    _In_ INT8 Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteInt8ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteUCharNoFence((volatile UCHAR*)Destination, (UCHAR)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT8
ReadInt8FromMode (
    _In_ const volatile INT8* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadInt8FromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (INT8)ReadUCharNoFence((volatile UCHAR*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUInt16ToUser (
    _Out_ _Deref_out_range_(==, Value) volatile UINT16* Destination,
    _In_ UINT16 Value
    )
{
    RtlWriteUShortToUser((volatile USHORT*)Destination, (USHORT)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT16
ReadUInt16FromUser (
    _In_ const volatile UINT16* Source
    )
{
    return (UINT16)RtlReadUShortFromUser((volatile USHORT*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUInt16ToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile UINT16* Destination,
    _In_ UINT16 Value
    )
{
    RtlWriteUShortToUserRelease((volatile USHORT*)Destination, (USHORT)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT16
ReadUInt16FromUserAcquire (
    _In_ const volatile UINT16* Source
    )
{
    return (UINT16)RtlReadUShortFromUserAcquire((volatile USHORT*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUInt16ToMode (
    _Out_ _Deref_out_range_(==, Value) volatile UINT16* Destination,
    _In_ UINT16 Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteUInt16ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteUShortNoFence((volatile USHORT*)Destination, (USHORT)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT16
ReadUInt16FromMode (
    _In_ const volatile UINT16* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadUInt16FromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (UINT16)ReadUShortNoFence((volatile USHORT*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteInt16ToUser (
    _Out_ _Deref_out_range_(==, Value) volatile INT16* Destination,
    _In_ INT16 Value
    )
{
    RtlWriteUShortToUser((volatile USHORT*)Destination, (USHORT)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT16
ReadInt16FromUser (
    _In_ const volatile INT16* Source
    )
{
    return (INT16)RtlReadUShortFromUser((volatile USHORT*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteInt16ToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile INT16* Destination,
    _In_ INT16 Value
    )
{
    RtlWriteUShortToUserRelease((volatile USHORT*)Destination, (USHORT)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT16
ReadInt16FromUserAcquire (
    _In_ const volatile INT16* Source
    )
{
    return (INT16)RtlReadUShortFromUserAcquire((volatile USHORT*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteInt16ToMode (
    _Out_ _Deref_out_range_(==, Value) volatile INT16* Destination,
    _In_ INT16 Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteInt16ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteUShortNoFence((volatile USHORT*)Destination, (USHORT)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT16
ReadInt16FromMode (
    _In_ const volatile INT16* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadInt16FromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (INT16)ReadUShortNoFence((volatile USHORT*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUInt32ToUser (
    _Out_ _Deref_out_range_(==, Value) volatile UINT32* Destination,
    _In_ UINT32 Value
    )
{
    RtlWriteULongToUser((volatile ULONG*)Destination, (ULONG)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT32
ReadUInt32FromUser (
    _In_ const volatile UINT32* Source
    )
{
    return (UINT32)RtlReadULongFromUser((volatile ULONG*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUInt32ToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile UINT32* Destination,
    _In_ UINT32 Value
    )
{
    RtlWriteULongToUserRelease((volatile ULONG*)Destination, (ULONG)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT32
ReadUInt32FromUserAcquire (
    _In_ const volatile UINT32* Source
    )
{
    return (UINT32)RtlReadULongFromUserAcquire((volatile ULONG*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUInt32ToMode (
    _Out_ _Deref_out_range_(==, Value) volatile UINT32* Destination,
    _In_ UINT32 Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteUInt32ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULongNoFence((volatile ULONG*)Destination, (ULONG)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT32
ReadUInt32FromMode (
    _In_ const volatile UINT32* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadUInt32FromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (UINT32)ReadULongNoFence((volatile ULONG*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteInt32ToUser (
    _Out_ _Deref_out_range_(==, Value) volatile INT32* Destination,
    _In_ INT32 Value
    )
{
    RtlWriteULongToUser((volatile ULONG*)Destination, (ULONG)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT32
ReadInt32FromUser (
    _In_ const volatile INT32* Source
    )
{
    return (INT32)RtlReadULongFromUser((volatile ULONG*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteInt32ToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile INT32* Destination,
    _In_ INT32 Value
    )
{
    RtlWriteULongToUserRelease((volatile ULONG*)Destination, (ULONG)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT32
ReadInt32FromUserAcquire (
    _In_ const volatile INT32* Source
    )
{
    return (INT32)RtlReadULongFromUserAcquire((volatile ULONG*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteInt32ToMode (
    _Out_ _Deref_out_range_(==, Value) volatile INT32* Destination,
    _In_ INT32 Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteInt32ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULongNoFence((volatile ULONG*)Destination, (ULONG)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT32
ReadInt32FromMode (
    _In_ const volatile INT32* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadInt32FromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (INT32)ReadULongNoFence((volatile ULONG*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUInt64ToUser (
    _Out_ _Deref_out_range_(==, Value) volatile UINT64* Destination,
    _In_ UINT64 Value
    )
{
    RtlWriteULong64ToUser((volatile ULONG64*)Destination, (ULONG64)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT64
ReadUInt64FromUser (
    _In_ const volatile UINT64* Source
    )
{
    return (UINT64)RtlReadULong64FromUser((volatile ULONG64*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUInt64ToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile UINT64* Destination,
    _In_ UINT64 Value
    )
{
    RtlWriteULong64ToUserRelease((volatile ULONG64*)Destination, (ULONG64)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT64
ReadUInt64FromUserAcquire (
    _In_ const volatile UINT64* Source
    )
{
    return (UINT64)RtlReadULong64FromUserAcquire((volatile ULONG64*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUInt64ToMode (
    _Out_ _Deref_out_range_(==, Value) volatile UINT64* Destination,
    _In_ UINT64 Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteUInt64ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULong64NoFence((volatile ULONG64*)Destination, (ULONG64)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT64
ReadUInt64FromMode (
    _In_ const volatile UINT64* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadUInt64FromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (UINT64)ReadULong64NoFence((volatile ULONG64*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteInt64ToUser (
    _Out_ _Deref_out_range_(==, Value) volatile INT64* Destination,
    _In_ INT64 Value
    )
{
    RtlWriteULong64ToUser((volatile ULONG64*)Destination, (ULONG64)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT64
ReadInt64FromUser (
    _In_ const volatile INT64* Source
    )
{
    return (INT64)RtlReadULong64FromUser((volatile ULONG64*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteInt64ToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile INT64* Destination,
    _In_ INT64 Value
    )
{
    RtlWriteULong64ToUserRelease((volatile ULONG64*)Destination, (ULONG64)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT64
ReadInt64FromUserAcquire (
    _In_ const volatile INT64* Source
    )
{
    return (INT64)RtlReadULong64FromUserAcquire((volatile ULONG64*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteInt64ToMode (
    _Out_ _Deref_out_range_(==, Value) volatile INT64* Destination,
    _In_ INT64 Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteInt64ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULong64NoFence((volatile ULONG64*)Destination, (ULONG64)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT64
ReadInt64FromMode (
    _In_ const volatile INT64* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadInt64FromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (INT64)ReadULong64NoFence((volatile ULONG64*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUIntPtrToUser (
    _Out_ _Deref_out_range_(==, Value) volatile UINT_PTR* Destination,
    _In_ UINT_PTR Value
    )
{
    RtlWriteULongPtrToUser((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT_PTR
ReadUIntPtrFromUser (
    _In_ const volatile UINT_PTR* Source
    )
{
    return (UINT_PTR)RtlReadULongPtrFromUser((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUIntPtrToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile UINT_PTR* Destination,
    _In_ UINT_PTR Value
    )
{
    RtlWriteULongPtrToUserRelease((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT_PTR
ReadUIntPtrFromUserAcquire (
    _In_ const volatile UINT_PTR* Source
    )
{
    return (UINT_PTR)RtlReadULongPtrFromUserAcquire((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUIntPtrToMode (
    _Out_ _Deref_out_range_(==, Value) volatile UINT_PTR* Destination,
    _In_ UINT_PTR Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteUIntPtrToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULongPtrNoFence((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UINT_PTR
ReadUIntPtrFromMode (
    _In_ const volatile UINT_PTR* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadUIntPtrFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (UINT_PTR)ReadULongPtrNoFence((volatile ULONG_PTR*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteIntPtrToUser (
    _Out_ _Deref_out_range_(==, Value) volatile INT_PTR* Destination,
    _In_ INT_PTR Value
    )
{
    RtlWriteULongPtrToUser((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT_PTR
ReadIntPtrFromUser (
    _In_ const volatile INT_PTR* Source
    )
{
    return (INT_PTR)RtlReadULongPtrFromUser((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteIntPtrToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile INT_PTR* Destination,
    _In_ INT_PTR Value
    )
{
    RtlWriteULongPtrToUserRelease((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT_PTR
ReadIntPtrFromUserAcquire (
    _In_ const volatile INT_PTR* Source
    )
{
    return (INT_PTR)RtlReadULongPtrFromUserAcquire((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteIntPtrToMode (
    _Out_ _Deref_out_range_(==, Value) volatile INT_PTR* Destination,
    _In_ INT_PTR Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteIntPtrToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULongPtrNoFence((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
INT_PTR
ReadIntPtrFromMode (
    _In_ const volatile INT_PTR* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadIntPtrFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (INT_PTR)ReadULongPtrNoFence((volatile ULONG_PTR*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WritePointerToUser (
    _Outptr_result_maybenull_ _Deref_out_range_(==, Value) volatile PVOID* Destination,
    _In_ PVOID Value
    )
{
    RtlWriteULongPtrToUser((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
PVOID
ReadPointerFromUser (
    _In_ const volatile PVOID* Source
    )
{
    return (PVOID)RtlReadULongPtrFromUser((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WritePointerToUserRelease (
    _Outptr_result_maybenull_ _Deref_out_range_(==, Value) volatile PVOID* Destination,
    _In_ PVOID Value
    )
{
    RtlWriteULongPtrToUserRelease((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
PVOID
ReadPointerFromUserAcquire (
    _In_ const volatile PVOID* Source
    )
{
    return (PVOID)RtlReadULongPtrFromUserAcquire((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WritePointerToMode (
    _Outptr_result_maybenull_ _Deref_out_range_(==, Value) volatile PVOID* Destination,
    _In_ PVOID Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WritePointerToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULongPtrNoFence((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
PVOID
ReadPointerFromMode (
    _In_ const volatile PVOID* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadPointerFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (PVOID)ReadULongPtrNoFence((volatile ULONG_PTR*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteSizeTToUser (
    _Out_ _Deref_out_range_(==, Value) volatile SIZE_T* Destination,
    _In_ SIZE_T Value
    )
{
    RtlWriteULongPtrToUser((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
SIZE_T
ReadSizeTFromUser (
    _In_ const volatile SIZE_T* Source
    )
{
    return (SIZE_T)RtlReadULongPtrFromUser((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteSizeTToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile SIZE_T* Destination,
    _In_ SIZE_T Value
    )
{
    RtlWriteULongPtrToUserRelease((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
SIZE_T
ReadSizeTFromUserAcquire (
    _In_ const volatile SIZE_T* Source
    )
{
    return (SIZE_T)RtlReadULongPtrFromUserAcquire((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteSizeTToMode (
    _Out_ _Deref_out_range_(==, Value) volatile SIZE_T* Destination,
    _In_ SIZE_T Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteSizeTToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULongPtrNoFence((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
SIZE_T
ReadSizeTFromMode (
    _In_ const volatile SIZE_T* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadSizeTFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (SIZE_T)ReadULongPtrNoFence((volatile ULONG_PTR*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteSSizeTToUser (
    _Out_ _Deref_out_range_(==, Value) volatile SSIZE_T* Destination,
    _In_ SSIZE_T Value
    )
{
    RtlWriteULongPtrToUser((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
SSIZE_T
ReadSSizeTFromUser (
    _In_ const volatile SSIZE_T* Source
    )
{
    return (SSIZE_T)RtlReadULongPtrFromUser((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteSSizeTToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile SSIZE_T* Destination,
    _In_ SSIZE_T Value
    )
{
    RtlWriteULongPtrToUserRelease((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
SSIZE_T
ReadSSizeTFromUserAcquire (
    _In_ const volatile SSIZE_T* Source
    )
{
    return (SSIZE_T)RtlReadULongPtrFromUserAcquire((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteSSizeTToMode (
    _Out_ _Deref_out_range_(==, Value) volatile SSIZE_T* Destination,
    _In_ SSIZE_T Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteSSizeTToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULongPtrNoFence((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
SSIZE_T
ReadSSizeTFromMode (
    _In_ const volatile SSIZE_T* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadSSizeTFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (SSIZE_T)ReadULongPtrNoFence((volatile ULONG_PTR*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteHandleToUser (
    _Outptr_result_maybenull_ _Deref_out_range_(==, Value) volatile HANDLE* Destination,
    _In_ HANDLE Value
    )
{
    RtlWriteULongPtrToUser((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
HANDLE
ReadHandleFromUser (
    _In_ const volatile HANDLE* Source
    )
{
    return (HANDLE)RtlReadULongPtrFromUser((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteHandleToUserRelease (
    _Outptr_result_maybenull_ _Deref_out_range_(==, Value) volatile HANDLE* Destination,
    _In_ HANDLE Value
    )
{
    RtlWriteULongPtrToUserRelease((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
HANDLE
ReadHandleFromUserAcquire (
    _In_ const volatile HANDLE* Source
    )
{
    return (HANDLE)RtlReadULongPtrFromUserAcquire((volatile ULONG_PTR*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteHandleToMode (
    _Outptr_result_maybenull_ _Deref_out_range_(==, Value) volatile HANDLE* Destination,
    _In_ HANDLE Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteHandleToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULongPtrNoFence((volatile ULONG_PTR*)Destination, (ULONG_PTR)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
HANDLE
ReadHandleFromMode (
    _In_ const volatile HANDLE* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadHandleFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (HANDLE)ReadULongPtrNoFence((volatile ULONG_PTR*)Source);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteNtStatusToUser (
    _Out_ _Deref_out_range_(==, Value) volatile NTSTATUS* Destination,
    _In_ NTSTATUS Value
    )
{
    RtlWriteULongToUser((volatile ULONG*)Destination, (ULONG)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
NTSTATUS
ReadNtStatusFromUser (
    _In_ const volatile NTSTATUS* Source
    )
{
    return (NTSTATUS)RtlReadULongFromUser((volatile ULONG*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteNtStatusToUserRelease (
    _Out_ _Deref_out_range_(==, Value) volatile NTSTATUS* Destination,
    _In_ NTSTATUS Value
    )
{
    RtlWriteULongToUserRelease((volatile ULONG*)Destination, (ULONG)Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
NTSTATUS
ReadNtStatusFromUserAcquire (
    _In_ const volatile NTSTATUS* Source
    )
{
    return (NTSTATUS)RtlReadULongFromUserAcquire((volatile ULONG*)Source);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteNtStatusToMode (
    _Out_ _Deref_out_range_(==, Value) volatile NTSTATUS* Destination,
    _In_ NTSTATUS Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteNtStatusToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULongNoFence((volatile ULONG*)Destination, (ULONG)Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
NTSTATUS
ReadNtStatusFromMode (
    _In_ const volatile NTSTATUS* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadNtStatusFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);
        return (NTSTATUS)ReadULongNoFence((volatile ULONG*)Source);
    }
}

//////////////////////////////////////////////
// AUTOGEN_END
//////////////////////////////////////////////

//
// Specialized structure accessors (safely deal with padding, weird casting, etc.)
//

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLargeIntegerToUser (
    _Out_ _Deref_out_range_(==, Value) volatile LARGE_INTEGER* Destination,
    _In_ LARGE_INTEGER Value
    )
{
    ULONG64 CastedValue = *(ULONG64*)&Value;
    RtlWriteULong64ToUser((volatile ULONG64*)Destination, CastedValue);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LARGE_INTEGER
ReadLargeIntegerFromUser (
    _In_ const volatile LARGE_INTEGER* Source
    )
{
    ULONG64 Value = RtlReadULong64FromUser((volatile ULONG64*)Source);
    return *(LARGE_INTEGER*)&Value;
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteLargeIntegerToMode (
    _Out_ _Deref_out_range_(==, Value) volatile LARGE_INTEGER* Destination,
    _In_ LARGE_INTEGER Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteLargeIntegerToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULong64NoFence((volatile ULONG64*)&Destination->QuadPart, (ULONG64)Value.QuadPart);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
LARGE_INTEGER
ReadLargeIntegerFromMode (
    _In_ const volatile LARGE_INTEGER* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadLargeIntegerFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);

        LARGE_INTEGER LocalLargeInteger;
        LocalLargeInteger.QuadPart = (LONGLONG)ReadULong64NoFence((volatile ULONG64*)&Source->QuadPart);
        return LocalLargeInteger;
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULargeIntegerToUser (
    _Out_ _Deref_out_range_(==, Value) volatile ULARGE_INTEGER* Destination,
    _In_ ULARGE_INTEGER Value
    )
{
    ULONG64 CastedValue = *(ULONG64*)&Value;
    RtlWriteULong64ToUser((volatile ULONG64*)Destination, CastedValue);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULARGE_INTEGER
ReadULargeIntegerFromUser (
    _In_ const volatile ULARGE_INTEGER* Source
    )
{
    ULONG64 Value = RtlReadULong64FromUser((volatile ULONG64*)Source);
    return *(ULARGE_INTEGER*)&Value;
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteULargeIntegerToMode (
    _Out_ _Deref_out_range_(==, Value) volatile ULARGE_INTEGER* Destination,
    _In_ ULARGE_INTEGER Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteULargeIntegerToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        WriteULong64NoFence((volatile ULONG64*)&Destination->QuadPart, (ULONG64)Value.QuadPart);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
ULARGE_INTEGER
ReadULargeIntegerFromMode (
    _In_ const volatile ULARGE_INTEGER* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadULargeIntegerFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);

        ULARGE_INTEGER LocalLargeInteger;
        LocalLargeInteger.QuadPart = (ULONGLONG)ReadULong64NoFence((volatile ULONG64*)&Source->QuadPart);
        return LocalLargeInteger;
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUnicodeStringToUser (
    _Out_ _Deref_out_range_(==, Value) volatile UNICODE_STRING* Destination,
    _In_ UNICODE_STRING Value
    )
{

    //
    // Copy Length (USHORT) and MaximumLength (USHORT) in a single operation.
    // Don't copy the padding between MaximumLength and Buffer as this could leak
    // kernel memory.
    //

    WriteULongToUser((volatile ULONG*)&Destination->Length, *(ULONG*)&Value.Length);
    WritePointerToUser((volatile PVOID*)&Destination->Buffer, (PWCH)Value.Buffer);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UNICODE_STRING
ReadUnicodeStringFromUser (
    _In_ const volatile UNICODE_STRING* Source
    )
{
    UNICODE_STRING LocalString;

    //
    // Copy Length (USHORT) and MaximumLength (USHORT) in a single operation.
    //

    *(ULONG*)&LocalString.Length = ReadULongFromUser((volatile ULONG*)&Source->Length);
    LocalString.Buffer = (PWCH)ReadPointerFromUser((volatile PVOID*)&Source->Buffer);

    return LocalString;
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteUnicodeStringToMode (
    _Out_ _Deref_out_range_(==, Value) UNICODE_STRING* Destination,
    _In_ UNICODE_STRING Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteUnicodeStringToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);

        //
        // Copy Length (USHORT) and MaximumLength (USHORT) in a single operation.
        //

        WriteULongNoFence((volatile ULONG*)&Destination->Length, *(ULONG*)&Value.Length);
        WritePointerNoFence((volatile PVOID*)&Destination->Buffer, (PVOID)Value.Buffer);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
_Ret_range_(==, *Source)
UNICODE_STRING
ReadUnicodeStringFromMode (
    _In_ const UNICODE_STRING* Source,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return ReadUnicodeStringFromUser(Source);
    } else {
        RtlFastFailIfUserPointer((PVOID)Source);

        UNICODE_STRING LocalString;

        //
        // Copy Length (USHORT) and MaximumLength (USHORT) in a single operation.
        //

        *(ULONG*)&LocalString.Length = ReadULongNoFence((volatile ULONG*)&Source->Length);
        LocalString.Buffer = (PWCH)ReadPointerNoFence((volatile PVOID*)&Source->Buffer);

        return LocalString;
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
SIZE_T
StringLengthFromUser (
    _In_z_ const CHAR* String
    )
{
    return RtlStringLengthFromUser((const volatile char*)String);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
SIZE_T
WideStringLengthFromUser (
    _In_z_ const WCHAR* String
    )
{
    return RtlWideStringLengthFromUser((const volatile wchar_t*)String);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
SIZE_T
StringLengthFromMode (
    _In_z_ volatile const CHAR* String,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return RtlStringLengthFromUser((const volatile char*)String);
    } else {
        //
        // Cast away volatile and use strlen. While this is not "volatile",
        // this API in general is not really volatile-correct because by
        // definition the length of the string can change after being
        // tested. If the string is under data race, the returned string
        // length cannot be trusted and care must be taken to ensure
        // the code is correct if the length changes after being queried.
        //
        return strlen((CHAR*)String);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
SIZE_T
WideStringLengthFromMode (
    _In_z_ volatile const WCHAR* String,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return RtlWideStringLengthFromUser((const volatile wchar_t*)String);
    } else {
        //
        // Cast away volatile and use strlen. While this is not "volatile",
        // this API in general is not really volatile-correct because by
        // definition the length of the string can change after being
        // tested. If the string is under data race, the returned string
        // length cannot be trusted and care must be taken to ensure
        // the code is correct if the length changes after being queried.
        //
        return wcslen((WCHAR*)String);
    }
}

//
// Atomics
//

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
LONG
InterlockedCompareExchangeToUser (
    _Inout_ LONG volatile *Destination,
    _In_ LONG ExChange,
    _In_ LONG Comperand
    )
{
    return RtlInterlockedCompareExchange32ToUser(Destination, ExChange, Comperand);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
LONG
InterlockedCompareExchangeToMode (
    _Inout_ LONG volatile *Destination,
    _In_ LONG ExChange,
    _In_ LONG Comperand,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return RtlInterlockedCompareExchange32ToUser(Destination, ExChange, Comperand);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        return InterlockedCompareExchange(Destination, ExChange, Comperand);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
LONG64
InterlockedCompareExchange64ToUser (
    _Inout_ LONG64 volatile *Destination,
    _In_ LONG64 ExChange,
    _In_ LONG64 Comperand
    )
{
    return RtlInterlockedCompareExchange64ToUser(Destination, ExChange, Comperand);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
LONG64
InterlockedCompareExchange64ToMode (
    _Inout_ LONG64 volatile *Destination,
    _In_ LONG64 ExChange,
    _In_ LONG64 Comperand,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return RtlInterlockedCompareExchange64ToUser(Destination, ExChange, Comperand);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        return InterlockedCompareExchange64(Destination, ExChange, Comperand);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
LONG
InterlockedOrToUser (
    _Inout_ LONG volatile *Destination,
    _In_ LONG Value
    )
{
    return RtlInterlockedOr32ToUser(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
LONG
InterlockedOrToMode (
    _Inout_ LONG volatile *Destination,
    _In_ LONG Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return RtlInterlockedOr32ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        return InterlockedOr(Destination, Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
LONG64
InterlockedOr64ToUser (
    _Inout_ LONG64 volatile *Destination,
    _In_ LONG64 Value
    )
{
    return RtlInterlockedOr64ToUser(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
LONG64
InterlockedOr64ToMode (
    _Inout_ LONG64 volatile *Destination,
    _In_ LONG64 Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return RtlInterlockedOr64ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        return InterlockedOr64(Destination, Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
LONG
InterlockedAndToUser (
    _Inout_ LONG volatile *Destination,
    _In_ LONG Value
    )
{
    return RtlInterlockedAnd32ToUser(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
LONG
InterlockedAndToMode (
    _Inout_ LONG volatile *Destination,
    _In_ LONG Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return RtlInterlockedAnd32ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        return InterlockedAnd(Destination, Value);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
LONG64
InterlockedAnd64ToUser (
    _Inout_ LONG64 volatile *Destination,
    _In_ LONG64 Value
    )
{
    return RtlInterlockedAnd64ToUser(Destination, Value);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
LONG64
InterlockedAnd64ToMode (
    _Inout_ LONG64 volatile *Destination,
    _In_ LONG64 Value,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        return RtlInterlockedAnd64ToUser(Destination, Value);
    } else {
        RtlFastFailIfUserPointer((PVOID)Destination);
        return InterlockedAnd64(Destination, Value);
    }
}

//
// Bulk operations
//

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
CopyToUser (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) const VOID* Source,
    _In_ SIZE_T Length
    )
{
    RtlFastFailIfUserPointer(Source);
    RtlCopyToUser(Destination, Source, Length);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
CopyToUserNonTemporal (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) const VOID* Source,
    _In_ SIZE_T Length
    )
{
    RtlFastFailIfUserPointer(Source);
    RtlCopyToUserNonTemporal(Destination, Source, Length);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
CopyToUserFromUser (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) volatile const VOID* Source,
    _In_ SIZE_T Length
    )
{
    RtlCopyToUserFromUser(Destination, Source, Length);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
MoveToUserFromUser (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) volatile const VOID* Source,
    _In_ SIZE_T Length
    )
{
    //
    // Provides memmove semantics (supports overlapping buffers)
    //

    RtlMoveToUserFromUser(Destination, Source, Length);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
CopyToMode (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) const VOID* Source,
    _In_ SIZE_T Length,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        CopyToUser(Destination, Source, Length);
    } else {
        if (Length != 0) {
            RtlFastFailIfUserPointer((PVOID)Destination);
            RtlFastFailIfUserPointer(Source);
        }
        RtlCopyVolatileMemory(Destination, Source, Length);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
CopyToModeNonTemporal (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) const VOID* Source,
    _In_ SIZE_T Length,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        CopyToUserNonTemporal(Destination, Source, Length);
    } else {
        if (Length != 0) {
            RtlFastFailIfUserPointer((PVOID)Destination);
            RtlFastFailIfUserPointer(Source);
        }
        RtlCopyMemoryNonTemporal((PVOID)Destination, Source, Length);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
CopyFromUser (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) volatile const VOID* Source,
    _In_ SIZE_T Length
    )
{
    RtlFastFailIfUserPointer((PVOID)Destination);
    RtlCopyFromUser(Destination, Source, Length);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
CopyFromUserNonTemporal (
    _Out_writes_bytes_all_(Length) VOID* Destination,
    _In_reads_bytes_(Length) volatile const VOID* Source,
    _In_ SIZE_T Length
    )
{
    RtlFastFailIfUserPointer(Destination);
    RtlCopyFromUserNonTemporal(Destination, Source, Length);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
CopyFromUserToMode (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_reads_bytes_(Length) volatile const VOID* Source,
    _In_ SIZE_T Length,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode == KernelMode) {
        CopyFromUser(Destination, Source, Length);
    } else {
        RtlCopyToUserFromUser(Destination, Source, Length);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
CopyFromMode (
    _Out_writes_bytes_all_(Length) VOID* Destination,
    _In_reads_bytes_(Length) const volatile VOID* Source,
    _In_ SIZE_T Length,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        CopyFromUser(Destination, Source, Length);
    } else {
        if (Length != 0) {
            RtlFastFailIfUserPointer(Destination);
            RtlFastFailIfUserPointer((PVOID)Source);
        }
        RtlCopyVolatileMemory(Destination, Source, Length);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
CopyFromModeNonTemporal (
    _Out_writes_bytes_all_(Length) VOID* Destination,
    _In_reads_bytes_(Length) const volatile VOID* Source,
    _In_ SIZE_T Length,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        CopyFromUserNonTemporal(Destination, Source, Length);
    } else {
        if (Length != 0) {
            RtlFastFailIfUserPointer(Destination);
            RtlFastFailIfUserPointer((PVOID)Source);
        }
        RtlCopyMemoryNonTemporal(Destination, (PVOID)Source, Length);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
FillUserMemory (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_ SIZE_T Length,
    _In_ UCHAR Fill
    )
{
    RtlSetUserMemory(Destination, Fill, Length);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
ZeroUserMemory (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_ SIZE_T Length
    )
{
    RtlSetUserMemory(Destination, 0, Length);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
SetUserMemory (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_ UCHAR Fill,
    _In_ SIZE_T Length
    )
{
    RtlSetUserMemory(Destination, Fill, Length);
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
FillModeMemory (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_ SIZE_T Length,
    _In_ UCHAR Fill,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        RtlSetUserMemory(Destination, Fill, Length);
    } else {
        if (Length != 0) {
            RtlFastFailIfUserPointer((PVOID)Destination);
        }
        RtlFillVolatileMemory(Destination, Length, Fill);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
ZeroModeMemory (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_ SIZE_T Length,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        RtlSetUserMemory(Destination, 0, Length);
    } else {
        if (Length != 0) {
            RtlFastFailIfUserPointer((PVOID)Destination);
        }
        RtlFillVolatileMemory(Destination, Length, 0);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
SetModeMemory (
    _Out_writes_bytes_all_(Length) volatile VOID* Destination,
    _In_ UCHAR Fill,
    _In_ SIZE_T Length,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        RtlSetUserMemory(Destination, Fill, Length);
    } else {
        if (Length != 0) {
            RtlFastFailIfUserPointer((PVOID)Destination);
        }
        RtlFillVolatileMemory(Destination, Length, Fill);
    }
}

//
// Structure helpers.
//

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteStructToUserHelper (
    _Out_writes_bytes_all_(Size) volatile VOID* Destination,
    _In_reads_bytes_(Size) const VOID* Source,
    _In_ SIZE_T Size
    )
{
    NT_ASSERT(Size != 0);
    RtlFastFailIfUserPointer(Source);
    if (Size == 1) {
        WriteUCharToUser((volatile UCHAR*)Destination, *(UCHAR*)Source);
    } else if (Size == 2) {
        WriteUShortToUser((volatile USHORT*)Destination, *(USHORT*)Source);
    } else if (Size == 4) {
        WriteULongToUser((volatile ULONG*)Destination, *(ULONG*)Source);
    } else if (Size == 8) {
        WriteULonglongToUser((volatile ULONGLONG*)Destination, *(ULONGLONG*)Source);
    } else {
        RtlCopyToUser(Destination, Source, Size);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
ReadStructFromUserHelper (
    _Out_writes_bytes_all_(Size) VOID* Destination,
    _In_reads_bytes_(Size) const volatile VOID* Source,
    _In_ SIZE_T Size
    )
{
    NT_ASSERT(Size != 0);
    RtlFastFailIfUserPointer(Destination);
    if (Size == 1) {
        UCHAR Value;
        Value = ReadUCharFromUser((volatile UCHAR*)Source);
        *(UCHAR*)Destination = Value;
    } else if (Size == 2) {
        USHORT Value;
        Value = ReadUShortFromUser((volatile USHORT*)Source);
        *(USHORT*)Destination = Value;
    } else if (Size == 4) {
        ULONG Value;
        Value = ReadULongFromUser((volatile ULONG*)Source);
        *(ULONG*)Destination = Value;
    } else if (Size == 8) {
        ULONGLONG Value;
        Value = ReadULonglongFromUser((volatile ULONGLONG*)Source);
        *(ULONGLONG*)Destination = Value;
    } else {
        RtlCopyFromUser(Destination, Source, Size);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
WriteStructToModeHelper (
    _Out_writes_bytes_all_(Size) volatile VOID* Destination,
    _In_reads_bytes_(Size) const VOID* Source,
    _In_ SIZE_T Size,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        WriteStructToUserHelper(Destination, Source, Size);
    } else {
        NT_ASSERT(Size != 0);
        RtlFastFailIfUserPointer((PVOID)Destination);
        RtlFastFailIfUserPointer(Source);
        RtlCopyVolatileMemory(Destination, Source, Size);
    }
}

_Maybe_raises_SEH_exception_
_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
VOID
ReadStructFromModeHelper (
    _Out_writes_bytes_all_(Size) VOID* Destination,
    _In_reads_bytes_(Size) const volatile VOID* Source,
    _In_ SIZE_T Size,
    _In_ KPROCESSOR_MODE Mode
    )
{
    if (Mode != KernelMode) {
        ReadStructFromUserHelper(Destination, Source, Size);
    } else {
        NT_ASSERT(Size != 0);
        RtlFastFailIfUserPointer(Destination);
        RtlFastFailIfUserPointer((PVOID)Source);
        RtlCopyVolatileMemory(Destination, Source, Size);
    }
}

//
// RtlTypesCompatible validates that two pointers point to the same type without
// using C++ (although C++ could do this better). Because the pointers are dereferenced
// and assigned within a "sizeof" statement they will not actually be executed
// but will be evaluated by the compiler. If they point to different types, the compiler
// will error.
//
// N.B. There's no way to determine that the types of scalars are identical
// (i.e. LONG versus ULONG) but we can assert that their size is the same.
//

#define RtlTypesCompatible(Destination, Source)                                                    \
            do {                                                                                    \
                (sizeof(*(Destination) = *(Source)), (void)0);                                      \
                C_ASSERT(sizeof(*(Destination)) == sizeof(*(Source)));                              \
            } while (0)

//
// The macros below always use the size of the kernel supplied structure to ensure that
// if there is a bug, kernel bounds are not violated. RtlTypesCompatible should ensure
// the size of both types is the same, but we do this anyways to be cautious.
//

#define WriteStructToMode(Destination, Source, Mode)                                                \
            do {                                                                                    \
                RtlTypesCompatible(Destination, Source);                                           \
                C_ASSERT(sizeof(Mode) == sizeof(KPROCESSOR_MODE));                                  \
                WriteStructToModeHelper((Destination), (Source), sizeof(*(Source)), Mode);          \
            } while (0)

#define WriteStructToModeAligned(Destination, Source, Mode, Alignment)                              \
            do {                                                                                    \
                RtlTypesCompatible(Destination, Source);                                           \
                C_ASSERT(sizeof(Mode) == sizeof(KPROCESSOR_MODE));                                  \
                if (Mode != KernelMode) {                                                           \
                    ExProbeAlignment((Destination), sizeof(*(Source)), (Alignment));                \
                }                                                                                   \
                WriteStructToModeHelper((Destination), (Source), sizeof(*(Source)), Mode);          \
            } while (0)

#define ReadStructFromMode(Destination, Source, Mode)                                               \
            do {                                                                                    \
                RtlTypesCompatible(Destination, Source);                                           \
                C_ASSERT(sizeof(Mode) == sizeof(KPROCESSOR_MODE));                                  \
                ReadStructFromModeHelper((Destination), (Source), sizeof(*(Destination)), Mode);    \
            } while (0)

#define ReadStructFromModeAligned(Destination, Source, Mode, Alignment)                             \
            do {                                                                                    \
                RtlTypesCompatible(Destination, Source);                                           \
                C_ASSERT(sizeof(Mode) == sizeof(KPROCESSOR_MODE));                                  \
                if (Mode != KernelMode) {                                                           \
                    ExProbeAlignment((Source), sizeof(*(Destination)), (Alignment));                \
                }                                                                                   \
                ReadStructFromModeHelper((Destination), (Source), sizeof(*(Destination)), Mode);    \
            } while (0)

#define WriteStructToUser(Destination, Source)                                                      \
            do {                                                                                    \
                RtlTypesCompatible(Destination, Source);                                           \
                WriteStructToUserHelper((Destination), (Source), sizeof(*(Source)));                \
            } while (0)

#define WriteStructToUserAligned(Destination, Source, Alignment)                                    \
            do {                                                                                    \
                RtlTypesCompatible(Destination, Source);                                           \
                ExProbeAlignment((Destination), sizeof(*(Source)), (Alignment));                    \
                WriteStructToUserHelper((Destination), (Source), sizeof(*(Source)));                \
            } while (0)

#define ReadStructFromUser(Destination, Source)                                                     \
            do {                                                                                    \
                RtlTypesCompatible(Destination, Source);                                           \
                ReadStructFromUserHelper((Destination), (Source), sizeof(*(Destination)));          \
            } while (0)

#define ReadStructFromUserAligned(Destination, Source, Alignment)                                   \
            do {                                                                                    \
                RtlTypesCompatible(Destination, Source);                                           \
                ExProbeAlignment((Source), sizeof(*(Destination)), (Alignment));                    \
                ReadStructFromUserHelper((Destination), (Source), sizeof(*(Destination)));          \
            } while (0)

#define CopyFromModeAligned(Destination, Source, Length, Mode, Alignment)                           \
            do {                                                                                    \
                if ((Mode) != KernelMode) {                                                         \
                    ExProbeAlignment((Source), (Length), (Alignment));                              \
                }                                                                                   \
                CopyFromMode((Destination), (Source), (Length), (Mode));                            \
            } while (0)

#define CopyFromUserAligned(Destination, Source, Length, Alignment)                                 \
            do {                                                                                    \
                ExProbeAlignment((Source), (Length), (Alignment));                                  \
                CopyFromUser((Destination), (Source), (Length));                                    \
            } while (0)

#define CopyToModeAligned(Destination, Source, Length, Mode, Alignment)                             \
            do {                                                                                    \
                if ((Mode) != KernelMode) {                                                         \
                    ExProbeAlignment((Destination), (Length), (Alignment));                         \
                }                                                                                   \
                CopyToMode((Destination), (Source), (Length), (Mode));                              \
            } while (0)

#define CopyToUserAligned(Destination, Source, Length, Alignment)                                   \
            do {                                                                                    \
                ExProbeAlignment((Destination), (Length), (Alignment));                             \
                CopyToUser((Destination), (Source), (Length));                                      \
            } while (0)

#if __STDC_VERSION__ >= 201112L

//
// Code that requires C11
//

#define ReadFromUser(Source) _Generic((Source), \
    UINT8*: ReadUInt8FromUser, \
    volatile UINT8*: ReadUInt8FromUser, \
    INT8*: ReadInt8FromUser, \
    volatile INT8*: ReadInt8FromUser, \
    UINT16*: ReadUInt16FromUser, \
    volatile UINT16*: ReadUInt16FromUser, \
    INT16*: ReadInt16FromUser, \
    volatile INT16*: ReadInt16FromUser, \
    UINT32*: ReadUInt32FromUser, \
    volatile UINT32*: ReadUInt32FromUser, \
    INT32*: ReadInt32FromUser, \
    volatile INT32*: ReadInt32FromUser, \
    UINT64*: ReadUInt64FromUser, \
    volatile UINT64*: ReadUInt64FromUser, \
    INT64*: ReadInt64FromUser, \
    volatile INT64*: ReadInt64FromUser, \
    PVOID*: ReadPointerFromUser, \
    volatile PVOID*: ReadPointerFromUser, \
    ULONG*: ReadULongFromUser, \
    volatile ULONG*: ReadULongFromUser, \
    LONG*: ReadLongFromUser, \
    volatile LONG*: ReadLongFromUser, \
    LARGE_INTEGER*: ReadLargeIntegerFromUser, \
    volatile LARGE_INTEGER*: ReadLargeIntegerFromUser, \
    ULARGE_INTEGER*: ReadULargeIntegerFromUser, \
    volatile ULARGE_INTEGER*: ReadULargeIntegerFromUser, \
    UNICODE_STRING*: ReadUnicodeStringFromUser, \
    volatile UNICODE_STRING*: ReadUnicodeStringFromUser \
)(Source)

#define WriteToUser(Destination, Value) _Generic((Destination), \
    UINT8*: WriteUInt8ToUser, \
    volatile UINT8*: WriteUInt8ToUser, \
    INT8*: WriteInt8ToUser, \
    volatile INT8*: WriteInt8ToUser, \
    UINT16*: WriteUInt16ToUser, \
    volatile UINT16*: WriteUInt16ToUser, \
    INT16*: WriteInt16ToUser, \
    volatile INT16*: WriteInt16ToUser, \
    UINT32*: WriteUInt32ToUser, \
    volatile UINT32*: WriteUInt32ToUser, \
    INT32*: WriteInt32ToUser, \
    volatile INT32*: WriteInt32ToUser, \
    UINT64*: WriteUInt64ToUser, \
    volatile UINT64*: WriteUInt64ToUser, \
    INT64*: WriteInt64ToUser, \
    volatile INT64*: WriteInt64ToUser, \
    PVOID*: WritePointerToUser, \
    volatile PVOID*: WritePointerToUser, \
    ULONG*: WriteULongToUser, \
    volatile ULONG*: WriteULongToUser, \
    LONG*: WriteLongToUser, \
    volatile LONG*: WriteLongToUser, \
    LARGE_INTEGER*: WriteLargeIntegerToUser, \
    volatile LARGE_INTEGER*: WriteLargeIntegerToUser, \
    ULARGE_INTEGER*: WriteULargeIntegerToUser, \
    volatile ULARGE_INTEGER*: WriteULargeIntegerToUser, \
    UNICODE_STRING*: WriteUnicodeStringToUser, \
    volatile UNICODE_STRING*: WriteUnicodeStringToUser \
)(Destination, Value)

#define ReadFromMode(Source, Mode) _Generic((Source), \
    UINT8*: ReadUInt8FromMode, \
    volatile UINT8*: ReadUInt8FromMode, \
    INT8*: ReadInt8FromMode, \
    volatile INT8*: ReadInt8FromMode, \
    UINT16*: ReadUInt16FromMode, \
    volatile UINT16*: ReadUInt16FromMode, \
    INT16*: ReadInt16FromMode, \
    volatile INT16*: ReadInt16FromMode, \
    UINT32*: ReadUInt32FromMode, \
    volatile UINT32*: ReadUInt32FromMode, \
    INT32*: ReadInt32FromMode, \
    volatile INT32*: ReadInt32FromMode, \
    UINT64*: ReadUInt64FromMode, \
    volatile UINT64*: ReadUInt64FromMode, \
    INT64*: ReadInt64FromMode, \
    volatile INT64*: ReadInt64FromMode, \
    PVOID*: ReadPointerFromMode, \
    volatile PVOID*: ReadPointerFromMode, \
    ULONG*: ReadULongFromMode, \
    volatile ULONG*: ReadULongFromMode, \
    LONG*: ReadLongFromMode, \
    volatile LONG*: ReadLongFromMode, \
    LARGE_INTEGER*: ReadLargeIntegerFromMode, \
    volatile LARGE_INTEGER*: ReadLargeIntegerFromMode, \
    ULARGE_INTEGER*: ReadULargeIntegerFromMode, \
    volatile ULARGE_INTEGER*: ReadULargeIntegerFromMode, \
    UNICODE_STRING*: ReadUnicodeStringFromMode, \
    volatile UNICODE_STRING*: ReadUnicodeStringFromMode \
)(Source, Mode)

#define WriteToMode(Destination, Value, Mode) _Generic((Destination), \
    UINT8*: WriteUInt8ToMode, \
    volatile UINT8*: WriteUInt8ToMode, \
    INT8*: WriteInt8ToMode, \
    volatile INT8*: WriteInt8ToMode, \
    UINT16*: WriteUInt16ToMode, \
    volatile UINT16*: WriteUInt16ToMode, \
    INT16*: WriteInt16ToMode, \
    volatile INT16*: WriteInt16ToMode, \
    UINT32*: WriteUInt32ToMode, \
    volatile UINT32*: WriteUInt32ToMode, \
    INT32*: WriteInt32ToMode, \
    volatile INT32*: WriteInt32ToMode, \
    UINT64*: WriteUInt64ToMode, \
    volatile UINT64*: WriteUInt64ToMode, \
    INT64*: WriteInt64ToMode, \
    volatile INT64*: WriteInt64ToMode, \
    PVOID*: WritePointerToMode, \
    volatile PVOID*: WritePointerToMode, \
    ULONG*: WriteULongToMode, \
    volatile ULONG*: WriteULongToMode, \
    LONG*: WriteLongToMode, \
    volatile LONG*: WriteLongToMode, \
    LARGE_INTEGER*: WriteLargeIntegerToMode, \
    volatile LARGE_INTEGER*: WriteLargeIntegerToMode, \
    ULARGE_INTEGER*: WriteULargeIntegerToMode, \
    volatile ULARGE_INTEGER*: WriteULargeIntegerToMode, \
    UNICODE_STRING*: WriteUnicodeStringToMode, \
    volatile UNICODE_STRING*: WriteUnicodeStringToMode \
)(Destination, Value, Mode)


#endif // __STDC_VERSION__ >= 201112L

_IRQL_requires_max_(APC_LEVEL)
FORCEINLINE
LONG
UmaExceptionFilter (
    _In_ KPROCESSOR_MODE Mode
    )
{
    return (Mode != KernelMode) ? EXCEPTION_EXECUTE_HANDLER :
                                  EXCEPTION_CONTINUE_SEARCH;
}

#endif // _EXCLUDE_EXTENDED_UMA_FUNCTIONS_

#ifdef __cplusplus
}
#endif
