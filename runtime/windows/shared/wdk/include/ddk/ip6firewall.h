/*++

Copyright (c) 2004-2005  Microsoft Corporation

Module Name:

    ip6firewall.h

Abstract:

    This module contains definitions for the IPv6 firewall hook.

Environment:

    Kernel mode only.

--*/

#if (NTDDI_VERSION < NTDDI_LONGHORN) 

#ifndef _IP6FIREWALL_
#define _IP6FIREWALL_

#pragma once

#include <ipinfo.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Types used by the firewall hook.
//

typedef enum {
    DirectionTransmit,
    DirectionReceive,
    DirectionMax
} IPv6Direction;

typedef enum {
    ActionAccept,
    ActionDrop,
    ActionMax
} IPv6Action;

__drv_functionClass(IPv6FirewallHookProcType)
__drv_maxIRQL(DISPATCH_LEVEL)
typedef 
IPv6Action
IPv6FirewallHookProcType (
    __in const IPv6Addr *SourceAddress,
    __in const IPv6Addr *DestinationAddress,
    __in uint PayloadLength,
    __in uchar HeaderType,
    __in const uchar *HeaderData,
    __in const void *PacketContext,
    __in uint DataLength,
    __in uint InterfaceIndex,
    __in IPv6Direction Direction,
    __in BOOLEAN IsLoopBack
    );

typedef IPv6FirewallHookProcType *IPv6FirewallHookProc;

typedef void
(*IPv6FirewallDeregistrationCompleteProc)(
    );

//
// Exported function declarations.
//

extern NTSTATUS
__drv_maxIRQL(DISPATCH_LEVEL)
__success(return==STATUS_SUCCESS)
IPv6EnableFirewallHook(
    __in IPv6FirewallHookProc FirewallHookFunction
    );

extern void
__drv_maxIRQL(DISPATCH_LEVEL)
IPv6DisableFirewallHook(
    __in IPv6FirewallDeregistrationCompleteProc CompletionRoutine
    );

extern const uchar *
__drv_maxIRQL(DISPATCH_LEVEL)
IPv6ObtainPacketData(
    __in const void *PacketContext,
    __in uint DataLength,
    __in uint Alignment
    );

extern IP_STATUS
__drv_maxIRQL(DISPATCH_LEVEL)
IPv6GetBestRouteInfo(
    __in const IPv6Addr *Addr,
    __in ulong ScopeId,
    __in ulong Index,
    __out IP6RouteEntry *Ire
    );
    
#ifdef __cplusplus
}
#endif

#endif // _IP6FIREWALL_

#endif //(NTDDI_VERSION < NTDDI_LONGHORN) 

