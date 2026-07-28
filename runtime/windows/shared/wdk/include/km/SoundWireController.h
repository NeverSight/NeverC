/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

 SoundWireController.h

Abstract:

    This module defines the types, constants, functions and control codes that are
    used by device drivers to interact with a SoundWire Controller driver.

Environment:

    Kernel-mode.

--*/

#ifndef _SOUNDWIRECONTROLLER_H_
#define _SOUNDWIRECONTROLLER_H_

#include <windef.h>
#include <ks.h>
#include <mmsystem.h>
#include <ksmedia.h>

#pragma warning(disable:4201) // nameless struct/union

#define SDCA_AUDIO_ADDRESS_VER_1        (1)
#define SOUNDWIRE_INTERRUPTS_VER_1      (1)
#define SOUNDWIRE_NOTIFICATIONS_VER_1   (1)

#define SOUNDWIRE_CAPABILITIES_VER_2    (2)

#define SOUNDWIRE_CONTROLLER_VER_1      (1)
#define SOUNDWIRE_CONTROLLER_VER_2      (2)                 // Support for Commit Groups Added
#define SOUNDWIRE_CONTROLLER_VER_3      (3)                 // Support for Peripheral and DataPort Capabilities
#define SOUNDWIRE_CONTROLLER_VER_4      (4)                 // Support for Clock Reference DDI
#define SOUNDWIRE_CONTROLLER_VER_5      (5)                 // Support for DataFormat in PrepareDataPort call

#define MAX_NUM_DATAPORTS               (15)
#define MAX_NUM_LANES                   (8)                 // Lane 0 through 7

typedef enum _SOUNDWIRE_COMMAND_PRIORITY
{
    SoundWireCommandPriorityInvalid     = 0,                // Invalid priority
    SoundWireCommandPriorityNormal      = 1,                // Normal priority
    SoundWireCommandPriorityHigh        = 2,                // High priority
    SoundWireCommandPriorityCritical    = 3,                // Critical priority
} SOUNDWIRE_COMMAND_PRIORITY;

typedef enum _SOUNDWIRE_DUAL_RANK_ATTRIBUTE
{
    SoundWireDualRankAttributeCvr = 0,                      // Current Value Register
    SoundWireDualRankAttributeNvr = 1,                      // Next Value Register
} SOUNDWIRE_DUAL_RANK_ATTRIBUTE;

typedef struct _SDCA_AUDIO_ADDRESS
{
    ULONG                           Size;                   // Size of this struct
    UINT8                           Version;                // Version of this struct
    union {
        UINT8                           FunctionId;         // deprecated, Soundwire specification update, this entry is renamed FunctionNumber
        UINT8                           FunctionNumber;     // Sdca Audio Function Number
    };
    UINT8                           EntityId;               // Sdca Entity Id
    UINT8                           ControlSelector;        // Sdca Control Selector
    UINT8                           ControlNumber;          // Sdca Control Number
    SOUNDWIRE_DUAL_RANK_ATTRIBUTE   DualRankAttribute;      // Current or Next Value
} SDCA_AUDIO_ADDRESS, *PSDCA_AUDIO_ADDRESS;

typedef struct _SDCA_AUDIO_CONTROL
{
    SDCA_AUDIO_ADDRESS          Address;                    // Sdca hierarchical address
    ULONG                       ValueSize;                  // Size (number of bytes) of Value
    LONGLONG                    Value;                      // Single of Multi-byte value (max 8 bytes) of an Sdca Control
    NTSTATUS                    Status;                     // Status of Read/Write operation
} SDCA_AUDIO_CONTROL, *PSDCA_AUDIO_CONTROL;

typedef struct _SDCA_AUDIO_CONTROLS
{
    ULONG                       Size;                       // Size of this struct including size of Control array
    SOUNDWIRE_COMMAND_PRIORITY  Priority;                   // Priority
    ULONG                       ControlCount;               // Count of Sdca Controls in Control array 
    _Field_size_(ControlCount)
    SDCA_AUDIO_CONTROL          Control[ANYSIZE_ARRAY];     // Array of Sdca Controls
} SDCA_AUDIO_CONTROLS, *PSDCA_AUDIO_CONTROLS;

typedef struct _SDCA_AUDIO_CONTROLS_2
{
    ULONG                       Size;                       // Size of this struct including size of Control array
    ULONG                       CommitGroupHandle;          // Commit Group Handle (optional)
    SOUNDWIRE_COMMAND_PRIORITY  Priority;                   // Priority
    ULONG                       ControlCount;               // Count of Sdca Controls in Control array 
    _Field_size_(ControlCount)
    SDCA_AUDIO_CONTROL          Control[ANYSIZE_ARRAY];     // Array of Sdca Controls
} SDCA_AUDIO_CONTROLS_2, *PSDCA_AUDIO_CONTROLS_2;

