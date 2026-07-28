/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

 SdcaXu.h

Abstract:

    This module defines the types, constants, and functions that are
    exposed to device drivers that advertise SDCA Extension Unit Interface.

--*/

#pragma once

//
// The interface class GUID for SDCA Extension Unit Interface
//
// {6A000532-8C2A-4392-9AD1-3F0305B3CF88}
DEFINE_GUID(SDCAXU_INTERFACE,
    0x6A000532, 0x8C2A, 0x4392, 0x9A, 0xD1, 0x3F, 0x03, 0x05, 0xB3, 0xCF, 0x88);

//
// Version 1.0
//
#define SDCAXU_INTERFACE_VERSION_0100   (0x0100)

//
// Version 1.1
//
#define SDCAXU_INTERFACE_VERSION_0101   (0x0101)

//
// Version 1.2
//
#define SDCAXU_INTERFACE_VERSION_0102   (0x0102)

//
// EvtSetXUEntities
// Set all the XU Entities which should be enabled
// params:
//   * Context      - Codec Driver supplied context
//   * pNumEntities - Number of elements in the array EntityIDs
//   * EntityIDs    - Array of Entity IDs
//
typedef NTSTATUS EVT_SDCAXU_SET_XU_ENTITIES
(
    _In_    PVOID   Context,
    _In_    ULONG   NumEntities,
    _In_reads_(NumEntities)
            ULONG   EntityIDs[]
);
typedef EVT_SDCAXU_SET_XU_ENTITIES *PFN_SDCAXU_SET_XU_ENTITIES;

#define MAX_EXTENSION_NUM_DATAPORTS (15)

//
// Size                 - Size of structure including size of the ANYSIZE_ARRAY
// SCPInterruptMask     - bit mask, each bit representing interrupt in SCP scope
// DataPortInterrupts   - Array of UINT8 masks, one for each Data Port
// SDCAInterruptMask    - bit mask, each bit representing interrupt in SDCA scope
//
typedef struct _SDCAXU_INTERRUPT_INFO
{
    ULONG   Size;
    ULONG   SCPInterruptMask;
    UINT8   DataPortInterrupts[MAX_EXTENSION_NUM_DATAPORTS];
    ULONG   SDCAInterruptMask;
} SDCAXU_INTERRUPT_INFO, * PSDCAXU_INTERRUPT_INFO;

//
// EvtRegisterForInterrupts
// Register for interrupts - EvtInterruptHandler will be invoked
// params:
//   * Context              - Codec Driver supplied context
//   * InterruptInfo        - Interrupt masks to register for
//
typedef NTSTATUS EVT_SDCAXU_REGISTER_FOR_INTERRUPTS
(
    _In_    PVOID                   Context,
    _In_    PSDCAXU_INTERRUPT_INFO  InterruptInfo
);
typedef EVT_SDCAXU_REGISTER_FOR_INTERRUPTS *PFN_SDCAXU_REGISTER_FOR_INTERRUPTS;

//
// EvtInterruptHandler
// Invoked for all the SDCA interrupts registered through EvtRegisterForInterrupts
// params:
//   * Context              - SdcaXu Driver supplied context
//   * Interrupts           - Interrupt Info containing all active interrupt bits set
//
typedef NTSTATUS EVT_SDCAXU_INTERRUPT_HANDLER
(
    _In_    PVOID                   Context,
    _Inout_ PSDCAXU_INTERRUPT_INFO  Interrupts
);
typedef EVT_SDCAXU_INTERRUPT_HANDLER *PFN_SDCAXU_INTERRUPT_HANDLER;

