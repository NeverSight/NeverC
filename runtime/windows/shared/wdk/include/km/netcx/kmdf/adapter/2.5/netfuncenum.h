/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

_WdfVersionBuild_

Module Name: NetFuncEnum.h

Abstract:
    Generated an enum of all WDF API functions

Environment:
    kernel mode only

    Warning: manual changes to this file will be lost.
--*/

#ifndef _NETFUNCENUM_2_5_H_
#define _NETFUNCENUM_2_5_H_

extern PNET_DRIVER_GLOBALS NetDriverGlobals;


//
//
// The first version of the framework that added the support
// for client version to be higher than the framework version
//
#define NET_FIRST_VERSION_SUPPORTING_CLIENT_VERSION_HIGHER_THAN_FRAMEWORK 4

//
// The function count from the first framework which added the support.
// Any function with index less than the count is always available
//
#define NET_ALWAYS_AVAILABLE_FUNCTION_COUNT  131

//
// Validate NET_MINIMUM_VERSION_REQUIRED falls into the correct range
//
#if defined(NET_MINIMUM_VERSION_REQUIRED)

    #if NET_MINIMUM_VERSION_REQUIRED < NET_FIRST_VERSION_SUPPORTING_CLIENT_VERSION_HIGHER_THAN_FRAMEWORK
    #error NET_MINIMUM_VERSION_REQUIRED < 4
    #endif

    #if NET_MINIMUM_VERSION_REQUIRED > NET_VERSION_MINOR
    #error NET_MINIMUM_VERSION_REQUIRED > NET_VERSION_MINOR
    #endif

#endif


//
// All functions/structures are always available in following two cases:
//
//  1) Building framework itself (which defines NET_EVERYTHING_ALWAYS_AVAILABLE)
//
//  2) Traditional client drivers who are not aware of the new feature
//     "client version can be higher than framework's" and thus implies
//     NET_VERSION_MINOR == NET_MINIMUM_VERSION_REQUIRED
//
#if defined(NET_MINIMUM_VERSION_REQUIRED) && (NET_VERSION_MINOR == NET_MINIMUM_VERSION_REQUIRED)

    #if !defined(NET_EVERYTHING_ALWAYS_AVAILABLE)
    #define NET_EVERYTHING_ALWAYS_AVAILABLE
    #endif

#endif


//
// private:
//
// TRUE - the client driver's target version is higher than the current framework version.
//     IS_FUNCTION_AVAILABLE() will compare function index agains function count to determine
//     whether the function is usable on the platform. The same for IS_FIELD_AVAILABLE().
//
// FALSE - the client driver is running on a new framework. All functions/Structures
//     from the target framework are always available
//
extern BOOLEAN NetClientVersionHigherThanFramework;

//
// private:
//
// Only valid when NetClientVersionHigherThanFramework is TRUE.
//
// Hold the count of usable functions from the framework. Used by IS_FUNCTION_AVAILABLE
// to determine whether a function is available on the platform.
//
extern ULONG   NetFunctionCount;

//
// private:
//
// Only valid when NetClientVersionHigherThanFramework is TRUE.
//
// Hold the count of usable structures from the framework. Used by IS_FIELD_AVAILABLE
// to determine whether a field in a structure is available on the platform.
//
extern ULONG   NetStructureCount;
extern WDF_STRUCT_INFO NetStructures;


//
// private:
//
// Only valid when NetClientVersionHigherThanFramework is TRUE.
//
// Hold the name of the Framework Extension.
//
extern PCWSTR  NetFrameworkExtensionName;


#ifndef NET_STUB

    #ifndef NET_VERSION_MAJOR
    #error  NET_VERSION_MAJOR is not defined
    #endif

    //
    // "Version Minor(Target version)" controls which version of public header files to include
    //
    #ifndef NET_VERSION_MINOR
    #error  NET_VERSION_MINOR is not defined
    #endif

    //
    // "Version Minor (Minimum Required)" controls the oldest version of framework to load the driver
    //
    #ifndef NET_MINIMUM_VERSION_REQUIRED
    #define NET_MINIMUM_VERSION_REQUIRED NET_VERSION_MINOR
    #endif

    __declspec(selectany)
    ULONG NetMinimumVersionRequired = NET_MINIMUM_VERSION_REQUIRED;