typedef struct _SOUNDWIRE_MEMORY
{
    ULONG                       Size;                       // Size of this struct including size of Buffer array
    SOUNDWIRE_COMMAND_PRIORITY  Priority;                   // Priority
    ULONG                       Address;                    // 32-bit Register Address
    ULONG                       BufferSize;                 // Size (number of Bytes) of Buffer
    _Field_size_(BufferSize)
    BYTE                        Buffer[ANYSIZE_ARRAY];      // Buffer containing 8-bit values
} SOUNDWIRE_MEMORY, *PSOUNDWIRE_MEMORY;

typedef struct _SOUNDWIRE_MEMORY_2
{
    ULONG                       Size;                       // Size of this struct including size of Buffer array
    ULONG                       CommitGroupHandle;          // Commit Group Handle (optional)
    SOUNDWIRE_COMMAND_PRIORITY  Priority;                   // Priority
    ULONG                       Address;                    // 32-bit Register Address
    ULONG                       BufferSize;                 // Size (number of Bytes) of Buffer
    _Field_size_(BufferSize)
    BYTE                        Buffer[ANYSIZE_ARRAY];      // Buffer containing 8-bit values
} SOUNDWIRE_MEMORY_2, *PSOUNDWIRE_MEMORY_2;

typedef struct _SOUNDWIRE_REGISTER
{
    ULONG                       Address;                    // 32-bit Register Address
    BYTE                        Value;                      // 8-bit Register Value
    BYTE                        Reserved1;                  // Reserved, must be set to zero
    BYTE                        Reserved2;                  // Reserved, must be set to zero
    BYTE                        Reserved3;                  // Reserved, must be set to zero
    NTSTATUS                    Status;                     // Status of Read/Write operation
} SOUNDWIRE_REGISTER, *PSOUNDWIRE_REGISTER;

typedef struct _SOUNDWIRE_REGISTERS
{
    ULONG                       Size;                       // Size of this struct including size of Register array
    SOUNDWIRE_COMMAND_PRIORITY  Priority;                   // Priority
    ULONG                       RegisterCount;              // Count of registers in Register array
    _Field_size_(RegisterCount)
    SOUNDWIRE_REGISTER          Register[ANYSIZE_ARRAY];    // Array of SoundWire registers
} SOUNDWIRE_REGISTERS, *PSOUNDWIRE_REGISTERS;

typedef struct _SOUNDWIRE_REGISTERS_2
{
    ULONG                       Size;                       // Size of this struct including size of Register array
    ULONG                       CommitGroupHandle;          // Commit Group Handle (optional)
    SOUNDWIRE_COMMAND_PRIORITY  Priority;                   // Priority
    ULONG                       RegisterCount;              // Count of registers in Register array
    _Field_size_(RegisterCount)
    SOUNDWIRE_REGISTER          Register[ANYSIZE_ARRAY];    // Array of SoundWire registers
} SOUNDWIRE_REGISTERS_2, *PSOUNDWIRE_REGISTERS_2;

typedef struct _SOUNDWIRE_INTERRUPTS
{
    ULONG                       Size;                                   // Size of this struct
    UINT8                       Version;                                // Version of this struct
    ULONG                       SCPInterrupts;                          // SCP interrupt registers
    UINT8                       DataPortInterrupts[MAX_NUM_DATAPORTS];  // Array of DataPort interrupt registers
} SOUNDWIRE_INTERRUPTS, *PSOUNDWIRE_INTERRUPTS;

/*++
Routine Description:
    Callback routine invoked by Controller driver when any masked interrupts
    are generated.

Arguments:
    Context         - Context provided by driver
    IntStats        - SoundWire interrupt status
    Size            - Size of struct pointed by IntStats parameter

Returns:
    NTSTATUS        - STATUS_SUCCESS if callback was processed successfully.
--*/
typedef NTSTATUS EVT_SOUNDWIRE_INTERRUPT_CALLBACK(
    _In_    PVOID                   Context,
    _In_    PSOUNDWIRE_INTERRUPTS   IntStats,
    _In_    ULONG                   Size
    );

typedef EVT_SOUNDWIRE_INTERRUPT_CALLBACK *PFNSOUNDWIRE_INTERRUPT_CALLBACK;

