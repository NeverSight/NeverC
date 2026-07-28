// Copyright (C) Microsoft Corporation. All rights reserved.
#pragma once

//
// flags used in NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES->SupportedStatistics
//

#define NDIS_STATISTICS_XMIT_OK_SUPPORTED                       0x00000001
#define NDIS_STATISTICS_RCV_OK_SUPPORTED                        0x00000002
#define NDIS_STATISTICS_XMIT_ERROR_SUPPORTED                    0x00000004
#define NDIS_STATISTICS_RCV_ERROR_SUPPORTED                     0x00000008
#define NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED                 0x00000010
#define NDIS_STATISTICS_DIRECTED_BYTES_XMIT_SUPPORTED           0x00000020
#define NDIS_STATISTICS_DIRECTED_FRAMES_XMIT_SUPPORTED          0x00000040
#define NDIS_STATISTICS_MULTICAST_BYTES_XMIT_SUPPORTED          0x00000080
#define NDIS_STATISTICS_MULTICAST_FRAMES_XMIT_SUPPORTED         0x00000100
#define NDIS_STATISTICS_BROADCAST_BYTES_XMIT_SUPPORTED          0x00000200
#define NDIS_STATISTICS_BROADCAST_FRAMES_XMIT_SUPPORTED         0x00000400
#define NDIS_STATISTICS_DIRECTED_BYTES_RCV_SUPPORTED            0x00000800
#define NDIS_STATISTICS_DIRECTED_FRAMES_RCV_SUPPORTED           0x00001000
#define NDIS_STATISTICS_MULTICAST_BYTES_RCV_SUPPORTED           0x00002000
#define NDIS_STATISTICS_MULTICAST_FRAMES_RCV_SUPPORTED          0x00004000
#define NDIS_STATISTICS_BROADCAST_BYTES_RCV_SUPPORTED           0x00008000
#define NDIS_STATISTICS_BROADCAST_FRAMES_RCV_SUPPORTED          0x00010000
#define NDIS_STATISTICS_RCV_CRC_ERROR_SUPPORTED                 0x00020000
#define NDIS_STATISTICS_TRANSMIT_QUEUE_LENGTH_SUPPORTED         0x00040000
#define NDIS_STATISTICS_BYTES_RCV_SUPPORTED                     0x00080000
#define NDIS_STATISTICS_BYTES_XMIT_SUPPORTED                    0x00100000
#define NDIS_STATISTICS_RCV_DISCARDS_SUPPORTED                  0x00200000
#define NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED                0x00400000
#define NDIS_STATISTICS_XMIT_DISCARDS_SUPPORTED                 0x08000000

#define NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1     1

#if (NDIS_SUPPORT_NDIS620)
#define NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2     2
#endif

typedef struct _NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES
{
    NDIS_OBJECT_HEADER              Header;
    ULONG                           Flags;
    NDIS_MEDIUM                     MediaType;
    NDIS_PHYSICAL_MEDIUM            PhysicalMediumType;
    ULONG                           MtuSize;
    ULONG64                         MaxXmitLinkSpeed;
    ULONG64                         XmitLinkSpeed;
    ULONG64                         MaxRcvLinkSpeed;
    ULONG64                         RcvLinkSpeed;
    NDIS_MEDIA_CONNECT_STATE        MediaConnectState;
    NDIS_MEDIA_DUPLEX_STATE         MediaDuplexState;
    ULONG                           LookaheadSize;
    PNDIS_PNP_CAPABILITIES          PowerManagementCapabilities; // 6.20 drivers must use PowerManagementCapabilitiesEx
    ULONG                           MacOptions;
    ULONG                           SupportedPacketFilters;
    ULONG                           MaxMulticastListSize;
    USHORT                          MacAddressLength;
    UCHAR                           PermanentMacAddress[NDIS_MAX_PHYS_ADDRESS_LENGTH];
    UCHAR                           CurrentMacAddress[NDIS_MAX_PHYS_ADDRESS_LENGTH];
    PNDIS_RECEIVE_SCALE_CAPABILITIES RecvScaleCapabilities;
    NET_IF_ACCESS_TYPE              AccessType; // NET_IF_ACCESS_BROADCAST for a typical ethernet adapter
    NET_IF_DIRECTION_TYPE           DirectionType; // NET_IF_DIRECTION_SENDRECEIVE for a typical ethernet adapter
    NET_IF_CONNECTION_TYPE          ConnectionType; // IF_CONNECTION_DEDICATED for a typical ethernet adapter
    NET_IFTYPE                      IfType; // IF_TYPE_ETHERNET_CSMACD for a typical ethernet adapter (regardless of speed)
    BOOLEAN                         IfConnectorPresent; // RFC 2665 TRUE if physical adapter
    ULONG                           SupportedStatistics; // use NDIS_STATISTICS_XXXX_SUPPORTED
    ULONG                           SupportedPauseFunctions; // IEEE 802.3 37.2.1
    ULONG                           DataBackFillSize;
    ULONG                           ContextBackFillSize;
    _Field_size_bytes_(SupportedOidListLength)
    PNDIS_OID                       SupportedOidList;
    ULONG                           SupportedOidListLength;
    ULONG                           AutoNegotiationFlags;
#if (NDIS_SUPPORT_NDIS620)
    PNDIS_PM_CAPABILITIES           PowerManagementCapabilitiesEx;
#endif
} NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES, *PNDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;

#define NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES, AutoNegotiationFlags)

#if (NDIS_SUPPORT_NDIS620)
#define NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES, PowerManagementCapabilitiesEx)
#endif