/*++

Copyright (c) 2004 Microsoft Corporation

Module Name:

    dispmprt.h

Abstract:

    This header contain the new Display Loader APIs.


--*/

#ifndef _DISPMPRT_H_
#define _DISPMPRT_H_

#pragma warning(push)
#pragma warning(disable:4115) // named type definition in parentheses
#pragma warning(disable:4201) // nameless struct/union
#pragma warning(disable:4214) // bit field types other than int

#ifndef _ACPIIOCT_H_
#include <acpiioct.h>
#endif

//
// Old includes which are only kept for now for compatibility
//

#ifndef _NTOSP_
#define _NTOSP_

//
// Structures used by the kernel drivers to describe which ports must be
// hooked out directly from the V86 emulator to the driver.
//

typedef enum _EMULATOR_PORT_ACCESS_TYPE {
    Uchar,
    Ushort,
    Ulong
} EMULATOR_PORT_ACCESS_TYPE, *PEMULATOR_PORT_ACCESS_TYPE;

typedef struct _EMULATOR_ACCESS_ENTRY {
    ULONG BasePort;
    ULONG NumConsecutivePorts;
    EMULATOR_PORT_ACCESS_TYPE AccessType;
    UCHAR AccessMode;
    UCHAR StringSupport;
    PVOID Routine;
} EMULATOR_ACCESS_ENTRY, *PEMULATOR_ACCESS_ENTRY;

#endif

//
// Graphics support routines.
//

typedef
VOID
(*PBANKED_SECTION_ROUTINE) (
    IN ULONG ReadBank,
    IN ULONG WriteBank,
    IN PVOID Context
    );

//
// Exclude some items from video.h
//

#define _NTOSDEF_

#include <ntddvdeo.h>
#include <video.h>

//
// Define types used in d3dukmdt.h, included via d3dkmdt.h that are not defined in this scope.
//

typedef unsigned int   UINT;
typedef unsigned long  DWORD;
typedef unsigned char  BYTE;

#include <windef.h>
#include <d3dkmddi.h>
#include <d3dkmdt.h>

#define DlDebugPrintEx(arg) DlDebugPrint arg

//
// ** New definitions *********************************************************
//

//
// Available only for Vista (LONGHORN) and later
//
#if (NTDDI_VERSION >= NTDDI_LONGHORN)

//
// Define ACPI event IDs
//

#define ACPI_NOTIFY_DOCK_EVENT              0x77
#define ACPI_NOTIFY_PANEL_SWITCH            0x80
#define ACPI_NOTIFY_DEVICE_HOTPLUG          0x81
#define ACPI_NOTIFY_CYCLE_DISPLAY_HOTKEY    0x82
#define ACPI_NOTIFY_NEXT_DISPLAY_HOTKEY     0x83
#define ACPI_NOTIFY_PREV_DISPLAY_HOTKEY     0x84
#define ACPI_NOTIFY_CYCLE_BRIGHTNESS_HOTKEY 0x85
#define ACPI_NOTIFY_INC_BRIGHTNESS_HOTKEY   0x86
#define ACPI_NOTIFY_DEC_BRIGHTNESS_HOTKEY   0x87
#define ACPI_NOTIFY_ZERO_BRIGHTNESS_HOTKEY  0x88
#define ACPI_NOTIFY_VIDEO_WAKEUP            0x90

//
// ACPI argument definitions
//

#define ACPI_ARG_ENABLE_SWITCH_EVENT         0x0
#define ACPI_ARG_ENABLE_AUTO_SWITCH          0x1
#define ACPI_ARG_DISABLE_SWITCH_EVENT        0x2
#define ACPI_ARG_ENABLE_AUTO_LCD_BRIGHTNESS  0x0
#define ACPI_ARG_DISABLE_AUTO_LCD_BRIGHTNESS 0x4

//
// ACPI methods for the adapter
//

#define ACPI_METHOD_DISPLAY_DOS (ULONG)('SOD_')
#define ACPI_METHOD_DISPLAY_DOD (ULONG)('DOD_')
#define ACPI_METHOD_DISPLAY_ROM (ULONG)('MOR_')
#define ACPI_METHOD_DISPLAY_GPD (ULONG)('DPG_')
#define ACPI_METHOD_DISPLAY_SPD (ULONG)('DPS_')
#define ACPI_METHOD_DISPLAY_VPO (ULONG)('OPV_')

//
// ACPI methods for children
//

#define ACPI_METHOD_OUTPUT_ADR (ULONG)('RDA_')
#define ACPI_METHOD_OUTPUT_BCL (ULONG)('LCB_')
#define ACPI_METHOD_OUTPUT_BCM (ULONG)('MCB_')
#define ACPI_METHOD_OUTPUT_DDC (ULONG)('CDD_')
#define ACPI_METHOD_OUTPUT_DCS (ULONG)('SCD_')
#define ACPI_METHOD_OUTPUT_DGS (ULONG)('SGD_')
#define ACPI_METHOD_OUTPUT_DSS (ULONG)('SSD_')

//
// ACPI Flags
//

#define DXGK_ACPI_POLL_DISPLAY_CHILDREN   0x00000001
#define DXGK_ACPI_CHANGE_DISPLAY_MODE     0x00000002
#define DXGK_ACPI_CHANGE_DISPLAY_TOPOLOGY 0x00000004

//
// Exclude adapter access flags.
//

#define DXGK_EXCLUDE_EVICT_ALL          0x00000001
#define DXGK_EXCLUDE_CALL_SYNCHRONOUS   0x00000002
#define DXGK_EXCLUDE_BRIDGE_ACCESS      0x00000004

//
// Max of 50 characters per string.
//

#define DXGK_MAX_STRING_LEN 50
#define DXGK_MAX_REG_SZ_LEN DXGK_MAX_STRING_LEN + 1

//
// Supported device space types.
//

#define DXGK_WHICHSPACE_CONFIG       PCI_WHICHSPACE_CONFIG
#define DXGK_WHICHSPACE_ROM          PCI_WHICHSPACE_ROM
#define DXGK_WHICHSPACE_MCH          0x80000000
#define DXGK_WHICHSPACE_BRIDGE       0x80000001

//
// Linked display adapter support.
//

typedef struct _LINKED_DEVICE {
    ULONG ChainUid;
    ULONG NumberOfLinksInChain;
    BOOLEAN LeadLink;
} LINKED_DEVICE, *PLINKED_DEVICE;

//
// Type of ACPI notification event.
//

typedef enum _DXGK_EVENT_TYPE {
    DxgkUndefinedEvent,
    DxgkAcpiEvent,
    DxgkPowerStateEvent,
    DxgkDockingEvent
} DXGK_EVENT_TYPE, *PDXGK_EVENT_TYPE;

