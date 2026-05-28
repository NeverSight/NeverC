/*++ BUILD Version: 0000    // Increment this if a change has global effects

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    ndiswan.h

Abstract:

    Main header file for the wan wrapper


Revision History:

--*/


#ifndef _NDIS_WAN_
#define _NDIS_WAN_

#pragma once

//
// Begin definitions for WANs
//

//
// Bit field set int he Reserved field for
// NdisRegisterMiniport or passed in NdisRegisterSpecial
//

#define NDIS_USE_WAN_WRAPPER            0x00000001

#define NDIS_STATUS_TAPI_INDICATION ((NDIS_STATUS)0x40010080L)


//
// NDIS WAN Framing bits
//
#define RAS_FRAMING                     0x00000001
#define RAS_COMPRESSION                 0x00000002

#define ARAP_V1_FRAMING                 0x00000004
#define ARAP_V2_FRAMING                 0x00000008
#define ARAP_FRAMING                    (ARAP_V1_FRAMING | ARAP_V2_FRAMING)

#define PPP_MULTILINK_FRAMING           0x00000010
#define PPP_SHORT_SEQUENCE_HDR_FORMAT   0x00000020
#define PPP_MC_MULTILINK_FRAMING        0x00000040

#define PPP_FRAMING                     0x00000100
#define PPP_COMPRESS_ADDRESS_CONTROL    0x00000200
#define PPP_COMPRESS_PROTOCOL_FIELD     0x00000400
#define PPP_ACCM_SUPPORTED              0x00000800

#define SLIP_FRAMING                    0x00001000
#define SLIP_VJ_COMPRESSION             0x00002000
#define SLIP_VJ_AUTODETECT              0x00004000

#define MEDIA_NRZ_ENCODING              0x00010000
#define MEDIA_NRZI_ENCODING             0x00020000
#define MEDIA_NLPID                     0x00040000

#define RFC_1356_FRAMING                0x00100000
#define RFC_1483_FRAMING                0x00200000
#define RFC_1490_FRAMING                0x00400000
#define LLC_ENCAPSULATION               0x00800000

#define SHIVA_FRAMING                   0x01000000
#define NBF_PRESERVE_MAC_ADDRESS        0x01000000
#define PASS_THROUGH_MODE               0x10000000

#ifndef _WAN50_
#define RAW_PASS_THROUGH_MODE           0x20000000
#endif

#define TAPI_PROVIDER                   0x80000000

//
// NDIS WAN Information structures used
// by NDIS 3.1 Wan Miniport drivers
//
typedef struct _NDIS_WAN_INFO
{
    __out ULONG                  MaxFrameSize;
    __out ULONG                  MaxTransmit;
    __out ULONG                  HeaderPadding;
    __out ULONG                  TailPadding;
    __out ULONG                  Endpoints;
    __out UINT                   MemoryFlags;
    __out NDIS_PHYSICAL_ADDRESS  HighestAcceptableAddress;
    __out ULONG                  FramingBits;
    __out ULONG                  DesiredACCM;
} NDIS_WAN_INFO, *PNDIS_WAN_INFO;

typedef struct _NDIS_WAN_SET_LINK_INFO
{
    __in  NDIS_HANDLE     NdisLinkHandle;
    __in  ULONG           MaxSendFrameSize;
    __in  ULONG           MaxRecvFrameSize;
          ULONG           HeaderPadding;
          ULONG           TailPadding;
    __in  ULONG           SendFramingBits;
    __in  ULONG           RecvFramingBits;
    __in  ULONG           SendCompressionBits;
    __in  ULONG           RecvCompressionBits;
    __in  ULONG           SendACCM;
    __in  ULONG           RecvACCM;
} NDIS_WAN_SET_LINK_INFO, *PNDIS_WAN_SET_LINK_INFO;