typedef struct _SOUNDWIRE_INTERRUPT_CALLBACK
{
    ULONG                                   Size;                   // Size of this struct
    PVOID                                   Context;                // Driver provided context
    PFNSOUNDWIRE_INTERRUPT_CALLBACK         EvtInterruptCallback;   // Interrupt callback routine
} SOUNDWIRE_INTERRUPT_CALLBACK, *PSOUNDWIRE_INTERRUPT_CALLBACK;

typedef enum _SOUNDWIRE_NOTIFICATION
{
    SoundWireNotificationInvalid = 0x00000000,              // Invalid notification
    SoundWireNotificationAttach  = 0x00000001,              // Peripheral attach notification
} SOUNDWIRE_NOTIFICATION;

typedef struct _SOUNDWIRE_NOTIFICATIONS
{
    ULONG                       Size;                       // Size of this struct
    UINT8                       Version;                    // Version of this struct
    ULONG                       Notifications;              // Bit mask of SoundWire notifications
} SOUNDWIRE_NOTIFICATIONS, *PSOUNDWIRE_NOTIFICATIONS;

/*++
Routine Description:
    Callback routine invoked by Controller driver when any masked notifications
    are generated.

Arguments:
    Context         - Context provided by driver
    Notifications   - SoundWire notifications
    Size            - Size of struct pointed by Notifications parameter

Returns:
    NTSTATUS        - STATUS_SUCCESS if callback was processed successfully.
--*/
typedef NTSTATUS EVT_SOUNDWIRE_NOTIFICATION_CALLBACK(
    _In_    PVOID                       Context,
    _In_    PSOUNDWIRE_NOTIFICATIONS    Notifications,
    _In_    ULONG                       Size
    );

typedef EVT_SOUNDWIRE_NOTIFICATION_CALLBACK *PFNSOUNDWIRE_NOTIFICATION_CALLBACK;

typedef struct _SOUNDWIRE_NOTIFICATION_CALLBACK
{
    ULONG                                   Size;                       // Size of this struct
    PVOID                                   Context;                    // Driver provided context
    PFNSOUNDWIRE_NOTIFICATION_CALLBACK      EvtNotificationCallback;    // Notification callback routine
} SOUNDWIRE_NOTIFICATION_CALLBACK, *PSOUNDWIRE_NOTIFICATION_CALLBACK;

typedef enum _SOUNDWIRE_PERIPHERAL_FLAGS
{
    SoundWirePeripheralFlagSubSystemIdPresent  = 0x00000001,            // SubSystem Id is present
    SoundWirePeripheralFlagControllerIdPresent = 0x00000002,            // Controller Id is present
    SoundWirePeripheralFlagLinkIdPresent       = 0x00000004,            // Link Id is present
    SoundWirePeripheralFlagPeripheralIdPresent = 0x00000008,            // Peripheral Id is present
} SOUNDWIRE_PERIPHERAL_FLAGS;

typedef struct _SOUNDWIRE_PERIPHERAL_INFORMATION
{
    ULONG                       Size;                       // Size of this struct
    ULONG                       Flags;                      // Bit mask indicating which flags are set
    ULONG                       SubSystemId;                // SubSystem Id
    UINT8                       ControllerId;               // Controller Id
    UINT8                       LinkId;                     // Link Id within a Controller
    UINT8                       PeripheralId;               // Manager assigned Peripheral Id
} SOUNDWIRE_PERIPHERAL_INFORMATION, *PSOUNDWIRE_PERIPHERAL_INFORMATION;

typedef enum _SOUNDWIRE_DATAPORT_TYPE
{
    SoundWireDataPortTypeInvalid    = 0,                    // Invalid DataPort type
    SoundWireDataPortTypeFull       = 1,                    // Full DataPort
    SoundWireDataPortTypeSimplified = 2,                    // Simplified DataPort 
    SoundWireDataPortTypeReduced    = 3,                    // Reduced DataPort
} SOUNDWIRE_DATAPORT_TYPE;

typedef enum _SOUNDWIRE_CHANNEL_PREPARE_TYPE
{
    SoundWireChannelPrepareTypeInvalid          = 0,        // Invalid Channel Prepare type
    SoundWireChannelPrepareTypeCpSm             = 1,        // Full Channel Prepare
    SoundWireChannelPrepareTypeSimplifiedCpSm   = 2,        // Simplified Channel Prepare
} SOUNDWIRE_CHANNEL_PREPARE_TYPE;

typedef enum _SOUNDWIRE_DATAPORT_MODE
{
    SoundWireDataPortModeInvalid            = 0x00000000,   // Invalid mode
    SoundWireDataPortModeIsochronous        = 0x00000001,   // Isochronous mode
    SoundWireDataPortModeTxControlled       = 0x00000002,   // Tx Controlled mode
    SoundWireDataPortModeRxControlled       = 0x00000004,   // Rx Controlled mode
    SoundWireDataPortModeFullAsynchronous   = 0x00000008,   // Asynchronous mode
} SOUNDWIRE_DATAPORT_MODE;