typedef struct _DXGK_VIDEO_OUTPUT_CAPABILITIES {
    D3DKMDT_VIDEO_OUTPUT_TECHNOLOGY InterfaceTechnology;
    D3DKMDT_MONITOR_ORIENTATION_AWARENESS MonitorOrientationAwareness;
    BOOLEAN SupportsSdtvModes;
} DXGK_VIDEO_OUTPUT_CAPABILITIES, *PDXGK_VIDEO_OUTPUT_CAPABILITIES;


typedef struct _DXGK_CHILD_CAPABILITIES {

    union
    {
        //
        // If (CHILD_DESCRIPTOR::ChildDeviceType == TypeVideoOutput)
        //

        DXGK_VIDEO_OUTPUT_CAPABILITIES  VideoOutput;

        //
        // If (CHILD_DESCRIPTOR::ChildDeviceType == TypeOther)
        //

        struct
        {
            UINT MustBeZero;
        }
        Other;
    } Type;

    DXGK_CHILD_DEVICE_HPD_AWARENESS HpdAwareness;
} DXGK_CHILD_CAPABILITIES, *PDXGK_CHILD_CAPABILITIES;

typedef enum _DXGK_CHILD_DEVICE_TYPE {
   TypeUninitialized,
   TypeVideoOutput,
   TypeOther
} DXGK_CHILD_DEVICE_TYPE, *PDXGK_CHILD_DEVICE_TYPE;

//
// Child descriptor structure returned to us from the miniport
//
// NOTE: If (ChildDeviceType==TypeVideoOutput) then (ChildUid == video present target ID)
//

typedef struct _DXGK_CHILD_DESCRIPTOR {
   DXGK_CHILD_DEVICE_TYPE ChildDeviceType;
   DXGK_CHILD_CAPABILITIES ChildCapabilities;
   ULONG AcpiUid;
   ULONG ChildUid;
} DXGK_CHILD_DESCRIPTOR, *PDXGK_CHILD_DESCRIPTOR;

typedef struct _DXGK_DEVICE_DESCRIPTOR {
   ULONG DescriptorOffset;
   ULONG DescriptorLength;
   PVOID DescriptorBuffer;
} DXGK_DEVICE_DESCRIPTOR, *PDXGK_DEVICE_DESCRIPTOR;

typedef struct _DXGK_GENERIC_DESCRIPTOR {
   WCHAR HardwareId[DXGK_MAX_REG_SZ_LEN];
   WCHAR InstanceId[DXGK_MAX_REG_SZ_LEN];
   WCHAR CompatibleId[DXGK_MAX_REG_SZ_LEN];
   WCHAR DeviceText[DXGK_MAX_REG_SZ_LEN];
} DXGK_GENERIC_DESCRIPTOR, *PDXGK_GENERIC_DESCRIPTOR;

//
// Types of status that the miniport can report back to us
//

typedef enum _DXGK_CHILD_STATUS_TYPE{
   StatusUninitialized,
   StatusConnection,
   StatusRotation
} DXGK_CHILD_STATUS_TYPE, *PDXGK_CHILD_STATUS_TYPE;

//
// Child Status structure which can be queried directly or
// indicated up by the miniport
//

typedef struct _DXGK_CHILD_STATUS {
   DXGK_CHILD_STATUS_TYPE Type;
   ULONG ChildUid;
   union {
      struct {
         BOOLEAN Connected;
      } HotPlug;
      struct {
         UCHAR Angle;
      } Rotation;
   };
} DXGK_CHILD_STATUS, *PDXGK_CHILD_STATUS;

//
// DxgkCbExcludeAdapterAccess callback.
//

typedef
VOID
(*DXGKDDI_PROTECTED_CALLBACK)(
    IN CONST PVOID MiniportDeviceContext,
    IN PVOID ProtectedCallbackContext,
    IN NTSTATUS ProtectionStatus
    );

//
// GUID_DEVINTERFACE_I2C {2564AA4F-DDDB-4495-B497-6AD4A84163D7}
//

DEFINE_GUID(GUID_DEVINTERFACE_I2C, 0x2564AA4F, 0xDDDB, 0x4495, 0xB4, 0x97, 0x6A, 0xD4, 0xA8, 0x41, 0x63, 0xD7);

//
// GUID_DEVINTERFACE_OPM {BF4672DE-6B4E-4BE4-A325-68A91EA49C09}
//

DEFINE_GUID(GUID_DEVINTERFACE_OPM, 0xBF4672DE, 0x6B4E, 0x4BE4, 0xA3, 0x25, 0x68, 0xA9, 0x1E, 0xA4, 0x9C, 0x09);

//
// GUID_DEVINTERFACE_BRIGHTNESS {FDE5BBA4-B3F9-46FB-BDAA-0728CE3100B4}
//

DEFINE_GUID( GUID_DEVINTERFACE_BRIGHTNESS, 0xFDE5BBA4, 0xB3F9, 0x46FB, 0xBD, 0xAA, 0x07, 0x28, 0xCE, 0x31, 0x00, 0xB4);

//
// I2C Interface queried from the miniport.
//

#define DXGK_I2C_INTERFACE_VERSION_1 0x01

typedef
NTSTATUS
(*DXGKDDI_I2C_TRANSMIT_DATA_TO_DISPLAY)(
    IN PVOID MiniportDeviceContext,
    IN D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    IN ULONG SevenBitI2CAddress,
    IN ULONG DataLength,
    IN CONST PVOID Data
    );

typedef
NTSTATUS
(*DXGKDDI_I2C_RECEIVE_DATA_FROM_DISPLAY)(
    IN PVOID MiniportDeviceContext,
    IN D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    IN ULONG SevenBitI2CAddress,
    IN ULONG Flags,
    IN ULONG DataLength,
    OUT PVOID Data
    );

typedef struct _DXGK_I2C_INTERFACE {
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;

    DXGKDDI_I2C_TRANSMIT_DATA_TO_DISPLAY  DxgkDdiI2CTransmitDataToDisplay;
    DXGKDDI_I2C_RECEIVE_DATA_FROM_DISPLAY DxgkDdiI2CReceiveDataFromDisplay;
} DXGK_I2C_INTERFACE, *PDXGK_I2C_INTERFACE;

//
// OPM Interface from the miniport.
//

#define DXGK_OPM_INTERFACE_VERSION_1 0x01

typedef
NTSTATUS
(*DXGKDDI_OPM_GET_CERTIFICATE_SIZE)(
    IN PVOID MiniportDeviceContext,
    IN DXGKMDT_CERTIFICATE_TYPE CertificateType,
    OUT PULONG CertificateSize
    );

typedef
NTSTATUS
(*DXGKDDI_OPM_GET_CERTIFICATE)(
    IN PVOID MiniportDeviceContext,
    IN DXGKMDT_CERTIFICATE_TYPE CertificateType,
    IN ULONG CertificateSize,
    OUT PVOID CertificateBuffer
    );

typedef
NTSTATUS
(*DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT)(
    IN PVOID MiniportDeviceContext,
    IN D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    IN DXGKMDT_OPM_VIDEO_OUTPUT_SEMANTICS NewVideoOutputSemantics,
    OUT PHANDLE NewProtectedOutputHandle
    );