//
// Types of notifications
//
// 
typedef enum _SDCAXU_NOTIFICATION_TYPE
{
    // Notification before power state changes
    SdcaXuNotificationTypePowerPre = 0,

    // Notification after power state changes
    SdcaXuNotificationTypePowerPost,


    // Notification when jack is detected
    // If SdcaXu Driver has called EvtSetJackOverride with TRUE, then the class driver
    // will notify about jack detection with the detected mode as parameter.
    // SdcaXu driver should immediately respond with EvtSetJackSelectedMode to
    // indicate the SelectedMode on jack.
    // SdcaXu driver can at a later point call the EvtSetJackSelectedMode again to change the jack mode.
    SdcaXuNotificationTypeJackDetect,

    // Notification after class driver has handled a Function_Has_Been_Reset bit of Function_Status control.
    // Class driver will perform necessary control restoration and initialization before sending.
    SdcaXuNotificationTypeHardwareReset,

    // Notification when the Commit Group Handle has either been set or cleared.
    // This Commit Group Handle should be used to ensure any operations that
    // require synchronization especially for aggregation are properly
    // synchronized
    SdcaXuNotificationTypeCommitGroup,

    // Notification when system posture (orientation or lid state) has changed
    // Class driver will notify SdcaXu driver of the posture changes.
    // SdcaXu driver can decide to handle or ignore the changes accordingly.
    SdcaXuNotificationTypePosture,

    // Notification after class driver has handled a Function_Needs_Initialization bit of Function_Status control.
    // Class driver will perform function initialization writes before sending.
    SdcaXuNotificationTypeFunctionNeedsInitialization,

    // Notification after class driver has handled a UMP_Sequence_Fault bit of Function_Status control.
    SdcaXuNotificationTypeUMPSequenceFault,

    // Notification after class driver has handled a Function_Fault bit of Function_Status control.
    SdcaXuNotificationTypeFunctionFault,

    // Notification after class driver has handled a Streaming_Stopped_Abnormally bit of Function_Status control. 
    SdcaXuNotificationTypeStreamingStoppedAbnormally,

    // Notification that class driver will start FDL process. Uses SDCAXU_NOTIFICATION_FDL_BEGIN.
    SdcaXuNotificationTypeFdlBegin,

    // Notification that class driver has completed FDL process. Uses SDCAXU_NOTIFICATION_FDL_END.
    SdcaXuNotificationTypeFdlEnd,

    SdcaXuNotificationTypeCount
} SDCAXU_NOTIFICATION_TYPE;

typedef enum _SDCAXU_POWER_STATE
{
    SdcaXuPowerState0 = 0,
    SdcaXuPowerState1,
    SdcaXuPowerState2,
    SdcaXuPowerState3,
    SdcaXuPowerState4,

    SdcaXuPowerStateCount
} SDCAXU_POWER_STATE;

typedef struct _SDCAXU_NOTIFICATION_POWER
{
    ULONG               PowerDomainEntityId;
    SDCAXU_POWER_STATE  OldState;
    SDCAXU_POWER_STATE  NewState;
} SDCAXU_NOTIFICATION_POWER, *PSDCAXU_NOTIFICATION_POWER;

typedef enum _SDCAXU_JACK_EVENT
{
    SdcaXuJackEventUnplugged = 0,
    SdcaXuJackEventPlugged,

    SdcaXuJackEventCount
} SDCAXU_JACK_EVENT;

typedef struct _SDCAXU_NOTIFICATION_JACK_DETECT
{
    // SDCA Group Entity ID for Jack(s)
    ULONG                   GroupEntityId;

    // Type of jack detected by hardware
    ULONG                   DetectedMode;

    // SDCA Jack event type (plugged/unplugged)
    SDCAXU_JACK_EVENT       JackEvent;
} SDCAXU_NOTIFICATION_JACK_DETECT, *PSDCAXU_NOTIFICATION_JACK_DETECT;

typedef struct _SDCAXU_NOTIFICATION_COMMIT_GROUP
{
    // Commit group handle to be used for synchronization
    // (or 0 if the commit group has been destroyed)
    ULONG                   CommitGroupHandle;
} SDCAXU_NOTIFICATION_COMMIT_GROUP, *PSDCAXU_NOTIFICATION_COMMIT_GROUP;