typedef enum _SOUNDWIRE_DATAPORT_DIRECTION
{
    SoundWireDataPortDirectionInvalid   = 0,                // Invalid direction
    SoundWireDataPortDirectionSink      = 1,                // Sink DataPort
    SoundWireDataPortDirectionSource    = 2,                // Source DataPort
} SOUNDWIRE_DATAPORT_DIRECTION;

typedef struct _SOUNDWIRE_DATAPORT_CONFIGURATION
{
    ULONG                       Size;                       // Size of this struct
    ULONG                       DataPortNumber;             // DataPort number
    ULONG                       EndpointId;                 // Endpoint Id
    ULONG                       Modes;                      // Bit mask of DataPort modes
    ULONG                       ChannelMask;                // DataPort channel mask
} SOUNDWIRE_DATAPORT_CONFIGURATION, *PSOUNDWIRE_DATAPORT_CONFIGURATION;

typedef struct _SOUNDWIRE_DATAPORT_CONFIGURATION_2
{
    ULONG                                   Size;            // Size of this struct
    ULONG                                   DataPortNumber;  // DataPort number
    ULONG                                   EndpointId;      // Endpoint Id
    ULONG                                   Modes;           // Bit mask of DataPort modes
    ULONG                                   ChannelMask;     // DataPort channel mask
    KSDATAFORMAT_WAVEFORMATEXTENSIBLE       Format;          // Stream format. Full structure may not be used.
} SOUNDWIRE_DATAPORT_CONFIGURATION_2, *PSOUNDWIRE_DATAPORT_CONFIGURATION_2;

typedef struct SOUNDWIRE_DATAPORT_CAPABILITIES
{
    ULONG                            Size;                  // Size of this struct
    ULONG                            DataPortNumber;        // DataPort number
    ULONG                            EndpointId;            // Endpoint Id
    SOUNDWIRE_DATAPORT_TYPE          DataPortType;          // DataPort type
    SOUNDWIRE_CHANNEL_PREPARE_TYPE   ChannelPrepareType;    // Channel prepare type
    SOUNDWIRE_DATAPORT_DIRECTION     Direction;             // DataPort direction
} SOUNDWIRE_DATAPORT_CAPABILITIES, *PSOUNDWIRE_DATAPORT_CAPABILITIES;

typedef enum _SOUNDWIRE_TRIGGER_PURPOSE
{
    SoundWireTriggerPurposeInvalid      = 0,                // invalid purpose
    SoundWireTriggerPurposeUltrasound   = 1,                // ultrasound trigger
    SoundWireTriggerPurposeSpeech       = 2,                // speech onset trigger
    SoundWireTriggerPurposeVoice        = 3,                // voice detection trigger
} SOUNDWIRE_TRIGGER_PURPOSE;

typedef struct _SOUNDWIRE_TRIGGER_CONFIGURATION
{
    ULONG                           Size;                   // Size of this struct
    ULONG                           DataPortNumber;         // DataPort number
    ULONG                           EndpointId;             // Endpoint Id
    SOUNDWIRE_TRIGGER_PURPOSE       Purpose;                // voice, speech, ultrasound
} SOUNDWIRE_TRIGGER_CONFIGURATION, *PSOUNDWIRE_TRIGGER_CONFIGURATION;

typedef struct _SOUNDWIRE_DATAPORT_STARTSTOP
{
    ULONG                           Size;
    ULONG                           CommitGroupHandle;
    ULONG                           DataPortNumber;
} SOUNDWIRE_DATAPORT_STARTSTOP, *PSOUNDWIRE_DATAPORT_STARTSTOP;

typedef enum _SOUNDWIRE_PORT15_READ_BEHAVIOR
{
    SoundWirePort15ReadBehaviorInvalid          = 0,
    SoundWirePort15ReadBehaviorCommandIgnored   = 1,
    SoundWirePort15ReadBehaviorCommandOk        = 2,
} SOUNDWIRE_PORT15_READ_BEHAVIOR, *PSOUNDWIRE_PORT15_READ_BEHAVIOR;