typedef struct _NDIS_WAN_GET_LINK_INFO {
    __in  NDIS_HANDLE     NdisLinkHandle;
    __out ULONG           MaxSendFrameSize;
    __out ULONG           MaxRecvFrameSize;
    __out ULONG           HeaderPadding;
    __out ULONG           TailPadding;
    __out ULONG           SendFramingBits;
    __out ULONG           RecvFramingBits;
    __out ULONG           SendCompressionBits;
    __out ULONG           RecvCompressionBits;
    __out ULONG           SendACCM;
    __out ULONG           RecvACCM;
} NDIS_WAN_GET_LINK_INFO, *PNDIS_WAN_GET_LINK_INFO;

//
// NDIS WAN Bridging Options
//
#define BRIDGING_FLAG_LANFCS            0x00000001
#define BRIDGING_FLAG_LANID             0x00000002
#define BRIDGING_FLAG_PADDING           0x00000004

//
// NDIS WAN Bridging Capabilities
//
#define BRIDGING_TINYGRAM               0x00000001
#define BRIDGING_LANID                  0x00000002
#define BRIDGING_NO_SPANNING_TREE       0x00000004
#define BRIDGING_8021D_SPANNING_TREE    0x00000008
#define BRIDGING_8021G_SPANNING_TREE    0x00000010
#define BRIDGING_SOURCE_ROUTING         0x00000020
#define BRIDGING_DEC_LANBRIDGE          0x00000040

//
// NDIS WAN Bridging Type
//
#define BRIDGING_TYPE_RESERVED          0x00000001
#define BRIDGING_TYPE_8023_CANON        0x00000002
#define BRIDGING_TYPE_8024_NO_CANON     0x00000004
#define BRIDGING_TYPE_8025_NO_CANON     0x00000008
#define BRIDGING_TYPE_FDDI_NO_CANON     0x00000010
#define BRIDGING_TYPE_8024_CANON        0x00000400
#define BRIDGING_TYPE_8025_CANON        0x00000800
#define BRIDGING_TYPE_FDDI_CANON        0x00001000

typedef struct _NDIS_WAN_GET_BRIDGE_INFO
{
    __in  NDIS_HANDLE     NdisLinkHandle;
    __out USHORT          LanSegmentNumber;
    __out UCHAR           BridgeNumber;
    __out UCHAR           BridgingOptions;
    __out ULONG           BridgingCapabilities;
    __out UCHAR           BridgingType;
    __out UCHAR           MacBytes[6];
} NDIS_WAN_GET_BRIDGE_INFO, *PNDIS_WAN_GET_BRIDGE_INFO;

typedef struct _NDIS_WAN_SET_BRIDGE_INFO
{
    __in  NDIS_HANDLE     NdisLinkHandle;
    __in  USHORT          LanSegmentNumber;
    __in  UCHAR           BridgeNumber;
    __in  UCHAR           BridgingOptions;
    __in  ULONG           BridgingCapabilities;
    __in  UCHAR           BridgingType;
    __in  UCHAR           MacBytes[6];
} NDIS_WAN_SET_BRIDGE_INFO, *PNDIS_WAN_SET_BRIDGE_INFO;

//
// NDIS WAN Compression Information
//

//
// Define MSCompType bit field, 0 disables all
//
#define NDISWAN_COMPRESSION             0x00000001
#define NDISWAN_ENCRYPTION              0x00000010
#define NDISWAN_40_ENCRYPTION           0x00000020
#define NDISWAN_128_ENCRYPTION          0x00000040
#define NDISWAN_56_ENCRYPTION           0x00000080
#define NDISWAN_HISTORY_LESS            0x01000000

//
// Define CompType codes
//
#define COMPTYPE_OUI                    0
#define COMPTYPE_NT31RAS                254
#define COMPTYPE_NONE                   255


typedef struct _NDIS_WAN_COMPRESS_INFO
{
    UCHAR   SessionKey[8];
    ULONG   MSCompType;

    // Fields above indicate NDISWAN capabilities.
    // Fields below indicate MAC-specific capabilities.

    UCHAR   CompType;
    USHORT  CompLength;

    union
    {
        struct
        {
            UCHAR   CompOUI[3];
            UCHAR   CompSubType;
            UCHAR   CompValues[32];
        } Proprietary;

        struct
        {
            UCHAR   CompValues[32];
        } Public;
    };
} NDIS_WAN_COMPRESS_INFO;

