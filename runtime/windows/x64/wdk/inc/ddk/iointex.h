/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    iointex.h

Abstract:

    This header exposes the new kernel APIs to connect and disconnect
    interrupts in a manner that makes it possible for drivers referencing
    these new APIs to run on downlevel systems.

--*/

#ifndef _IOINTEX_IOINTEX_H_
#define _IOINTEX_IOINTEX_H_
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

//
// Supply an overrideable library implementation of IoConnectInterruptEx.
// See DDK documentation for more details on this API.
//

#undef IoConnectInterruptEx
#define IoConnectInterruptEx WdmlibIoConnectInterruptEx

__drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS
WdmlibIoConnectInterruptEx (
    __inout PIO_CONNECT_INTERRUPT_PARAMETERS Parameters
    );

//
// Supply an overrideable library implementation of IoDisconnectInterruptEx.
// See DDK documentation for more details on this API.
//

#undef IoDisconnectInterruptEx
#define IoDisconnectInterruptEx WdmlibIoDisconnectInterruptEx

__drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS
WdmlibIoDisconnectInterruptEx (
    __inout PIO_DISCONNECT_INTERRUPT_PARAMETERS Parameters
    );

//
// Supply an overrideable library implementation of IoGetAffinityInterrupt.
// See DDK documentation for more details on this API.
//

#undef IoGetAffinityInterrupt
#define IoGetAffinityInterrupt WdmlibIoGetAffinityInterrupt

NTSTATUS
WdmlibIoGetAffinityInterrupt (
    __in PKINTERRUPT InterruptObject,
    __out PGROUP_AFFINITY GroupAffinity
    );


#ifdef __cplusplus
} // extern "C"
#endif

#endif // !defined(_IOINTEX_IOINTEX_H_)

