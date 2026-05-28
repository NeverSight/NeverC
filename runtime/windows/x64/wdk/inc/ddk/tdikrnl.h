/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    tdikrnl.h

Abstract:

    This header file contains interface definitions for NT transport
    providers running in kernel mode.  This interface is documented in the
    NT Transport Driver Interface (TDI) Specification, Version 2.

Revision History:

--*/

#ifndef _TDI_KRNL_
#define _TDI_KRNL_

#pragma once

#include <tdi.h>   // get the user mode includes
#include <netpnp.h>

#pragma warning(push)
#pragma warning(disable:4201) // nameless struct/union

//
// In this TDI, a kernel mode client calls TDI using IoCallDriver with the
// current Irp stack pointer set to 16 bytes of pointers to other structures.
// each of the supported NtDeviceIoControlFile analogs has a somehat different
// structure, laid out below.
//
// The IrpSP information passed by kernel mode clients looks like:
//

typedef struct _TDI_REQUEST_KERNEL {
    ULONG_PTR RequestFlags;
    PTDI_CONNECTION_INFORMATION RequestConnectionInformation;
    PTDI_CONNECTION_INFORMATION ReturnConnectionInformation;
    PVOID RequestSpecific;
} TDI_REQUEST_KERNEL, *PTDI_REQUEST_KERNEL;

//
// defined request codes for the kernel clients. We make these the same
// as the IOCTL codes mostly for convenience; either can be used with
// the same results.
//

#define TDI_ASSOCIATE_ADDRESS    (0x01)
#define TDI_DISASSOCIATE_ADDRESS (0x02)
#define TDI_CONNECT              (0x03)
#define TDI_LISTEN               (0x04)
#define TDI_ACCEPT               (0x05)
#define TDI_DISCONNECT           (0x06)
#define TDI_SEND                 (0x07)
#define TDI_RECEIVE              (0x08)
#define TDI_SEND_DATAGRAM        (0x09)
#define TDI_RECEIVE_DATAGRAM     (0x0A)
#define TDI_SET_EVENT_HANDLER    (0x0B)
#define TDI_QUERY_INFORMATION    (0x0C)
#define TDI_SET_INFORMATION      (0x0D)
#define TDI_ACTION               (0x0E)

#define TDI_DIRECT_SEND          (0x27)
#define TDI_DIRECT_SEND_DATAGRAM (0x29)
#define TDI_DIRECT_ACCEPT        (0x2A)

//
// TdiOpenAddress (Not Used)
// TdiCloseAddress (Not Used)
// TdiOpenConnection (Not Used)
// TdiCloseConnection (Not Used)
//

//
// some useful constants for comparison when determining the file type;
// not required.
//

#define TDI_TRANSPORT_ADDRESS_FILE  1
#define TDI_CONNECTION_FILE 2
#define TDI_CONTROL_CHANNEL_FILE 3

//
// Internal TDI IOCTLS
//

#define IOCTL_TDI_QUERY_DIRECT_SEND_HANDLER     _TDI_CONTROL_CODE( 0x80, METHOD_NEITHER )
#define IOCTL_TDI_QUERY_DIRECT_SENDDG_HANDLER   _TDI_CONTROL_CODE( 0x81, METHOD_NEITHER )

//
// TdiAssociateAddress
//

typedef struct _TDI_REQUEST_KERNEL_ASSOCIATE {
    HANDLE AddressHandle;
} TDI_REQUEST_KERNEL_ASSOCIATE, *PTDI_REQUEST_KERNEL_ASSOCIATE;

//
// TdiDisassociateAddress -- None supplied
//

typedef TDI_REQUEST_KERNEL TDI_REQUEST_KERNEL_DISASSOCIATE,
    *PTDI_REQUEST_KERNEL_DISASSOCIATE;

//
// TdiConnect uses the structure given above (TDI_REQUEST_KERNEL); it's
// defined again below for convenience
//

typedef TDI_REQUEST_KERNEL TDI_REQUEST_KERNEL_CONNECT,
    *PTDI_REQUEST_KERNEL_CONNECT;

//
// TdiDisconnect uses the structure given above (TDI_REQUEST_KERNEL); it's
// defined again below for convenience
//

typedef TDI_REQUEST_KERNEL TDI_REQUEST_KERNEL_DISCONNECT,
    *PTDI_REQUEST_KERNEL_DISCONNECT;

//
// TdiListen uses the structure given above (TDI_REQUEST_KERNEL); it's
// defined again below for convenience
//

typedef TDI_REQUEST_KERNEL TDI_REQUEST_KERNEL_LISTEN,
    *PTDI_REQUEST_KERNEL_LISTEN;

//
// TdiAccept
//

typedef struct _TDI_REQUEST_KERNEL_ACCEPT {
    PTDI_CONNECTION_INFORMATION RequestConnectionInformation;
    PTDI_CONNECTION_INFORMATION ReturnConnectionInformation;
} TDI_REQUEST_KERNEL_ACCEPT, *PTDI_REQUEST_KERNEL_ACCEPT;

//
// TdiSend
//

typedef struct _TDI_REQUEST_KERNEL_SEND {
    ULONG SendLength;
    ULONG SendFlags;
} TDI_REQUEST_KERNEL_SEND, *PTDI_REQUEST_KERNEL_SEND;

//
// TdiReceive
//

typedef struct _TDI_REQUEST_KERNEL_RECEIVE {
    ULONG ReceiveLength;
    ULONG ReceiveFlags;
} TDI_REQUEST_KERNEL_RECEIVE, *PTDI_REQUEST_KERNEL_RECEIVE;

//
// TdiSendDatagram
//

typedef struct _TDI_REQUEST_KERNEL_SENDDG {
    ULONG SendLength;
    __field_bcount(SendLength) PTDI_CONNECTION_INFORMATION SendDatagramInformation;
} TDI_REQUEST_KERNEL_SENDDG, *PTDI_REQUEST_KERNEL_SENDDG;

//
// TdiReceiveDatagram
//

typedef struct _TDI_REQUEST_KERNEL_RECEIVEDG {
    ULONG ReceiveLength;
    PTDI_CONNECTION_INFORMATION ReceiveDatagramInformation;
    PTDI_CONNECTION_INFORMATION ReturnDatagramInformation;
    ULONG ReceiveFlags;
} TDI_REQUEST_KERNEL_RECEIVEDG, *PTDI_REQUEST_KERNEL_RECEIVEDG;

//
// TdiSetEventHandler
//

