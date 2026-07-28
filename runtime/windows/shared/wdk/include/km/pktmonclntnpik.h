/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Abstract:

    Public PktMonClnt NPI (NMR) interface for producing network packet events

Environment:

    Kernel mode

--*/

#ifdef _MSC_VER
#pragma once
#endif //_MSC_VER

#ifndef _PKTMONCLNTNPIK_H_
#define _PKTMONCLNTNPIK_H_

#include <PktMonNpik.h>
#include <netiodef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if (NTDDI_VERSION >= NTDDI_WIN10_RS5)

#ifndef USER_MODE

#ifndef ADDRESS_FAMILY
typedef USHORT ADDRESS_FAMILY;
#endif

//
// Packet buffer type
//
typedef enum _PKTMON_BUFFER_TYPE
{
    PktMonBuffer_NblChain  = 1,
    PktMonBuffer_NblSingle = 2,
    PktMonBuffer_Wsk_Buf   = 3,
    PktMonBuffer_None      = 4,

} PKTMON_BUFFER_TYPE;

//
// Component description struct
// Used as parameter for ComponentRegister()
//
typedef struct _PKTMON_COMPONENT_IN
{
    PKTMON_HEADER Header;

    HANDLE CompContext;

    PCUNICODE_STRING Name;
    PCUNICODE_STRING Description;
    PKTMON_COMPONENT_TYPE Type;

    PKTMON_DIRECTION DirTagIn;
    PKTMON_DIRECTION DirTagOut;

} PKTMON_COMPONENT_IN;

//
// Edge description
// Used as parameter for EdgeAdd()
//
typedef struct _PKTMON_EDGE_IN
{
    PKTMON_HEADER Header;

    PCUNICODE_STRING Name;

    PKTMON_DIRECTION DirTagIn;
    PKTMON_DIRECTION DirTagOut;

} PKTMON_EDGE_IN;

//
// Component property
// Used as parameter for SetCompProperty()
//
typedef struct _PKTMON_COMP_PROPERTY_IN
{
    PKTMON_HEADER Header;

    PKTMON_COMPONENT_PROPERTY_ID Id;
    CONST VOID* Value;
    USHORT Size;

} PKTMON_COMP_PROPERTY_IN;

//
// Packet header info
//
typedef struct _PKTMON_PACKET_HEADER_INFO
{
    ADDRESS_FAMILY AddrFamily; // AF_INET, AF_INET6

    PKTMON_IP_ADDRESS IpAddrLocal;
    PKTMON_IP_ADDRESS IpAddrRemote;

    UCHAR IpProtocol;

    union _PKTMON_TRANSPORT
    {
        struct _PKTMON_TRANSPORT_UDP
        {
            USHORT PortLocal;
            USHORT PortRemote;
        } Udp;

        struct _PKTMON_TRANSPORT_TCP
        {
            USHORT PortLocal;
            USHORT PortRemote;
            UCHAR  Flags;
        } Tcp;

        struct _PKTMON_TRANSPORT_ICMP
        {
            UCHAR Type;
            UCHAR Code;
        } Icmp;

    } Transport;

} PKTMON_PACKET_HEADER_INFO;

//
// Packet description for logging
// Used as parameter for PacketLog()
//
typedef struct _PKTMON_PACKET_LOG_IN
{
    PKTMON_HEADER Header;

    VOID* Buffer;
    PKTMON_BUFFER_TYPE BufferType;
    PKTMON_PACKET_TYPE PacketType;
    PKTMON_DIRECTION Direction;

    UINT32 Flags;

    PKTMON_PACKET_HEADER_INFO* PacketHeaderInfo;

} PKTMON_PACKET_LOG_IN;

#define PKTMON_SIZEOF_PACKET_LOG_IN_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(PKTMON_PACKET_LOG_IN, Direction)

#define PKTMON_SIZEOF_PACKET_LOG_IN_REVISION_2    \
        RTL_SIZEOF_THROUGH_FIELD(PKTMON_PACKET_LOG_IN, HeaderInfo)