typedef struct _SDCAXU_NOTIFICATION_FDL_BEGIN
{
    // ID of the XU entity that is beginning FDL
    ULONG                   FdlEntityId;
} SDCAXU_NOTIFICATION_FDL_BEGIN, *PSDCAXU_NOTIFICATION_FDL_BEGIN;

typedef struct _SDCAXU_NOTIFICATION_FDL_END
{
    // ID of the XU entity that completed FDL
    ULONG                   FdlEntityId;

    // Status of the FDL
    //  STATUS_SUCCESS: The FDL sequence completed
    //  STATUS_ABORT: The FDL sequence was aborted by the hardware or driver
    NTSTATUS                FdlStatus;
} SDCAXU_NOTIFICATION_FDL_END, *PSDCAXU_NOTIFICATION_FDL_END;

typedef enum _SDCAXU_POSTURE
{
    SdcaXuPostureOrientationNotRotated = 0,
    SdcaXuPostureOrientationRotated90DegreesCounterClockwise,
    SdcaXuPostureOrientationRotated180DegreesCounterClockwise,
    SdcaXuPostureOrientationRotated270DegreesCounterClockwise,
    SdcaXuPostureLidClosed,

    SdcaXuPostureCount
} SDCAXU_POSTURE, *PSDCAXU_POSTURE;

typedef struct _SDCAXU_NOTIFICATION_POSTURE
{
    SDCAXU_POSTURE          Posture;
} SDCAXU_NOTIFICATION_POSTURE, *PSDCAXU_NOTIFICATION_POSTURE;

//
// EvtChangeNotification
// This will be called to notify changes in the SDCA device
// params:
//   * Context              - SdcaXu Driver supplied context
//   * NotificationType     - Type of Change notification
//   * NotificationData     - Optional data associated with the notification
//   * NotificationDataSize - Size of buffer pointed to by NotificationData
//
typedef NTSTATUS EVT_SDCAXU_CHANGE_NOTIFICATION
(
    _In_        PVOID                       Context,
    _In_        SDCAXU_NOTIFICATION_TYPE    NotificationType,
    _In_opt_    PVOID                       NotificationData,
    _In_        ULONG                       NotificationDataSize
);
typedef EVT_SDCAXU_CHANGE_NOTIFICATION *PFN_SDCAXU_CHANGE_NOTIFICATION;

//
// Types of Hardware configuration data
// 
typedef enum _SDCAXU_HW_CONFIG_TYPE
{
     // Vendor specific data from Acpi
    SdcaXuHwConfigTypeAcpiBlob = 0,

    SdcaXuHwConfigTypeCount
} SDCAXU_HW_CONFIG_TYPE;

//
// EvtSetHwConfig
// SDCA class driver will call to set hardware specific information
// params:
//   * Context              - SdcaXu Driver supplied context
//   * HwConfigType         - Type of Hardware configuration
//   * HwConfigData         - Pointer to Hardware configuration data
//   * HwConfigDataSize     - Size of buffer pointed to by HwConfigData
//
typedef NTSTATUS EVT_SDCAXU_SET_HW_CONFIG
(
    _In_        PVOID                   Context,
    _In_        SDCAXU_HW_CONFIG_TYPE   HwConfigType,
    _In_opt_    PVOID                   HwConfigData,
    _In_        ULONG                   HwConfigDataSize
);
typedef EVT_SDCAXU_SET_HW_CONFIG *PFN_SDCAXU_SET_HW_CONFIG;

//
// ACX circuit information
//
typedef struct _SDCAXU_ACX_CIRCUIT_CONFIG
{
    // Size of structure including the storage
    // for UNICODE_STRING.Buffer
    ULONG cbSize;

    // Component IDs
    GUID ComponentID;

    // Component URI
    UNICODE_STRING ComponentUri;

    // Container ID
    GUID ContainerID;

    // Circuit Name
    UNICODE_STRING CircuitName;

    // Circuit Type
    ULONG CircuitType;

    // SDCA Circuit Context
    PVOID CircuitContext;

} SDCAXU_ACX_CIRCUIT_CONFIG, *PSDCAXU_ACX_CIRCUIT_CONFIG;