typedef struct _TDI_REQUEST_KERNEL_SET_EVENT {
    LONG EventType;
    PVOID EventHandler;
    PVOID EventContext;
} TDI_REQUEST_KERNEL_SET_EVENT, *PTDI_REQUEST_KERNEL_SET_EVENT;

//
// TdiQueryInformation
//

typedef struct _TDI_REQUEST_KERNEL_QUERY_INFO {
    LONG QueryType;
    PTDI_CONNECTION_INFORMATION RequestConnectionInformation;
} TDI_REQUEST_KERNEL_QUERY_INFORMATION, *PTDI_REQUEST_KERNEL_QUERY_INFORMATION;

//
// TdiSetInformation
//

typedef struct _TDI_REQUEST_KERNEL_SET_INFO {
    LONG SetType;
    PTDI_CONNECTION_INFORMATION RequestConnectionInformation;
} TDI_REQUEST_KERNEL_SET_INFORMATION, *PTDI_REQUEST_KERNEL_SET_INFORMATION;

//
// Event types that are known
//

#define TDI_EVENT_CONNECT           ((USHORT)0) // TDI_IND_CONNECT event handler.
#define TDI_EVENT_DISCONNECT        ((USHORT)1) // TDI_IND_DISCONNECT event handler.
#define TDI_EVENT_ERROR             ((USHORT)2) // TDI_IND_ERROR event handler.
#define TDI_EVENT_RECEIVE           ((USHORT)3) // TDI_IND_RECEIVE event handler.
#define TDI_EVENT_RECEIVE_DATAGRAM  ((USHORT)4) // TDI_IND_RECEIVE_DATAGRAM event handler.
#define TDI_EVENT_RECEIVE_EXPEDITED ((USHORT)5) // TDI_IND_RECEIVE_EXPEDITED event handler.
#define TDI_EVENT_SEND_POSSIBLE     ((USHORT)6) // TDI_IND_SEND_POSSIBLE event handler
#define TDI_EVENT_CHAINED_RECEIVE   ((USHORT)7) // TDI_IND_CHAINED_RECEIVE event handler.
#define TDI_EVENT_CHAINED_RECEIVE_DATAGRAM  ((USHORT)8) // TDI_IND_CHAINED_RECEIVE_DATAGRAM event handler.
#define TDI_EVENT_CHAINED_RECEIVE_EXPEDITED ((USHORT)9) // TDI_IND_CHAINED_RECEIVE_EXPEDITED event handler.
#define TDI_EVENT_ERROR_EX      ((USHORT)10) // TDI_IND_UNREACH_ERROR event handler.


//
// indicate connection event prototype. This is invoked when a request for
// connection has been received by the provider and the user wishes to either
// accept or reject that request.
//

typedef
NTSTATUS
(*PTDI_IND_CONNECT)(
    __in_opt PVOID TdiEventContext,
    __in LONG RemoteAddressLength,
    __in_bcount(RemoteAddressLength) PVOID RemoteAddress,
    __in LONG UserDataLength,
    __in_bcount_opt(UserDataLength) PVOID UserData,
    __in LONG OptionsLength,
    __in_bcount_opt(OptionsLength) PVOID Options,
    __out CONNECTION_CONTEXT *ConnectionContext,
    __out PIRP *AcceptIrp
    );

NTSTATUS
TdiDefaultConnectHandler (
    __in_opt PVOID TdiEventContext,
    __in LONG RemoteAddressLength,
    __in_bcount(RemoteAddressLength) PVOID RemoteAddress,
    __in LONG UserDataLength,
    __in_bcount_opt(UserDataLength) PVOID UserData,
    __in LONG OptionsLength,
    __in_bcount_opt(OptionsLength) PVOID Options,
    __out CONNECTION_CONTEXT *ConnectionContext,
    __out PIRP *AcceptIrp
    );

//
// Disconnection indication prototype. This is invoked when a connection is
// being disconnected for a reason other than the user requesting it. Note that
// this is a change from TDI V1, which indicated only when the remote caused
// a disconnection. Any non-directed disconnection will cause this indication.
//

typedef
NTSTATUS
(*PTDI_IND_DISCONNECT)(
    __in_opt PVOID TdiEventContext,
    __in_opt CONNECTION_CONTEXT ConnectionContext,
    __in LONG DisconnectDataLength,
    __in_bcount_opt(DisconnectDataLength) PVOID DisconnectData,
    __in LONG DisconnectInformationLength,
    __in_bcount_opt(DisconnectInformationLength) PVOID DisconnectInformation,
    __in ULONG DisconnectFlags
    );

NTSTATUS
TdiDefaultDisconnectHandler (
    __in_opt PVOID TdiEventContext,
    __in_opt CONNECTION_CONTEXT ConnectionContext,
    __in LONG DisconnectDataLength,
    __in_bcount_opt(DisconnectDataLength) PVOID DisconnectData,
    __in LONG DisconnectInformationLength,
    __in_bcount_opt(DisconnectInformationLength) PVOID DisconnectInformation,
    __in ULONG DisconnectFlags
    );

//
// A protocol error has occurred when this indication happens. This indication
// occurs only for errors of the worst type; the address this indication is
// delivered to is no longer usable for protocol-related operations, and
// should not be used for operations henceforth. All connections associated
// it are invalid.
// For NetBIOS-type providers, this indication is also delivered when a name
// in conflict or duplicate name occurs.
//

typedef
NTSTATUS
(*PTDI_IND_ERROR)(
    __in_opt PVOID TdiEventContext,    // the endpoint's file object.
    __in NTSTATUS Status               // status code indicating error type.
    );



typedef
NTSTATUS
(*PTDI_IND_ERROR_EX)(
    __in_opt PVOID TdiEventContext,    // the endpoint's file object.
    __in NTSTATUS Status,              // status code indicating error type.
    __in PVOID Buffer
    );


NTSTATUS
TdiDefaultErrorHandler (
    __in_opt PVOID TdiEventContext,    // the endpoint's file object.
    __in NTSTATUS Status               // status code indicating error type.
    );

//
// TDI_IND_RECEIVE indication handler definition.  This client routine is
// called by the transport provider when a connection-oriented TSDU is received
// that should be presented to the client.
//

typedef
NTSTATUS
(*PTDI_IND_RECEIVE)(
    __in_opt PVOID TdiEventContext,
    __in_opt CONNECTION_CONTEXT ConnectionContext,
    __in ULONG ReceiveFlags,
    __in ULONG BytesIndicated,
    __in ULONG BytesAvailable,
    __out ULONG *BytesTaken,
    __in PVOID Tsdu,                   // pointer describing this TSDU, typically a lump of bytes
    __out_opt PIRP *IoRequestPacket    // TdiReceive IRP if MORE_PROCESSING_REQUIRED.
    );