#define LANE_MAPPING_MAX_LENGTH 31  // Allow up to 5 characters for peripheral link tag plus terminator: "mipi-sdw-peripheral-link-12345"
typedef struct _SOUNDWIRE_PERIPHERAL_CAPABILITIES
{
    ULONG                           Size;
    ULONG                           Version;
    union
    {
        ULONG                       SwInterfaceRevision;
        struct
        {
            ULONG                   SwInterfaceDisCoMinorVersion            : 16;
            ULONG                   SwInterfaceDisCoMajorVersion            : 16;
        };
    };
    union
    {
        ULONG                       SdcaInterfaceRevision;
        struct
        {
            ULONG                   SdcaInterfaceDraftRevision              : 8;
            ULONG                   SdcaInterfaceMinorVersion               : 8;
            ULONG                   SdcaInterfaceMajorVersion               : 16;
        };
    };
    BOOLEAN                         WakeUpUnavailable;
    BOOLEAN                         TestModeSupported;
    BOOLEAN                         ClockStopMode1Supported;
    BOOLEAN                         SimplifiedClockStopPrepareSMSupported;
    ULONG                           ClockStopPrepareTimeoutInMS;
    ULONG                           PeripheralChannelPrepareTimeoutInMS;
    union
    {
        ULONG                       ClockStopPrepareHardResetBehavior;
        struct
        {
            ULONG                   ClockStopPrepareHardResetKeepSMStatus   : 1;
            ULONG                   Reserved                                : 31;
        };
    };
    BOOLEAN                         HighPHYCapable;
    BOOLEAN                         PagingSupported;
    BOOLEAN                         BankDelaySupported;
    SOUNDWIRE_PORT15_READ_BEHAVIOR  Port15ReadBehavior;
    union
    {
        ULONG                       LaneBusHolder;          // Bitmap
        struct
        {
            ULONG                   LaneBusHolderReserved0                  : 1;
            ULONG                   LaneBusHolder1                          : 1;
            ULONG                   LaneBusHolder2                          : 1;
            ULONG                   LaneBusHolder3                          : 1;
            ULONG                   LaneBusHolder4                          : 1;
            ULONG                   LaneBusHolder5                          : 1;
            ULONG                   LaneBusHolder6                          : 1;
            ULONG                   LaneBusHolder7                          : 1;
            ULONG                   LaneBusHolderReservedN                  : 24;
        };
    };

    // Array of zero-terminated strings that indicate the lane mapping. These are directly from the ACPI.
    // They will be either:
    // mipi-sdw-manager-lane-<m> (where m is 1 through 7 inclusive)
    // mipi-sdw-peripheral-link-<tag> (where tag is 1 to 8 characters between 0-9, a-z, or A-Z, that uniquely identify a Link)
    // LaneMapping[0] will be an empty string since Lane 0 can't be mapped.
    CHAR                            LaneMapping[MAX_NUM_LANES][LANE_MAPPING_MAX_LENGTH];

    ULONG                           SdcaInterruptRegisterList;
    ULONG                           DataPortSourceList;     // Bitmap
    ULONG                           DataPortSinkList;       // Bitmap
    BOOLEAN                         DataPort0Supported;
    BOOLEAN                         CommitRegisterSupported;
} SOUNDWIRE_PERIPHERAL_CAPABILITIES, *PSOUNDWIRE_PERIPHERAL_CAPABILITIES;

typedef struct _SOUNDWIRE_BRA_MODE
{
    ULONG           Size;
    ULONG           BRAMode; // Bitmap
    ULONG           BusFrequencyMin;
    ULONG           BusFrequencyMax;
    ULONG           DataPerFrameMax;
    ULONG           TransactionDelayInUS;
    ULONG           BandwidthMax;
    ULONG           BlockAlignment;
    ULONG           BusFrequencyListCount;
    _Field_size_(BusFrequencyListCount)
    ULONG           BusFrequencyList[ANYSIZE_ARRAY]; // Could this be limited to a specific max number of frequencies
} SOUNDWIRE_BRA_MODE, *PSOUNDWIRE_BRA_MODE;

typedef struct _SOUNDWIRE_DATAPORT_CAPS_HEADER
{
    ULONG           Size; // Size of the complete DATAPORT0 or DATAPORTN structure including additional arrays and add-on data
    ULONG           Version;
    ULONG           DataPortNumber;
} SOUNDWIRE_DATAPORT_CAPS_HEADER, *PSOUNDWIRE_DATAPORT_CAPS_HEADER;

