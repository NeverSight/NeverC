/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    csq.h

Abstract:

    This header exposes the cancel safe queue DDIs for use on Win2K at later.
    Drivers that use this header should link to csq.lib. If a driver only needs
    to work on XP or later, neither this header or the lib are required (the
    XP kernel supports the cancel safe queue DDIs natively.)

Revision History:

--*/

// Cancel SAFE DDI set start
//
// The following DDIs are to help ease the pain of writing queue packages that
// handle the cancellation race well. The idea of this set of DDIs is to not
// force a single queue data structure but allow the cancel logic to be hidden
// from the drivers. A driver implements a queue and as part of its header
// includes the IO_CSQ structure. In its initialization routine it calls
// IoInitializeCsq. Then in the dispatch routine when the driver wants to
// insert an IRP into the queue it calls IoCsqInsertIrp. When the driver wants
// to remove something from the queue it calls IoCsqRemoveIrp. Note that Insert
// can fail if the IRP was cancelled in the meantime. Remove can also fail if
// the IRP was already cancelled.
//
// There are typically two modes where drivers queue IRPs. These two modes are
// covered by the cancel safe queue DDI set.
//
// Mode 1:
// One is where the driver queues the IRP and at some later
// point in time dequeues an IRP and issues the IO request.
// For this mode the driver should use IoCsqInsertIrp and IoCsqRemoveNextIrp.
// The driver in this case is expected to pass NULL to the irp context
// parameter in IoInsertIrp.
//
// Mode 2:
// In this the driver queues theIRP, issues the IO request (like issuing a DMA
// request or writing to a register) and when the IO request completes (either
// using a DPC or timer) the driver dequeues the IRP and completes it. For this
// mode the driver should use IoCsqInsertIrp and IoCsqRemoveIrp. In this case
// the driver should allocate an IRP context and pass it in to IoCsqInsertIrp.
// The cancel DDI code creates an association between the IRP and the context
// and thus ensures that when the time comes to remove the IRP it can ascertain
// correctly.
//
// Note that the cancel DDI set assumes that the field DriverContext[3] is
// always available for use and that the driver does not use it.
//

#ifndef _CSQ_H_
#define _CSQ_H_
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

//
// If the wdm.h/ntddk.h we're including already defines cancel safe DDIs, we
// can skip the structure definitions. Otherwise, we do the rest here:
//
#ifndef IO_TYPE_CSQ_IRP_CONTEXT

//
// Bookkeeping structure. This should be opaque to drivers.
// Drivers typically include this as part of their queue headers.
// Given a CSQ pointer the driver should be able to get its
// queue header using CONTAINING_RECORD macro
//

typedef struct _IO_CSQ IO_CSQ, *PIO_CSQ;

#define IO_TYPE_CSQ_IRP_CONTEXT 1
#define IO_TYPE_CSQ             2

//
// IRP context structure. This structure is necessary if the driver is using
// the second mode.
//


typedef struct _IO_CSQ_IRP_CONTEXT {
    ULONG   Type;
    PIRP    Irp;
    PIO_CSQ Csq;
} IO_CSQ_IRP_CONTEXT, *PIO_CSQ_IRP_CONTEXT;

//
// Routines that insert/remove IRP
//

typedef VOID
IO_CSQ_INSERT_IRP (
    __in struct _IO_CSQ    *Csq,
    __in PIRP              Irp
    );

typedef IO_CSQ_INSERT_IRP *PIO_CSQ_INSERT_IRP;

typedef VOID
IO_CSQ_REMOVE_IRP (
    __in PIO_CSQ Csq,
    __in PIRP    Irp
    );

typedef IO_CSQ_REMOVE_IRP *PIO_CSQ_REMOVE_IRP;

//
// Retrieves next entry after Irp from the queue.
// Returns NULL if there are no entries in the queue.
// If Irp is NUL, returns the entry in the head of the queue.
// This routine does not remove the IRP from the queue.
//


typedef PIRP
IO_CSQ_PEEK_NEXT_IRP (
    __in PIO_CSQ Csq,
    __in PIRP    Irp,
    __in PVOID   PeekContext
    );

typedef IO_CSQ_PEEK_NEXT_IRP *PIO_CSQ_PEEK_NEXT_IRP;

//
// Lock routine that protects the cancel safe queue.
//

typedef VOID
IO_CSQ_ACQUIRE_LOCK (
     __in PIO_CSQ Csq,
     __out PKIRQL  Irql
     );

typedef IO_CSQ_ACQUIRE_LOCK *PIO_CSQ_ACQUIRE_LOCK;

typedef VOID
IO_CSQ_RELEASE_LOCK (
     __in PIO_CSQ Csq,
     __in KIRQL   Irql
     );

typedef IO_CSQ_RELEASE_LOCK *PIO_CSQ_RELEASE_LOCK;

//
// Completes the IRP with STATUS_CANCELLED. IRP is guaranteed to be valid
// In most cases this routine just calls IoCompleteRequest(Irp, STATUS_CANCELLED);
//

typedef VOID
IO_CSQ_COMPLETE_CANCELED_IRP (
    __in PIO_CSQ    Csq,
    __in PIRP       Irp
    );