//
// Packet drop details
// Used as parameter for PacketDrop()
//
// DropReason:
//     0 - 0x7FFFFFFF: Reserved for Microsoft.
//     0x80000000 - 0xFFFFFFFF: Free to be used.
//
// LocationCode:
//     0 - 0x7FFFFFFF: Free to be used.
//     0x80000000 - 0xFFFFFFFF: Reserved for Microsoft.
//
typedef struct _PKTMON_DROP_REPORT_IN
{
    PKTMON_HEADER Header;

    UINT32 DropReason;
    UINT32 LocationCode;

} PKTMON_DROP_REPORT_IN;

//
// Component enabling arguments
// Used as parameter for CompEnable()
//
typedef struct _PKTMON_CLIENT_COMP_ENABLE_IN
{
    PKTMON_HEADER Header;

    HANDLE CompContext;

    BOOLEAN FlowEnabled;
    BOOLEAN DropEnabled;

} PKTMON_CLIENT_COMP_ENABLE_IN;

//
// Packet context
// Used as parameter for PacketLog() and PacketDrop()
//
// Id:
//     0 - 0x7FFFFFFF: Reserved for Microsoft.
//     0x80000000 - 0xFFFFFFFF: Free to be used.
//
typedef struct _PKTMON_PACKET_CONTEXT_IN
{
    PKTMON_HEADER Header;

    INT Id;
    CONST VOID* Value;
    USHORT Size;

} PKTMON_PACKET_CONTEXT_IN;

//
// Register network component for packet monitoring.
// A component may represent an object instance or a logical sub-component.
// Component handle is used for reporting packet drops within the component.
// A single component may report packet drops for two packet direction.
// E.g. Send / Receive, Ingress / Egress, etc.
// Each direction is assigned two counters:
//     1. Packet drop counter
//     2. Byte drop counter
//
typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
(NTAPI PKTMON_PROVIDER_REGISTER_COMPONENT)(
    _In_ VOID* ProviderBindingContext,
    _In_ CONST PKTMON_COMPONENT_IN* Component,
    _Out_ HANDLE* CompHandle
    );

typedef PKTMON_PROVIDER_REGISTER_COMPONENT(*PKTMON_PROVIDER_REGISTER_COMPONENT_HANDLER);

//
// Unregister previously registered component
//
typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
(NTAPI PKTMON_PROVIDER_UNREGISTER_COMPONENT)(
    _In_ VOID* ProviderBindingContext,
    _In_ HANDLE CompHandle
    );

typedef PKTMON_PROVIDER_UNREGISTER_COMPONENT(*PKTMON_PROVIDER_UNREGISTER_COMPONENT_HANDLER);

//
// Add monitoring edge (boundary) to a registered component.
// A single component may register multiple edges.
// Each edge represents a pair of entry / exit points.
// Edge handle is used for reporting packet propagation through components boundary.
// A single edge may report packet propagation in two packet direction.
// E.g. Send / Receive, Ingress / Egress, In / Out, etc.
// Each direction is assigned two counters:
//     1. Packet counter
//     2. Byte counter
//
typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
(NTAPI PKTMON_PROVIDER_ADD_EDGE)(
    _In_ VOID *ProviderBindingContext,
    _In_ HANDLE CompHandle,
    _In_ CONST PKTMON_EDGE_IN* Edge,
    _Out_ HANDLE* EdgeHandle
    );

typedef PKTMON_PROVIDER_ADD_EDGE(*PKTMON_PROVIDER_ADD_EDGE_HANDLER);

//
// Set component property
// E.g. IP address, ifIndex, etc.
//
typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
(NTAPI PKTMON_PROVIDER_SET_COMP_PROPERTY)(
    _In_ VOID* ProviderBindingContext,
    _In_ HANDLE CompHandle,
    _In_ CONST PKTMON_COMP_PROPERTY_IN* Property
    );

typedef PKTMON_PROVIDER_SET_COMP_PROPERTY(*PKTMON_PROVIDER_SET_COMP_PROPERTY_HANDLER);

