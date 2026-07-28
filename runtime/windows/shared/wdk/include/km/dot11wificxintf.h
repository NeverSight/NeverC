/*
    Copyright (c) Microsoft Corporation.  All rights reserved.

    Microsoft Confidential

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

    Module Name:
    dot11wificxintf.h

    Abstract:
    WDI based NDIS miniport driver interface (Specific to NetAdapter-based drivers)

    */
#ifndef __DOT11_WIFICX_INTF_H__
#define __DOT11_WIFICX_INTF_H__

#pragma once
#include <winapifamily.h>

#pragma region Desktop Family
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)



#ifdef __cplusplus
extern "C" {
#endif

//
// Message IDs
//
#define WDI_TASK_OPEN                                               1
#define WDI_TASK_CLOSE                                              2
#define WDI_SET_HOST_DETECT_ERROR                                   3
#define WDI_TASK_SCAN                                               4
#define WDI_TASK_P2P_DISCOVER                                       5
#define WDI_TASK_CONNECT                                            6
#define WDI_TASK_DOT11_RESET                                        7
#define WDI_TASK_DISCONNECT                                         8
#define WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME                      9
#define WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME                     10
#define WDI_TASK_SET_RADIO_STATE                                    11
#define WDI_TASK_CREATE_PORT                                        12
#define WDI_TASK_DELETE_PORT                                        13
#define WDI_TASK_START_AP                                           14
#define WDI_TASK_STOP_AP                                            15

#define WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE                       17
#define WDI_TASK_SET_READY_TO_RECEIVE_INDICATIONS                   18
#define WDI_SET_POWER_STATE                                         19

#define WDI_INDICATION_DISCONNECT_COMPLETE                          21
#define WDI_INDICATION_SET_RADIO_STATE_COMPLETE                     22
#define WDI_INDICATION_SET_READY_TO_RECEIVE_INDICATIONS_COMPLETE    23
#define WDI_SET_OPERATION_MODE                                      24
#define WDI_SET_P2P_ADDITIONAL_IE                                   25
#define WDI_SET_P2P_LISTEN_STATE                                    26

#define WDI_SET_PRIVACY_EXEMPTION_LIST                              28
#define WDI_SET_ADD_CIPHER_KEYS                                     29
#define WDI_SET_DELETE_CIPHER_KEYS                                  30
#define WDI_SET_DEFAULT_KEY_ID                                      31
#define WDI_SET_CONNECTION_QUALITY                                  32

#define WDI_GET_STATISTICS                                          34

#define WDI_SET_RECEIVE_PACKET_FILTER                               36
#define WDI_GET_ADAPTER_CAPABILITIES                                37

#define WDI_SET_NETWORK_LIST_OFFLOAD                                41

#define WDI_SET_RECEIVE_COALESCING                                  46
#define WDI_GET_BSS_ENTRY_LIST                                      47

#define WDI_INDICATION_DISASSOCIATION                               51
#define WDI_INDICATION_ROAMING_NEEDED                               52
#define WDI_INDICATION_LINK_STATE_CHANGE                            53
#define WDI_INDICATION_P2P_ACTION_FRAME_RECEIVED                    54
#define WDI_INDICATION_AP_ASSOCIATION_REQUEST_RECEIVED              55
#define WDI_INDICATION_NLO_DISCOVERY                                56
#define WDI_INDICATION_WAKE_REASON                                  57
#define WDI_INDICATION_PMKID_CANDIDATE_LIST_UPDATE                  58
#define WDI_INDICATION_TKIP_MIC_FAILURE                             59
#define WDI_INDICATION_SCAN_COMPLETE                                60
#define WDI_INDICATION_P2P_DISCOVERY_COMPLETE                       61
#define WDI_INDICATION_BSS_ENTRY_LIST                               62
#define WDI_INDICATION_DOT11_RESET_COMPLETE                         63
#define WDI_INDICATION_CONNECT_COMPLETE                             64
#define WDI_INDICATION_P2P_SEND_REQUEST_ACTION_FRAME_COMPLETE       65
#define WDI_INDICATION_P2P_SEND_RESPONSE_ACTION_FRAME_COMPLETE      66
#define WDI_INDICATION_RADIO_STATUS                                 67
#define WDI_INDICATION_CREATE_PORT_COMPLETE                         68
#define WDI_INDICATION_DELETE_PORT_COMPLETE                         69
#define WDI_INDICATION_START_AP_COMPLETE                            70
#define WDI_INDICATION_STOP_AP_COMPLETE                             71

#define WDI_INDICATION_SEND_AP_ASSOCIATION_RESPONSE_COMPLETE        73

#define WDI_INDICATION_ASSOCIATION_RESULT                           76
#define WDI_SET_AUTO_POWER_SAVE                                     77
#define WDI_GET_AUTO_POWER_SAVE                                     78
#define WDI_SET_ADD_WOL_PATTERN                                     79
#define WDI_SET_REMOVE_WOL_PATTERN                                  80
#define WDI_SET_MULTICAST_LIST                                      81
#define WDI_SET_ADD_PM_PROTOCOL_OFFLOAD                             82
#define WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD                          83
#define WDI_INDICATION_P2P_GROUP_OPERATING_CHANNEL                  84
#define WDI_SET_ADAPTER_CONFIGURATION                               85
#define WDI_GET_RECEIVE_COALESCING_MATCH_COUNT                      86
#define WDI_SET_CLEAR_RECEIVE_COALESCING                            87
#define WDI_GET_PM_PROTOCOL_OFFLOAD                                 88
#define WDI_SET_ADVERTISEMENT_INFORMATION                           89
#define WDI_TASK_CHANGE_OPERATION_MODE                              90
#define WDI_INDICATION_CHANGE_OPERATION_MODE_COMPLETE               91
#define WDI_TASK_DELETE_PEER_STATE                                  92
#define WDI_IHV_REQUEST                                             93
#define WDI_INDICATION_IHV_EVENT                                    94
#define WDI_INDICATION_OPEN_COMPLETE                                95
#define WDI_INDICATION_CLOSE_COMPLETE                               96
#define WDI_SET_FLUSH_BSS_ENTRY                                     97
#define WDI_INDICATION_ASSOCIATION_PARAMETERS_REQUEST               98
#define WDI_SET_ASSOCIATION_PARAMETERS                              99
#define WDI_TASK_ROAM                                               100
#define WDI_INDICATION_ROAM_COMPLETE                                101
#define WDI_TASK_SEND_REQUEST_ACTION_FRAME                          102
#define WDI_INDICATION_ACTION_FRAME_RECEIVED                        103
#define WDI_INDICATION_TASK_OFFLOAD_CURRENT_CONFIG                  104
#define WDI_TASK_SEND_RESPONSE_ACTION_FRAME                         105
#define WDI_INDICATION_SEND_REQUEST_ACTION_FRAME_COMPLETE           106
#define WDI_INDICATION_SEND_RESPONSE_ACTION_FRAME_COMPLETE          107
#define WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN                      108
#define WDI_TASK_IHV                                                109
#define WDI_INDICATION_IHV_TASK_REQUEST                             110
#define WDI_INDICATION_IHV_TASK_COMPLETE                            111

#define WDI_INDICATION_STOP_AP                                      115
#define WDI_INDICATION_CAN_SUSTAIN_AP                               116
#define WDI_SET_TCP_OFFLOAD_PARAMETERS                              117
#define WDI_TCP_RSC_STATISTICS                                      118
#define WDI_SET_P2P_START_BACKGROUND_DISCOVERY                      119
#define WDI_SET_P2P_STOP_BACKGROUND_DISCOVERY                       120
#define WDI_INDICATION_FIRMWARE_STALLED                             121
#define WDI_INDICATION_P2P_OPERATING_CHANNEL_ATTRIBUTES             122
#define WDI_SET_P2P_WPS_ENABLED                                     123
#define WDI_SET_ENCAPSULATION_OFFLOAD                               124
#define WDI_SET_END_DWELL_TIME                                      125
#define WDI_INDICATION_FT_ASSOC_PARAMS_NEEDED                       126
#define WDI_SET_FAST_BSS_TRANSITION_PARAMETERS                      127
#define WDI_SET_NEIGHBOR_REPORT_ENTRIES                             128
#define WDI_INDICATION_ADAPTER_STATE                                129
#define WDI_GET_SUPPORTED_DEVICE_SERVICES                           130
#define WDI_DEVICE_SERVICE_COMMAND                                  131
#define WDI_INDICATION_CIPHER_KEY_UPDATED                           132
#define WDI_INDICATION_DEVICE_SERVICE_EVENT                         133
#define WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED                       134
#define WDI_SET_SAE_AUTH_PARAMS                                     135
#define WDI_TASK_REQUEST_FTM                                        136
#define WDI_INDICATION_REQUEST_FTM_COMPLETE                         137
#define WDI_SET_LOCATION_PRIVACY                                    138
#define WDI_SET_OWE_DH_IE                                           139

// WiFiCx codes
#define WDI_INDICATION_SECONDARY_STA_CONNECTIVITY                   200

// Special codes
/*++
 Abort task request
 -- */
#define WDI_ABORT_TASK                                              75


//
// Type defs and constants
//
typedef struct _WDI_MAC_ADDRESS
{
    UINT8 Address[6];
} WDI_MAC_ADDRESS, *PWDI_MAC_ADDRESS;
typedef const struct _WDI_MAC_ADDRESS * PCWDI_MAC_ADDRESS;

typedef struct _MLO_LINK_INFO
{
    UINT32 LinkID;
    WDI_MAC_ADDRESS StaLinkAddress;
    WDI_MAC_ADDRESS APLinkAddress;
} MLO_LINK_INFO, *PMLO_LINK_INFO;

typedef UINT16 WDI_PORT_ID;

#define WDI_PORT_ID_ADAPTER ((UINT16)-1) // special port id to represent adapter


//
// Indications, except ones for task command completion indications, has this special transaction Id
//
#define WDI_TRANSACTION_ID_UNSOLICIT (0)

typedef struct _WDI_MESSAGE_HEADER
{
    // MessageId, MessageLength come from the NDIS_OID_REQUEST or NDIS_STATUS fields
    WDI_PORT_ID  PortId;
    UINT16 Reserved;
    NDIS_STATUS Status;         // Operation completion status for indication & response
    UINT32  TransactionId;
    UINT32  IhvSpecificId;
} WDI_MESSAGE_HEADER, *PWDI_MESSAGE_HEADER;


typedef enum _WDI_OPERATION_MODE
{
    WDI_OPERATION_MODE_STA = 0x01,
    // Reserved 0x02
    // Reserved 0x04
    WDI_OPERATION_MODE_P2P_DEVICE = 0x08,
    WDI_OPERATION_MODE_P2P_CLIENT = 0x10,
    WDI_OPERATION_MODE_P2P_GO = 0x20,
    // Reserved 0x40
}WDI_OPERATION_MODE;

typedef struct _WDI_P2P_SERVICE_NAME_HASH
{
    UINT8 Hash[6];
} WDI_P2P_SERVICE_NAME_HASH, *PWDI_P2P_SERVICE_NAME_HASH;
typedef const struct _WDI_P2P_SERVICE_NAME_HASH * PCWDI_P2P_SERVICE_NAME_HASH;

typedef enum _WDI_EXEMPTION_ACTION_TYPE
{
    WDI_EXEMPT_NO_EXEMPTION = 0,
    WDI_EXEMPT_ALWAYS = 1,
    WDI_EXEMPT_ON_KEY_MAPPING_KEY_UNAVAILABLE = 2,
} WDI_EXEMPTION_ACTION_TYPE;

typedef enum {
    DiagnoseLevelNone               =0,
    DiagnoseLevelHardwareRegisters  =1, // only device registers
    DiagnoseLevelFirmwareImageDump  =2, // + firmware image dump
    DiagnoseLevelDriverStateDump    =3  // + driver state dump
} eDiagnoseLevel;

#include <pshpack1.h>

// For 1.0 compliant drivers
#define WDI_VERSION_1_0                     ((1 << 16) | (0 << 8) | 0x0)

// For 1.0.1 compliant drivers
#define WDI_VERSION_1_0_1                   ((1 << 16) | (0 << 8) | 0x1)

// For 1.0.10 compliant drivers
#define WDI_VERSION_1_0_10                  ((1 << 16) | (0 << 8) | 0xA)

// For 1.0.20 compliant drivers
#define WDI_VERSION_1_0_20                  ((1 << 16) | (0 << 8) | 0x14)

// For 1.0.21 compliant drivers
#define WDI_VERSION_1_0_21                  ((1 << 16) | (0 << 8) | 0x15)

// For 1.1.0 compliant drivers
#define WDI_VERSION_1_1_0                   ((1 << 16) | (1 << 8) | 0x0)

// For 1.1.4 compliant drivers
#define WDI_VERSION_1_1_4                   ((1 << 16) | (1 << 8) | 0x4)

// For 1.1.5 compliant drivers
#define WDI_VERSION_1_1_5                   ((1 << 16) | (1 << 8) | 0x5)

// For 1.1.6 compliant drivers
#define WDI_VERSION_1_1_6                   ((1 << 16) | (1 << 8) | 0x6)

// For 1.1.7 compliant drivers
#define WDI_VERSION_1_1_7                   ((1 << 16) | (1 << 8) | 0x7)

// For 1.1.8 compliant drivers
#define WDI_VERSION_1_1_8                   ((1 << 16) | (1 << 8) | 0x8)

// For 1.1.9 compliant drivers
#define WDI_VERSION_1_1_9                   ((1 << 16) | (1 << 8) | 0x9)

// For 1.1.10 compliant drivers
#define WDI_VERSION_1_1_10                  ((1 << 16) | (1 << 8) | 0xa)

// For 1.1.11 compliant drivers
#define WDI_VERSION_1_1_11                  ((1 << 16) | (1 << 8) | 0xb)

// For 2.0.0 compliant drivers
#define WDI_VERSION_2_0_0                   ((2 << 16) | (0 << 8) | 0x0)

// For 2.0.1 compliant drivers
#define WDI_VERSION_2_0_1                   ((2 << 16) | (0 << 8) | 0x1)

// For 2.0.2 compliant drivers - Co release (21H2)
#define WDI_VERSION_2_0_2                   ((2 << 16) | (0 << 8) | 0x2)

// For 2.0.3 compliant drivers - Ni release (22H2)
#define WDI_VERSION_2_0_3                   ((2 << 16) | (0 << 8) | 0x3)

// For 2.0.4 compliant drivers
#define WDI_VERSION_2_0_4                   ((2 << 16) | (0 << 8) | 0x4)

// For 2.0.5 compliant drivers
#define WDI_VERSION_2_0_5                   ((2 << 16) | (0 << 8) | 0x5)

// For 2.0.6 compliant drivers
#define WDI_VERSION_2_0_6                   ((2 << 16) | (0 << 8) | 0x6)

// For 2.0.7 compliant drivers
#define WDI_VERSION_2_0_7                   ((2 << 16) | (0 << 8) | 0x7)

// For 2.0.8 compliant drivers
#define WDI_VERSION_2_0_8                   ((2 << 16) | (0 << 8) | 0x8)

// For 2.0.9 compliant drivers
#define WDI_VERSION_2_0_9                   ((2 << 16) | (0 << 8) | 0x9)

// For 2.0.10 compliant drivers
#define WDI_VERSION_2_0_10                  ((2 << 16) | (0 << 8) | 0xa)

// For 2.0.11 compliant drivers
#define WDI_VERSION_2_0_11                   ((2 << 16) | (0 << 8) | 0xb)

// For 2.0.12 compliant drivers - Ge release (24H2)
#define WDI_VERSION_2_0_12                   ((2 << 16) | (0 << 8) | 0xc)

// For 2.0.13 compliant drivers
#define WDI_VERSION_2_0_13                   ((2 << 16) | (0 << 8) | 0xd)

// For 2.0.14 compliant drivers
#define WDI_VERSION_2_0_14                   ((2 << 16) | (0 << 8) | 0xe)

#define WDI_VERSION_LATEST                  WDI_VERSION_2_0_14

//
// OIDS & Indications
//

#define WDI_OID_PREFIX 0x0e4400000U
#define WDI_INDICATION_PREFIX 0x40050000U

#define WDI_DEFINE_OID(_messageId)    \
    ((WDI_OID_PREFIX) | ((_messageId)))

#define WDI_DEFINE_INDICATION(_messageId) \
    ((WDI_INDICATION_PREFIX) | ((_messageId)))

/*
    Task IOCTL codes
*/
#define OID_WDI_TASK_OPEN    \
    WDI_DEFINE_OID(WDI_TASK_OPEN)

#define OID_WDI_TASK_CLOSE    \
    WDI_DEFINE_OID(WDI_TASK_CLOSE)

#define OID_WDI_TASK_SCAN    \
    WDI_DEFINE_OID(WDI_TASK_SCAN)

#define OID_WDI_TASK_P2P_DISCOVER    \
    WDI_DEFINE_OID(WDI_TASK_P2P_DISCOVER)

#define OID_WDI_TASK_CONNECT    \
    WDI_DEFINE_OID(WDI_TASK_CONNECT)

#define OID_WDI_TASK_DOT11_RESET    \
    WDI_DEFINE_OID(WDI_TASK_DOT11_RESET)

#define OID_WDI_TASK_DISCONNECT    \
    WDI_DEFINE_OID(WDI_TASK_DISCONNECT)

#define OID_WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME    \
    WDI_DEFINE_OID(WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME)

#define OID_WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME    \
    WDI_DEFINE_OID(WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME)

#define OID_WDI_TASK_SET_RADIO_STATE    \
    WDI_DEFINE_OID(WDI_TASK_SET_RADIO_STATE)

#define OID_WDI_TASK_CREATE_PORT    \
    WDI_DEFINE_OID(WDI_TASK_CREATE_PORT)

#define OID_WDI_TASK_DELETE_PORT    \
    WDI_DEFINE_OID(WDI_TASK_DELETE_PORT)

#define OID_WDI_TASK_START_AP    \
    WDI_DEFINE_OID(WDI_TASK_START_AP)

#define OID_WDI_TASK_STOP_AP    \
    WDI_DEFINE_OID(WDI_TASK_STOP_AP)

#define OID_WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE    \
    WDI_DEFINE_OID(WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE)

#define OID_WDI_SET_POWER_STATE    \
    WDI_DEFINE_OID(WDI_SET_POWER_STATE)

#define OID_WDI_SET_OPERATION_MODE    \
    WDI_DEFINE_OID(WDI_SET_OPERATION_MODE)

#define OID_WDI_SET_P2P_ADDITIONAL_IE    \
    WDI_DEFINE_OID(WDI_SET_P2P_ADDITIONAL_IE)

#define OID_WDI_SET_P2P_LISTEN_STATE    \
    WDI_DEFINE_OID(WDI_SET_P2P_LISTEN_STATE)

#define OID_WDI_SET_PRIVACY_EXEMPTION_LIST    \
    WDI_DEFINE_OID(WDI_SET_PRIVACY_EXEMPTION_LIST)

#define OID_WDI_SET_ADD_CIPHER_KEYS    \
    WDI_DEFINE_OID(WDI_SET_ADD_CIPHER_KEYS)

#define OID_WDI_SET_DELETE_CIPHER_KEYS    \
    WDI_DEFINE_OID(WDI_SET_DELETE_CIPHER_KEYS)

#define OID_WDI_SET_DEFAULT_KEY_ID    \
    WDI_DEFINE_OID(WDI_SET_DEFAULT_KEY_ID)

#define OID_WDI_SET_CONNECTION_QUALITY    \
    WDI_DEFINE_OID(WDI_SET_CONNECTION_QUALITY)

#define OID_WDI_GET_STATISTICS    \
    WDI_DEFINE_OID(WDI_GET_STATISTICS)

#define OID_WDI_SET_RECEIVE_PACKET_FILTER    \
    WDI_DEFINE_OID(WDI_SET_RECEIVE_PACKET_FILTER)

#define OID_WDI_GET_ADAPTER_CAPABILITIES    \
    WDI_DEFINE_OID(WDI_GET_ADAPTER_CAPABILITIES)

#define OID_WDI_SET_NETWORK_LIST_OFFLOAD    \
    WDI_DEFINE_OID(WDI_SET_NETWORK_LIST_OFFLOAD)

#define OID_WDI_SET_RECEIVE_COALESCING    \
    WDI_DEFINE_OID(WDI_SET_RECEIVE_COALESCING)

#define OID_WDI_GET_BSS_ENTRY_LIST    \
    WDI_DEFINE_OID(WDI_GET_BSS_ENTRY_LIST)

#define OID_WDI_SET_AUTO_POWER_SAVE    \
    WDI_DEFINE_OID(WDI_SET_AUTO_POWER_SAVE)

#define OID_WDI_GET_AUTO_POWER_SAVE    \
    WDI_DEFINE_OID(WDI_GET_AUTO_POWER_SAVE)

#define OID_WDI_SET_ADD_WOL_PATTERN    \
    WDI_DEFINE_OID(WDI_SET_ADD_WOL_PATTERN)

#define OID_WDI_SET_REMOVE_WOL_PATTERN    \
    WDI_DEFINE_OID(WDI_SET_REMOVE_WOL_PATTERN)

#define OID_WDI_SET_MULTICAST_LIST    \
    WDI_DEFINE_OID(WDI_SET_MULTICAST_LIST)

#define OID_WDI_SET_ADD_PM_PROTOCOL_OFFLOAD    \
    WDI_DEFINE_OID(WDI_SET_ADD_PM_PROTOCOL_OFFLOAD)

#define OID_WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD    \
    WDI_DEFINE_OID(WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD)

#define OID_WDI_SET_ADAPTER_CONFIGURATION    \
    WDI_DEFINE_OID(WDI_SET_ADAPTER_CONFIGURATION)

#define OID_WDI_GET_RECEIVE_COALESCING_MATCH_COUNT    \
    WDI_DEFINE_OID(WDI_GET_RECEIVE_COALESCING_MATCH_COUNT)

#define OID_WDI_SET_CLEAR_RECEIVE_COALESCING    \
    WDI_DEFINE_OID(WDI_SET_CLEAR_RECEIVE_COALESCING)

#define OID_WDI_GET_PM_PROTOCOL_OFFLOAD    \
    WDI_DEFINE_OID(WDI_GET_PM_PROTOCOL_OFFLOAD)

#define OID_WDI_SET_ADVERTISEMENT_INFORMATION    \
    WDI_DEFINE_OID(WDI_SET_ADVERTISEMENT_INFORMATION)

#define OID_WDI_TASK_CHANGE_OPERATION_MODE    \
    WDI_DEFINE_OID(WDI_TASK_CHANGE_OPERATION_MODE)

#define OID_WDI_TASK_DELETE_PEER_STATE    \
    WDI_DEFINE_OID(WDI_TASK_DELETE_PEER_STATE)

#define OID_WDI_IHV_REQUEST    \
    WDI_DEFINE_OID(WDI_IHV_REQUEST)

#define OID_WDI_TASK_ROAM    \
    WDI_DEFINE_OID(WDI_TASK_ROAM)

#define OID_WDI_SET_FLUSH_BSS_ENTRY    \
    WDI_DEFINE_OID(WDI_SET_FLUSH_BSS_ENTRY)

#define OID_WDI_SET_ASSOCIATION_PARAMETERS    \
    WDI_DEFINE_OID(WDI_SET_ASSOCIATION_PARAMETERS)

#define OID_WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN    \
    WDI_DEFINE_OID(WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN)

#define OID_WDI_TASK_SEND_REQUEST_ACTION_FRAME    \
    WDI_DEFINE_OID(WDI_TASK_SEND_REQUEST_ACTION_FRAME)

#define OID_WDI_TASK_SEND_RESPONSE_ACTION_FRAME    \
    WDI_DEFINE_OID(WDI_TASK_SEND_RESPONSE_ACTION_FRAME)

#define OID_WDI_SET_TCP_OFFLOAD_PARAMETERS    \
    WDI_DEFINE_OID(WDI_SET_TCP_OFFLOAD_PARAMETERS)

#define OID_WDI_TCP_RSC_STATISTICS    \
    WDI_DEFINE_OID(WDI_TCP_RSC_STATISTICS)

#define OID_WDI_SET_P2P_WPS_ENABLED \
    WDI_DEFINE_OID(WDI_SET_P2P_WPS_ENABLED)

#define OID_WDI_SET_P2P_START_BACKGROUND_DISCOVERY \
    WDI_DEFINE_OID(WDI_SET_P2P_START_BACKGROUND_DISCOVERY)

#define OID_WDI_SET_P2P_STOP_BACKGROUND_DISCOVERY \
    WDI_DEFINE_OID(WDI_SET_P2P_STOP_BACKGROUND_DISCOVERY)

#define OID_WDI_TASK_IHV \
    WDI_DEFINE_OID(WDI_TASK_IHV)

#define OID_WDI_SET_ENCAPSULATION_OFFLOAD \
    WDI_DEFINE_OID(WDI_SET_ENCAPSULATION_OFFLOAD)

#define OID_WDI_SET_END_DWELL_TIME \
    WDI_DEFINE_OID(WDI_SET_END_DWELL_TIME)

#define OID_WDI_SET_FAST_BSS_TRANSITION_PARAMETERS \
    WDI_DEFINE_OID(WDI_SET_FAST_BSS_TRANSITION_PARAMETERS)

#define OID_WDI_SET_NEIGHBOR_REPORT_ENTRIES \
    WDI_DEFINE_OID(WDI_SET_NEIGHBOR_REPORT_ENTRIES)

#define OID_WDI_GET_SUPPORTED_DEVICE_SERVICES \
    WDI_DEFINE_OID(WDI_GET_SUPPORTED_DEVICE_SERVICES)

#define OID_WDI_DEVICE_SERVICE_COMMAND \
    WDI_DEFINE_OID(WDI_DEVICE_SERVICE_COMMAND)

#define OID_WDI_SET_SAE_AUTH_PARAMS \
    WDI_DEFINE_OID(WDI_SET_SAE_AUTH_PARAMS)

#define OID_WDI_TASK_REQUEST_FTM \
    WDI_DEFINE_OID(WDI_TASK_REQUEST_FTM)


// Status indications

#define NDIS_STATUS_WDI_INDICATION_DISCONNECT_COMPLETE    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_DISCONNECT_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_SET_RADIO_STATE_COMPLETE    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_SET_RADIO_STATE_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_DISASSOCIATION    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_DISASSOCIATION)

#define NDIS_STATUS_WDI_INDICATION_ROAMING_NEEDED    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_ROAMING_NEEDED)

#define NDIS_STATUS_WDI_INDICATION_LINK_STATE_CHANGE    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_LINK_STATE_CHANGE)

#define NDIS_STATUS_WDI_INDICATION_P2P_ACTION_FRAME_RECEIVED    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_P2P_ACTION_FRAME_RECEIVED)

#define NDIS_STATUS_WDI_INDICATION_AP_ASSOCIATION_REQUEST_RECEIVED    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_AP_ASSOCIATION_REQUEST_RECEIVED)

#define NDIS_STATUS_WDI_INDICATION_NLO_DISCOVERY   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_NLO_DISCOVERY)

#define NDIS_STATUS_WDI_INDICATION_WAKE_REASON    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_WAKE_REASON)

#define NDIS_STATUS_WDI_INDICATION_PMKID_CANDIDATE_LIST_UPDATE    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_PMKID_CANDIDATE_LIST_UPDATE)

#define NDIS_STATUS_WDI_INDICATION_TKIP_MIC_FAILURE    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_TKIP_MIC_FAILURE)

#define NDIS_STATUS_WDI_INDICATION_ACTION_FRAME_RECEIVED    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_ACTION_FRAME_RECEIVED)

#define NDIS_STATUS_WDI_INDICATION_SEND_REQUEST_ACTION_FRAME_COMPLETE    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_SEND_REQUEST_ACTION_FRAME_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_SEND_RESPONSE_ACTION_FRAME_COMPLETE    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_SEND_RESPONSE_ACTION_FRAME_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_SCAN_COMPLETE    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_SCAN_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_P2P_DISCOVERY_COMPLETE    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_P2P_DISCOVERY_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_BSS_ENTRY_LIST    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_BSS_ENTRY_LIST)

#define NDIS_STATUS_WDI_INDICATION_DOT11_RESET_COMPLETE    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_DOT11_RESET_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_CONNECT_COMPLETE    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_CONNECT_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_P2P_SEND_REQUEST_ACTION_FRAME_COMPLETE    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_P2P_SEND_REQUEST_ACTION_FRAME_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_P2P_SEND_RESPONSE_ACTION_FRAME_COMPLETE    \
    WDI_DEFINE_INDICATION(WDI_INDICATION_P2P_SEND_RESPONSE_ACTION_FRAME_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_RADIO_STATUS   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_RADIO_STATUS)

#define NDIS_STATUS_WDI_INDICATION_CREATE_PORT_COMPLETE   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_CREATE_PORT_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_DELETE_PORT_COMPLETE   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_DELETE_PORT_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_START_AP_COMPLETE   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_START_AP_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_STOP_AP_COMPLETE   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_STOP_AP_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_STOP_AP   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_STOP_AP)

#define NDIS_STATUS_WDI_INDICATION_CAN_SUSTAIN_AP   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_CAN_SUSTAIN_AP)

#define NDIS_STATUS_WDI_INDICATION_SEND_AP_ASSOCIATION_RESPONSE_COMPLETE   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_SEND_AP_ASSOCIATION_RESPONSE_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_ASSOCIATION_RESULT   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_ASSOCIATION_RESULT)

#define NDIS_STATUS_WDI_INDICATION_P2P_GROUP_OPERATING_CHANNEL   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_P2P_GROUP_OPERATING_CHANNEL)

#define NDIS_STATUS_WDI_INDICATION_CHANGE_OPERATION_MODE_COMPLETE   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_CHANGE_OPERATION_MODE_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_IHV_EVENT   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_IHV_EVENT)

#define NDIS_STATUS_WDI_INDICATION_OPEN_COMPLETE   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_OPEN_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_CLOSE_COMPLETE   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_CLOSE_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_ROAM_COMPLETE   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_ROAM_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_ASSOCIATION_PARAMETERS_REQUEST   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_ASSOCIATION_PARAMETERS_REQUEST)

#define NDIS_STATUS_WDI_INDICATION_TASK_OFFLOAD_CURRENT_CONFIG   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_TASK_OFFLOAD_CURRENT_CONFIG)

#define NDIS_STATUS_WDI_TCP_RSC_STATISTICS   \
    WDI_DEFINE_INDICATION(WDI_TCP_RSC_STATISTICS)

#define NDIS_STATUS_WDI_INDICATION_P2P_OPERATING_CHANNEL_ATTRIBUTES   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_P2P_OPERATING_CHANNEL_ATTRIBUTES)

#define NDIS_STATUS_WDI_INDICATION_IHV_TASK_REQUEST   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_IHV_TASK_REQUEST)

#define NDIS_STATUS_WDI_INDICATION_IHV_TASK_COMPLETE   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_IHV_TASK_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_FIRMWARE_STALLED \
    WDI_DEFINE_INDICATION(WDI_INDICATION_FIRMWARE_STALLED)

#define NDIS_STATUS_WDI_INDICATION_FT_ASSOC_PARAMS_NEEDED \
    WDI_DEFINE_INDICATION(WDI_INDICATION_FT_ASSOC_PARAMS_NEEDED)

#define NDIS_STATUS_WDI_INDICATION_ADAPTER_STATE   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_ADAPTER_STATE)

#define NDIS_STATUS_WDI_INDICATION_CIPHER_KEY_UPDATED   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_CIPHER_KEY_UPDATED)

#define NDIS_STATUS_WDI_INDICATION_DEVICE_SERVICE_EVENT   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_DEVICE_SERVICE_EVENT)

#define NDIS_STATUS_WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED   \
    WDI_DEFINE_INDICATION(WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED)

#define NDIS_STATUS_WDI_INDICATION_REQUEST_FTM_COMPLETE \
    WDI_DEFINE_INDICATION(WDI_INDICATION_REQUEST_FTM_COMPLETE)

#define NDIS_STATUS_WDI_INDICATION_SECONDARY_STA_CONNECTIVITY \
    WDI_DEFINE_INDICATION(WDI_INDICATION_SECONDARY_STA_CONNECTIVITY)

// Special codes
/*++
 Abort task request
 -- */
#define OID_WDI_ABORT_TASK    \
    WDI_DEFINE_OID(WDI_ABORT_TASK)

#ifdef __cplusplus
}
#endif

#include <poppack.h>

#endif /* WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP) */
#pragma endregion

#endif  // __DOT11_WIFICX_INTF_H__