//
// Types of Endpoint configuration data
// 
typedef enum _SDCAXU_ENDPOINT_CONFIG_TYPE
{
    //
    // ACX circuit information
    // SdcaXu driver can use this information to build ACX circuit that will
    // be included in the composition
    //
    SdcaXuEndpointConfigTypeAcxCircuitConfig = 0,

    SdcaXuEndpointConfigTypeCount
} SDCAXU_ENDPOINT_CONFIG_TYPE;

//
// EvtSetEndpointConfig
// SDCA class driver will call to set Endpoint specific information
// params:
//   * Context                  - SdcaXu Driver supplied context
//   * EndpointConfigType       - Type of Endpoint configuration
//   * EndpointConfigData       - Pointer to Endpoint configuration data
//   * EndpointConfigDataSize   - Size of buffer pointed to by EndpointConfigData
//
typedef NTSTATUS EVT_SDCAXU_SET_ENDPOINT_CONFIG
(
    _In_        PVOID                           Context,
    _In_        SDCAXU_ENDPOINT_CONFIG_TYPE     EndpointConfigType,
    _In_opt_    PVOID                           EndpointConfigData,
    _In_        ULONG                           EndpointConfigDataSize
);
typedef EVT_SDCAXU_SET_ENDPOINT_CONFIG *PFN_SDCAXU_SET_ENDPOINT_CONFIG;

//
// EvtRemoveEndpointConfig
// SDCA class driver will call to tell SdcaXu to remove this endpoint
// params:
//   * Context                  - SdcaXu Driver supplied context
//   * EndpointConfigType       - Type of Endpoint configuration
//   * EndpointConfigData       - Pointer to Endpoint configuration data
//   * EndpointConfigDataSize   - Size of buffer pointed to by EndpointConfigData
//
typedef NTSTATUS EVT_SDCAXU_REMOVE_ENDPOINT_CONFIG
(
    _In_        PVOID                           Context,
    _In_        SDCAXU_ENDPOINT_CONFIG_TYPE     EndpointConfigType,
    _In_opt_    PVOID                           EndpointConfigData,
    _In_        ULONG                           EndpointConfigDataSize
);
typedef EVT_SDCAXU_REMOVE_ENDPOINT_CONFIG *PFN_SDCAXU_REMOVE_ENDPOINT_CONFIG;

//
// EvtRetrieveSwftFileOverride
// SDCA class driver will call to allow SdcaXu to override an SWFT file used for File Download (FDL) 
//
// The SDCA class driver will provide the Version and Length of the file data it has found in the SWFT.
// If no file was found in the SWFT, Version and Length will be 0.
//
// The SdcaXu driver is not required to use the SwftFileVersion to determine if it has a suitable override.
// If the SdcaXu returns a file, that file will be used by the SDCA class driver regardless of the version.
//
// The SDCA class driver may first call with 0 NewFileBufferLength and NULL NewFileBuffer.
// The SdcaXu driver would fill out the correct NewFileLength if it will override.
// The SDCA class driver will then allocate the NewFileBuffer based on that length.
// 
// params:
//   * Context                  - SdcaXu Driver supplied context
//   * VendorID                 - Vendor_ID for file
//   * FileID                   - File_ID for file
//   * SwftFileVersion          - Version of the SWFT file class driver intends to use
//   * SwftFileLength           - Length of the SWFT file class driver intends to use, not including the SWFT file header
//   * NewFileVersion           - Version of the new file
//   * NewFileLength            - Length of the new file, not including any header
//   * NewFileBufferLength      - Length in bytes of the NewFileBuffer if allocated
//                                0 is used when querying the size to allocate
//   * NewFileBuffer            - Buffer that receives the override file bytes without header
//
// return:
//   STATUS_NOT_FOUND           - The SdcaXu driver has no suitable override for the file
//   STATUS_BUFFER_OVERFLOW     - NewFileBufferLength was 0 but the SdcaXu has an override file.
//                                The SdcaXu also fills out NewFileLength to the correct size.
//   STATUS_INVALID_PARAMETER   - Parameters were invalid
//   STATUS_SUCCESS             - A new file override is returned
//
typedef NTSTATUS EVT_SDCAXU_RETRIEVE_SWFT_FILE_OVERRIDE
(
    _In_        PVOID   Context,
    _In_        USHORT  VendorID,
    _In_        ULONG   FileID,
    _In_        USHORT  SwftFileVersion,
    _In_        ULONG   SwftFileLength,
    _Out_       PUSHORT NewFileVersion,
    _Out_       PULONG  NewFileLength,
    _In_        ULONG   NewFileBufferLength,
    _Out_writes_bytes_opt_(NewFileBufferLength)
                PVOID   NewFileBuffer
);
typedef EVT_SDCAXU_RETRIEVE_SWFT_FILE_OVERRIDE *PFN_SDCAXU_RETRIEVE_SWFT_FILE_OVERRIDE;