typedef
NTSTATUS
(*DXGKDDI_OPM_GET_RANDOM_NUMBER)(
    IN PVOID MiniportDeviceContext,
    IN HANDLE ProtectedOutputHandle,
    OUT PDXGKMDT_OPM_RANDOM_NUMBER RandomNumber
    );

typedef
NTSTATUS
(*DXGKDDI_OPM_SET_SIGNING_KEY_AND_SEQUENCE_NUMBERS)(
    IN PVOID MiniportDeviceContext,
    IN HANDLE ProtectedOutputHandle,
    IN CONST PDXGKMDT_OPM_ENCRYPTED_PARAMETERS EncryptedParameters
    );

typedef
NTSTATUS
(*DXGKDDI_OPM_GET_INFORMATION)(
    IN PVOID MiniportDeviceContext,
    IN HANDLE ProtectedOutputHandle,
    IN CONST PDXGKMDT_OPM_GET_INFO_PARAMETERS Parameters,
    OUT PDXGKMDT_OPM_REQUESTED_INFORMATION RequestedInformation
    );

typedef
NTSTATUS
(*DXGKDDI_OPM_GET_COPP_COMPATIBLE_INFORMATION)(
    IN PVOID MiniportDeviceContext,
    IN HANDLE ProtectedOutputHandle,
    IN CONST PDXGKMDT_OPM_COPP_COMPATIBLE_GET_INFO_PARAMETERS Parameters,
    OUT PDXGKMDT_OPM_REQUESTED_INFORMATION RequestedInformation
    );

typedef
NTSTATUS
(*DXGKDDI_OPM_CONFIGURE_PROTECTED_OUTPUT)(
    IN PVOID MiniportDeviceContext,
    IN HANDLE ProtectedOutputHandle,
    IN CONST PDXGKMDT_OPM_CONFIGURE_PARAMETERS Parameters,
    IN ULONG AdditionalParametersSize,
    IN CONST PVOID AdditionalParameters
    );

typedef
NTSTATUS
(*DXGKDDI_OPM_DESTROY_PROTECTED_OUTPUT)(
    IN PVOID MiniportDeviceContext,
    IN HANDLE ProtectedOutputHandle
    );

typedef struct _DXGK_OPM_INTERFACE {
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;

    DXGKDDI_OPM_GET_CERTIFICATE_SIZE                 DxgkDdiOPMGetCertificateSize;
    DXGKDDI_OPM_GET_CERTIFICATE                      DxgkDdiOPMGetCertificate;
    DXGKDDI_OPM_CREATE_PROTECTED_OUTPUT              DxgkDdiOPMCreateProtectedOutput;
    DXGKDDI_OPM_GET_RANDOM_NUMBER                    DxgkDdiOPMGetRandomNumber;
    DXGKDDI_OPM_SET_SIGNING_KEY_AND_SEQUENCE_NUMBERS DxgkDdiOPMSetSigningKeyAndSequenceNumbers;
    DXGKDDI_OPM_GET_INFORMATION                      DxgkDdiOPMGetInformation;
    DXGKDDI_OPM_GET_COPP_COMPATIBLE_INFORMATION      DxgkDdiOPMGetCOPPCompatibleInformation;
    DXGKDDI_OPM_CONFIGURE_PROTECTED_OUTPUT           DxgkDdiOPMConfigureProtectedOutput;
    DXGKDDI_OPM_DESTROY_PROTECTED_OUTPUT             DxgkDdiOPMDestroyProtectedOutput;
} DXGK_OPM_INTERFACE, *PDXGK_OPM_INTERFACE;


#define DXGK_BRIGHTNESS_INTERFACE_VERSION_1 0x01

typedef
NTSTATUS
(*DXGK_BRIGHTNESS_GET_POSSIBLE)(
    IN  PVOID  Context,
    IN  ULONG  BufferSize,
    OUT PUCHAR LevelCount,
    OUT PUCHAR BrightnessLevels
    );

typedef
NTSTATUS
(*DXGK_BRIGHTNESS_SET)(
    IN  PVOID  Context,
    IN  UCHAR  Brightness
    );

typedef
NTSTATUS
(*DXGK_BRIGHTNESS_GET)(
    IN  PVOID  Context,
    IN  PUCHAR Brightness
    );

typedef struct
{
    IN USHORT                           Size;
    IN USHORT                           Version;
    OUT PVOID                           Context;
    OUT PINTERFACE_REFERENCE            InterfaceReference;
    OUT PINTERFACE_DEREFERENCE          InterfaceDereference;
    OUT DXGK_BRIGHTNESS_GET_POSSIBLE    GetPossibleBrightness;
    OUT DXGK_BRIGHTNESS_SET             SetBrightness;
    OUT DXGK_BRIGHTNESS_GET             GetBrightness;
} DXGK_BRIGHTNESS_INTERFACE, *PDXGK_BRIGHTNESS_INTERFACE;

//
// Services exported by DxgkCbQueryServices()
//

typedef enum
{
    DxgkServicesAgp,
    DxgkServicesDebugReport,
    DxgkServicesTimedOperation
} DXGK_SERVICES;

//
// AGP Services
//

#define DXGK_AGP_INTERFACE_VERSION_1 0x01

#define DXGK_AGPCOMMAND_AGP1X       0x00001
#define DXGK_AGPCOMMAND_AGP2X       0x00002
#define DXGK_AGPCOMMAND_AGP4X       0x00004
#define DXGK_AGPCOMMAND_AGP8X       0x00008
#define DXGK_AGPCOMMAND_DISABLE_SBA 0x10000
#define DXGK_AGPCOMMAND_DISABLE_FW  0x20000

typedef
NTSTATUS
(APIENTRY *DXGKCB_AGP_ALLOCATE_POOL)(
    IN HANDLE Context,
    IN ULONG AllocationSize,
    IN MEMORY_CACHING_TYPE CacheType,
    OUT PPHYSICAL_ADDRESS PhysicalAddress,
    OUT PVOID *VirtualAddress
    );

typedef
NTSTATUS
(APIENTRY *DXGKCB_AGP_FREE_POOL)(
    IN HANDLE Context,
    IN PVOID VirtualAddress
    );

typedef
NTSTATUS
(APIENTRY *DXGKCB_AGP_SET_COMMAND)(
    IN HANDLE Context,
    IN ULONG Command
    );

typedef struct _DXGK_AGP_INTERFACE {
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;

    DXGKCB_AGP_ALLOCATE_POOL AgpAllocatePool;
    DXGKCB_AGP_FREE_POOL AgpFreePool;
    DXGKCB_AGP_SET_COMMAND AgpSetCommand;
} DXGK_AGP_INTERFACE, *PDXGK_AGP_INTERFACE;

//
// Debug Report API
//