NTSTATUS
TdiDefaultReceiveHandler (
    __in_opt PVOID TdiEventContext,
    __in_opt CONNECTION_CONTEXT ConnectionContext,
    __in ULONG ReceiveFlags,
    __in ULONG BytesIndicated,
    __in ULONG BytesAvailable,
    __out ULONG *BytesTaken,
    __in PVOID Tsdu,                   // pointer describing this TSDU, typically a lump of bytes
    __out_opt PIRP *IoRequestPacket    // TdiReceive IRP if MORE_PROCESSING_REQUIRED.
    );

//
// TDI_IND_RECEIVE_DATAGRAM indication handler definition.  This client routine
// is called by the transport provider when a connectionless TSDU is received
// that should be presented to the client.
//

typedef
NTSTATUS
(*PTDI_IND_RECEIVE_DATAGRAM)(
    __in_opt PVOID TdiEventContext, // the event context
    __in LONG SourceAddressLength, // length of the originator of the datagram
    __in_bcount(SourceAddressLength) PVOID SourceAddress, // originator of the datagram
    __in LONG OptionsLength, // options for the receive
    __in_bcount_opt(OptionsLength) PVOID Options, //
    __in ULONG ReceiveDatagramFlags, //
    __in ULONG BytesIndicated, // number of bytes this indication
    __in ULONG BytesAvailable, // number of bytes in complete Tsdu
    __out ULONG *BytesTaken, // number of bytes used
    __in PVOID Tsdu, // pointer describing this TSDU, typically a lump of bytes
    __out_opt PIRP *IoRequestPacket // TdiReceive IRP if MORE_PROCESSING_REQUIRED.
    );

NTSTATUS
TdiDefaultRcvDatagramHandler (
    __in_opt PVOID TdiEventContext, // the event context
    __in LONG SourceAddressLength, // length of the originator of the datagram
    __in_bcount(SourceAddressLength) PVOID SourceAddress, // originator of the datagram
    __in LONG OptionsLength, // options for the receive
    __in_bcount_opt(OptionsLength) PVOID Options, //
    __in ULONG ReceiveDatagramFlags, //
    __in ULONG BytesIndicated, // number of bytes this indication
    __in ULONG BytesAvailable, // number of bytes in complete Tsdu
    __out ULONG *BytesTaken, // number of bytes used
    __in PVOID Tsdu, // pointer describing this TSDU, typically a lump of bytes
    __out_opt PIRP *IoRequestPacket // TdiReceive IRP if MORE_PROCESSING_REQUIRED.
    );

//
// This indication is delivered if expedited data is received on the connection.
// This will only occur in providers that support expedited data.
//

typedef
NTSTATUS
(*PTDI_IND_RECEIVE_EXPEDITED)(
    __in_opt PVOID TdiEventContext,
    __in_opt CONNECTION_CONTEXT ConnectionContext,
    __in ULONG ReceiveFlags, //
    __in ULONG BytesIndicated, // number of bytes in this indication
    __in ULONG BytesAvailable, // number of bytes in complete Tsdu
    __out ULONG *BytesTaken, // number of bytes used by indication routine
    __in PVOID Tsdu, // pointer describing this TSDU, typically a lump of bytes
    __out_opt PIRP *IoRequestPacket // TdiReceive IRP if MORE_PROCESSING_REQUIRED.
    );

NTSTATUS
TdiDefaultRcvExpeditedHandler (
    __in_opt PVOID TdiEventContext,
    __in_opt CONNECTION_CONTEXT ConnectionContext,
    __in ULONG ReceiveFlags, //
    __in ULONG BytesIndicated, // number of bytes in this indication
    __in ULONG BytesAvailable, // number of bytes in complete Tsdu
    __out ULONG *BytesTaken, // number of bytes used by indication routine
    __in PVOID Tsdu, // pointer describing this TSDU, typically a lump of bytes
    __out_opt PIRP *IoRequestPacket // TdiReceive IRP if MORE_PROCESSING_REQUIRED.
    );

//
// TDI_IND_CHAINED_RECEIVE indication handler definition.  This client routine
// is called by the transport provider when a connection-oriented TSDU is
// received that should be presented to the client. The TSDU is stored in an
// MDL chain. The client may take ownership of the TSDU and return it at a
// later time.
//

typedef
NTSTATUS
(*PTDI_IND_CHAINED_RECEIVE)(
    __in_opt PVOID TdiEventContext,
    __in_opt CONNECTION_CONTEXT ConnectionContext,
    __in ULONG ReceiveFlags,
    __in ULONG ReceiveLength, // length of client data in TSDU
    __in ULONG StartingOffset, // offset of start of client data in TSDU
    __in PMDL  Tsdu, // TSDU data chain
    __in PVOID TsduDescriptor // for call to TdiReturnChainedReceives
    );

NTSTATUS
TdiDefaultChainedReceiveHandler (
    __in_opt PVOID TdiEventContext,
    __in_opt CONNECTION_CONTEXT ConnectionContext,
    __in ULONG ReceiveFlags,
    __in ULONG ReceiveLength, // length of client data in TSDU
    __in ULONG StartingOffset, // offset of start of client data in TSDU
    __in PMDL  Tsdu, // TSDU data chain
    __in PVOID TsduDescriptor // for call to TdiReturnChainedReceives
    );

//
// TDI_IND_CHAINED_RECEIVE_DATAGRAM indication handler definition.  This client
// routine is called by the transport provider when a connectionless TSDU is
// received that should be presented to the client. The TSDU is stored in an
// MDL chain. The client may take ownership of the TSDU and return it at a
// later time.
//

typedef
NTSTATUS
(*PTDI_IND_CHAINED_RECEIVE_DATAGRAM)(
    __in_opt PVOID TdiEventContext, // the event context
    __in LONG SourceAddressLength, // length of the originator of the datagram
    __in_bcount(SourceAddressLength) PVOID SourceAddress, // originator of the datagram
    __in LONG OptionsLength, // options for the receive
    __in_bcount_opt(OptionsLength) PVOID Options, //
    __in ULONG ReceiveDatagramFlags,  //
    __in ULONG ReceiveDatagramLength, // length of client data in TSDU
    __in ULONG StartingOffset, // offset of start of client data in TSDU
    __in PMDL  Tsdu, // TSDU data chain
    __in PVOID TsduDescriptor // for call to TdiReturnChainedReceives
    );