//
// EvtSetJackOverride
// SdcaXu driver will call this method to indicate that jack type
// will be overridden by the SdcaXu driver upon detection.
//
// In response SDCA will notify SdcaXu driver of jack detection,
// but will not activate audio endpoint upon hardware jack detection.
//
// SDCA will initiate audio endpoint activation when SdcaXu driver
// calls "SetJackSelectedMode".
//
// params:
//   * Context              - SDCA Driver supplied context
//   * Override             - Input parameter indicates whether to override Jack selected mode
//                            TRUE: Override
//                            FALSE: Default SDCA behavior
//
typedef NTSTATUS EVT_SDCAXU_SET_JACK_OVERRIDE
(
    _In_ PVOID      Context,
    _In_ BOOLEAN    Override
);
typedef EVT_SDCAXU_SET_JACK_OVERRIDE *PFN_SDCAXU_SET_JACK_OVERRIDE;

//
// EvtSetJackSelectedMode
// SdcaXu driver will call this method to update the Hardware jack type
//
// params:
//   * Context              - SDCA Driver supplied context
//   * GroupEntityId        - SDCA Group Entity ID for the Jack
//   * SelectedMode         - Mode to set for the jack
//
typedef NTSTATUS EVT_SDCAXU_SET_JACK_SELECTED_MODE
(
    _In_ PVOID Context,
    _In_ ULONG GroupEntityId,
    _In_ ULONG SelectedMode
);
typedef EVT_SDCAXU_SET_JACK_SELECTED_MODE *PFN_SDCAXU_SET_JACK_SELECTED_MODE;

//
// EvtPDEPowerReferenceAcquire
// SdcaXu driver will call this method to communicate power requirements for an entity to the class driver.
// The class driver will ensure the specified Power Domain Entity is in or above the required power state.
// If the RequiredState is not supported by the device, this function will return STATUS_NOT_SUPPORTED
// PDEPowerReferenceAcquire can be called any number of times.
// Each successful call to PDEPowerReferenceAcquire should be matched with a call to PDEPowerReferenceRelease.
//
// If the power state changes due to this call, the SdcaXu driver will receive Power Pre and Post notifications
// The SdcaXu driver must not hold a lock while calling this that would interfere with the Power Pre and Post notifications
//
// params:
//   * Context              - SDCA Driver supplied context
//   * PowerDomainEntityId  - Entity ID for the power domain to be updated
//   * RequiredState        - The power state required by the SdcaXu driver for this power domain
//
typedef NTSTATUS EVT_SDCAXU_PDE_POWER_REFERENCE_ACQUIRE
(
    _In_ PVOID                  Context,
    _In_ ULONG                  PowerDomainEntityId,
    _In_ SDCAXU_POWER_STATE     RequiredState
);
typedef EVT_SDCAXU_PDE_POWER_REFERENCE_ACQUIRE *PFN_SDCAXU_PDE_POWER_REFERENCE_ACQUIRE;