#else

    //
    // Implemented in client driver code
    //
    extern ULONG NetMinimumVersionRequired;

#endif

//
// BOOLEAN
// NET_IS_FUNCTION_AVAILABLE(
//     FunctionName
//     );
//
#define NET_IS_FUNCTION_AVAILABLE(FunctionName)                                \
(                                                                              \
    (FunctionName ## TableIndex < NET_ALWAYS_AVAILABLE_FUNCTION_COUNT)         \
    ||                                                                         \
    (!NetClientVersionHigherThanFramework)                                     \
    ||                                                                         \
    (FunctionName ## TableIndex < NetFunctionCount)                            \
)

//
// BOOLEAN
// NET_IS_STRUCTURE_AVAILABLE(
//     StructName
//     );
//
#define NET_IS_STRUCTURE_AVAILABLE(StructName)                                 \
(                                                                              \
    (!NetClientVersionHigherThanFramework)                                     \
    ||                                                                         \
    (INDEX_ ## StructName < NetStructureCount)                                 \
)

//
// BOOLEAN
// NET_IS_FIELD_AVAILABLE(
//     StructName,
//     FieldName
//     );
//
#define NET_IS_FIELD_AVAILABLE(StructName, FieldName)                          \
(                                                                              \
    (!NetClientVersionHigherThanFramework)                                     \
    ||                                                                         \
    (                                                                          \
        (INDEX_ ## StructName < NetStructureCount)                             \
        &&                                                                     \
        (FIELD_OFFSET(StructName, FieldName) < NetStructures[INDEX_ ## StructName])\
    )                                                                          \
)

//
// ULONG
// NET_STRUCTURE_SIZE(
//     StructName
//     );
//
// NOTE: if it returns (-1), the structure is not available on the
//       current framework (and should not be used).
//
#if defined(NET_EVERYTHING_ALWAYS_AVAILABLE)

#define NET_STRUCTURE_SIZE(StructName)  (ULONG)sizeof(StructName)

#else

#define NET_STRUCTURE_SIZE(StructName)                                         \
(ULONG)                                                                        \
(                                                                              \
    NetClientVersionHigherThanFramework                                        \
        ? (                                                                    \
            (INDEX_ ## StructName < NetStructureCount)                         \
            ? NetStructures[INDEX_ ## StructName]                              \
            : (size_t)(-1)                                                     \
          )                                                                    \
        : sizeof(StructName)                                                   \
)

#endif // NET_STRUCTURE_SIZE

typedef enum _NETFUNCENUM {

    NetAdapterInitAllocateTableIndex = 0,
    NetAdapterInitFreeTableIndex = 1,
    NetAdapterInitSetDatapathCallbacksTableIndex = 2,
    NetAdapterCreateTableIndex = 3,
    NetAdapterStartTableIndex = 4,
    NetAdapterStopTableIndex = 5,
    NetAdapterSetLinkLayerCapabilitiesTableIndex = 6,
    NetAdapterSetLinkLayerMtuSizeTableIndex = 7,
    NetAdapterPowerOffloadSetArpCapabilitiesTableIndex = 8,
    NetAdapterPowerOffloadSetNSCapabilitiesTableIndex = 9,
    NetAdapterWakeSetBitmapCapabilitiesTableIndex = 10,
    NetAdapterWakeSetMediaChangeCapabilitiesTableIndex = 11,
    NetAdapterWakeSetMagicPacketCapabilitiesTableIndex = 12,
    NetAdapterWakeSetPacketFilterCapabilitiesTableIndex = 13,
    NetAdapterSetDataPathCapabilitiesTableIndex = 14,
    NetAdapterSetLinkStateTableIndex = 15,
    NetAdapterGetNetLuidTableIndex = 16,
    NetAdapterOpenConfigurationTableIndex = 17,
    NetAdapterSetPermanentLinkLayerAddressTableIndex = 18,
    NetAdapterSetCurrentLinkLayerAddressTableIndex = 19,
    NetAdapterOffloadSetChecksumCapabilitiesTableIndex = 20,
    NetOffloadIsChecksumIPv4EnabledTableIndex = 21,
    NetOffloadIsChecksumTcpEnabledTableIndex = 22,
    NetOffloadIsChecksumUdpEnabledTableIndex = 23,
    NetAdapterReportWakeReasonPacketTableIndex = 24,
    NetAdapterReportWakeReasonMediaChangeTableIndex = 25,
    NetAdapterInitGetCreatedAdapterTableIndex = 26,
    NetAdapterExtensionInitAllocateTableIndex = 27,
    NetAdapterExtensionInitSetOidRequestPreprocessCallbackTableIndex = 28,
    NetAdapterDispatchPreprocessedOidRequestTableIndex = 29,
    NetAdapterGetParentTableIndex = 30,
    NetAdapterGetLinkLayerMtuSizeTableIndex = 31,
    NetAdapterExtensionInitSetNdisPmCapabilitiesTableIndex = 32,
    NetAdapterWdmGetNdisHandleTableIndex = 33,
    NetAdapterDriverWdmGetHandleTableIndex = 34,
    NetConfigurationCloseTableIndex = 35,
    NetConfigurationOpenSubConfigurationTableIndex = 36,
    NetConfigurationQueryUlongTableIndex = 37,
    NetConfigurationQueryStringTableIndex = 38,
    NetConfigurationQueryMultiStringTableIndex = 39,
    NetConfigurationQueryBinaryTableIndex = 40,
    NetConfigurationQueryLinkLayerAddressTableIndex = 41,
    NetConfigurationAssignUlongTableIndex = 42,
    NetConfigurationAssignUnicodeStringTableIndex = 43,
    NetConfigurationAssignMultiStringTableIndex = 44,
    NetConfigurationAssignBinaryTableIndex = 45,
    NetDeviceInitConfigTableIndex = 46,
    NetDeviceOpenConfigurationTableIndex = 47,
    NetDeviceInitSetPowerPolicyEventCallbacksTableIndex = 48,
    NetDeviceInitSetResetConfigTableIndex = 49,
    NetDeviceAssignSupportedOidListTableIndex = 50,
    NetPowerOffloadGetTypeTableIndex = 51,
    NetPowerOffloadGetArpParametersTableIndex = 52,
    NetPowerOffloadGetNSParametersTableIndex = 53,
    NetDeviceGetPowerOffloadListTableIndex = 54,
    NetPowerOffloadListGetCountTableIndex = 55,
    NetPowerOffloadListGetElementTableIndex = 56,
    NetAdapterSetReceiveScalingCapabilitiesTableIndex = 57,
    NetRxQueueCreateTableIndex = 58,
    NetRxQueueNotifyMoreReceivedPacketsAvailableTableIndex = 59,
    NetRxQueueInitGetQueueIdTableIndex = 60,
    NetRxQueueGetRingCollectionTableIndex = 61,
    NetRxQueueGetExtensionTableIndex = 62,
    NetTxQueueCreateTableIndex = 63,
    NetTxQueueNotifyMoreCompletedPacketsAvailableTableIndex = 64,
    NetTxQueueInitGetQueueIdTableIndex = 65,
    NetTxQueueGetRingCollectionTableIndex = 66,
    NetTxQueueGetExtensionTableIndex = 67,
    NetWakeSourceGetTypeTableIndex = 68,
    NetWakeSourceGetAdapterTableIndex = 69,
    NetWakeSourceGetBitmapParametersTableIndex = 70,
    NetWakeSourceGetMediaChangeParametersTableIndex = 71,
    NetDeviceGetWakeSourceListTableIndex = 72,
    NetWakeSourceListGetCountTableIndex = 73,
    NetWakeSourceListGetElementTableIndex = 74,
    NetAdapterWakeSetEapolPacketCapabilitiesTableIndex = 75,
    NetAdapterSetReceiveFilterCapabilitiesTableIndex = 76,
    NetReceiveFilterGetPacketFilterTableIndex = 77,
    NetReceiveFilterGetMulticastAddressCountTableIndex = 78,
    NetReceiveFilterGetMulticastAddressListTableIndex = 79,
    NetDriverExtensionInitializeTableIndex = 80,
    NetAdapterExtensionInitSetDirectOidRequestPreprocessCallbackTableIndex = 81,
    NetExAdapterInitSetDirectOidPreprocessCallbackTableIndex = 82,
    NetAdapterExtensionInitSetTxPeerDemuxCallbackTableIndex = 83,
    NetAdapterDispatchPreprocessedDirectOidRequestTableIndex = 84,
    NetExAdapterDispatchPreprocessedDirectOidTableIndex = 85,
    NetAdapterExtensionSetNdisPmCapabilitiesTableIndex = 86,
    NetAdapterInitAllocateContextTableIndex = 87,
    NetAdapterInitGetTypedContextWorkerTableIndex = 88,
    NetAdapterOffloadSetTxChecksumCapabilitiesTableIndex = 89,
    NetOffloadIsTxChecksumIPv4EnabledTableIndex = 90,
    NetOffloadIsTxChecksumTcpEnabledTableIndex = 91,
    NetOffloadIsTxChecksumUdpEnabledTableIndex = 92,
    NetAdapterOffloadSetRxChecksumCapabilitiesTableIndex = 93,
    NetOffloadIsRxChecksumIPv4EnabledTableIndex = 94,
    NetOffloadIsRxChecksumTcpEnabledTableIndex = 95,
    NetOffloadIsRxChecksumUdpEnabledTableIndex = 96,
    NetAdapterOffloadSetGsoCapabilitiesTableIndex = 97,
    NetOffloadIsLsoIPv4EnabledTableIndex = 98,
    NetOffloadIsLsoIPv6EnabledTableIndex = 99,
    NetOffloadIsUsoIPv4EnabledTableIndex = 100,
    NetOffloadIsUsoIPv6EnabledTableIndex = 101,
    NetAdapterOffloadSetRscCapabilitiesTableIndex = 102,
    NetOffloadIsTcpRscIPv4EnabledTableIndex = 103,
    NetOffloadIsTcpRscIPv6EnabledTableIndex = 104,
    NetOffloadIsRscTcpTimestampOptionEnabledTableIndex = 105,
    NetAdapterOffloadSetIeee8021qTagCapabilitiesTableIndex = 106,
    NetAdapterInitAddTxDemuxTableIndex = 107,
    NetAdapterGetTxPeerAddressDemuxTableIndex = 108,
    NetBufferQueueCreateTableIndex = 109,
    NetBufferQueueGetExtensionTableIndex = 110,
    NetBufferQueueGetRingTableIndex = 111,
    NetDeviceInitSetResetCapabilitiesTableIndex = 112,
    NetDeviceStoreResetDiagnosticsTableIndex = 113,
    NetDeviceRequestResetTableIndex = 114,
    NetExecutionContextCreateTableIndex = 115,
    NetExecutionContextTaskCreateTableIndex = 116,
    NetExecutionContextNotifyTableIndex = 117,
    NetExecutionContextTaskEnqueueTableIndex = 118,
    NetExecutionContextTaskWaitCompletionTableIndex = 119,
    NetPowerOffloadGetAdapterTableIndex = 120,
    NetTxQueueGetDemux8021pTableIndex = 121,
    NetAdapterExtensionInitSetPowerPolicyCallbacksTableIndex = 122,
    NetAdapterInitSetSelfManagedPowerReferencesTableIndex = 123,
    NetAdapterLightweightInitAllocateTableIndex = 124,
    NetAdapterWifiDestroyPeerAddressDatapathTableIndex = 125,
    NetAdapterPauseOffloadCapabilitiesTableIndex = 126,
    NetAdapterResumeOffloadCapabilitiesTableIndex = 127,
    NetAdapterQueryWakeReasonTableIndex = 128,
    NetTxQueueGetDemuxWmmInfoTableIndex = 129,
    NetTxQueueGetDemuxPeerAddressTableIndex = 130,
    NetOffloadIsUdpRscEnabledTableIndex = 131,
    NetFunctionTableNumEntries = 132,
} NETFUNCENUM;

typedef enum _NETSTRUCTENUM {

    INDEX_NET_ADAPTER_DATAPATH_CALLBACKS               = 0,
    INDEX_NET_ADAPTER_DMA_CAPABILITIES                 = 1,
    INDEX_NET_ADAPTER_EXTENSION_POWER_POLICY_CALLBACKS = 2,
    INDEX_NET_ADAPTER_LINK_LAYER_ADDRESS               = 3,
    INDEX_NET_ADAPTER_LINK_LAYER_CAPABILITIES          = 4,
    INDEX_NET_ADAPTER_LINK_STATE                       = 5,
    INDEX_NET_ADAPTER_NDIS_PM_CAPABILITIES             = 6,
    INDEX_NET_ADAPTER_OFFLOAD_CHECKSUM_CAPABILITIES    = 7,
    INDEX_NET_ADAPTER_OFFLOAD_GSO_CAPABILITIES         = 8,
    INDEX_NET_ADAPTER_OFFLOAD_IEEE8021Q_TAG_CAPABILITIES = 9,
    INDEX_NET_ADAPTER_OFFLOAD_RSC_CAPABILITIES         = 10,
    INDEX_NET_ADAPTER_OFFLOAD_RX_CHECKSUM_CAPABILITIES = 11,
    INDEX_NET_ADAPTER_OFFLOAD_TX_CHECKSUM_CAPABILITIES = 12,
    INDEX_NET_ADAPTER_POWER_OFFLOAD_ARP_CAPABILITIES   = 13,
    INDEX_NET_ADAPTER_POWER_OFFLOAD_NS_CAPABILITIES    = 14,
    INDEX_NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES      = 15,
    INDEX_NET_ADAPTER_RECEIVE_SCALING_CAPABILITIES     = 16,
    INDEX_NET_ADAPTER_RECEIVE_SCALING_HASH_SECRET_KEY  = 17,
    INDEX_NET_ADAPTER_RECEIVE_SCALING_INDIRECTION_ENTRIES = 18,
    INDEX_NET_ADAPTER_RECEIVE_SCALING_INDIRECTION_ENTRY = 19,
    INDEX_NET_ADAPTER_RX_CAPABILITIES                  = 20,
    INDEX_NET_ADAPTER_TX_CAPABILITIES                  = 21,
    INDEX_NET_ADAPTER_TX_DEMUX                         = 22,
    INDEX_NET_ADAPTER_WAKE_BITMAP_CAPABILITIES         = 23,
    INDEX_NET_ADAPTER_WAKE_EAPOL_PACKET_CAPABILITIES   = 24,
    INDEX_NET_ADAPTER_WAKE_MAGIC_PACKET_CAPABILITIES   = 25,
    INDEX_NET_ADAPTER_WAKE_MEDIA_CHANGE_CAPABILITIES   = 26,
    INDEX_NET_ADAPTER_WAKE_PACKET_FILTER_CAPABILITIES  = 27,
    INDEX_NET_ADAPTER_WAKE_REASON_PACKET               = 28,
    INDEX_NET_BUFFER_QUEUE_CONFIG                      = 29,
    INDEX_NET_DEVICE_POWER_POLICY_EVENT_CALLBACKS      = 30,
    INDEX_NET_DEVICE_RESET_CAPABILITIES                = 31,
    INDEX_NET_DEVICE_RESET_CONFIG                      = 32,
    INDEX_NET_DRIVER_EXTENSION_CONFIG                  = 33,
    INDEX_NET_DRIVER_GLOBALS                           = 34,
    INDEX_NET_EXECUTION_CONTEXT_CONFIG                 = 35,
    INDEX_NET_EXECUTION_CONTEXT_TASK_CONFIG            = 36,
    INDEX_NET_EXTENSION_QUERY                          = 37,
    INDEX_NET_PACKET_QUEUE_CONFIG                      = 38,
    INDEX_NET_POWER_OFFLOAD_ARP_PARAMETERS             = 39,
    INDEX_NET_POWER_OFFLOAD_LIST                       = 40,
    INDEX_NET_POWER_OFFLOAD_NS_PARAMETERS              = 41,
    INDEX_NET_RXQUEUE_BUFFER_LAYOUT_HINT               = 42,
    INDEX_NET_WAKE_SOURCE_BITMAP_PARAMETERS            = 43,
    INDEX_NET_WAKE_SOURCE_LIST                         = 44,
    INDEX_NET_WAKE_SOURCE_MEDIA_CHANGE_PARAMETERS      = 45,
    NET_STRUCTURE_TABLE_NUM_ENTRIES                    = 46,
} NETSTRUCTENUM;

#define Net_STRUCTURE_TABLE_NUM_ENTRIES NET_STRUCTURE_TABLE_NUM_ENTRIES

#endif // _NETFUNCENUM_2_5_H_