typedef NDIS_WAN_COMPRESS_INFO UNALIGNED *PNDIS_WAN_COMPRESS_INFO;

typedef struct _NDIS_WAN_GET_COMP_INFO
{
    __in  NDIS_HANDLE             NdisLinkHandle;
    __out NDIS_WAN_COMPRESS_INFO  SendCapabilities;
    __out NDIS_WAN_COMPRESS_INFO  RecvCapabilities;
} NDIS_WAN_GET_COMP_INFO, *PNDIS_WAN_GET_COMP_INFO;

typedef struct _NDIS_WAN_SET_COMP_INFO
{
    __in  NDIS_HANDLE             NdisLinkHandle;
    __in  NDIS_WAN_COMPRESS_INFO  SendCapabilities;
    __in  NDIS_WAN_COMPRESS_INFO  RecvCapabilities;
} NDIS_WAN_SET_COMP_INFO, *PNDIS_WAN_SET_COMP_INFO;

//
// NDIS WAN Statistics Information
//

typedef struct _NDIS_WAN_GET_STATS_INFO
{
    __in  NDIS_HANDLE NdisLinkHandle;
    __out ULONG       BytesSent;
    __out ULONG       BytesRcvd;
    __out ULONG       FramesSent;
    __out ULONG       FramesRcvd;
    __out ULONG       CRCErrors;                      // Serial-like info only
    __out ULONG       TimeoutErrors;                  // Serial-like info only
    __out ULONG       AlignmentErrors;                // Serial-like info only
    __out ULONG       SerialOverrunErrors;            // Serial-like info only
    __out ULONG       FramingErrors;                  // Serial-like info only
    __out ULONG       BufferOverrunErrors;            // Serial-like info only
    __out ULONG       BytesTransmittedUncompressed;   // Compression info only
    __out ULONG       BytesReceivedUncompressed;      // Compression info only
    __out ULONG       BytesTransmittedCompressed;     // Compression info only
    __out ULONG       BytesReceivedCompressed;        // Compression info only
    __out ULONG       TunnelPacketsRecieved;
    __out ULONG       TunnelRecievePacketsPending;
    __out ULONG       TunnelPacketsIndicatedUp;
    __out ULONG       TunnelRecievePacketsRejected;
    __out ULONG       TunnelPacketsSent;
    __out ULONG       TunnelPacketsSentComplete;
    __out ULONG       TunnelTransmitPacketsPending;
    __out ULONG       TunnelPacketsTransmitError;
    __out ULONG       TunnelPacketsSentError;
    __out ULONG       TunnelTransmitPacketsRejected;
    __out ULONG       TunnelAcksSent;    
    __out ULONG       TunnelAcksSentComplete;
    __out ULONG       TunnelGeneric1;
    __out ULONG       TunnelGeneric2;
    __out ULONG       TunnelGeneric3;
} NDIS_WAN_GET_STATS_INFO, *PNDIS_WAN_GET_STATS_INFO;

#define NdisMWanInitializeWrapper(NdisWrapperHandle,                                \
                                  SystemSpecific1,                                  \
                                  SystemSpecific2,                                  \
                                  SystemSpecific3)                                  \
{                                                                                   \
    NdisMInitializeWrapper(NdisWrapperHandle,                                       \
                            SystemSpecific1,                                        \
                            SystemSpecific2,                                        \
                            SystemSpecific3);                                       \
}

typedef struct _NDIS_MAC_LINE_UP
{
    __in  ULONG               LinkSpeed;
    __in  NDIS_WAN_QUALITY    Quality;
    __in  USHORT              SendWindow;
    __in  NDIS_HANDLE         ConnectionWrapperID;
    __in  NDIS_HANDLE         NdisLinkHandle;
    __out NDIS_HANDLE         NdisLinkContext;
} NDIS_MAC_LINE_UP, *PNDIS_MAC_LINE_UP;