//
// Report successful packet propagation through an edge.
// Custom context can be attached to the report.
//
typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
(NTAPI PKTMON_PROVIDER_PACKET_LOG)(
    _In_ VOID* ProviderBindingContext,
    _In_ HANDLE EdgeHandle,
    _In_ CONST PKTMON_PACKET_LOG_IN* PacketLog,
    _In_opt_ CONST PKTMON_PACKET_CONTEXT_IN* Context
    );

typedef PKTMON_PROVIDER_PACKET_LOG(*PKTMON_PROVIDER_PACKET_LOG_HANDLER);

//
// Report packet drop within a component,
// together with the drop reason and drop location.
// Custom context can be attached to the report.
//
typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
(NTAPI PKTMON_PROVIDER_PACKET_DROP)(
    _In_ VOID* ProviderBindingContext,
    _In_ HANDLE CompHandle,
    _In_ CONST PKTMON_PACKET_LOG_IN* PacketLog,
    _In_ CONST PKTMON_DROP_REPORT_IN* DropReport,
    _In_opt_ CONST PKTMON_PACKET_CONTEXT_IN* Context
    );

typedef PKTMON_PROVIDER_PACKET_DROP(*PKTMON_PROVIDER_PACKET_DROP_HANDLER);

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
(NTAPI PKTMON_CLIENT_ENABLE)(
    _In_ BOOLEAN Enable
    );

typedef PKTMON_CLIENT_ENABLE(*PKTMON_CLIENT_ENABLE_HANDLER);

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
(NTAPI PKTMON_CLIENT_COMP_ENABLE)(
    _In_ PKTMON_CLIENT_COMP_ENABLE_IN* Enable
    );

typedef PKTMON_CLIENT_COMP_ENABLE(*PKTMON_CLIENT_COMP_ENABLE_HANDLER);

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
(NTAPI PKTMON_CLIENT_COMP_CLOSE)(
    _In_ HANDLE CompContext
    );

typedef PKTMON_CLIENT_COMP_CLOSE(*PKTMON_CLIENT_COMP_CLOSE_HANDLER);

//
// PktMonClnt NMR provider dispatch table
//
typedef struct _PKTMON_PROVIDER_DISPATCH
{
    USHORT Size;

    PKTMON_PROVIDER_REGISTER_COMPONENT_HANDLER   ComponentRegister;
    PKTMON_PROVIDER_UNREGISTER_COMPONENT_HANDLER ComponentUnregister;
    PKTMON_PROVIDER_SET_COMP_PROPERTY_HANDLER    SetCompProperty;
    PKTMON_PROVIDER_ADD_EDGE_HANDLER             EdgeAdd;

    PKTMON_PROVIDER_PACKET_LOG_HANDLER  PacketLog;
    PKTMON_PROVIDER_PACKET_DROP_HANDLER PacketDrop;

} PKTMON_PROVIDER_DISPATCH;

//
// PktMonClnt NMR client dispatch table
//
typedef struct _PKTMON_CLIENT_DISPATCH
{
    USHORT Size;

    PKTMON_CLIENT_ENABLE_HANDLER      ClientEnable;
    PKTMON_CLIENT_COMP_ENABLE_HANDLER CompEnable;
    PKTMON_CLIENT_COMP_CLOSE_HANDLER  CompClose;

} PKTMON_CLIENT_DISPATCH;

//
// PktMonClnt NMR provider NPI specific characteristics
//
// Supported provider versions
// (Major.Minor)
// - 1.0
//
// NOTE: v1.0 providers will advertise either 1.0 support or will have a NULL
//       NpiSpecificCharacteristics.
//
typedef struct _PKTMON_PROVIDER_SPECIFIC_CHARACTERISTICS
{
    PKTMON_HEADER Header;

    USHORT ProviderMajorVersion;
    USHORT ProviderMinorVersion;

} PKTMON_PROVIDER_SPECIFIC_CHARACTERISTICS;

//
// PktMonClnt NMR interface id and module id
//
extern CONST NPIID NPI_PKTMON_INTERFACE_ID;
extern CONST NPI_MODULEID NPI_MS_PKTMON_MODULEID;

#endif // USER_MODE

#endif // (NTDDI_VERSION >= NTDDI_WIN10_RS5)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // _PKTMONCLNTNPIK_H_
