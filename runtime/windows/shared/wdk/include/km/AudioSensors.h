/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

 AudioSensors.h

Abstract:

    This module defines the types, constants, and functions that are
    exposed to device drivers that advertise Audio Sensors Interface.

--*/

#pragma once
//
// The interface class GUID for Audio Sensors Interface
//
// {00E6037C-59D1-4890-8CD8-E090891A250C}
DEFINE_GUID(AUDIO_SENSORS_INTERFACE, 0xe6037c, 0x59d1, 0x4890, 0x8c, 0xd8, 0xe0, 0x90, 0x89, 0x1a, 0x25, 0xc);

//
// The interface class GUID for Audio Interface types
//

// The interface class GUID for SDCA Audio Interface type
// {009CA0AA-94B9-4EC3-8D8D-D13233B7132E}
DEFINE_GUID(AUDIO_INTERFACE_SDCA, 0x9ca0aa, 0x94b9, 0x4ec3, 0x8d, 0x8d, 0xd1, 0x32, 0x33, 0xb7, 0x13, 0x2e);

//
// Version 1.0
//
#define AUDIO_SENSORS_INTERFACE_VERSION_0100        (0x0100)

#define AUDIO_ENDPOINT_CONFIG_VERSION_1             (1)
#define SDCA_ENDPOINT_CONFIG_VERSION_1              (1)
#define SDCA_FUNCTION_INFORMATION_VERSION_1         (1)

typedef struct _AUDIO_MODULE_ID
{
    // Audio Module Class Id
    GUID        ClassId;

    // Audio Module Instance Id
    ULONG       InstanceId;
} AUDIO_MODULE_ID, *PAUDIO_MODULE_ID;

//
// EvtAudioSensorsGetBuffer
// EvtAudioSensorsSetBuffer
// EvtAudioSensorsSubmitReadReport
// params:
//   * Context              - Audio Sensors Driver supplied Context
//   * ModuleId             - Module Id information
//   * SensorsSessionId     - Session Id returned by Start Session method.
//                            Value 0 indicates a request is not associated to any specific Sensors Session.
//   * BufferSize           - Size of the Buffer
//   * Buffer               - Proprietary Buffer
//
typedef NTSTATUS EVT_AUDIO_SENSORS_BUFFER
(
    _In_ PVOID                                      Context,
    _In_ AUDIO_MODULE_ID                            ModuleId,
    _In_ ULONG                                      SensorsSessionId,
    _Inout_ PULONG                                  BufferSize,
    _Inout_updates_bytes_opt_(BufferSize) PVOID     Buffer
);
typedef EVT_AUDIO_SENSORS_BUFFER *PFN_AUDIO_SENSORS_GET_BUFFER;
typedef EVT_AUDIO_SENSORS_BUFFER *PFN_AUDIO_SENSORS_SET_BUFFER;
typedef EVT_AUDIO_SENSORS_BUFFER *PFN_AUDIO_SENSORS_SUBMIT_READ_REPORT;

typedef struct _SDCA_FUNCTION_INFORMATION
{
    // Size of entire structure
    ULONG       Size;

    // Version of this struct
    UINT8       Version;

    // SDCA Function Number
    UINT8       FunctionNumber;

    // SDCA Function Type
    UINT8       FunctionType;

    // SDCA Function Manufacturer Id
    UINT16      FunctionManufacturerId;

    // SDCA Function Id
    UINT16      FunctionId;

    // SoundWire Controller Id this function is attached to
    UINT8       ControllerId;

    // Link within the SoundWire Controller this function is attached to 
    UINT8       LinkId;

    // Peripheral device's Unique Id
    UINT8       UniqueId;
} SDCA_FUNCTION_INFORMATION, *PSDCA_FUNCTION_INFORMATION;

typedef struct _SDCA_ENDPOINT_CONFIG
{
    // Size of entire structure including size of SdcaFunctionInformation array
    ULONG                           Size;

    // Version of this struct
    UINT8                           Version;

    // Count of the SDCA Functions that need to be configured
    ULONG                           SdcaFunctionInformationCount;

    // Details of the SDCA Functions that need to be configured
    SDCA_FUNCTION_INFORMATION       SdcaFunctionInformation[ANYSIZE_ARRAY];
} SDCA_ENDPOINT_CONFIG, *PSDCA_ENDPOINT_CONFIG;

typedef struct _AUDIO_ENDPOINT_CONFIG
{
    // Size of entire structure including additional
    // audio technology specific endpoint configuration bytes
    ULONG                           Size;

    // Version of this struct
    UINT8                           Version;

    // GUID to identify Audio Interface type
    // GUID for SDCA interface is defined in this header 
    GUID                            AudioInterfaceType;

    // Additional bytes following this structure provide
    // Audio Interface specific endpoint configuration.
    // When Audio Interface is SDCA, bytes following this
    // structure will be SDCA_ENDPOINT_CONFIG structure.

} AUDIO_ENDPOINT_CONFIG, *PAUDIO_ENDPOINT_CONFIG;

//
// EvtAudioSensorsStartSession
// params:
//   * Context                      - Audio Driver supplied Context
//   * ModuleId                     - Module Id information
//   * AudioRenderEndpointConfig    - Optional Audio Render Endpoint Configuration details to be used for this session
//   * AudioCaptureEndpointConfig   - Optional Audio Capture Endpoint Configuration details to be used for this session
//   * SensorsSessionId             - Non-Zero Session Id returned by the Audio Driver
//
typedef NTSTATUS EVT_AUDIO_SENSORS_START_SESSION
(
    _In_ PVOID                              Context,
    _In_ AUDIO_MODULE_ID                    ModuleId,
    _In_opt_ PAUDIO_ENDPOINT_CONFIG         AudioRenderEndpointConfig,
    _In_opt_ PAUDIO_ENDPOINT_CONFIG         AudioCaptureEndpointConfig,
    _Out_ PULONG                            SensorsSessionId
);
typedef EVT_AUDIO_SENSORS_START_SESSION *PFN_AUDIO_SENSORS_START_SESSION;

//
// EvtAudioSensorsStopSession
// params:
//   * Context              - Audio Driver supplied Context
//   * ModuleId             - Module Id information
//   * SensorsSessionId     - Session Id returned by Start Session method
//
typedef NTSTATUS EVT_AUDIO_SENSORS_STOP_SESSION
(
    _In_ PVOID              Context,
    _In_ AUDIO_MODULE_ID    ModuleId,
    _In_ ULONG              SensorsSessionId
);
typedef EVT_AUDIO_SENSORS_STOP_SESSION *PFN_AUDIO_SENSORS_STOP_SESSION;

typedef struct _AUDIO_SENSORS_INTERFACE_V0100
{
    INTERFACE InterfaceHeader;

    // Audio Sensors Driver to Audio Driver
    PFN_AUDIO_SENSORS_START_SESSION             EvtAudioSensorsStartSession;
    PFN_AUDIO_SENSORS_STOP_SESSION              EvtAudioSensorsStopSession;

    PFN_AUDIO_SENSORS_GET_BUFFER                EvtAudioSensorsGetBuffer;
    PFN_AUDIO_SENSORS_SET_BUFFER                EvtAudioSensorsSetBuffer;

    // Audio Driver to Audio Sensors Driver
    PFN_AUDIO_SENSORS_SUBMIT_READ_REPORT        EvtAudioSensorsSubmitReadReport;

} AUDIO_SENSORS_INTERFACE_V0100, *PAUDIO_SENSORS_INTERFACE_V0100;