NTSTATUS
TdiDefaultChainedRcvDatagramHandler (
    __in_opt PVOID TdiEventContext, // the event context
    __in LONG SourceAddressLength, // length of the originator of the datagram
    __in_bcount(SourceAddressLength) PVOID SourceAddress, // originator of the datagram
    __in LONG OptionsLength, // options for the receive
    __in_bcount_opt(OptionsLength) PVOID Options, //
    __in ULONG ReceiveDatagramFlags,  //
    __in ULONG ReceiveDatagramLength, // length of client data in TSDU
    __in ULONG StartingOffset, // offset of start of client data in TSDU
    __in PMDL  Tsdu, // TSDU data chain
    __in PVOID TsduDescriptor // for call to TdiReturnChainedReceives
    );

//
// This indication is delivered if expedited data is received on the connection.
// This will only occur in providers that support expedited data. The TSDU is
// stored in an MDL chain. The client may take ownership of the TSDU and
// return it at a later time.
//

typedef
NTSTATUS
(*PTDI_IND_CHAINED_RECEIVE_EXPEDITED)(
    __in_opt PVOID TdiEventContext,
    __in_opt CONNECTION_CONTEXT ConnectionContext,
    __in ULONG ReceiveFlags,
    __in ULONG ReceiveLength, // length of client data in TSDU
    __in ULONG StartingOffset, // offset of start of client data in TSDU
    __in PMDL  Tsdu, // TSDU data chain
    __in PVOID TsduDescriptor// for call to TdiReturnChainedReceives
    );

NTSTATUS
TdiDefaultChainedRcvExpeditedHandler (
    __in_opt PVOID TdiEventContext,
    __in_opt CONNECTION_CONTEXT ConnectionContext,
    __in ULONG ReceiveFlags,
    __in ULONG ReceiveLength, // length of client data in TSDU
    __in ULONG StartingOffset, // offset of start of client data in TSDU
    __in PMDL  Tsdu, // TSDU data chain
    __in PVOID TsduDescriptor// for call to TdiReturnChainedReceives
    );

//
// This indication is delivered if there is room for a send in the buffer of
// a buffering protocol.
//

typedef
NTSTATUS
(*PTDI_IND_SEND_POSSIBLE)(
    __in_opt PVOID TdiEventContext,
    __in_opt PVOID ConnectionContext,
    __in ULONG BytesAvailable);

NTSTATUS
TdiDefaultSendPossibleHandler (
    __in_opt PVOID TdiEventContext,
    __in_opt PVOID ConnectionContext,
    __in ULONG BytesAvailable);

//
// defined MACROS to allow the kernel mode client to easily build an IRP for
// any function.
//

#define TdiBuildAssociateAddress(Irp, DevObj, FileObj, CompRoutine, Contxt, AddrHandle)                           \
    {                                                                        \
        PTDI_REQUEST_KERNEL_ASSOCIATE p;                                     \
        PIO_STACK_LOCATION _IRPSP;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_ASSOCIATE_ADDRESS;                       \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL_ASSOCIATE)&_IRPSP->Parameters;              \
        p->AddressHandle = (HANDLE)(AddrHandle);                             \
    }

#define TdiBuildDisassociateAddress(Irp, DevObj, FileObj, CompRoutine, Contxt)                                    \
    {                                                                        \
        PTDI_REQUEST_KERNEL_DISASSOCIATE p;                                  \
        PIO_STACK_LOCATION _IRPSP;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_DISASSOCIATE_ADDRESS;                    \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL_DISASSOCIATE)&_IRPSP->Parameters;           \
    }

#define TdiBuildConnect(Irp, DevObj, FileObj, CompRoutine, Contxt, Time, RequestConnectionInfo, ReturnConnectionInfo)\
    {                                                                        \
        PTDI_REQUEST_KERNEL p;                                               \
        PIO_STACK_LOCATION _IRPSP;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_CONNECT;                                 \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL)&_IRPSP->Parameters;                        \
        p->RequestConnectionInformation = RequestConnectionInfo;             \
        p->ReturnConnectionInformation = ReturnConnectionInfo;               \
        p->RequestSpecific = (PVOID)Time;                                    \
    }

#define TdiBuildListen(Irp, DevObj, FileObj, CompRoutine, Contxt, Flags, RequestConnectionInfo, ReturnConnectionInfo)\
    {                                                                        \
        PTDI_REQUEST_KERNEL p;                                               \
        PIO_STACK_LOCATION _IRPSP;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_LISTEN;                                  \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL)&_IRPSP->Parameters;                        \
        p->RequestFlags = Flags;                                             \
        p->RequestConnectionInformation = RequestConnectionInfo;             \
        p->ReturnConnectionInformation = ReturnConnectionInfo;               \
    }

#define TdiBuildAccept(Irp, DevObj, FileObj, CompRoutine, Contxt, RequestConnectionInfo, ReturnConnectionInfo)\
    {                                                                        \
        PTDI_REQUEST_KERNEL_ACCEPT p;                                        \
        PIO_STACK_LOCATION _IRPSP;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_ACCEPT;                                  \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL_ACCEPT)&_IRPSP->Parameters;                 \
        p->RequestConnectionInformation = RequestConnectionInfo;             \
        p->ReturnConnectionInformation = ReturnConnectionInfo;               \
    }

#if (NTDDI_VERSION < NTDDI_WINXP)
#define TdiBuildDirectAccept(Irp, DevObj, FileObj, CompRoutine, Contxt, RequestConnectionInfo, ReturnConnectionInfo)\
    {                                                                        \
        PTDI_REQUEST_KERNEL_ACCEPT p;                                        \
        PIO_STACK_LOCATION _IRPSP;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_DIRECT_ACCEPT;                           \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL_ACCEPT)&_IRPSP->Parameters;                 \
        p->RequestConnectionInformation = RequestConnectionInfo;             \
        p->ReturnConnectionInformation = ReturnConnectionInfo;               \
    }
#endif    

#define TdiBuildDisconnect(Irp, DevObj, FileObj, CompRoutine, Contxt, Time, Flags, RequestConnectionInfo, ReturnConnectionInfo)\
    {                                                                        \
        PTDI_REQUEST_KERNEL p;                                               \
        PIO_STACK_LOCATION _IRPSP;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_DISCONNECT;                              \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL)&_IRPSP->Parameters;                        \
        p->RequestFlags = Flags;                                             \
        p->RequestConnectionInformation = RequestConnectionInfo;             \
        p->ReturnConnectionInformation = ReturnConnectionInfo;               \
        p->RequestSpecific = (PVOID)Time;                                    \
    }