DECLARE_HANDLE(DXGK_DEBUG_REPORT_HANDLE);
#define DXGK_DEBUG_REPORT_INTERFACE_VERSION_1 0x01
#define DXGK_DEBUG_REPORT_MAX_SIZE 0xF800

typedef struct _DXGK_DEBUG_REPORT_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;

    DXGK_DEBUG_REPORT_HANDLE
    (*DbgReportCreate)(
        IN HANDLE DeviceHandle,
        IN ULONG ulCode,
        IN ULONG_PTR ulpArg1,
        IN ULONG_PTR ulpArg2,
        IN ULONG_PTR ulpArg3,
        IN ULONG_PTR ulpArg4
        );

    BOOLEAN
    (*DbgReportSecondaryData)(
        IN OUT DXGK_DEBUG_REPORT_HANDLE hReport,
        IN __in_bcount(ulDataSize) PVOID pvData,
        IN ULONG ulDataSize
        );

    VOID
    (*DbgReportComplete)(
        IN OUT DXGK_DEBUG_REPORT_HANDLE hReport
        );
} DXGK_DEBUG_REPORT_INTERFACE, *PDXGK_DEBUG_REPORT_INTERFACE;

//
// Timed Operation API
//

#define DXGK_TIMED_OPERATION_INTERFACE_VERSION_1    0x01
#define DXGK_TIMED_OPERATION_TIMEOUT_MAX_SECONDS    5

typedef struct _DXGK_TIMED_OPERATION
{
    USHORT Size;
    ULONG_PTR OwnerTag;
    BOOLEAN OsHandled;
    BOOLEAN TimeoutTriggered;
    LARGE_INTEGER Timeout;
    LARGE_INTEGER StartTick;
} DXGK_TIMED_OPERATION, *PDXGK_TIMED_OPERATION;

typedef struct _DXGK_TIMED_OPERATION_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;

    NTSTATUS
    (*TimedOperationStart)(
        OUT DXGK_TIMED_OPERATION* Op,
        IN const LARGE_INTEGER* Timeout,
        IN BOOLEAN OsHandled
        );

    NTSTATUS
    (*TimedOperationDelay)(
        IN OUT DXGK_TIMED_OPERATION* Op,
        IN KPROCESSOR_MODE  WaitMode,
        IN BOOLEAN  Alertable,
        IN const LARGE_INTEGER* Interval OPTIONAL
        );

    NTSTATUS
    (*TimedOperationWaitForSingleObject)(
        IN OUT DXGK_TIMED_OPERATION* Op,
        IN PVOID Object,
        IN KWAIT_REASON WaitReason,
        IN KPROCESSOR_MODE WaitMode,
        IN BOOLEAN Alertable,
        IN const LARGE_INTEGER* Timeout OPTIONAL
        );
} DXGK_TIMED_OPERATION_INTERFACE, *PDXGK_TIMED_OPERATION_INTERFACE;

typedef enum {
    DockStateUnsupported = 0,
    DockStateUnDocked    = 1,
    DockStateDocked      = 2,
    DockStateUnknown     = 3,
} DOCKING_STATE;

//
// Device Information Structure to provide OS provided data
// structures to the miniport
//

typedef struct _DXGK_DEVICE_INFO {
    PVOID MiniportDeviceContext;
    PDEVICE_OBJECT PhysicalDeviceObject;
    UNICODE_STRING DeviceRegistryPath;
    PCM_RESOURCE_LIST TranslatedResourceList;
    LARGE_INTEGER SystemMemorySize;
    PHYSICAL_ADDRESS HighestPhysicalAddress;
    PHYSICAL_ADDRESS AgpApertureBase;
    SIZE_T AgpApertureSize;
    DOCKING_STATE DockingState;
} DXGK_DEVICE_INFO, *PDXGK_DEVICE_INFO;

//
// DxgKrnl interface
//

#define DXGK_ACPI_PASS_ARGS_TO_CHILDREN 'araP'


typedef
__drv_requiresIRQL(PASSIVE_LEVEL) 
NTSTATUS
(APIENTRY *DXGKCB_EVAL_ACPI_METHOD)(
    IN HANDLE DeviceHandle,
    IN ULONG DeviceUid,
    IN PACPI_EVAL_INPUT_BUFFER_COMPLEX AcpiInputBuffer,
    IN ULONG AcpiInputSize,
    IN OUT PACPI_EVAL_OUTPUT_BUFFER AcpiOutputBuffer,
    IN ULONG AcpiOutputSize
    );

typedef
__drv_requiresIRQL(PASSIVE_LEVEL) 
NTSTATUS
(APIENTRY *DXGKCB_GET_DEVICE_INFORMATION)(
    IN HANDLE DeviceHandle,
    OUT PDXGK_DEVICE_INFO DeviceInfo
    );

typedef
NTSTATUS
(APIENTRY *DXGKCB_INDICATE_CHILD_STATUS)(
    IN HANDLE DeviceHandle,
    IN PDXGK_CHILD_STATUS ChildStatus
    );

typedef
__drv_requiresIRQL(PASSIVE_LEVEL) 
NTSTATUS
(APIENTRY *DXGKCB_MAP_MEMORY)(
    IN HANDLE DeviceHandle,
    IN PHYSICAL_ADDRESS TranslatedAddress,
    IN ULONG Length,
    IN BOOLEAN InIoSpace,
    IN BOOLEAN MapToUserMode,
    IN MEMORY_CACHING_TYPE CacheType,
    OUT PVOID *VirtualAddress
    );

typedef
__drv_requiresIRQL(PASSIVE_LEVEL) 
NTSTATUS
(APIENTRY *DXGKCB_QUERY_SERVICES)(
    IN HANDLE DeviceHandle,
    IN DXGK_SERVICES ServicesType,
    IN OUT PINTERFACE Interface
    );

typedef
BOOLEAN
(APIENTRY *DXGKCB_QUEUE_DPC)(
    IN HANDLE DeviceHandle
    );

typedef
NTSTATUS
(APIENTRY *DXGKCB_READ_DEVICE_SPACE)(
    IN HANDLE DeviceHandle,
    IN ULONG DataType,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length,
    OUT PULONG BytesRead
    );

typedef
NTSTATUS
(APIENTRY *DXGKCB_SYNCHRONIZE_EXECUTION)(
    IN HANDLE DeviceHandle,
    IN PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
    IN PVOID Context,
    IN ULONG MessageNumber,
    OUT PBOOLEAN ReturnValue
    );

typedef
__drv_requiresIRQL(PASSIVE_LEVEL) 
NTSTATUS
(APIENTRY *DXGKCB_UNMAP_MEMORY)(
    IN HANDLE DeviceHandle,
    IN PVOID VirtualAddress
    );

typedef
NTSTATUS
(APIENTRY *DXGKCB_WRITE_DEVICE_SPACE)(
    IN HANDLE DeviceHandle,
    IN ULONG DataType,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length,
    OUT PULONG BytesWritten
    );

