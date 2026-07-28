/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Abstract:

   Public core datatypes shared by the pktmon stack

--*/

#ifdef _MSC_VER
#pragma once
#endif //_MSC_VER

#ifndef _PKTMONDEFK_H_ // include guard for 3rd party interop
#define _PKTMONDEFK_H_

#if (NTDDI_VERSION >= NTDDI_WIN10_RS5)

#ifdef __cplusplus
extern "C" {
#endif

#define PKTMON_MAX_PROPERTY_LENGTH_BYTES        256

#define PKTMON_MAC_ADDRESS_SIZE                 6
typedef UCHAR PKTMON_MAC_ADDRESS[PKTMON_MAC_ADDRESS_SIZE];

#define PKTMON_IPV4_ADDRESS_SIZE                4
#define PKTMON_IPV6_ADDRESS_SIZE                16

//
// IP Address (in network-byte order)
//
typedef union _PKTMON_IP_ADDRESS
{
    ULONG IPv4;
    UCHAR IPv4_bytes[PKTMON_IPV4_ADDRESS_SIZE];

    ULONGLONG IPv6[2];
    UCHAR IPv6_bytes[PKTMON_IPV6_ADDRESS_SIZE];
} PKTMON_IP_ADDRESS;

//
// Component types
//
typedef enum _PKTMON_COMPONENT_TYPE
{
    PktMonComp_Ndis             = 1,
    PktMonComp_Miniport         = 2,
    PktMonComp_Filter           = 3,
    PktMonComp_Protocol         = 4,
    PktMonComp_VmsVmNic         = 5,
    PktMonComp_VmsMiniport      = 6,
    PktMonComp_VmsExtMiniport   = 7,
    PktMonComp_VmsProtocolNic   = 8,
    PktMonComp_NetVsc           = 9,
    PktMonComp_HTTP             = 10,
    PktMonComp_IpInterface      = 11,
    PktMonComp_Slbmux           = 12,
    PktMonComp_Ipsec            = 13,
    PktMonComp_NetCx            = 14,
    PktMonComp_HTTPMessage      = 15,
} PKTMON_COMPONENT_TYPE;

//
// Component property Id
//
typedef enum _PKTMON_COMPONENT_PROPERTY_ID
{
    PktMonCompProp_IfIndex          = 1,
    PktMonCompProp_MiniportIfIndex  = 2,
    PktMonCompProp_LowerIfIndex     = 3,
    PktMonCompProp_IfGuid           = 4,
    PktMonCompProp_NdisMedium       = 5,
    PktMonCompProp_PhysAddress      = 6,
    PktMonCompProp_EtherType        = 7,
    PktMonCompProp_OptDataPath      = 8,
    PktMonCompProp_NdisObject       = 9,
    PktMonCompProp_VMSwitchName     = 10,
    PktMonCompProp_VmsExtIfIndex    = 11,
    PktMonCompProp_LowestIfIndex    = 12,
    PktMonCompProp_IpAddress        = 13,
    PktMonCompProp_IpIfIndex        = 14,
    PktMonCompProp_Vsid             = 15,
    PktMonCompProp_Vlan             = 16,
    PktMonCompProp_CompartmentId    = 17,

    // Must be last
    PktMonCompProp_Max,
} PKTMON_COMPONENT_PROPERTY_ID;

//
// Packet capture type
//
typedef enum _PKTMON_CAPTURE_TYPE
{
    PktMonCapture_All = 1,
    PktMonCapture_Flow,
    PktMonCapture_Drop,
    PktMonCapture_None
} PKTMON_CAPTURE_TYPE;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // (NTDDI_VERSION >= NTDDI_WIN10_RS5)

#endif // _PKTMONDEFK_H_