#define TdiBuildReceive(Irp, DevObj, FileObj, CompRoutine, Contxt, MdlAddr, InFlags, ReceiveLen)\
    {                                                                        \
        PTDI_REQUEST_KERNEL_RECEIVE p;                                       \
        PIO_STACK_LOCATION _IRPSP;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_RECEIVE;                                 \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL_RECEIVE)&_IRPSP->Parameters;                \
        p->ReceiveFlags = InFlags;                                           \
        p->ReceiveLength = ReceiveLen;                                       \
        Irp->MdlAddress = MdlAddr;                                           \
    }

#define TdiBuildSend(Irp, DevObj, FileObj, CompRoutine, Contxt, MdlAddr, InFlags, SendLen)\
    {                                                                        \
        PTDI_REQUEST_KERNEL_SEND p;                                          \
        PIO_STACK_LOCATION _IRPSP;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_SEND;                                    \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL_SEND)&_IRPSP->Parameters;                   \
        p->SendFlags = InFlags;                                              \
        p->SendLength = SendLen;                                             \
        Irp->MdlAddress = MdlAddr;                                           \
    }

#define TdiBuildSendDatagram(Irp, DevObj, FileObj, CompRoutine, Contxt, MdlAddr, SendLen, SendDatagramInfo)\
    {                                                                        \
        PTDI_REQUEST_KERNEL_SENDDG p;                                        \
        PIO_STACK_LOCATION _IRPSP;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_SEND_DATAGRAM;                           \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL_SENDDG)&_IRPSP->Parameters;                 \
        p->SendLength = SendLen;                                             \
        p->SendDatagramInformation = SendDatagramInfo;                       \
        Irp->MdlAddress = MdlAddr;                                           \
    }

#define TdiBuildReceiveDatagram(Irp, DevObj, FileObj, CompRoutine, Contxt, MdlAddr, ReceiveLen, ReceiveDatagramInfo, ReturnInfo, InFlags)\
    {                                                                        \
        PTDI_REQUEST_KERNEL_RECEIVEDG p;                                     \
        PIO_STACK_LOCATION _IRPSP;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_RECEIVE_DATAGRAM;                        \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL_RECEIVEDG)&_IRPSP->Parameters;              \
        p->ReceiveLength = ReceiveLen;                                       \
        p->ReceiveDatagramInformation = ReceiveDatagramInfo;                 \
        p->ReturnDatagramInformation = ReturnInfo;                           \
        p->ReceiveFlags = InFlags;                                           \
        Irp->MdlAddress = MdlAddr;                                           \
    }

#define TdiBuildSetEventHandler(Irp, DevObj, FileObj, CompRoutine, Contxt, InEventType, InEventHandler, InEventContext) \
    {                                                                        \
        PTDI_REQUEST_KERNEL_SET_EVENT p;                                     \
        PIO_STACK_LOCATION _IRPSP;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_SET_EVENT_HANDLER;                       \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL_SET_EVENT)&_IRPSP->Parameters;              \
        p->EventType = InEventType;                                          \
        p->EventHandler = (PVOID)InEventHandler;                             \
        p->EventContext = (PVOID)InEventContext;                             \
    }

#define TdiBuildQueryInformationEx(Irp, DevObj, FileObj, CompRoutine, Contxt, QType, MdlAddr, ConnInfo)\
    {                                                                        \
        PTDI_REQUEST_KERNEL_QUERY_INFORMATION p;                             \
        PIO_STACK_LOCATION _IRPSP;                                           \
        Irp->MdlAddress = MdlAddr;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_QUERY_INFORMATION;                       \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL_QUERY_INFORMATION)&_IRPSP->Parameters;      \
        p->QueryType = (ULONG)QType;                                         \
        p->RequestConnectionInformation = ConnInfo;                          \
    }


#define TdiBuildQueryInformation(Irp, DevObj, FileObj, CompRoutine, Contxt, QType, MdlAddr)\
        TdiBuildQueryInformationEx(Irp, DevObj, FileObj, CompRoutine, Contxt, QType, MdlAddr, NULL);


#define TdiBuildSetInformation(Irp, DevObj, FileObj, CompRoutine, Contxt, SType, MdlAddr)\
    {                                                                        \
        PTDI_REQUEST_KERNEL_SET_INFORMATION p;                                          \
        PIO_STACK_LOCATION _IRPSP;                                           \
        Irp->MdlAddress = MdlAddr;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_SET_INFORMATION;                         \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        p = (PTDI_REQUEST_KERNEL_SET_INFORMATION)&_IRPSP->Parameters;                   \
        p->SetType = (ULONG)SType;                                           \
        p->RequestConnectionInformation = NULL;                              \
    }

#define TdiBuildAction(Irp, DevObj, FileObj, CompRoutine, Contxt, MdlAddr)\
    {                                                                        \
        PIO_STACK_LOCATION _IRPSP;                                           \
        if ( CompRoutine != NULL) {                                          \
            IoSetCompletionRoutine( Irp, CompRoutine, Contxt, TRUE, TRUE, TRUE);\
        } else {                                                             \
            IoSetCompletionRoutine( Irp, NULL, NULL, FALSE, FALSE, FALSE);   \
        }                                                                    \
        _IRPSP = IoGetNextIrpStackLocation (Irp);                            \
        _IRPSP->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;              \
        _IRPSP->MinorFunction = TDI_ACTION;                                  \
        _IRPSP->DeviceObject = DevObj;                                       \
        _IRPSP->FileObject = FileObj;                                        \
        Irp->MdlAddress = MdlAddr;                                           \
    }

//
// definitions for the helper routines for TDI compliant transports and clients
//
// Note that the IOCTL used here for the Irp Function is not real; it is used
// to avoid this IO routine having to map buffers (which we don't want).
//
//PIRP
//TdiBuildInternalDeviceControlIrp (
//    IN CCHAR IrpSubFunction,
//    IN PDEVICE_OBJECT DeviceObject,
//    IN PFILE_OBJECT FileObject,
//    IN PKEVENT Event,
//    IN PIO_STATUS_BLOCK IoStatusBlock
//    );

#define TdiBuildInternalDeviceControlIrp(IrpSubFunction,DeviceObject,FileObject,Event,IoStatusBlock) \
    IoBuildDeviceIoControlRequest (\
        0x00000003,\
        DeviceObject, \
        NULL,   \
        0,      \
        NULL,   \
        0,      \
        TRUE,   \
        Event,  \
        IoStatusBlock)