typedef
__drv_requiresIRQL(PASSIVE_LEVEL) 
NTSTATUS
(APIENTRY *DXGKCB_IS_DEVICE_PRESENT)(
    IN HANDLE DeviceHandle,
    IN PPCI_DEVICE_PRESENCE_PARAMETERS DevicePresenceParameters,
    OUT PBOOLEAN DevicePresent
    );

typedef
VOID
(APIENTRY *DXGKCB_LOG_ETW_EVENT)(
    IN CONST LPCGUID EventGuid,
    IN UCHAR Type,
    IN USHORT EventBufferSize,
    IN PVOID EventBuffer
    );

typedef
__drv_requiresIRQL(PASSIVE_LEVEL) 
NTSTATUS
(APIENTRY *DXGKCB_EXCLUDE_ADAPTER_ACCESS)(
    IN HANDLE DeviceHandle,
    IN ULONG Attributes,
    IN DXGKDDI_PROTECTED_CALLBACK DxgkProtectedCallback,
    IN PVOID ProtectedCallbackContext
    );

typedef struct _DXGK_START_INFO {
    ULONG                          RequiredDmaQueueEntry;
    GUID                           AdapterGuid;
} DXGK_START_INFO, *PDXGK_START_INFO;


typedef struct _DXGKRNL_INTERFACE {
    ULONG                          Size;
    ULONG                          Version;
    HANDLE                         DeviceHandle;

    DXGKCB_EVAL_ACPI_METHOD        DxgkCbEvalAcpiMethod;
    DXGKCB_GET_DEVICE_INFORMATION  DxgkCbGetDeviceInformation;
    DXGKCB_INDICATE_CHILD_STATUS   DxgkCbIndicateChildStatus;
    DXGKCB_MAP_MEMORY              DxgkCbMapMemory;
    DXGKCB_QUEUE_DPC               DxgkCbQueueDpc;
    DXGKCB_QUERY_SERVICES          DxgkCbQueryServices;
    DXGKCB_READ_DEVICE_SPACE       DxgkCbReadDeviceSpace;
    DXGKCB_SYNCHRONIZE_EXECUTION   DxgkCbSynchronizeExecution;
    DXGKCB_UNMAP_MEMORY            DxgkCbUnmapMemory;
    DXGKCB_WRITE_DEVICE_SPACE      DxgkCbWriteDeviceSpace;
    DXGKCB_IS_DEVICE_PRESENT       DxgkCbIsDevicePresent;

    DXGKCB_GETHANDLEDATA           DxgkCbGetHandleData;
    DXGKCB_GETHANDLEPARENT         DxgkCbGetHandleParent;
    DXGKCB_ENUMHANDLECHILDREN      DxgkCbEnumHandleChildren;
    DXGKCB_NOTIFY_INTERRUPT        DxgkCbNotifyInterrupt;
    DXGKCB_NOTIFY_DPC              DxgkCbNotifyDpc;
    DXGKCB_QUERYVIDPNINTERFACE     DxgkCbQueryVidPnInterface;
    DXGKCB_QUERYMONITORINTERFACE   DxgkCbQueryMonitorInterface;
    DXGKCB_GETCAPTUREADDRESS       DxgkCbGetCaptureAddress;

    DXGKCB_LOG_ETW_EVENT           DxgkCbLogEtwEvent;

    DXGKCB_EXCLUDE_ADAPTER_ACCESS  DxgkCbExcludeAdapterAccess;
} DXGKRNL_INTERFACE, *PDXGKRNL_INTERFACE;

//
// Kernel Mode Driver Interface
//

//
//  Define parameter types for SAL-annotated DDI parameters.  These are used by
//  the DDI function name typedefs.  They are not needed in a driver's DDI function
//  definitions, and should not be used in driver code.
//
//  Naming convention: Concatenate all SAL annotations, an underscore, other modifiers
//                     such as CONST, and the type of the parameter, and use uppercase
//                     only.
//
typedef __in    CONST PDEVICE_OBJECT     IN_CONST_PDEVICE_OBJECT;
typedef __inout PLINKED_DEVICE           INOUT_PLINKED_DEVICE;
typedef __inout PDXGK_CHILD_DESCRIPTOR   INOUT_PDXGK_CHILD_DESCRIPTOR;
typedef __in    PDXGK_CHILD_STATUS       IN_PDXGK_CHILD_STATUS;
typedef __inout PDXGK_DEVICE_DESCRIPTOR  INOUT_PDXGK_DEVICE_DESCRIPTOR;
typedef __in    DXGK_EVENT_TYPE          IN_DXGK_EVENT_TYPE;
typedef __in    PDXGK_START_INFO         IN_PDXGK_START_INFO;
typedef __in    PDXGKRNL_INTERFACE       IN_PDXGKRNL_INTERFACE;
typedef __in    PQUERY_INTERFACE         IN_PQUERY_INTERFACE;
typedef __in    PVIDEO_REQUEST_PACKET    IN_PVIDEO_REQUEST_PACKET;

//
//     Function name typedefs
//

typedef
    __checkReturn
NTSTATUS
DXGKDDI_ADD_DEVICE(
    IN_CONST_PDEVICE_OBJECT     PhysicalDeviceObject,
    OUT_PPVOID                  MiniportDeviceContext
    );

typedef
    __checkReturn
NTSTATUS
DXGKDDI_START_DEVICE(
    IN_CONST_PVOID          MiniportDeviceContext,
    IN_PDXGK_START_INFO     DxgkStartInfo,
    IN_PDXGKRNL_INTERFACE   DxgkInterface,
    OUT_PULONG              NumberOfVideoPresentSources,
    OUT_PULONG              NumberOfChildren
    );

typedef
    __checkReturn
NTSTATUS
DXGKDDI_STOP_DEVICE(
    IN_CONST_PVOID  MiniportDeviceContext
    );

typedef
    __checkReturn
NTSTATUS
DXGKDDI_REMOVE_DEVICE(
    IN_CONST_PVOID  MiniportDeviceContext
    );

typedef
    __checkReturn
NTSTATUS
DXGKDDI_DISPATCH_IO_REQUEST(
    IN_CONST_PVOID              MiniportDeviceContext,
    IN_ULONG                    VidPnSourceId,
    IN_PVIDEO_REQUEST_PACKET    VideoRequestPacket
    );

typedef
    __checkReturn
NTSTATUS
DXGKDDI_QUERY_CHILD_RELATIONS(
    IN_CONST_PVOID                  MiniportDeviceContext,
    INOUT_PDXGK_CHILD_DESCRIPTOR    ChildRelations,
    IN_ULONG                        ChildRelationsSize
    );

typedef
    __checkReturn
NTSTATUS
DXGKDDI_QUERY_CHILD_STATUS(
    IN_CONST_PVOID          MiniportDeviceContext,
    IN_PDXGK_CHILD_STATUS   ChildStatus,
    IN_BOOLEAN              NonDestructiveOnly
    );

