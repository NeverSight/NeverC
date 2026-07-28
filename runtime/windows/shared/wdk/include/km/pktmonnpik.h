/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Abstract:

    Common defines for the public NPI (NMR) interfaces for network packet monitoring

Environment:

    Kernel mode

--*/

#ifdef _MSC_VER
#pragma once
#endif //_MSC_VER

#ifndef _PKTMONNPIK_H_ // include guard for 3rd party interop
#define _PKTMONNPIK_H_

#include <PktMonDefk.h>
#include <netiodef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if (NTDDI_VERSION >= NTDDI_WIN10_RS5)

//
// Common header for version control
//
typedef struct _PKTMON_HEADER
{
    USHORT Size;
    USHORT Version;

} PKTMON_HEADER;

//
// Packet direction.
// Each component reports packets in two directions only (IN or OUT).
// Labels are being assigned during component or edge registration.
//
typedef enum _PKTMON_DIRECTION
{
    PktMonDir_In = 1,
    PktMonDir_Out,
} PKTMON_DIRECTION;

//
// Packet payload type
//
typedef enum _PKTMON_PACKET_TYPE
{
    PktMonPayload_Unknown,
    PktMonPayload_Ethernet,
    PktMonPayload_WiFi,
    PktMonPayload_IP,
    PktMonPayload_HTTP,
    PktMonPayload_TCP,
    PktMonPayload_UDP,
    PktMonPayload_ARP,
    PktMonPayload_ICMP,
    PktMonPayload_ESP,
    PktMonPayload_AH,
    PktMonPayload_L4Payload
} PKTMON_PACKET_TYPE;

#endif // (NTDDI_VERSION >= NTDDI_WIN10_RS5)

#include "PktMonClntNpik.h"
#include "PktMonNetEvtNpik.h"

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // _PKTMONNPIK_H_