typedef IO_CSQ_COMPLETE_CANCELED_IRP *PIO_CSQ_COMPLETE_CANCELED_IRP;

//
// Bookkeeping structure. This should be opaque to drivers.
// Drivers typically include this as part of their queue headers.
// Given a CSQ pointer the driver should be able to get its
// queue header using CONTAINING_RECORD macro
//

typedef struct _IO_CSQ {
    ULONG                            Type;
    PIO_CSQ_INSERT_IRP               CsqInsertIrp;
    PIO_CSQ_REMOVE_IRP               CsqRemoveIrp;
    PIO_CSQ_PEEK_NEXT_IRP            CsqPeekNextIrp;
    PIO_CSQ_ACQUIRE_LOCK             CsqAcquireLock;
    PIO_CSQ_RELEASE_LOCK             CsqReleaseLock;
    PIO_CSQ_COMPLETE_CANCELED_IRP    CsqCompleteCanceledIrp;
    PVOID                            ReservePointer;    // Future expansion
} IO_CSQ, *PIO_CSQ;

#endif // IO_TYPE_CSQ_IRP_CONTEXT


//
// Add in new extensions to the csq.h library.
//

#ifndef IO_TYPE_CSQ_EX

#define IO_TYPE_CSQ_EX          3

typedef NTSTATUS
IO_CSQ_INSERT_IRP_EX (
    __in struct _IO_CSQ    *Csq,
    __in PIRP              Irp,
    __in PVOID             InsertContext
    );

typedef IO_CSQ_INSERT_IRP_EX *PIO_CSQ_INSERT_IRP_EX;

#endif // IO_TYPE_CSQ_EX


//
// These defines ensure the backward compatible CSQ library can be used within
// the XP build environment in which the kernel supports the functions natively.
//

#define CSQLIB_DDI(x)   Wdmlib##x

//
// Initializes the cancel queue structure.
//

#undef IoCsqInitialize
#define IoCsqInitialize         WdmlibIoCsqInitialize

NTSTATUS
CSQLIB_DDI(IoCsqInitialize)(
    __in PIO_CSQ                          Csq,
    __in PIO_CSQ_INSERT_IRP               CsqInsertIrp,
    __in PIO_CSQ_REMOVE_IRP               CsqRemoveIrp,
    __in PIO_CSQ_PEEK_NEXT_IRP            CsqPeekNextIrp,
    __in PIO_CSQ_ACQUIRE_LOCK             CsqAcquireLock,
    __in PIO_CSQ_RELEASE_LOCK             CsqReleaseLock,
    __in PIO_CSQ_COMPLETE_CANCELED_IRP    CsqCompleteCanceledIrp
    );


#undef IoCsqInitializeEx
#define IoCsqInitializeEx       WdmlibIoCsqInitializeEx

NTSTATUS
CSQLIB_DDI(IoCsqInitializeEx)(
    __in PIO_CSQ                          Csq,
    __in PIO_CSQ_INSERT_IRP_EX            CsqInsertIrp,
    __in PIO_CSQ_REMOVE_IRP               CsqRemoveIrp,
    __in PIO_CSQ_PEEK_NEXT_IRP            CsqPeekNextIrp,
    __in PIO_CSQ_ACQUIRE_LOCK             CsqAcquireLock,
    __in PIO_CSQ_RELEASE_LOCK             CsqReleaseLock,
    __in PIO_CSQ_COMPLETE_CANCELED_IRP    CsqCompleteCanceledIrp
    );


//
// The caller calls this routine to insert the IRP and return STATUS_PENDING.
//

#undef IoCsqInsertIrp
#define IoCsqInsertIrp          WdmlibIoCsqInsertIrp

VOID
CSQLIB_DDI(IoCsqInsertIrp)(
    __in PIO_CSQ                 Csq,
    __in PIRP                    Irp,
    __in_opt PIO_CSQ_IRP_CONTEXT Context
    );


#undef IoCsqInsertIrpEx
#define IoCsqInsertIrpEx        WdmlibIoCsqInsertIrpEx

NTSTATUS
CSQLIB_DDI(IoCsqInsertIrpEx)(
    __in PIO_CSQ                 Csq,
    __in PIRP                    Irp,
    __in_opt PIO_CSQ_IRP_CONTEXT Context,
    __in_opt PVOID               InsertContext
    );

//
// Returns an IRP if one can be found. NULL otherwise.
//

#undef IoCsqRemoveNextIrp
#define IoCsqRemoveNextIrp      WdmlibIoCsqRemoveNextIrp

PIRP
CSQLIB_DDI(IoCsqRemoveNextIrp)(
    __in PIO_CSQ   Csq,
    __in_opt PVOID PeekContext
    );

//
// This routine is called from timeout or DPCs.
// The context is presumably part of the DPC or timer context.
// If succesfull returns the IRP associated with context.
//

#undef IoCsqRemoveIrp
#define IoCsqRemoveIrp          WdmlibIoCsqRemoveIrp

PIRP
CSQLIB_DDI(IoCsqRemoveIrp)(
    __in PIO_CSQ             Csq,
    __in PIO_CSQ_IRP_CONTEXT Context
    );

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _CSQ_H_

// Cancel SAFE DDI set end