typedef
    __checkReturn
BOOLEAN
DXGKDDI_INTERRUPT_ROUTINE(
    IN_CONST_PVOID  MiniportDeviceContext,
    IN_ULONG        MessageNumber
    );

typedef
VOID
DXGKDDI_DPC_ROUTINE(
    IN_CONST_PVOID  MiniportDeviceContext
    );

typedef
    __checkReturn
NTSTATUS
DXGKDDI_QUERY_DEVICE_DESCRIPTOR(
    IN_CONST_PVOID                  MiniportDeviceContext,
    IN_ULONG                        ChildUid,
    INOUT_PDXGK_DEVICE_DESCRIPTOR   DeviceDescriptor
    );

typedef
    __checkReturn
NTSTATUS
DXGKDDI_SET_POWER_STATE(
    IN_CONST_PVOID          MiniportDeviceContext,
    IN_ULONG                DeviceUid,
    IN_DEVICE_POWER_STATE   DevicePowerState,
    IN_POWER_ACTION         ActionType
    );

typedef
    __checkReturn
NTSTATUS
DXGKDDI_NOTIFY_ACPI_EVENT(
    IN_CONST_PVOID      MiniportDeviceContext,
    IN_DXGK_EVENT_TYPE  EventType,
    IN_ULONG            Event,
    IN_PVOID            Argument,
    OUT_PULONG          AcpiFlags
    );

typedef
VOID
DXGKDDI_RESET_DEVICE(
    IN_CONST_PVOID  MiniportDeviceContext
    );

typedef
VOID
DXGKDDI_UNLOAD(
    VOID
    );

typedef
    __checkReturn
NTSTATUS
DXGKDDI_QUERY_INTERFACE(
    IN_CONST_PVOID          MiniportDeviceContext,
    IN_PQUERY_INTERFACE     QueryInterface
    );

typedef
VOID
DXGKDDI_CONTROL_ETW_LOGGING(
    IN_BOOLEAN  Enable,
    IN_ULONG    Flags,
    IN_UCHAR    Level
    );

typedef
    __checkReturn
NTSTATUS
DXGKDDI_LINK_DEVICE(
    IN_CONST_PDEVICE_OBJECT   PhysicalDeviceObject,
    IN_CONST_PVOID            MiniportDeviceContext,
    INOUT_PLINKED_DEVICE      LinkedDevice
    );

//
//     Function pointer typedefs
//

typedef DXGKDDI_ADD_DEVICE              *PDXGKDDI_ADD_DEVICE;
typedef DXGKDDI_START_DEVICE            *PDXGKDDI_START_DEVICE;
typedef DXGKDDI_STOP_DEVICE             *PDXGKDDI_STOP_DEVICE;
typedef DXGKDDI_REMOVE_DEVICE           *PDXGKDDI_REMOVE_DEVICE;
typedef DXGKDDI_DISPATCH_IO_REQUEST     *PDXGKDDI_DISPATCH_IO_REQUEST;
typedef DXGKDDI_QUERY_CHILD_RELATIONS   *PDXGKDDI_QUERY_CHILD_RELATIONS;
typedef DXGKDDI_QUERY_CHILD_STATUS      *PDXGKDDI_QUERY_CHILD_STATUS;
typedef DXGKDDI_INTERRUPT_ROUTINE       *PDXGKDDI_INTERRUPT_ROUTINE;
typedef DXGKDDI_DPC_ROUTINE             *PDXGKDDI_DPC_ROUTINE;
typedef DXGKDDI_QUERY_DEVICE_DESCRIPTOR *PDXGKDDI_QUERY_DEVICE_DESCRIPTOR;
typedef DXGKDDI_SET_POWER_STATE         *PDXGKDDI_SET_POWER_STATE;
typedef DXGKDDI_NOTIFY_ACPI_EVENT       *PDXGKDDI_NOTIFY_ACPI_EVENT;
typedef DXGKDDI_RESET_DEVICE            *PDXGKDDI_RESET_DEVICE;
typedef DXGKDDI_UNLOAD                  *PDXGKDDI_UNLOAD;
typedef DXGKDDI_QUERY_INTERFACE         *PDXGKDDI_QUERY_INTERFACE;
typedef DXGKDDI_CONTROL_ETW_LOGGING     *PDXGKDDI_CONTROL_ETW_LOGGING;
typedef DXGKDDI_LINK_DEVICE             *PDXGKDDI_LINK_DEVICE;

//
// Driver initialization data structure
//