typedef struct _SOUNDWIRE_DATAPORT0_CAPABILITIES_2
{
    SOUNDWIRE_DATAPORT_CAPS_HEADER  Header;

    ULONGLONG                       WordLengthsSupported; // Bitmap: Bit 0 = length of 1 supported, bit 63 = length of 64 supported
    BOOLEAN                         BRAFlowControlled;
    BOOLEAN                         BRAImpDefResponseSupported;
    ULONG                           BRARoleSupported;
    BOOLEAN                         SimplifiedChannelPrepareStateMachine;
    BOOLEAN                         ChannelPrepareTimeoutPresent;
    ULONG                           ChannelPrepareTimeoutInMS;
    union
    {
        ULONG                       ImpDefDp0InterruptsSupported; // Bitmap
        struct
        {
            ULONG                   ImpDefDp0Interrupt1Supported    : 1;
            ULONG                   ImpDefDp0Interrupt2Supported    : 1;
            ULONG                   ImpDefDp0Interrupt3Supported    : 1;
            ULONG                   ImpDefDp0InterruptReserved      : 29;
        };
    };
    BOOLEAN                         ImpDefBptSupported;
    ULONG                           LaneListCount;
    UINT8                           LaneList[MAX_NUM_LANES];
    ULONG                           BRAModeCount;
    _Field_size_(BRAModeCount)
    ULONG                           BRAModeByteOffsetFromBeginningOfThisStructure[ANYSIZE_ARRAY];
} SOUNDWIRE_DATAPORT0_CAPABILITIES_2, *PSOUNDWIRE_DATAPORT0_CAPABILITIES_2;

typedef struct _SOUNDWIRE_DATAPORTN_CAPABILITIES_2
{
    SOUNDWIRE_DATAPORT_CAPS_HEADER  Header;

    SOUNDWIRE_DATAPORT_DIRECTION    Direction;
    ULONGLONG                       WordLengthsSupported; // Bitmap: Bit 0 = length of 1 supported, bit 63 = length of 64 supported
    SOUNDWIRE_DATAPORT_TYPE         DataPortType;
    BOOLEAN                         MaxGroupingSupportedPresent;
    ULONG                           MaxGroupingSupported;
    BOOLEAN                         SimplifiedChannelPrepareStateMachine;
    BOOLEAN                         ChannelPrepareTimeoutPresent;
    ULONG                           ChannelPrepareTimeoutInMS;
    union
    {
        ULONG                       ImpDefDpNInterruptsSupported; // Bitmap
        struct
        {
            ULONG                   ImpDefDpNInterrupt1Supported    : 1;
            ULONG                   ImpDefDpNInterrupt2Supported    : 1;
            ULONG                   ImpDefDpNInterrupt3Supported    : 1;
            ULONG                   ImpDefDpNInterruptReserved      : 29;
        };
    };
    ULONG                           ChannelNumberList; // Bitmap: Bit 0 = channel 0, Bit 7 = channel 7
    BOOLEAN                         ModesSupportedPresent;
    union
    {
        ULONG                       ModesSupported; // Bitmap
        struct
        {
            ULONG                   ModesSupportedIsochronous       : 1;
            ULONG                   ModesSupportedTx                : 1;
            ULONG                   ModesSupportedRx                : 1;
            ULONG                   ModesSupportedAsync             : 1;
            ULONG                   ModesSupportedReserved          : 28;
        };
    };
    ULONG                           MaxAsyncBuffer;
    BOOLEAN                         BlockPackingModePresent;
    BOOLEAN                         BlockPackingMode;
    BOOLEAN                         PortEncodingTypePresent;
    union
    {
        ULONG                       PortEncodingType;
        struct
        {
            ULONG                   PortEncodingTypeTwosComplement  : 1;
            ULONG                   PortEncodingTypeSignMagnitude   : 1;
            ULONG                   PortEncodingTypeIeee32          : 1;
            ULONG                   PortEncodingTypeReserved        : 29;
        };
    };
    ULONG                           LaneListCount;
    UINT8                           LaneList[MAX_NUM_LANES];
    ULONG                           ChannelCombinationListCount;
    _Field_size_(ChannelCombinationListCount)
    ULONG                           ChannelCombinationList[ANYSIZE_ARRAY];
} SOUNDWIRE_DATAPORTN_CAPABILITIES_2, *PSOUNDWIRE_DATAPORTN_CAPABILITIES_2;

//
// Control codes
//

#define SOUNDWIRE_IOCTL(_index_) \
    CTL_CODE (FILE_DEVICE_SOUNDWIRE, _index_, METHOD_NEITHER, FILE_ANY_ACCESS)

//
// Retrieve the SoundWire Controller Interface Version.
//
// Required for SOUNDWIRE_CONTROLLER_VER_2 and newer versions
// (Failure indicates VER_1)
//
// InputBuffer - NULL
// OutputBuffer - ULONG
//
#define IOCTL_SOUNDWIRE_GET_CONTROLLER_VERSION                               SOUNDWIRE_IOCTL (0)

//
// Returns information about peripheral.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - NULL
// OutputBuffer - SOUNDWIRE_PERIPHERAL_INFORMATION
//
#define IOCTL_SOUNDWIRE_GET_PERIPHERAL_INFORMATION                           SOUNDWIRE_IOCTL (1)