//
// VOID
// TdiCopyLookaheadData(
//     IN PVOID Destination,
//     IN PVOID Source,
//     IN ULONG Length,
//     IN ULONG ReceiveFlags
//     );
//

#ifdef _M_IX86
#define TdiCopyLookaheadData(_Destination,_Source,_Length,_ReceiveFlags)   \
    RtlCopyMemory(_Destination,_Source,_Length)
#else
#define TdiCopyLookaheadData(_Destination,_Source,_Length,_ReceiveFlags) { \
    if ((_ReceiveFlags) & TDI_RECEIVE_COPY_LOOKAHEAD) {                    \
        RtlCopyMemory(_Destination,_Source,_Length);                       \
    } else {                                                               \
        PUCHAR _Src = (PUCHAR)(_Source);                                   \
        PUCHAR _Dest = (PUCHAR)(_Destination);                             \
        PUCHAR _End = _Dest + (_Length);                                   \
        while (_Dest < _End) {                                             \
            *_Dest++ = *_Src++;                                            \
        }                                                                  \
    }                                                                      \
}
#endif


__drv_maxIRQL(PASSIVE_LEVEL)
NTSTATUS
TdiMapUserRequest(
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp,
    __in PIO_STACK_LOCATION IrpSp
    );

#if (NTDDI_VERSION < NTDDI_WINXP)
__drv_preferredFunction("(see documentation)", "Obsolete")
VOID
TdiMapBuffer (
    __in PMDL MdlChain
    );

__drv_preferredFunction("(see documentation)", "Obsolete")
VOID
TdiUnmapBuffer (
    __in PMDL MdlChain
    );
#endif

__drv_maxIRQL(DISPATCH_LEVEL)
NTSTATUS
TdiCopyBufferToMdl (
    __in PVOID SourceBuffer,
    __in ULONG SourceOffset,
    __in ULONG SourceBytesToCopy,
    __in PMDL DestinationMdlChain,
    __in ULONG DestinationOffset,
    __out PULONG BytesCopied
    );

__drv_maxIRQL(DISPATCH_LEVEL)
NTSTATUS
TdiCopyMdlToBuffer(
    __in PMDL SourceMdlChain,
    __in ULONG SourceOffset,
    __out_bcount(DestinationBufferSize) PVOID DestinationBuffer,
    __in ULONG DestinationOffset,
    __in ULONG DestinationBufferSize,
    __out PULONG BytesCopied
    );

#if (NTDDI_VERSION >= NTDDI_WINXP)
__drv_maxIRQL(DISPATCH_LEVEL)
NTSTATUS
TdiCopyMdlChainToMdlChain(
    __in PMDL SourceMdlChain,
    __in ULONG SourceOffset,
    __in PMDL DestinationMdlChain,
    __in ULONG DestinationOffset,
    __out PULONG BytesCopied
    );
#endif

#if (NTDDI_VERSION >= NTDDI_WS03)
VOID
TdiCopyBufferToMdlWithReservedMappingAtDpcLevel(
    __in PVOID SourceBuffer,
    __in PMDL DestinationMdl,
    __in ULONG DestinationOffset,
    __in ULONG BytesToCopy
    );

__inline
VOID
TdiCopyBufferToMdlWithReservedMapping(
    __in PVOID SourceBuffer,
    __in PMDL DestinationMdl,
    __in ULONG DestinationOffset,
    __in ULONG BytesToCopy
    )
{
    KIRQL OldIrql;
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    TdiCopyBufferToMdlWithReservedMappingAtDpcLevel(SourceBuffer,
                                                    DestinationMdl,
                                                    DestinationOffset,
                                                    BytesToCopy);
    KeLowerIrql(OldIrql);
}
#endif

VOID
TdiBuildNetbiosAddress (
    __in PUCHAR NetbiosName,
    __in BOOLEAN IsGroupName,
    __inout PTA_NETBIOS_ADDRESS NetworkName
    );

NTSTATUS
TdiBuildNetbiosAddressEa (
    __out PUCHAR Buffer,
    __in  BOOLEAN IsGroupName,
    __in  PUCHAR NetbiosName
    );

//++
//
//  VOID
//  TdiCompleteRequest (
//      IN PIRP Irp,
//      IN NTSTATUS Status
//      );
//
//  Routine Description:
//
//      This routine is used to complete an IRP with the indicated
//      status.
//
//  Arguments:
//
//      Irp - Supplies a pointer to the Irp to complete
//
//      Status - Supplies the completion status for the Irp
//
//  Return Value:
//
//      None.
//
//--

#define TdiCompleteRequest(IRP,STATUS) {              \
    (IRP)->IoStatus.Status = (STATUS);                \
    IoCompleteRequest( (IRP), IO_NETWORK_INCREMENT ); \
}


__drv_maxIRQL(DISPATCH_LEVEL)
VOID
TdiReturnChainedReceives(
    __in PVOID *TsduDescriptors,
    __in ULONG  NumberOfTsdus
    );


// The type definition for a TDI Bind handler callout. This callout is
// called when a new transport device arrives.

typedef VOID
(*TDI_BIND_HANDLER)(
    __in PUNICODE_STRING DeviceName
    );

typedef VOID
(*TDI_UNBIND_HANDLER)(
    __in PUNICODE_STRING DeviceName
    );

// The type definition for a TDI address handler callout.
// This is typedefed  defined at the end (with the others)

typedef VOID
(*TDI_ADD_ADDRESS_HANDLER)(
    __in PTA_ADDRESS Address
    );

typedef VOID
(*TDI_DEL_ADDRESS_HANDLER)(
    __in PTA_ADDRESS Address
    );

typedef VOID
(* TDI_NET_READY_HANDLER)(
    __in NTSTATUS ProviderStatus
    );

typedef VOID
(* ProviderPnPPowerComplete)(
    __in PNET_PNP_EVENT  NetEvent,
    __in NTSTATUS        ProviderStatus
    );


__drv_preferredFunction("TdiRegisterPnPHandlers", "Obsolete")
NTSTATUS
TdiRegisterAddressChangeHandler(
    __in TDI_ADD_ADDRESS_HANDLER  AddHandler,
    __in TDI_DEL_ADDRESS_HANDLER  DeleteHandler,
    __out HANDLE                  *BindingHandle
    );