typedef struct _DRIVER_INITIALIZATION_DATA {
    ULONG                                   Version;
    PDXGKDDI_ADD_DEVICE                     DxgkDdiAddDevice;
    PDXGKDDI_START_DEVICE                   DxgkDdiStartDevice;
    PDXGKDDI_STOP_DEVICE                    DxgkDdiStopDevice;
    PDXGKDDI_REMOVE_DEVICE                  DxgkDdiRemoveDevice;
    PDXGKDDI_DISPATCH_IO_REQUEST            DxgkDdiDispatchIoRequest;
    PDXGKDDI_INTERRUPT_ROUTINE              DxgkDdiInterruptRoutine;
    PDXGKDDI_DPC_ROUTINE                    DxgkDdiDpcRoutine;
    PDXGKDDI_QUERY_CHILD_RELATIONS          DxgkDdiQueryChildRelations;
    PDXGKDDI_QUERY_CHILD_STATUS             DxgkDdiQueryChildStatus;
    PDXGKDDI_QUERY_DEVICE_DESCRIPTOR        DxgkDdiQueryDeviceDescriptor;
    PDXGKDDI_SET_POWER_STATE                DxgkDdiSetPowerState;
    PDXGKDDI_NOTIFY_ACPI_EVENT              DxgkDdiNotifyAcpiEvent;
    PDXGKDDI_RESET_DEVICE                   DxgkDdiResetDevice;
    PDXGKDDI_UNLOAD                         DxgkDdiUnload;
    PDXGKDDI_QUERY_INTERFACE                DxgkDdiQueryInterface;
    PDXGKDDI_CONTROL_ETW_LOGGING            DxgkDdiControlEtwLogging;

    PDXGKDDI_QUERYADAPTERINFO               DxgkDdiQueryAdapterInfo;
    PDXGKDDI_CREATEDEVICE                   DxgkDdiCreateDevice;
    PDXGKDDI_CREATEALLOCATION               DxgkDdiCreateAllocation;
    PDXGKDDI_DESTROYALLOCATION              DxgkDdiDestroyAllocation;
    PDXGKDDI_DESCRIBEALLOCATION             DxgkDdiDescribeAllocation;
    PDXGKDDI_GETSTANDARDALLOCATIONDRIVERDATA DxgkDdiGetStandardAllocationDriverData;
    PDXGKDDI_ACQUIRESWIZZLINGRANGE          DxgkDdiAcquireSwizzlingRange;
    PDXGKDDI_RELEASESWIZZLINGRANGE          DxgkDdiReleaseSwizzlingRange;
    PDXGKDDI_PATCH                          DxgkDdiPatch;
    PDXGKDDI_SUBMITCOMMAND                  DxgkDdiSubmitCommand;
    PDXGKDDI_PREEMPTCOMMAND                 DxgkDdiPreemptCommand;
    PDXGKDDI_BUILDPAGINGBUFFER              DxgkDdiBuildPagingBuffer;
    PDXGKDDI_SETPALETTE                     DxgkDdiSetPalette;
    PDXGKDDI_SETPOINTERPOSITION             DxgkDdiSetPointerPosition;
    PDXGKDDI_SETPOINTERSHAPE                DxgkDdiSetPointerShape;
    PDXGKDDI_RESETFROMTIMEOUT               DxgkDdiResetFromTimeout;
    PDXGKDDI_RESTARTFROMTIMEOUT             DxgkDdiRestartFromTimeout;
    PDXGKDDI_ESCAPE                         DxgkDdiEscape;
    PDXGKDDI_COLLECTDBGINFO                 DxgkDdiCollectDbgInfo;
    PDXGKDDI_QUERYCURRENTFENCE              DxgkDdiQueryCurrentFence;
    PDXGKDDI_ISSUPPORTEDVIDPN               DxgkDdiIsSupportedVidPn;
    PDXGKDDI_RECOMMENDFUNCTIONALVIDPN       DxgkDdiRecommendFunctionalVidPn;
    PDXGKDDI_ENUMVIDPNCOFUNCMODALITY        DxgkDdiEnumVidPnCofuncModality;
    PDXGKDDI_SETVIDPNSOURCEADDRESS          DxgkDdiSetVidPnSourceAddress;
    PDXGKDDI_SETVIDPNSOURCEVISIBILITY       DxgkDdiSetVidPnSourceVisibility;
    PDXGKDDI_COMMITVIDPN                    DxgkDdiCommitVidPn;
    PDXGKDDI_UPDATEACTIVEVIDPNPRESENTPATH   DxgkDdiUpdateActiveVidPnPresentPath;
    PDXGKDDI_RECOMMENDMONITORMODES          DxgkDdiRecommendMonitorModes;
    PDXGKDDI_RECOMMENDVIDPNTOPOLOGY         DxgkDdiRecommendVidPnTopology;
    PDXGKDDI_GETSCANLINE                    DxgkDdiGetScanLine;
    PDXGKDDI_STOPCAPTURE                    DxgkDdiStopCapture;
    PDXGKDDI_CONTROLINTERRUPT               DxgkDdiControlInterrupt;
    PDXGKDDI_CREATEOVERLAY                  DxgkDdiCreateOverlay;

    //
    // Device functions
    //

    PDXGKDDI_DESTROYDEVICE                  DxgkDdiDestroyDevice;
    PDXGKDDI_OPENALLOCATIONINFO             DxgkDdiOpenAllocation;
    PDXGKDDI_CLOSEALLOCATION                DxgkDdiCloseAllocation;
    PDXGKDDI_RENDER                         DxgkDdiRender;
    PDXGKDDI_PRESENT                        DxgkDdiPresent;

    //
    // Overlay functions
    //

    PDXGKDDI_UPDATEOVERLAY                  DxgkDdiUpdateOverlay;
    PDXGKDDI_FLIPOVERLAY                    DxgkDdiFlipOverlay;
    PDXGKDDI_DESTROYOVERLAY                 DxgkDdiDestroyOverlay;

    //
    // Context supports.
    //

    PDXGKDDI_CREATECONTEXT                  DxgkDdiCreateContext;
    PDXGKDDI_DESTROYCONTEXT                 DxgkDdiDestroyContext;

    //
    // Linked Display Adapter support.
    //

    PDXGKDDI_LINK_DEVICE                    DxgkDdiLinkDevice;
    PDXGKDDI_SETDISPLAYPRIVATEDRIVERFORMAT  DxgkDdiSetDisplayPrivateDriverFormat;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
    //
    // Extended for WDDM 2.0
    //
    PDXGKDDI_DESCRIBEPAGETABLE              DxgkDdiDescribePageTable;
    PDXGKDDI_UPDATEPAGETABLE                DxgkDdiUpdatePageTable;
    PDXGKDDI_UPDATEPAGEDIRECTORY            DxgkDdiUpdatePageDirectory;
    PDXGKDDI_MOVEPAGEDIRECTORY              DxgkDdiMovePageDirectory;

    PDXGKDDI_SUBMITRENDER                   DxgkDdiSubmitRender;
    PDXGKDDI_CREATEALLOCATION2              DxgkDdiCreateAllocation2;

    //
    // GDI acceleration. Extended for WDDM 1.0
    //
    PDXGKDDI_RENDER                         DxgkDdiRenderKm;

    //
    // New DMM DDIs for CCD support
    //
    VOID*                                   Reserved;
    PDXGKDDI_QUERYVIDPNHWCAPABILITY         DxgkDdiQueryVidPnHWCapability;

#endif // DXGKDDI_INTERFACE_VERSION

} DRIVER_INITIALIZATION_DATA, *PDRIVER_INITIALIZATION_DATA;

//
// *** Displib definitions ****************************************************
//

typedef enum _DEBUG_LEVEL {
    DlDebugError,
    DlDebugWarning,
    DlDebugTrace,
    DlDebugInfo
} DEBUG_LEVEL;

//
// Functions exported by DispLib
//

NTSTATUS
DxgkInitialize(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath,
    IN PDRIVER_INITIALIZATION_DATA DriverInitializationData
    );

PVOID
DlAllocateCommonBuffer(
    IN PVOID DeviceHandle,
    IN PVP_DMA_ADAPTER DlpDmaAdapter,
    IN ULONG DesiredLength,
    OUT PPHYSICAL_ADDRESS LogicalAddress,
    IN BOOLEAN CacheEnabled,
    OUT PVOID Reserved
    );

PVOID
DlAllocatePool(
    IN PVOID DeviceHandle,
    IN VP_POOL_TYPE PoolType,
    IN SIZE_T NumberOfBytes,
    IN ULONG Tag
    );

VOID
DlClearEvent(
    IN PVOID DeviceHandle,
    IN PEVENT pEvent
    );

VP_STATUS
DlCreateEvent(
    IN PVOID DeviceHandle,
    IN ULONG EventFlag,
    PVOID Unused,
    OUT PEVENT *ppEvent
    );

VOID
DlDebugPrint(
    IN DEBUG_LEVEL DebugPrintLevel,
    IN __in PCHAR DebugMessage,
    ...
    );

VP_STATUS
DlDeleteEvent(
    IN PVOID DeviceHandle,
    IN PEVENT pEvent
    );

VP_STATUS
DlEnumerateChildren(
    IN PVOID DeviceHandle,
    IN PVOID Reserved
    );

