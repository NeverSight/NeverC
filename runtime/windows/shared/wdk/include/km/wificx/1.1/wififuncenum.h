/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

_WdfVersionBuild_

Module Name: WifiFuncEnum.h

Abstract:
    Generated an enum of all WDF API functions

Environment:
    kernel mode only

    Warning: manual changes to this file will be lost.
--*/

#ifndef _WIFIFUNCENUM_H_
#define _WIFIFUNCENUM_H_

extern PWIFI_DRIVER_GLOBALS WifiDriverGlobals;


//
//
// The first version of the framework that added the support
// for client version to be higher than the framework version
//
#define WIFI_FIRST_VERSION_SUPPORTING_CLIENT_VERSION_HIGHER_THAN_FRAMEWORK 0

//
// The function count from the first framework which added the support.
// Any function with index less than the count is always available
//
#define WIFI_ALWAYS_AVAILABLE_FUNCTION_COUNT  39

//
// Validate WIFI_MINIMUM_VERSION_REQUIRED falls into the correct range
//
#if defined(WIFI_MINIMUM_VERSION_REQUIRED)

    #if WIFI_MINIMUM_VERSION_REQUIRED > WIFI_VERSION_MINOR
    #error WIFI_MINIMUM_VERSION_REQUIRED > WIFI_VERSION_MINOR
    #endif

#endif


//
// All functions/structures are always available in following two cases:
//
//  1) Building framework itself (which defines WIFI_EVERYTHING_ALWAYS_AVAILABLE)
//
//  2) Traditional client drivers who are not aware of the new feature
//     "client version can be higher than framework's" and thus implies
//     WIFI_VERSION_MINOR == WIFI_MINIMUM_VERSION_REQUIRED
//
#if defined(WIFI_MINIMUM_VERSION_REQUIRED) && (WIFI_VERSION_MINOR == WIFI_MINIMUM_VERSION_REQUIRED)

    #if !defined(WIFI_EVERYTHING_ALWAYS_AVAILABLE)
    #define WIFI_EVERYTHING_ALWAYS_AVAILABLE
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
extern BOOLEAN WifiClientVersionHigherThanFramework;

//
// private:
//
// Only valid when WifiClientVersionHigherThanFramework is TRUE.
//
// Hold the count of usable functions from the framework. Used by IS_FUNCTION_AVAILABLE
// to determine whether a function is available on the platform.
//
extern ULONG   WifiFunctionCount;

//
// private:
//
// Only valid when WifiClientVersionHigherThanFramework is TRUE.
//
// Hold the count of usable structures from the framework. Used by IS_FIELD_AVAILABLE
// to determine whether a field in a structure is available on the platform.
//
extern ULONG   WifiStructureCount;
extern WDF_STRUCT_INFO WifiStructures;


//
// private:
//
// Only valid when WifiClientVersionHigherThanFramework is TRUE.
//
// Hold the name of the Framework Extension.
//
extern PCWSTR  WifiFrameworkExtensionName;


#ifndef WIFI_STUB

    #ifndef WIFI_VERSION_MAJOR
    #error  WIFI_VERSION_MAJOR is not defined
    #endif

    //
    // "Version Minor(Target version)" controls which version of public header files to include
    //
    #ifndef WIFI_VERSION_MINOR
    #error  WIFI_VERSION_MINOR is not defined
    #endif

    //
    // "Version Minor (Minimum Required)" controls the oldest version of framework to load the driver
    //
    #ifndef WIFI_MINIMUM_VERSION_REQUIRED
    #define WIFI_MINIMUM_VERSION_REQUIRED WIFI_VERSION_MINOR
    #endif

    __declspec(selectany)
    ULONG WifiMinimumVersionRequired = WIFI_MINIMUM_VERSION_REQUIRED;

#else

    //
    // Implemented in client driver code
    //
    extern ULONG WifiMinimumVersionRequired;

#endif