//
// EvtPDEPowerReferenceRelease
// SdcaXu driver will call this method to communicate that it no longer requires a power state
// for a Power Domain Entity.
// PDEPowerReferenceRelease should be called once for each PDEPowerReferenceAcquire.
//
// If the power state changes due to this call, the SdcaXu driver will receive Power Pre and Post notifications.
// The SdcaXu driver must not hold a lock while calling this that would interfere with the Power Pre and Post notifications
//
// params:
//   * Context              - SDCA Driver supplied context
//   * PowerDomainEntityId  - Entity ID for the power domain to be updated
//   * ReleasedState        - The power state no longer required by the SdcaXu driver for this power domain
//
typedef NTSTATUS EVT_SDCAXU_PDE_POWER_REFERENCE_RELEASE
(
    _In_ PVOID                  Context,
    _In_ ULONG                  PowerDomainEntityId,
    _In_ SDCAXU_POWER_STATE     ReleasedState
);
typedef EVT_SDCAXU_PDE_POWER_REFERENCE_RELEASE *PFN_SDCAXU_PDE_POWER_REFERENCE_RELEASE;

//
// EvtReadDeferredAudioControls
// SdcaXu driver will call this method to read deferred audio controls.
//
//
// params:
//   * Context              - SDCA Driver supplied context
//   * Controls             - Array of SDCA Audio Controls
//
typedef NTSTATUS EVT_SDCAXU_READ_DEFERRED_AUDIO_CONTROLS
(
    _In_ PVOID                          Context,
    _Inout_ PSDCA_AUDIO_CONTROLS        Controls
);
typedef EVT_SDCAXU_READ_DEFERRED_AUDIO_CONTROLS *PFN_SDCAXU_READ_DEFERRED_AUDIO_CONTROLS;

//
// EvtWriteDeferredAudioControls
// SdcaXu driver will call this method to write deferred audio controls.
//
//
// params:
//   * Context              - SDCA Driver supplied context
//   * Controls             - Array of SDCA Audio Controls
//
typedef NTSTATUS EVT_SDCAXU_WRITE_DEFERRED_AUDIO_CONTROLS
(
    _In_ PVOID                          Context,
    _Inout_ PSDCA_AUDIO_CONTROLS        Controls
);
typedef EVT_SDCAXU_WRITE_DEFERRED_AUDIO_CONTROLS *PFN_SDCAXU_WRITE_DEFERRED_AUDIO_CONTROLS;

typedef struct _SDCAXU_INTERFACE_V0100
{
    //
    // SDCA To SdcaXu Interface
    //
    INTERFACE InterfaceHeader;
    PFN_SDCAXU_SET_HW_CONFIG                EvtSetHwConfig;
    PFN_SDCAXU_SET_ENDPOINT_CONFIG          EvtSetEndpointConfig;
    PFN_SDCAXU_REMOVE_ENDPOINT_CONFIG       EvtRemoveEndpointConfig;
    PFN_SDCAXU_INTERRUPT_HANDLER            EvtInterruptHandler;
    PFN_SDCAXU_CHANGE_NOTIFICATION          EvtChangeNotification;

    //
    // SdcaXu to SDCA Interface
    //
    PFN_SDCAXU_SET_XU_ENTITIES              EvtSetXUEntities;
    PFN_SDCAXU_REGISTER_FOR_INTERRUPTS      EvtRegisterForInterrupts;
    PFN_SDCAXU_SET_JACK_OVERRIDE            EvtSetJackOverride;
    PFN_SDCAXU_SET_JACK_SELECTED_MODE       EvtSetJackSelectedMode;
    PFN_SDCAXU_PDE_POWER_REFERENCE_ACQUIRE  EvtPDEPowerReferenceAcquire;
    PFN_SDCAXU_PDE_POWER_REFERENCE_RELEASE  EvtPDEPowerReferenceRelease;
} SDCAXU_INTERFACE_V0100, *PSDCAXU_INTERFACE_V0100;