//
// Provides data port capabilities details.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SOUNDWIRE_DATAPORT_CAPABILITIES
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_SET_DATAPORT_CAPABILITIES                            SOUNDWIRE_IOCTL (2)

//
// Writes variable-length value to the SDCA register address.
// Equivalent to calling IOCTL_SDCA_WRITE_AUDIO_CONTROLS_2 with CommitGroupHandle 0.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SDCA_AUDIO_CONTROLS
// OutputBuffer - NULL
//
#define IOCTL_SDCA_WRITE_AUDIO_CONTROLS                                      SOUNDWIRE_IOCTL (3)

//
// Reads variable-length value from the SDCA register address.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SDCA_AUDIO_CONTROLS
// OutputBuffer - NULL
//
#define IOCTL_SDCA_READ_AUDIO_CONTROLS                                       SOUNDWIRE_IOCTL (4)

//
// Writes single byte to one or more registers.
// Equivalent to calling IOCTL_SDCA_WRITE_REGISTERS_2 with CommitGroupHandle 0.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SOUNDWIRE_REGISTERS
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_WRITE_REGISTERS                                      SOUNDWIRE_IOCTL (5)

//
// Reads single byte from one or more registers.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SOUNDWIRE_REGISTERS
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_READ_REGISTERS                                       SOUNDWIRE_IOCTL (6)

//
// Writes one or more bytes to the registers starting from given address.
// Controller will use Bulk Register Access for applicable sizes when supported.
// Equivalent to calling IOCTL_SDCA_WRITE_MEMORY_2 with CommitGroupHandle 0.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SOUNDWIRE_MEMORY
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_WRITE_MEMORY                                         SOUNDWIRE_IOCTL (7)

//
// Reads one or more bytes from the registers starting from given address.
// Controller will use Bulk Register Access for applicable sizes when supported.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SOUNDWIRE_MEMORY
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_READ_MEMORY                                          SOUNDWIRE_IOCTL (8)

//
// Enables or disables notifications.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SOUNDWIRE_NOTIFICATIONS
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_MASK_NOTIFICATIONS                                   SOUNDWIRE_IOCTL (9)

//
// Registers notification callback routine.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SOUNDWIRE_NOTIFICATION_CALLBACK
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_REGISTER_NOTIFICATION_CALLBACK                       SOUNDWIRE_IOCTL (10)

//
// Unregisters notification callback routine.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SOUNDWIRE_NOTIFICATION_CALLBACK
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_UNREGISTER_NOTIFICATION_CALLBACK                     SOUNDWIRE_IOCTL (11)

//
// Enables or disables interrupts.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SOUNDWIRE_INTERRUPTS
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_MASK_INTERRUPTS                                      SOUNDWIRE_IOCTL (12)

//
// Registers interrupt callback routine.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SOUNDWIRE_INTERRUPT_CALLBACK
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_REGISTER_INTERRUPT_CALLBACK                          SOUNDWIRE_IOCTL (13)

//
// Unregisters interrupt callback routine.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SOUNDWIRE_INTERRUPT_CALLBACK
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_UNREGISTER_INTERRUPT_CALLBACK                        SOUNDWIRE_IOCTL (14)

//
// Configures data port for streaming.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SOUNDWIRE_DATAPORT_CONFIGURATION
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_PREPARE_DATAPORT                                     SOUNDWIRE_IOCTL (15)

//
// Deconfigures data port for streaming.
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - SOUNDWIRE_DATAPORT_CONFIGURATION
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_DEPREPARE_DATAPORT                                   SOUNDWIRE_IOCTL (16)

//
// Starts data port for streaming.
// Equivalent to calling IOCTL_SOUNDWIRE_START_DATAPORT_2 with CommitGroupHandle 0
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - ULONG
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_START_DATAPORT                                       SOUNDWIRE_IOCTL (17)

//
// Stops data port for streaming.
// Equivalent to calling IOCTL_SOUNDWIRE_STOP_DATAPORT_2 with CommitGroupHandle 0
//
// Required for SOUNDWIRE_CONTROLLER_VER_1 and newer versions
//
// InputBuffer - ULONG
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_STOP_DATAPORT                                        SOUNDWIRE_IOCTL (18)

// Writes variable-length value to the SDCA register address.
//
// Required for SOUNDWIRE_CONTROLLER_VER_2 and newer versions
//
// InputBuffer - SDCA_AUDIO_CONTROLS_2
// OutputBuffer - NULL
//
#define IOCTL_SDCA_WRITE_AUDIO_CONTROLS_2                                    SOUNDWIRE_IOCTL (19)

