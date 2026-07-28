/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Abstract:

    Public PktMonNetEvt NPI (NMR) interface for consuming network packet events

Environment:

    Kernel mode

--*/

#ifdef _MSC_VER
#pragma once
#endif //_MSC_VER

#ifndef _PKTMONNETEVTNPIK_H_
#define _PKTMONNETEVTNPIK_H_

#include <PktMonNpik.h>
#include <netiodef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if (NTDDI_VERSION >= NTDDI_WIN11_ZN)

#ifndef USER_MODE

//
// Packet Monitor NetEvt NMR provider characteristics
//
typedef struct _PKTMON_NETEVT_PROVIDER_CHARACTERISTICS
{
    PKTMON_HEADER Header;

} PKTMON_NETEVT_PROVIDER_CHARACTERISTICS;

typedef struct _PKTMON_NETEVT_DISPATCH_HEADER
{
    UINT16 Version;
    SIZE_T Size;

} PKTMON_NETEVT_DISPATCH_HEADER;

//
// Packet Monitor NetEvt NMR provider dispatch table
//
typedef struct _PKTMON_NETEVT_PROVIDER_DISPATCH
{
    PKTMON_NETEVT_DISPATCH_HEADER Header;
    UINT32 HandlerCount;
    UINT64* Handlers;

} PKTMON_NETEVT_PROVIDER_DISPATCH;

//
// Packet Monitor NetEvt NMR client dispatch table
//
typedef struct _PKTMON_NETEVT_CLIENT_DISPATCH
{
    PKTMON_NETEVT_DISPATCH_HEADER Header;
    UINT32 HandlerCount;
    UINT64* Handlers;

} PKTMON_NETEVT_CLIENT_DISPATCH;

//
// Packet Monitor NetEvt NMR client V2 dispatch table
//
typedef struct _PKTMON_NETEVT_CLIENT_DISPATCH_V2
{
    PKTMON_NETEVT_DISPATCH_HEADER Header;
    PKTMON_CAPTURE_TYPE CaptureType;
    UINT32 HandlerCount;
    UINT64* Handlers;

} PKTMON_NETEVT_CLIENT_DISPATCH_V2;

//
// Packet monitor NetEvt NMR client context
//
typedef struct _PKTMON_NETEVT_CLIENT_CONTEXT
{
    HANDLE  NmrClientHandle;

    PEX_RUNDOWN_REF_CACHE_AWARE RundownRef;

    BOOLEAN Enabled;

    //
    // Provider dispatch
    //
    PVOID ProviderContext;
    PKTMON_NETEVT_PROVIDER_DISPATCH* ProviderDispatch;

} PKTMON_NETEVT_CLIENT_CONTEXT;

#endif // USER_MODE

//
// Packet Monitor NetEvt event types/IDs that can be reported
//
typedef UCHAR PKTMON_NETEVT_EVENT_TYPE;
#define PKTMON_EVENT_TYPE_PACKET_DROP     ((PKTMON_NETEVT_EVENT_TYPE)100)
#define PKTMON_EVENT_TYPE_PACKET_FLOW     ((PKTMON_NETEVT_EVENT_TYPE)101)

#pragma pack(push, 1)

//
// Packet descriptor used for event streaming
//
typedef struct _PKTMON_EVT_STREAM_PACKET_DESCRIPTOR
{
    ULONG PacketOriginalLength;
    ULONG PacketLoggedLength;
    ULONG PacketMetaDataLength;
} PKTMON_EVT_STREAM_PACKET_DESCRIPTOR;

//
// Metadata information used for event streaming
//
typedef struct _PKTMON_EVT_STREAM_METADATA
{
    UINT64  PktGroupId;
    UINT16  PktCount;
    UINT16  AppearanceCount;
    UINT16  DirectionName;
    UINT16  PacketType;
    UINT16  ComponentId;
    UINT16  EdgeId;
    UINT16  FilterId;
    UINT32  DropReason;
    UINT32  DropLocation;
    UINT16  ProcNum;
    LARGE_INTEGER TimeStamp;
} PKTMON_EVT_STREAM_METADATA;

//
// Packet header used for event streaming
//
typedef struct _PKTMON_EVT_STREAM_PACKET_HEADER
{
    UCHAR EventId;
    PKTMON_EVT_STREAM_PACKET_DESCRIPTOR PacketDescriptor;
    PKTMON_EVT_STREAM_METADATA Metadata;
} PKTMON_EVT_STREAM_PACKET_HEADER;

#pragma pack(pop)

//
// Start and end pointer of the buffer containing the dropped packet information
//
typedef struct _PKTMON_NETEVT_CLIENT_REPORT_PACKET_DROP_OUT
{
    PKTMON_EVT_STREAM_PACKET_HEADER* BufferStart;
    UCHAR* BufferEnd;
} PKTMON_NETEVT_CLIENT_REPORT_PACKET_DROP_OUT;

//
// Start and end pointer of the buffer containing the packet information
//
typedef struct _PKTMON_NETEVT_CLIENT_REPORT_PACKET_EVENT_OUT
{
    PKTMON_EVT_STREAM_PACKET_HEADER* BufferStart;
    UCHAR* BufferEnd;
} PKTMON_NETEVT_CLIENT_REPORT_PACKET_EVENT_OUT;

#endif // (NTDDI_VERSION >= NTDDI_WIN11_ZN)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // _PKTMONNETEVTNPIK_H_