typedef struct _NDIS_MAC_LINE_DOWN
{
    __in  NDIS_HANDLE      NdisLinkContext;
} NDIS_MAC_LINE_DOWN, *PNDIS_MAC_LINE_DOWN;


//
// These are the error values that can be indicated by the driver.
// This bit field is set when calling NdisIndicateStatus.
//
#define WAN_ERROR_CRC               ((ULONG)0x00000001)
#define WAN_ERROR_FRAMING           ((ULONG)0x00000002)
#define WAN_ERROR_HARDWAREOVERRUN   ((ULONG)0x00000004)
#define WAN_ERROR_BUFFEROVERRUN     ((ULONG)0x00000008)
#define WAN_ERROR_TIMEOUT           ((ULONG)0x00000010)
#define WAN_ERROR_ALIGNMENT         ((ULONG)0x00000020)

typedef struct _NDIS_MAC_FRAGMENT
{
    __in  NDIS_HANDLE     NdisLinkContext;
    __in  ULONG           Errors;
} NDIS_MAC_FRAGMENT, *PNDIS_MAC_FRAGMENT;

//
// NDIS WAN Information structures used
// by NDIS 5.0 Miniport drivers
//

//
// Defines for the individual fields are the
// same as for NDIS 3.x/4.x Wan miniports.
//
// See the DDK.
//

//
// Information that applies to all VC's on
// this adapter.
//
// OID: OID_WAN_CO_GET_INFO
//
typedef struct _NDIS_WAN_CO_INFO {
    __out ULONG           MaxFrameSize;
    __out ULONG           MaxSendWindow;
    __out ULONG           FramingBits;
    __out ULONG           DesiredACCM;
} NDIS_WAN_CO_INFO, *PNDIS_WAN_CO_INFO;

//
// Set VC specific PPP framing information.
//
// OID: OID_WAN_CO_SET_LINK_INFO
//
typedef struct _NDIS_WAN_CO_SET_LINK_INFO {
    __in  ULONG           MaxSendFrameSize;
    __in  ULONG           MaxRecvFrameSize;
    __in  ULONG           SendFramingBits;
    __in  ULONG           RecvFramingBits;
    __in  ULONG           SendCompressionBits;
    __in  ULONG           RecvCompressionBits;
    __in  ULONG           SendACCM;
    __in  ULONG           RecvACCM;
} NDIS_WAN_CO_SET_LINK_INFO, *PNDIS_WAN_CO_SET_LINK_INFO;

//
// Get VC specific PPP framing information.
//
// OID: OID_WAN_CO_GET_LINK_INFO
//
typedef struct _NDIS_WAN_CO_GET_LINK_INFO {
    __out ULONG           MaxSendFrameSize;
    __out ULONG           MaxRecvFrameSize;
    __out ULONG           SendFramingBits;
    __out ULONG           RecvFramingBits;
    __out ULONG           SendCompressionBits;
    __out ULONG           RecvCompressionBits;
    __out ULONG           SendACCM;
    __out ULONG           RecvACCM;
} NDIS_WAN_CO_GET_LINK_INFO, *PNDIS_WAN_CO_GET_LINK_INFO;

//
// Get VC specific PPP compression information
//
// OID: OID_WAN_CO_GET_COMP_INFO
//
typedef struct _NDIS_WAN_CO_GET_COMP_INFO {
    __out NDIS_WAN_COMPRESS_INFO  SendCapabilities;
    __out NDIS_WAN_COMPRESS_INFO  RecvCapabilities;
} NDIS_WAN_CO_GET_COMP_INFO, *PNDIS_WAN_CO_GET_COMP_INFO;


//
// Set VC specific PPP compression information
//
// OID: OID_WAN_CO_SET_COMP_INFO
//
typedef struct _NDIS_WAN_CO_SET_COMP_INFO {
    __in  NDIS_WAN_COMPRESS_INFO  SendCapabilities;
    __in  NDIS_WAN_COMPRESS_INFO  RecvCapabilities;
} NDIS_WAN_CO_SET_COMP_INFO, *PNDIS_WAN_CO_SET_COMP_INFO;