__drv_preferredFunction("TdiDeregisterPnPHandlers", "Obsolete")
NTSTATUS
TdiDeregisterAddressChangeHandler(
    __in HANDLE BindingHandle
);

__drv_preferredFunction("TdiRegisterPnPHandlers", "Obsolete")
NTSTATUS
TdiRegisterNotificationHandler(
    __in TDI_BIND_HANDLER   BindHandler,
    __in TDI_UNBIND_HANDLER UnbindHandler,
    __out HANDLE            *BindingHandle
);

__drv_preferredFunction("TdiDeregisterPnPHandlers", "Obsolete")
NTSTATUS
TdiDeregisterNotificationHandler(
    __in HANDLE BindingHandle
);

__drv_maxIRQL(APC_LEVEL)
NTSTATUS
TdiRegisterDeviceObject(
    __in PUNICODE_STRING DeviceName,
    __out HANDLE         *RegistrationHandle
);

__drv_maxIRQL(APC_LEVEL)
NTSTATUS
TdiDeregisterDeviceObject(
    __in HANDLE RegistrationHandle
);

__drv_maxIRQL(APC_LEVEL)
NTSTATUS
TdiDeregisterNetAddress(
    __in HANDLE RegistrationHandle
);

__drv_maxIRQL(PASSIVE_LEVEL) 
VOID
TdiInitialize(
    VOID
);


// PnP extensions to TDI. Spec : TdiPnp.doc : MunilS

typedef enum _TDI_PNP_OPCODE {
    TDI_PNP_OP_MIN,
    TDI_PNP_OP_ADD,
    TDI_PNP_OP_DEL,
    TDI_PNP_OP_UPDATE,
    TDI_PNP_OP_PROVIDERREADY,
    TDI_PNP_OP_NETREADY,
    TDI_PNP_OP_ADD_IGNORE_BINDING,
    TDI_PNP_OP_DELETE_IGNORE_BINDING,
    TDI_PNP_OP_MAX,
} TDI_PNP_OPCODE;

typedef struct _TDI_PNP_CONTEXT_WIN2K {
    USHORT ContextSize;
    USHORT ContextType;
    UCHAR POINTER_ALIGNMENT ContextData[1];
} TDI_PNP_CONTEXT_WIN2K, *PTDI_PNP_CONTEXT_WIN2K;

typedef struct _TDI_PNP_CONTEXT_XP {
    USHORT ContextSize;
    USHORT ContextType;
    UCHAR POINTER_ALIGNMENT ContextData[1];
} TDI_PNP_CONTEXT_XP, *PTDI_PNP_CONTEXT_XP;

#if (NTDDI_VERSION >= NTDDI_WINXP)
typedef TDI_PNP_CONTEXT_XP TDI_PNP_CONTEXT;
#else
typedef TDI_PNP_CONTEXT_WIN2K TDI_PNP_CONTEXT;
#endif

typedef TDI_PNP_CONTEXT* PTDI_PNP_CONTEXT;

typedef VOID
(*TDI_BINDING_HANDLER)(
    __in TDI_PNP_OPCODE PnPOpcode,
    __in PUNICODE_STRING DeviceName,
    __in PWSTR MultiSZBindList
    );

typedef VOID
(*TDI_ADD_ADDRESS_HANDLER_V2)(
    __in  PTA_ADDRESS      Address,
    __in  PUNICODE_STRING  DeviceName,
    __in  PTDI_PNP_CONTEXT Context
    );

typedef VOID
(*TDI_DEL_ADDRESS_HANDLER_V2)(
    __in  PTA_ADDRESS      Address,
    __in  PUNICODE_STRING  DeviceName,
    __in  PTDI_PNP_CONTEXT Context
    );

typedef NTSTATUS
(*TDI_PNP_POWER_HANDLER)(
    __in PUNICODE_STRING  DeviceName,
    __in PNET_PNP_EVENT   PowerEvent,
    __in PTDI_PNP_CONTEXT Context1,
    __in PTDI_PNP_CONTEXT Context2
    );

// When the user makes changes using the NCPA, a TdiMakeNCPAChanges request
// is generated through NDIS. The following structure is used to communicate
// these changes.

typedef struct _TDI_NCPA_BINDING_INFO {
    PUNICODE_STRING TdiClientName;
    PUNICODE_STRING TdiProviderName;
    PUNICODE_STRING BindList;
    PVOID           ReconfigBuffer;
    unsigned int    ReconfigBufferSize;
    TDI_PNP_OPCODE  PnpOpcode;
} TDI_NCPA_BINDING_INFO, *PTDI_NCPA_BINDING_INFO;

//
// The following structure makes it easy for consistency/integrity checking
//
typedef struct _TDI_VERSION_ {
    union {
        struct {
            UCHAR MajorTdiVersion;
            UCHAR MinorTdiVersion;
        };
        USHORT TdiVersion;
    };
} TDI_VERSION, *PTDI_VERSION;

#define TDI20
typedef struct _TDI20_CLIENT_INTERFACE_INFO {
     union {
       struct {
          UCHAR MajorTdiVersion;
          UCHAR MinorTdiVersion;
       };
       USHORT TdiVersion;
    };

    //TDI_VERSION                       TdiVersion;
        USHORT                          Unused;
        PUNICODE_STRING         ClientName;
        TDI_PNP_POWER_HANDLER   PnPPowerHandler;

    union {

        TDI_BINDING_HANDLER     BindingHandler;

        struct {
            //
            // Putting these back in for backward compatibility.
            //

            TDI_BIND_HANDLER        BindHandler;
            TDI_UNBIND_HANDLER      UnBindHandler;

        };
    };


    union {
        struct {

            TDI_ADD_ADDRESS_HANDLER_V2 AddAddressHandlerV2;
            TDI_DEL_ADDRESS_HANDLER_V2 DelAddressHandlerV2;

        };
        struct {

            //
            // Putting these back in for backward compatibility.
            //

            TDI_ADD_ADDRESS_HANDLER AddAddressHandler;
            TDI_DEL_ADDRESS_HANDLER DelAddressHandler;

        };

    };

//    TDI_NET_READY_HANDLER       NetReadyHandler;

} TDI20_CLIENT_INTERFACE_INFO, *PTDI20_CLIENT_INTERFACE_INFO;


#ifdef TDI20

#define TDI_CURRENT_MAJOR_VERSION (2)
#define TDI_CURRENT_MINOR_VERSION (0)

typedef TDI20_CLIENT_INTERFACE_INFO TDI_CLIENT_INTERFACE_INFO;