VOID
DlFreeDeviceBase(
    IN PVOID DeviceHandle,
    IN PVOID MappedAddress
    );

VOID
DlFreePool(
    IN PVOID DeviceHandle,
    IN PVOID Ptr
    );

NTSTATUS
DlGetAccessRanges(
    PVOID DeviceHandle,
    ULONG NumRequestedResources,
    PIO_RESOURCE_DESCRIPTOR RequestedResources OPTIONAL,
    ULONG NumAccessRanges,
    PVIDEO_ACCESS_RANGE AccessRanges,
    PVOID VendorId,
    PVOID DeviceId,
    PULONG Slot
    );

PVOID
DlGetAssociatedDeviceExtension(
    IN PVOID DeviceObject
    );

PVOID
DlGetDeviceBase(
    IN PVOID DeviceHandle,
    IN PHYSICAL_ADDRESS IoAddress,
    IN ULONG NumberOfUchars,
    IN UCHAR InIoSpace
    );

PVP_DMA_ADAPTER
DlGetDmaAdapter(
    IN PVOID DeviceHandle,
    IN PVP_DEVICE_DESCRIPTION DlpDeviceDescription
    );

VP_STATUS
DlGetRegistryParameters(
    IN PVOID DeviceHandle,
    IN __in PWSTR ParameterName,
    IN UCHAR IsParameterFileName,
    IN PMINIPORT_GET_REGISTRY_ROUTINE CallbackRoutine,
    IN PVOID Context
    );

__allocator
PVOID
DlGetRomImage(
    IN PVOID DeviceHandle,
    IN PVOID Unused1,
    IN ULONG Unused2,
    IN ULONG Length
    );

VP_STATUS
DlGetVersion(
    IN PVOID DeviceHandle,
    OUT PVPOSVERSIONINFO pDlpOsVersionInfo
    );

PVOID
DlLockBuffer(
    IN PVOID DeviceHandle,
    IN PVOID BaseAddress,
    IN ULONG Length,
    IN VP_LOCK_OPERATION Operation
    );

VOID
DlLogError(
    IN PVOID DeviceHandle,
    IN PVIDEO_REQUEST_PACKET Vrp OPTIONAL,
    IN VP_STATUS ErrorCode,
    IN ULONG UniqueId
    );

VP_STATUS
DlMapMemory(
    IN PVOID DeviceHandle,
    IN PHYSICAL_ADDRESS TranslatedAddress,
    IN OUT PULONG Length,
    IN PULONG InIoSpace,
    IN OUT PVOID *VirtualAddress
    );

LONGLONG
DlQueryPerformanceCounter(
    IN PVOID pDeviceHandle,
    OUT PLONGLONG pllPerformanceFrequency OPTIONAL
    );

VOID
DlReleaseCommonBuffer(
    IN PVOID DeviceHandle,
    IN PVP_DMA_ADAPTER DlpDmaAdapter,
    IN ULONG Length,
    IN PHYSICAL_ADDRESS LogicalAddress,
    IN PVOID VirtualAddress,
    IN BOOLEAN CacheEnabled
    );

BOOLEAN
DlScanRom(
    IN PVOID DeviceHandle,
    IN PUCHAR RomBase,
    IN ULONG RomLength,
    IN PUCHAR String
    );

LONG
DlSetEvent(
    IN PVOID DeviceHandle,
    IN PEVENT pEvent
    );

VP_STATUS
DlSetRegistryParameters(
    IN PVOID DeviceHandle,
    IN __in PWSTR ValueName,
    IN __in_bcount(ValueLength) PVOID ValueData,
    IN ULONG ValueLength
    );

VP_STATUS
DlSetTrappedEmulatorPorts(
    IN PVOID DeviceHandle,
    IN ULONG NumAccessRanges,
    IN PVIDEO_ACCESS_RANGE AccessRange
    );

VOID
DlStopTimer(
    PVOID DeviceHandle
    );

VOID
DlUnlockBuffer(
    IN PVOID DeviceHandle,
    IN PVOID Mdl
    );

VP_STATUS
DlVerifyAccessRanges(
    PVOID DeviceHandle,
    ULONG NumAccessRanges,
    PVIDEO_ACCESS_RANGE AccessRanges
    );

VP_STATUS
DlWaitForSingleObject(
    IN PVOID DeviceHandle,
    IN PVOID pEvent,
    IN PLARGE_INTEGER Timeout
    );

VP_STATUS
DlDisableInterrupt(
    IN PVOID DeviceHandle
    );

VP_STATUS
DlEnableInterrupt(
    IN PVOID DeviceHandle
    );

NTSTATUS
DlEvalAcpiMethod(
    IN HANDLE DeviceHandle,
    IN ULONG DeviceUid,
    IN PACPI_EVAL_INPUT_BUFFER_COMPLEX AcpiInputBuffer,
    IN ULONG AcpiInputSize,
    IN OUT PACPI_EVAL_OUTPUT_BUFFER AcpiOutputBuffer,
    IN ULONG AcpiOutputSize
    );

NTSTATUS
DlGetDeviceInformation(
    IN PVOID DeviceHandle,
    OUT PDXGK_DEVICE_INFO DeviceInfo
    );

NTSTATUS
DlIndicateChildStatus(
    IN PVOID DeviceHandle,
    IN PDXGK_CHILD_STATUS ChildStatus
    );

NTSTATUS
DlMapMemoryEx(
    IN PVOID DeviceHandle,
    IN PHYSICAL_ADDRESS TranslatedAddress,
    IN OUT PULONG Length,
    IN PULONG InIoSpace,
    IN HANDLE ProcessHandle,
    IN OUT PVOID *VirtualAddress,
    IN BOOLEAN MapToUserMode
    );

NTSTATUS
DlQueryServices(
    IN PVOID DeviceHandle,
    IN VIDEO_PORT_SERVICES ServicesType,
    IN OUT PINTERFACE Interface
    );

BOOLEAN
DlQueueDpc(
    IN PVOID DeviceHandle
    );

ULONG
DlReadDeviceSpace(
    IN PVOID DeviceHandle,
    IN ULONG DataType,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    );

ULONG
DlSetBusData(
    IN PVOID DeviceHandle,
    IN ULONG BusDataType,
    IN ULONG SlotNumber,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length
    );

BOOLEAN
DlSynchronizeExecution(
    PVOID DeviceHandle,
    VIDEO_SYNCHRONIZE_PRIORITY Priority,
    PMINIPORT_SYNCHRONIZE_ROUTINE SynchronizeRoutine,
    PVOID Context,
    ULONG MessageNumber
    );

NTSTATUS
DlUnmapMemory(
    PVOID DeviceHandle,
    PVOID VirtualAddress,
    HANDLE ProcessHandle
    );

#endif // (NTDDI_VERSION >= NTDDI_LONGHORN)

#pragma warning(pop)

#endif // _DISPMPRT_H_