//
// Get VC specific statistics
//
// OID: OID_WAN_CO_GET_STATS_INFO
//
typedef struct _NDIS_WAN_CO_GET_STATS_INFO {
    __out ULONG       BytesSent;
    __out ULONG       BytesRcvd;
    __out ULONG       FramesSent;
    __out ULONG       FramesRcvd;
    __out ULONG       CRCErrors;                      // Serial-like info only
    __out ULONG       TimeoutErrors;                  // Serial-like info only
    __out ULONG       AlignmentErrors;                // Serial-like info only
    __out ULONG       SerialOverrunErrors;            // Serial-like info only
    __out ULONG       FramingErrors;                  // Serial-like info only
    __out ULONG       BufferOverrunErrors;            // Serial-like info only
    __out ULONG       BytesTransmittedUncompressed;   // Compression info only
    __out ULONG       BytesReceivedUncompressed;      // Compression info only
    __out ULONG       BytesTransmittedCompressed;     // Compression info only
    __out ULONG       BytesReceivedCompressed;        // Compression info only
    __out ULONG       TunnelPacketsRecieved;
    __out ULONG       TunnelRecievePacketsPending;
    __out ULONG       TunnelPacketsIndicatedUp;
    __out ULONG       TunnelRecievePacketsRejected;
    __out ULONG       TunnelPacketsSent;
    __out ULONG       TunnelPacketsSentComplete;
    __out ULONG       TunnelTransmitPacketsPending;
    __out ULONG       TunnelPacketsTransmitError;
    __out ULONG       TunnelPacketsSentError;
    __out ULONG       TunnelTransmitPacketsRejected;
    __out ULONG       TunnelAcksSent;    
    __out ULONG       TunnelAcksSentComplete;
    __out ULONG       TunnelGeneric1;
    __out ULONG       TunnelGeneric2;
    __out ULONG       TunnelGeneric3;
} NDIS_WAN_CO_GET_STATS_INFO, *PNDIS_WAN_CO_GET_STATS_INFO;

//
// Used to notify NdisWan of Errors.  See error
// bit mask in ndiswan.h
//
// NDIS_STATUS: NDIS_STATUS_WAN_CO_FRAGMENT
//
typedef struct _NDIS_WAN_CO_FRAGMENT {
    __in  ULONG           Errors;
} NDIS_WAN_CO_FRAGMENT, *PNDIS_WAN_CO_FRAGMENT;

//
// Used to notify NdisWan of changes in link speed and
// send window.  Can be given at any time.  NdisWan will honor
// any send window (even zero).  NdisWan will default zero
// TransmitSpeed/ReceiveSpeed settings to 28.8Kbs.
//
// NDIS_STATUS: NDIS_STATUS_WAN_CO_LINKPARAMS
//
typedef struct _WAN_CO_LINKPARAMS {
    ULONG   TransmitSpeed;              // Transmit speed of the VC in Bytes/sec
    ULONG   ReceiveSpeed;               // Receive speed of the VC in Bytes/sec
    ULONG   SendWindow;                 // Current send window for the VC
} WAN_CO_LINKPARAMS, *PWAN_CO_LINKPARAMS;


//
// Used to notify NdisWan of changes in link speed and
// send window.  Can be given at any time.  NdisWan will honor
// any send window (even zero).  NdisWan will default zero
// TransmitSpeed/ReceiveSpeed settings to 28.8Kbs.
//
// NDIS_STATUS: NDIS_STATUS_WAN_CO_MTULINKPARAMS
//

typedef struct _WAN_CO_MTULINKPARAMS {
    ULONG   Version;                    // Specifies the version: In future it can be extended using this version field: Current Supported version: 1
    ULONG   TransmitSpeed;              // Transmit speed of the VC in Bytes/sec
    ULONG   ReceiveSpeed;               // Receive speed of the VC in Bytes/sec
    ULONG   SendWindow;                 // Current send window for the VC
    ULONG   MTU;                        // MTU for the current Vc
} WAN_CO_MTULINKPARAMS, *PWAN_CO_MTULINKPARAMS;

#endif  // _NDIS_WAN