typedef struct _SDCAXU_INTERFACE_V0101
{
    //
    // SDCA To SdcaXu Interface
    //
    INTERFACE InterfaceHeader;
    PFN_SDCAXU_SET_HW_CONFIG                        EvtSetHwConfig;
    PFN_SDCAXU_SET_ENDPOINT_CONFIG                  EvtSetEndpointConfig;
    PFN_SDCAXU_REMOVE_ENDPOINT_CONFIG               EvtRemoveEndpointConfig;
    PFN_SDCAXU_INTERRUPT_HANDLER                    EvtInterruptHandler;
    PFN_SDCAXU_CHANGE_NOTIFICATION                  EvtChangeNotification;

    //
    // SdcaXu to SDCA Interface
    //
    PFN_SDCAXU_SET_XU_ENTITIES                      EvtSetXUEntities;
    PFN_SDCAXU_REGISTER_FOR_INTERRUPTS              EvtRegisterForInterrupts;
    PFN_SDCAXU_SET_JACK_OVERRIDE                    EvtSetJackOverride;
    PFN_SDCAXU_SET_JACK_SELECTED_MODE               EvtSetJackSelectedMode;
    PFN_SDCAXU_PDE_POWER_REFERENCE_ACQUIRE          EvtPDEPowerReferenceAcquire;
    PFN_SDCAXU_PDE_POWER_REFERENCE_RELEASE          EvtPDEPowerReferenceRelease;
    PFN_SDCAXU_READ_DEFERRED_AUDIO_CONTROLS         EvtReadDeferredAudioControls;
    PFN_SDCAXU_WRITE_DEFERRED_AUDIO_CONTROLS        EvtWriteDeferredAudioControls;
} SDCAXU_INTERFACE_V0101, *PSDCAXU_INTERFACE_V0101;

typedef struct _SDCAXU_INTERFACE_V0102
{
    //
    // SDCA To SdcaXu Interface
    //
    INTERFACE InterfaceHeader;
    PFN_SDCAXU_SET_HW_CONFIG                        EvtSetHwConfig;
    PFN_SDCAXU_SET_ENDPOINT_CONFIG                  EvtSetEndpointConfig;
    PFN_SDCAXU_REMOVE_ENDPOINT_CONFIG               EvtRemoveEndpointConfig;
    PFN_SDCAXU_INTERRUPT_HANDLER                    EvtInterruptHandler;
    PFN_SDCAXU_CHANGE_NOTIFICATION                  EvtChangeNotification;
    PFN_SDCAXU_RETRIEVE_SWFT_FILE_OVERRIDE          EvtRetrieveSwftFileOverride;

    //
    // SdcaXu to SDCA Interface
    //
    PFN_SDCAXU_SET_XU_ENTITIES                      EvtSetXUEntities;
    PFN_SDCAXU_REGISTER_FOR_INTERRUPTS              EvtRegisterForInterrupts;
    PFN_SDCAXU_SET_JACK_OVERRIDE                    EvtSetJackOverride;
    PFN_SDCAXU_SET_JACK_SELECTED_MODE               EvtSetJackSelectedMode;
    PFN_SDCAXU_PDE_POWER_REFERENCE_ACQUIRE          EvtPDEPowerReferenceAcquire;
    PFN_SDCAXU_PDE_POWER_REFERENCE_RELEASE          EvtPDEPowerReferenceRelease;
    PFN_SDCAXU_READ_DEFERRED_AUDIO_CONTROLS         EvtReadDeferredAudioControls;
    PFN_SDCAXU_WRITE_DEFERRED_AUDIO_CONTROLS        EvtWriteDeferredAudioControls;
} SDCAXU_INTERFACE_V0102, *PSDCAXU_INTERFACE_V0102;