#define TDI_CURRENT_VERSION ((TDI_CURRENT_MINOR_VERSION) << 8 | \
                        (TDI_CURRENT_MAJOR_VERSION))

#endif // TDI20

#define TDI_VERSION_ONE 0x0001

typedef TDI_CLIENT_INTERFACE_INFO *PTDI_CLIENT_INTERFACE_INFO;


__drv_maxIRQL(APC_LEVEL)
NTSTATUS
TdiRegisterPnPHandlers(
    __in_bcount(InterfaceInfoSize) PTDI_CLIENT_INTERFACE_INFO ClientInterfaceInfo,
    __in ULONG InterfaceInfoSize,
    __out HANDLE *BindingHandle
    );

__drv_maxIRQL(APC_LEVEL)
NTSTATUS
TdiDeregisterPnPHandlers(
    __in HANDLE BindingHandle
    );

__drv_maxIRQL(APC_LEVEL)
NTSTATUS
TdiPnPPowerRequest(
    __in PUNICODE_STRING  DeviceName,
    __in PNET_PNP_EVENT   PowerEvent,
    __in PTDI_PNP_CONTEXT Context1,
    __in PTDI_PNP_CONTEXT Context2,
    __in ProviderPnPPowerComplete ProtocolCompletionHandler
    );

__drv_maxIRQL(APC_LEVEL) 
VOID
TdiPnPPowerComplete(
    __in HANDLE           BindingHandle,
    __in PNET_PNP_EVENT   PowerEvent,
    __in NTSTATUS         Status
    );

__drv_maxIRQL(APC_LEVEL)
NTSTATUS
TdiRegisterNetAddress(
    __in PTA_ADDRESS Address,
    __in PUNICODE_STRING DeviceName,
    __in PTDI_PNP_CONTEXT Context,
    __out HANDLE *RegistrationHandle
    );

NTSTATUS
TdiMakeNCPAChanges(
    __in TDI_NCPA_BINDING_INFO NcpaBindingInfo
    );

//
// Enumerate all TDI addresses for a client
//
__drv_maxIRQL(APC_LEVEL)
NTSTATUS
TdiEnumerateAddresses(
    __in HANDLE BindingHandle
    );

//
// Introducing the concept of Transport provider.
//

__drv_maxIRQL(APC_LEVEL)
NTSTATUS
TdiRegisterProvider(
    __in PUNICODE_STRING ProviderName,
    __out HANDLE *ProviderHandle
    );

__drv_maxIRQL(APC_LEVEL)
NTSTATUS
TdiProviderReady(
    __in HANDLE      ProviderHandle
    );

__drv_maxIRQL(APC_LEVEL)
NTSTATUS
TdiDeregisterProvider(
    __in HANDLE  ProviderHandle
    );

BOOLEAN
TdiMatchPdoWithChainedReceiveContext(
    __in PVOID TsduDescriptor,
    __in PVOID PDO
    );



#define  TDI_STATUS_BAD_VERSION             0xC0010004L // same as NDIS, is that OK?
#define  TDI_STATUS_BAD_CHARACTERISTICS     0xC0010005L // ,,


//
// PNP context types
//
#define TDI_PNP_CONTEXT_TYPE_IF_NAME            0x1
#define TDI_PNP_CONTEXT_TYPE_IF_ADDR            0x2
#define TDI_PNP_CONTEXT_TYPE_PDO                0x3
#define TDI_PNP_CONTEXT_TYPE_FIRST_OR_LAST_IF   0x4

// The following structures and macros are for handlers that support returning
// ancillary data via a control structure
//

//
// Layout of ancillary data objects in the control buffer
//
typedef struct _TDI_CMSGHDR_XP {
    SIZE_T      cmsg_len;
    LONG        cmsg_level;
    LONG        cmsg_type;
    /* followed by UCHAR cmsg_data[] */
} TDI_CMSGHDR_XP, *PTDI_CMSGHDR_XP;

#if (NTDDI_VERSION >= NTDDI_WINXP)
typedef TDI_CMSGHDR_XP TDI_CMSGHDR;
typedef TDI_CMSGHDR* PTDI_CMSGHDR;
#endif

//
// Alignment macros for header and data members of
// the control buffer.
//
#define TDI_CMSGHDR_ALIGN(length)                           \
            ( ((length) + TYPE_ALIGNMENT(TDI_CMSGHDR)-1) &   \
                (~(TYPE_ALIGNMENT(TDI_CMSGHDR)-1)) )         \

#define TDI_CMSGDATA_ALIGN(length)                          \
            ( ((length) + MAX_NATURAL_ALIGNMENT-1) &        \
                (~(MAX_NATURAL_ALIGNMENT-1)) )



//  Returns a pointer to the first byte of data (what is referred 
//  to as the cmsg_data member though it is not defined in 
//  the structure).
//
//  UCHAR *
//  TDI_CMSG_DATA (
//      PTDI_CMSGHDR   pcmsg
//      );
//
#define TDI_CMSG_DATA(cmsg)             \
            ( (UCHAR *)(cmsg) + TDI_CMSGDATA_ALIGN(sizeof(TDI_CMSGHDR)) )

//
//  Returns total size of an ancillary data object given 
//  the amount of data. Used to allocate the correct amount 
//  of space.
//
//  SIZE_T
//  TDI_CMSG_SPACE (
//      SIZE_T length
//      );
//
#define TDI_CMSG_SPACE(length)  \
        (TDI_CMSGDATA_ALIGN(sizeof(TDI_CMSGHDR) + TDI_CMSGHDR_ALIGN(length)))

//  Returns the value to store in cmsg_len given the amount of data.
//
//  SIZE_T
//  TDI_CMSG_LEN (
//      SIZE_T length
//  );
//
#define TDI_CMSG_LEN(length)    \
        (TDI_CMSGDATA_ALIGN(sizeof(TDI_CMSGHDR)) + length)


// Initializes the members of a TDI_CMSGHDR structure
//
// VOID
// TDI_INIT_CMSGHDR (
//      PTDI_CMSGHDR cmsg,
//      INT level,
//      INT type,
//      SIZE_T length,
//      );
//
#define TDI_INIT_CMSGHDR(cmsg, level, type, length) { \
        ((TDI_CMSGHDR *) cmsg)->cmsg_level = level; \
        ((TDI_CMSGHDR *) cmsg)->cmsg_type = type;  \
        ((TDI_CMSGHDR *) cmsg)->cmsg_len = TDI_CMSG_LEN(length); \
        }
        
#pragma warning(pop)
#endif // _TDI_KRNL_