//
// BOOLEAN
// WIFI_IS_FUNCTION_AVAILABLE(
//     FunctionName
//     );
//
#define WIFI_IS_FUNCTION_AVAILABLE(FunctionName)                               \
(                                                                              \
    (FunctionName ## TableIndex < WIFI_ALWAYS_AVAILABLE_FUNCTION_COUNT)        \
    ||                                                                         \
    (!WifiClientVersionHigherThanFramework)                                    \
    ||                                                                         \
    (FunctionName ## TableIndex < WifiFunctionCount)                           \
)

//
// BOOLEAN
// WIFI_IS_STRUCTURE_AVAILABLE(
//     StructName
//     );
//
#define WIFI_IS_STRUCTURE_AVAILABLE(StructName)                                \
(                                                                              \
    (!WifiClientVersionHigherThanFramework)                                    \
    ||                                                                         \
    (INDEX_ ## StructName < WifiStructureCount)                                \
)

//
// BOOLEAN
// WIFI_IS_FIELD_AVAILABLE(
//     StructName,
//     FieldName
//     );
//
#define WIFI_IS_FIELD_AVAILABLE(StructName, FieldName)                         \
(                                                                              \
    (!WifiClientVersionHigherThanFramework)                                    \
    ||                                                                         \
    (                                                                          \
        (INDEX_ ## StructName < WifiStructureCount)                            \
        &&                                                                     \
        (FIELD_OFFSET(StructName, FieldName) < WifiStructures[INDEX_ ## StructName])\
    )                                                                          \
)

//
// ULONG
// WIFI_STRUCTURE_SIZE(
//     StructName
//     );
//
// NOTE: if it returns (-1), the structure is not available on the
//       current framework (and should not be used).
//
#if defined(WIFI_EVERYTHING_ALWAYS_AVAILABLE)

#define WIFI_STRUCTURE_SIZE(StructName)  (ULONG)sizeof(StructName)

#else

#define WIFI_STRUCTURE_SIZE(StructName)                                        \
(ULONG)                                                                        \
(                                                                              \
    WifiClientVersionHigherThanFramework                                       \
        ? (                                                                    \
            (INDEX_ ## StructName < WifiStructureCount)                        \
            ? WifiStructures[INDEX_ ## StructName]                             \
            : (size_t)(-1)                                                     \
          )                                                                    \
        : sizeof(StructName)                                                   \
)

#endif // WIFI_STRUCTURE_SIZE

typedef enum _WIFIFUNCENUM {

    WifiDeviceInitConfigTableIndex = 0,
    WifiDeviceGetOsWdiVersionTableIndex = 1,
    WifiDeviceSetDeviceCapabilitiesTableIndex = 2,
    WifiDeviceSetStationCapabilitiesTableIndex = 3,
    WifiDeviceSetWiFiDirectCapabilitiesTableIndex = 4,
    WifiDeviceSetBandCapabilitiesTableIndex = 5,
    WifiDeviceSetPhyCapabilitiesTableIndex = 6,
    WifiDeviceInitializeTableIndex = 7,
    WifiDeviceReceiveIndicationTableIndex = 8,
    WifiAdapterInitializeTableIndex = 9,
    WifiRequestGetInOutBufferTableIndex = 10,
    WifiRequestGetMessageIdTableIndex = 11,
    WifiRequestSetBytesNeededTableIndex = 12,
    WifiRequestCompleteTableIndex = 13,
    WifiAdapterPowerOffloadSetRsnRekeyCapabilitiesTableIndex = 14,
    WifiAdapterSetWakeCapabilitiesTableIndex = 15,
    WifiAdapterReportWakeReasonTableIndex = 16,
    WifiDirectDeviceCreateTableIndex = 17,
    WifiDirectDeviceInitializeTableIndex = 18,
    WifiAdapterGetPortIdTableIndex = 19,
    WifiAdapterInitGetTypeTableIndex = 20,
    WifiAdapterGetTypeTableIndex = 21,
    WifiDirectDeviceGetPortIdTableIndex = 22,
    WifiAdapterInitAddTxDemuxTableIndex = 23,
    WifiTxQueueGetDemuxPeerAddressTableIndex = 24,
    WifiTxQueueGetDemuxWmmInfoTableIndex = 25,
    WifiAdapterAddPeerTableIndex = 26,
    WifiAdapterRemovePeerTableIndex = 27,
    WifiPowerOffloadGetTypeTableIndex = 28,
    WifiPowerOffloadGetAdapterTableIndex = 29,
    WifiPowerOffloadGet80211RSNRekeyParametersTableIndex = 30,
    WifiDeviceGetPowerOffloadListTableIndex = 31,
    WifiPowerOffloadListGetCountTableIndex = 32,
    WifiPowerOffloadListGetElementTableIndex = 33,
    WifiWakeSourceGetTypeTableIndex = 34,
    WifiWakeSourceGetAdapterTableIndex = 35,
    WifiDeviceGetWakeSourceListTableIndex = 36,
    WifiWakeSourceListGetCountTableIndex = 37,
    WifiWakeSourceListGetElementTableIndex = 38,
    WifiFunctionTableNumEntries = 39,
} WIFIFUNCENUM;

typedef enum _WIFISTRUCTENUM {

    INDEX_WIFI_ADAPTER_ATTRIBUTES                      = 0,
    INDEX_WIFI_ADAPTER_POWER_OFFLOAD_RSN_REKEY_CAPABILITIES = 1,
    INDEX_WIFI_ADAPTER_TX_DEMUX                        = 2,
    INDEX_WIFI_ADAPTER_WAKE_CAPABILITIES               = 3,
    INDEX_WIFI_BAND_CAPABILITIES                       = 4,
    INDEX_WIFI_BAND_INFO                               = 5,
    INDEX_WIFI_DEVICE_CAPABILITIES                     = 6,
    INDEX_WIFI_DEVICE_CONFIG                           = 7,
    INDEX_WIFI_DRIVER_GLOBALS                          = 8,
    INDEX_WIFI_PHY_CAPABILITIES                        = 9,
    INDEX_WIFI_PHY_INFO                                = 10,
    INDEX_WIFI_POWER_OFFLOAD_80211RSNREKEY_PARAMETERS  = 11,
    INDEX_WIFI_POWER_OFFLOAD_LIST                      = 12,
    INDEX_WIFI_STATION_CAPABILITIES                    = 13,
    INDEX_WIFI_WAKE_SOURCE_LIST                        = 14,
    INDEX_WIFI_WIFIDIRECT_CAPABILITIES                 = 15,
    WIFI_STRUCTURE_TABLE_NUM_ENTRIES                   = 16,
} WIFISTRUCTENUM;

#define Wifi_STRUCTURE_TABLE_NUM_ENTRIES WIFI_STRUCTURE_TABLE_NUM_ENTRIES

#endif // _WIFIFUNCENUM_H_