//
// Writes single byte to one or more registers.
//
// Required for SOUNDWIRE_CONTROLLER_VER_2 and newer versions
//
// InputBuffer - SOUNDWIRE_REGISTERS_2
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_WRITE_REGISTERS_2                                    SOUNDWIRE_IOCTL (20)

//
// Writes one or more bytes to the registers starting from given address.
// Controller will use Bulk Register Access for applicable sizes when supported.
//
// Required for SOUNDWIRE_CONTROLLER_VER_2 and newer versions
//
// InputBuffer - SOUNDWIRE_MEMORY_2
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_WRITE_MEMORY_2                                       SOUNDWIRE_IOCTL (21)

//
// Starts data port for streaming.
//
// Required for SOUNDWIRE_CONTROLLER_VER_2 and newer versions
//
// InputBuffer - SOUNDWIRE_DATAPORT_STARTSTOP
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_START_DATAPORT_2                                     SOUNDWIRE_IOCTL (22)

//
// Stops data port for streaming.
//
// Required for SOUNDWIRE_CONTROLLER_VER_2 and newer versions
//
// InputBuffer - SOUNDWIRE_DATAPORT_STARTSTOP
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_STOP_DATAPORT_2                                      SOUNDWIRE_IOCTL (23)

//
// Communicates trigger configuration for the data port.
//
// Required for SOUNDWIRE_CONTROLLER_VER_2 and newer versions
//
// InputBuffer - SOUNDWIRE_TRIGGER_CONFIGURATION
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_PREPARE_TRIGGER                                      SOUNDWIRE_IOCTL (24)

//
// Communicates trigger configuration for the data port.
//
// Required for SOUNDWIRE_CONTROLLER_VER_2 and newer versions
//
// InputBuffer - SOUNDWIRE_TRIGGER_CONFIGURATION
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_DEPREPARE_TRIGGER                                    SOUNDWIRE_IOCTL (25)

//
// Communicates private vendor/platfor-specific information to Controller.
//
// Required for SOUNDWIRE_CONTROLLER_VER_2 and newer versions
//
// InputBuffer - PVOID - pointer to vendor-defined structure
// OutputBuffer - PVOID - pointer to vendor-defined structure
//
#define IOCTL_SOUNDWIRE_VENDOR_SPECIFIC                                      SOUNDWIRE_IOCTL (26)

//
// Provides peripheral device capabilities.
//
// Required for SOUNDWIRE_CONTROLLER_VER_3 and newer versions
//
// InputBuffer - SOUNDWIRE_PERIPHERAL_CAPABILITIES
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_SET_PERIPHERAL_CAPABILITIES                          SOUNDWIRE_IOCTL (27)

//
// Provides data port capabilities details. Use the DataPort member to determine if the struct
// is DATAPORT0 or DATAPORTN.
//
// Required for SOUNDWIRE_CONTROLLER_VER_3 and newer versions
//
// InputBuffer - SOUNDWIRE_DATAPORT0_CAPABILITIES_2 OR SOUNDWIRE_DATAPORTN_CAPABILITIES_2
// OutputBuffer - NULL
//
#define IOCTL_SOUNDWIRE_SET_DATAPORT_CAPABILITIES_2                          SOUNDWIRE_IOCTL (28)

//
// Increases Clock Reference count by One.
//
// Required for SOUNDWIRE_CONTROLLER_VER_4 and newer versions
//
// InputBuffer - NULL
// OutputBuffer - ULONG
//
#define IOCTL_SOUNDWIRE_ACQUIRE_CLOCK_REFERENCE                              SOUNDWIRE_IOCTL (29)

//
// Decreases Clock Reference count by One.
//
// Required for SOUNDWIRE_CONTROLLER_VER_4 and newer versions
//
// InputBuffer - NULL
// OutputBuffer - ULONG
//
#define IOCTL_SOUNDWIRE_RELEASE_CLOCK_REFERENCE                              SOUNDWIRE_IOCTL (30)

//
// Configures data port for streaming.
//
// Required for SOUNDWIRE_CONTROLLER_VER_5 and newer versions
//
// InputBuffer - SOUNDWIRE_DATAPORT_CONFIGURATION_2
// OutputBuffer - NULL
//
// Note: IOCTL_SOUNDWIRE_DEPREPARE_DATAPORT will be called to
// deconfigure a data port that was configured using this IOCTL.
//
#define IOCTL_SOUNDWIRE_PREPARE_DATAPORT_2                                   SOUNDWIRE_IOCTL (31)

#endif // _SOUNDWIRECONTROLLER_H_
