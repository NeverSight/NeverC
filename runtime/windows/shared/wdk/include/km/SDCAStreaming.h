//
// Copyright (c) Microsoft Corporation. All rights reserved.
//

#pragma once

#include <soundwirecontroller.h>

#ifndef ANYSIZE_ARRAY
#define ANYSIZE_ARRAY 1
#endif

#define MAX_PATH_DESCRIPTORS 16
#define MAX_DATAPORT_PER_TERMINAL 16

//
// KSPROPERTYSETID_Sdca
//
#define STATIC_KSPROPERTYSETID_Sdca\
    0x55eff601, 0x40d2, 0x4fc9, { 0xb4, 0x1d, 0x9, 0xb1, 0x8c, 0x6b, 0xfb, 0x1f
DEFINE_GUIDSTRUCT("55EFF601-40D2-4FC9-B41D-09B18C6BFB1F", KSPROPERTYSETID_Sdca);
#define KSPROPERTYSETID_Sdca DEFINE_GUIDNAMED(KSPROPERTYSETID_Sdca)

typedef enum {
    KSPROPERTY_SDCA_FUNCTION_INFORMATION    = 1,   // get
    KSPROPERTY_SDCA_VENDOR_SPECIFIC         = 2,   // get/set. Vendor defined data may follow KSPROPERTY header
    KSPROPERTY_SDCA_FUNCTION_CAPABILITY     = 3,
    KSPROPERTY_SDCA_PATH_DESCRIPTORS        = 4,
    KSPROPERTY_SDCA_CREATE_PATH             = 5,   // set, takes SDCA_PATH_DESCRIPTORS
    KSPROPERTY_SDCA_DESTROY_PATH            = 6,
    KSPROPERTY_SDCA_START_PATH              = 7,
    KSPROPERTY_SDCA_STOP_PATH               = 8,
    KSPROPERTY_SDCA_ACCESS_EVENTS           = 9,   // set
    KSPROPERTY_SDCA_RESERVED                = 10,
    KSPROPERTY_SDCA_CREATE_PATH2            = 11,  // set, takes SDCA_PATH_DESCRIPTORS2
} KSPROPERTY_SDCA;

typedef enum _SDCA_PATH {
    SdcaPathUltrasoundRender                = 0x1,
    SdcaPathUltrasoundCapture               = 0x2, 
    SdcaPathReferenceStream                 = 0x4,
    SdcaPathIvSense                         = 0x8,
    SdcaPathDefault                         = 0x10,
} SDCA_PATH, *PSDCA_PATH;

typedef enum _SDCA_STREAMING_FUNCTION_INFORMATION_FLAGS
{
    SdcaFunctionInformationFlagSubSystemIdPresent  = 0x00000001,
    SdcaFunctionInformationFlagControllerIdPresent = 0x00000002,
    SdcaFunctionInformationFlagLinkIdPresent       = 0x00000004,
    SdcaFunctionInformationFlagPeripheralIdPresent = 0x00000008,
} SDCA_STREAMING_FUNCTION_INFORMATION_FLAGS;

typedef struct _SDCA_STREAMING_FUNCTION_INFORMATION
{
    ULONG Size;
    ULONG Flags;
    ULONG FunctionInformationId;
    UINT8 FunctionNumber;
    UINT8 FunctionSdcaVersion;
    UINT8 FunctionSdcaRevision;
    UINT8 FunctionType;
    UINT16 FunctionManufacturerId;
    UINT16 FunctionId;
    UINT8 FunctionVersion;
    ULONG SubSystemId;
    UINT8 ControllerId;
    UINT8 LinkId;
    UINT8 PeripheralId;
    UINT8 UniqueId;
} SDCA_STREAMING_FUNCTION_INFORMATION, *PSDCA_STREAMING_FUNCTION_INFORMATION;

typedef struct 
{ 
    ULONG FunctionCount; 
    SDCA_STREAMING_FUNCTION_INFORMATION FunctionInfoList[ANYSIZE_ARRAY]; 
} SDCA_FUNCTION_INFORMATION_LIST, * PSDCA_FUNCTION_INFORMATION_LIST;

typedef struct 
{ 
    ULONG Size;
    ULONG FunctionInformationId; 
    ULONG DataPortCount;
    UINT8 DataPorts[MAX_DATAPORT_PER_TERMINAL];
    ULONG FormatCount;
    WAVEFORMATEXTENSIBLE Formats[ANYSIZE_ARRAY];
} SDCA_PATH_DESCRIPTOR, * PSDCA_PATH_DESCRIPTOR;

typedef struct 
{
    ULONG Size;
    SDCA_PATH SdcaPath;
    ULONG EndpointId;
    ULONG DescriptorCount;
} SDCA_PATH_DESCRIPTORS, * PSDCA_PATH_DESCRIPTORS;

#define MAX_DATAPORT_PER_TERMINAL2 4

#define SDCA_PATH_DESCRIPTOR2_VERSION_1 1

typedef enum _SDCA_DATAPORT_MAP
{
    SdcaDataPortMapIndexA = 0x00000001,
    SdcaDataPortMapIndexB = 0x00000002,
    SdcaDataPortMapIndexC = 0x00000004,
    SdcaDataPortMapIndexD = 0x00000008
} SDCA_DATAPORT_MAP;

typedef struct _SDCA_PATH_DESCRIPTOR2
{
    ULONG                         Size;                         // Size of this struct
    UINT8                         Version;                      // Version of this struct
    ULONG                         FunctionInformationId;        // Function Information Id
    ULONG                         TerminalEntityId;             // Entity Id of the Streaming IT/OT
    ULONG                         DataPortMap;                  // Bit map of Dataports to be used.Bit 0->Index_A,Bit 1->Index_B, Bit 2->Index_C, Bit 3->Index_D

    // EndpointId in DataPortConfig structures is ignored
    SOUNDWIRE_DATAPORT_CONFIGURATION DataPortConfig[MAX_DATAPORT_PER_TERMINAL2];
} SDCA_PATH_DESCRIPTOR2, *PSDCA_PATH_DESCRIPTOR2;

typedef struct _SDCA_PATH_DESCRIPTORS2
{
    ULONG                         Size;                         // Size of this struct including all Descriptor array entries
    UINT8                         Version;                      // Version of this struct
    SDCA_PATH                     SdcaPath;                     // SdcaPathDefault for normal stream
    ULONG                         EndpointId;                   // EndpointId
    union
    {
        WAVEFORMATEXTENSIBLE      SpecialPathFormat;            // Format for a special path
        BYTE                      Reserved[sizeof(WAVEFORMATEXTENSIBLE)]; // all 0 for normal stream
    };
    ULONG                         DescriptorCount;              // Count of Descriptors
    SDCA_PATH_DESCRIPTOR2         Descriptor[ANYSIZE_ARRAY];    // Detailed description of each Descriptor
} SDCA_PATH_DESCRIPTORS2, *PSDCA_PATH_DESCRIPTORS2;

#ifdef DECLARE_CONST_ACXOBJECTBAG_SOUNDWIRE_PROPERTY_NAME
DECLARE_CONST_ACXOBJECTBAG_SOUNDWIRE_PROPERTY_NAME(SdcaPropertyPathDescriptors2);
#endif

//
// KSPROPERTYSETID_SdcaKws
//
#define STATIC_KSPROPERTYSETID_SdcaKws\
    0x947d5153, 0x1286, 0x468c, 0xa6, 0x8d, 0xf8, 0x39, 0x56, 0x3, 0x7f, 0xfd
DEFINE_GUIDSTRUCT("947D5153-1286-468C-A68D-F83956037FFD", KSPROPERTYSETID_SdcaKws);
#define KSPROPERTYSETID_SdcaKws DEFINE_GUIDNAMED(KSPROPERTYSETID_SdcaKws)

#define MAX_SDCA_ENTITY_LABEL_LENGTH 12

#define MAX_INPUT_PIN_COUNT 4

typedef enum {
    KSPROPERTY_SDCAKWS_DEVICE_CAPABILITY    = 1,    // get
    KSPROPERTY_SDCAKWS_VAD_CAPABILITY       = 2,    // get
    KSPROPERTY_SDCAKWS_VAD_ENTITIES         = 3,    // get
    KSPROPERTY_SDCAKWS_ACCESS_EVENTS        = 4,    // set
    KSPROPERTY_SDCAKWS_CONFIGURE_VAD_PORT   = 5,    // set
    KSPROPERTY_SDCAKWS_CLEANUP_VAD_PORT     = 6,    // set
} KSPROPERTY_SDCAKWS;

typedef enum _DATA_PATHS
{
    SupportedDataPathsRawPCM        = 0x1,
    SupportedDataPathsBufferedRaw   = 0x2
} DATA_PATHS, *PDATA_PATHS;

typedef enum _HISTORY_BUFFER_MODES
{
    HistoryBufferModesFlowThrough   = 0x1,
    HistoryBufferModesFastStream    = 0x2,
    HistoryBufferModesRead          = 0x4
} HISTORY_BUFFER_MODES, *PHISTORY_BUFFER_MODES;

typedef enum _VAD_ACCESS_MODE
{
    VADAccessModeRW     = 0x0,
    VADAccessModeDual   = 0x1,
    VADAccessModeRW1C   = 0x2,
    VADAccessModeRO     = 0x3,
    VADAccessModeW1S    = 0x4,
    VADAccessModeDC     = 0x5,
} VAD_ACCESS_MODE, *PVAD_ACCESS_MODE;

typedef struct _DEVICE_KWS_CAPABILITY_DESCRIPTOR
{
    ULONG DataPathsSupported;                               // bitmask of DATA_PATHS
} DEVICE_KWS_CAPABILITY_DESCRIPTOR, * PDEVICE_KWS_CAPABILITY_DESCRIPTOR;

typedef struct _VAD_DESCRIPTOR
{
    UINT8 VadInterruptPosition;
    ULONG SupportedHistoryBufferModes;                      // bitmask of HISTORY_BUFFER_MODES
    ULONG HistoryBufferPreamble;
    ULONG HistoryBufferPreambleOverFlow;
    VAD_ACCESS_MODE HistoryBufferMessageOffsetAccessMode;   // VAD_ACCESS_MODE for message offset
    ULONG HistoryBufferMessageOffsetDCValue;                // message offset value, if access mode is DC
    VAD_ACCESS_MODE HistoryBufferMessageLengthAccessMode;   // VAD_ACCESS_MODE for message length
    ULONG HistoryBufferMessageLengthDCValue;                // message length value, if access mode is DC
    ULONG FormatCount;                                      // number of formats in Format array
    WAVEFORMATEXTENSIBLE Format[ANYSIZE_ARRAY];
} VAD_DESCRIPTOR, *PVAD_DESCRIPTOR;

typedef struct _ENTITY_INFO
{
    UINT8 EntityType;
    UINT8 EntityId;
    char EntityLabel[MAX_SDCA_ENTITY_LABEL_LENGTH];         // The debug ID string for the entity
    UINT8 InputPin[MAX_INPUT_PIN_COUNT];                    // Array of input pins, 0 indicates not connected
} ENTITY_INFO, * PENTITY_INFO;

typedef struct _VAD_ENTITIES
{
    ULONG EntityCount;                                      // number of entities in SDCAVadEntities array
    ENTITY_INFO SDCAVadEntities[ANYSIZE_ARRAY];
} VAD_ENTITIES, *PVAD_ENTITIES;

typedef struct _SDCA_KWS_NOTIFICATIONS
{
    KEVENT Suspend;                                         // event indicating that KWS has been suspended
    KEVENT Resume;                                          // event indicating KWS suspention has concluded
} SDCA_KWS_NOTIFICATIONS, *PSDCA_KWS_NOTIFICATIONS, SDCA_ACCESS_EVENTS, *PSDCA_ACCESS_EVENTS;

typedef enum
{
    VadModeStreaming,                                       // Streaming hardware is configured by soundwire class driver
    VadModeSharedHardware                                   // streaming hardware is shared, stream configuration done by IHV driver
} VAD_MODE;

typedef struct _SDCA_KWS_PREPARE_PARAMS
{
    VAD_MODE VadMode;                                       // whether VAD is being prepared for streaming, or shared hardware
    ULONG EndpointId;
    WAVEFORMATEXTENSIBLE DetectionFormat;                   // The format to configure (if being prepared for streaming)
} SDCA_KWS_PREPARE_PARAMS, *PSDCA_KWS_PREPARE_PARAMS;

