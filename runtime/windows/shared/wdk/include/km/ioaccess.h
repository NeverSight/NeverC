/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    ioaccess.h

Abstract:

    Definitions of function prototypes for accessing I/O ports and
    memory on I/O adapters from display drivers.

    Cloned from parts of nti386.h.

--*/

#ifndef NO_PORT_MACROS

//
// I/O space read and write macros.
//
//  The READ/WRITE_REGISTER_* calls manipulate MEMORY registers.
//  (Use x86 move instructions, with LOCK prefix to force correct behavior
//   w.r.t. caches and write buffers.)
//
//  The READ/WRITE_PORT_* calls manipulate I/O ports.
//  (Use x86 in/out instructions.)
//

#if defined(_X86_)

#define READ_REGISTER_UCHAR(Register)          (*(volatile UCHAR *)(Register))
#define READ_REGISTER_USHORT(Register)         (*(volatile USHORT *)(Register))
#define READ_REGISTER_ULONG(Register)          (*(volatile ULONG *)(Register))
#define WRITE_REGISTER_UCHAR(Register, Value)  (*(volatile UCHAR *)(Register) = (Value))
#define WRITE_REGISTER_USHORT(Register, Value) (*(volatile USHORT *)(Register) = (Value))
#define WRITE_REGISTER_ULONG(Register, Value)  (*(volatile ULONG *)(Register) = (Value))
#define READ_PORT_UCHAR(Port)                  (UCHAR)(inp (Port))
#define READ_PORT_USHORT(Port)                 (USHORT)(inpw (Port))
#define READ_PORT_ULONG(Port)                  (ULONG)(inpd (Port))
#define WRITE_PORT_UCHAR(Port, Value)          outp ((Port), (Value))
#define WRITE_PORT_USHORT(Port, Value)         outpw ((Port), (Value))
#define WRITE_PORT_ULONG(Port, Value)          outpd ((Port), (Value))

#elif defined(_AMD64_)

UCHAR
__inbyte (
    _In_ USHORT Port
    );

USHORT
__inword (
    _In_ USHORT Port
    );

ULONG
__indword (
    _In_ USHORT Port
    );

VOID
__outbyte (
    _In_ USHORT Port,
    _In_ UCHAR Data
    );

VOID
__outword (
    _In_ USHORT Port,
    _In_ USHORT Data
    );

VOID
__outdword (
    _In_ USHORT Port,
    _In_ ULONG Data
    );

#pragma intrinsic(__inbyte)
#pragma intrinsic(__inword)
#pragma intrinsic(__indword)
#pragma intrinsic(__outbyte)
#pragma intrinsic(__outword)
#pragma intrinsic(__outdword)

LONG
_InterlockedOr (
    _Inout_ LONG volatile *Target,
    _In_ LONG Set
    );

#pragma intrinsic(_InterlockedOr)

__inline
UCHAR
READ_REGISTER_UCHAR (
    _In_ PVOID Register
    )
{
    return *(UCHAR volatile *)Register;
}

__inline
USHORT
READ_REGISTER_USHORT (
    _In_ PVOID Register
    )
{
    return *(USHORT volatile *)Register;
}

__inline
ULONG
READ_REGISTER_ULONG (
    _In_ PVOID Register
    )
{
    return *(ULONG volatile *)Register;
}

__inline
VOID
WRITE_REGISTER_UCHAR (
    _In_ PVOID Register,
    _In_ UCHAR Value
    )
{
    LONG Synch;

    *(UCHAR volatile *)Register = Value;
    _InterlockedOr(&Synch, 1);
    return;
}

__inline
VOID
WRITE_REGISTER_USHORT (
    _In_ PVOID Register,
    _In_ USHORT Value
    )
{
    LONG Synch;

    *(USHORT volatile *)Register = Value;
    _InterlockedOr(&Synch, 1);
    return;
}

__inline
VOID
WRITE_REGISTER_ULONG (
    _In_ PVOID Register,
    _In_ ULONG Value
    )
{
    LONG Synch;

    *(ULONG volatile *)Register = Value;
    _InterlockedOr(&Synch, 1);
    return;
}

__inline
UCHAR
READ_PORT_UCHAR (
    _In_ PVOID Port
    )

{
    return __inbyte((USHORT)((ULONG64)Port));
}

__inline
USHORT
READ_PORT_USHORT (
    _In_ PVOID Port
    )

{
    return __inword((USHORT)((ULONG64)Port));
}

__inline
ULONG
READ_PORT_ULONG (
    _In_ PVOID Port
    )

{
    return __indword((USHORT)((ULONG64)Port));
}

__inline
VOID
WRITE_PORT_UCHAR (
    _In_ PVOID Port,
    _In_ UCHAR Value
    )

{
    __outbyte((USHORT)((ULONG64)Port), Value);
    return;
}

__inline
VOID
WRITE_PORT_USHORT (
    _In_ PVOID Port,
    _In_ USHORT Value
    )

{
    __outword((USHORT)((ULONG64)Port), Value);
    return;
}

__inline
VOID
WRITE_PORT_ULONG (
    _In_ PVOID Port,
    _In_ ULONG Value
    )

{
    __outdword((USHORT)((ULONG64)Port), Value);
    return;
}

#endif

#endif // NO_PORT_MACROS
