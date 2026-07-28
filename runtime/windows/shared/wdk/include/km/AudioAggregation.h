//
// Copyright (c) Microsoft Corporation. All rights reserved.
//

#pragma once

#ifndef _AUDIOAGGREGATION_H_
#define _AUDIOAGGREGATION_H_

#pragma warning(disable:4201) // nameless struct/union
#pragma warning(disable:4214) // bit field types other than int

#define MAX_DATAPORTS_PER_CONNECTION    4

typedef enum _SOUNDWIRE_FUNCTION_FLAGS
{
    SoundWireFunctionFlagSubSystemIdPresent    = 0x00000001,            // SubSystem Id is present
    SoundWireFunctionFlagControllerIdPresent   = 0x00000002,            // Controller Id is present
    SoundWireFunctionFlagLinkIdPresent         = 0x00000004,            // Link Id is present
    SoundWireFunctionFlagPeripheralIdPresent   = 0x00000008,            // Peripheral Id is present
    SoundWireFunctionFlagFunctionNumberPresent = 0x00000010             // Function Number is present

} SOUNDWIRE_FUNCTION_FLAGS;

// Due to a SDCA specification update and clarification around the FunctionId and FunctionNumber,
// this entry is renamed to FunctionNumber. 
#define SoundWireFunctionFlagFunctionIdPresent SoundWireFunctionFlagFunctionNumberPresent
#define VALID_SOUNDWIRE_FUNCTION_FLAGS 0x1F;

typedef struct _SOUNDWIRE_FUNCTION_INFORMATION
{
    ULONG   Size;
    ULONG   Flags;
    ULONG   SubSystemId;
    UINT8   ControllerId;
    UINT8   LinkId;
    UINT8   PeripheralId;
    union {
        UINT8   FunctionId; // deprecated, SDCA specification update, this entry is renamed FunctionNumber
        UINT8   FunctionNumber;
    };
} SOUNDWIRE_FUNCTION_INFORMATION, *PSOUNDWIRE_FUNCTION_INFORMATION;

typedef ULONG AGGREGATION_COMMIT_GROUP_HANDLE, *PAGGREGATION_COMMIT_GROUP_HANDLE;

typedef enum _AGGREGATION_TECHNLOGY_TYPE
{
    AggregationTechnologyTypeInvalid = 0,
    AggregationTechnologyTypeSoundWire
} AGGREGATION_TECHNOLOGY_TYPE, *PAGGREGATION_TECHNOLOGY_TYPE;

typedef struct _AGGREGATION_COMMIT_GROUP
{
    ULONG                               Size;
    AGGREGATION_TECHNOLOGY_TYPE         Technology;
    union
    {
        struct
        {
            UINT8                               FunctionCount;
            _Field_size_(FunctionCount)
            SOUNDWIRE_FUNCTION_INFORMATION      FunctionInfo[ANYSIZE_ARRAY];
        } SoundWireCommitGroup;
    };
} AGGREGATION_COMMIT_GROUP, *PAGGREGATION_COMMIT_GROUP;

typedef enum _AGGREGATION_COMMIT_OPERATION
{
    AggregationCommitOperationInvalid = 0,
    AggregationCommitOperationCommit,
    AggregationCommitOperationDiscard
} AGGREGATION_COMMIT_OPERATION, *PAGGREGATION_COMMIT_OPERATION;

typedef struct _AGGREGATION_END_COMMIT_GROUP
{
    ULONG                               Size;
    AGGREGATION_COMMIT_GROUP_HANDLE     CommitGroupHandle;
    AGGREGATION_COMMIT_OPERATION        CommitOperation;
} AGGREGATION_END_COMMIT_GROUP, *PAGGREGATION_END_COMMIT_GROUP;

typedef ULONG AGGREGATION_CONNECTION_MAP_HANDLE, *PAGGREGATION_CONNECTION_MAP_HANDLE;

typedef struct _AGGREGATION_DATAPORT_CONNECTION
{
    ULONG                                 Size;
    SOUNDWIRE_FUNCTION_INFORMATION        SrcFunctionInfo;
    UINT8                                 SrcDataPortCount;
    ULONG                                 SrcDataPortNumbers[MAX_DATAPORTS_PER_CONNECTION];
    SOUNDWIRE_FUNCTION_INFORMATION        DstFunctionInfo;
    UINT8                                 DstDataPortCount;
    ULONG                                 DstDataPortNumbers[MAX_DATAPORTS_PER_CONNECTION];
} AGGREGATION_DATAPORT_CONNECTION, *PAGGREGATION_DATAPORT_CONNECTION;

typedef struct _AGGREGATION_CONNECTION_MAP
{
    ULONG                               Size;
    UINT8                               ConnectionCount;
    _Field_size_(ConnectionCount)
    AGGREGATION_DATAPORT_CONNECTION     Connection[ANYSIZE_ARRAY];
} AGGREGATION_CONNECTION_MAP, *PAGGREGATION_CONNECTION_MAP;

//
// The control codes used by the Audio Aggregation driver
//
#define AUDIO_AGGREGATOR_INDEX(x) (0x100 + x)
#define AUDIO_AGGREGATOR_IOCTL(_index_) \
    CTL_CODE (FILE_DEVICE_UNKNOWN, AUDIO_AGGREGATOR_INDEX(_index_), METHOD_NEITHER, FILE_ANY_ACCESS)

//
// Creates a Commit Group across multiple devices.
// Output is Commit Group Handle that uniquely identifies the combined commit group
// InputBuffer - AGGREGATION_COMMIT_GROUP
// OutputBuffer - AGGREGATION_COMMIT_GROUP_HANDLE
//
#define IOCTL_AUDIO_AGGREGATOR_CREATE_COMMIT_GROUP                                  AUDIO_AGGREGATOR_IOCTL (1)

//
// Destroys a Commit Group that was previously created.
// Takes previously returned Commit Group Handle
// InputBuffer - AGGREGATION_COMMIT_GROUP_HANDLE
// OutputBuffer - NULL
//
#define IOCTL_AUDIO_AGGREGATOR_DESTROY_COMMIT_GROUP                                 AUDIO_AGGREGATOR_IOCTL (2)

//
// Indicates to the Controller to start tracking commands for a commit group.
// InputBuffer - AGGREGATION_COMMIT_GROUP_HANDLE
// OutputBuffer - NULL
//
#define IOCTL_AUDIO_AGGREGATOR_BEGIN_COMMIT_GROUP                                   AUDIO_AGGREGATOR_IOCTL (3)

//
// Indicates to the Controller to commit all requests being tracked for the commit group.
// Controller will also stop tracking commands.
// InputBuffer - AGGREGATION_END_COMMIT_GROUP
// OutputBuffer - NULL
//
#define IOCTL_AUDIO_AGGREGATOR_END_COMMIT_GROUP                                     AUDIO_AGGREGATOR_IOCTL (4)

//
// Sends device to device dataport connection information.
// InputBuffer - AGGREGATION_CONNECTION_MAP
// OutputBuffer - AGGREGATION_CONNECTION_MAP_HANDLE
//
#define IOCTL_AUDIO_AGGREGATOR_ADD_DEVICE_TO_DEVICE_CONNECTION_MAP                  AUDIO_AGGREGATOR_IOCTL (5)

//
// Removes previously added device to device dataport connection information.
// InputBuffer - AGGREGATION_CONNECTION_MAP_HANDLE
// OutputBuffer - NULL
//
#define IOCTL_AUDIO_AGGREGATOR_REMOVE_DEVICE_TO_DEVICE_CONNECTION_MAP               AUDIO_AGGREGATOR_IOCTL (6)

// Include the KS Properties for aggregation below only if ks is configured
#ifdef DEFINE_GUIDSTRUCT

//
// KSPROPERTYSETID_SdcaAgg
//
#define STATIC_KSPROPERTYSETID_SdcaAgg\
    0xae003375, 0x750c, 0x4a78, 0xb8, 0xfb, 0x71, 0x76, 0xf3, 0xe0, 0x53, 0x66
DEFINE_GUIDSTRUCT("ae003375-750c-4a78-b8fb-7176f3e05366", KSPROPERTYSETID_SdcaAgg);
#define KSPROPERTYSETID_SdcaAgg DEFINE_GUIDNAMED(KSPROPERTYSETID_SdcaAgg)

typedef enum {
    KSPROPERTY_SDCAAGG_AGGREGATED_DEVICES = 1,   // get
    KSPROPERTY_SDCAAGG_COMMIT_GROUP_HANDLE = 2,  // set
} KSPROPERTY_SDCAAGG;

typedef struct _SDCA_AGGREGATION_DEVICE
{
    ULONG FunctionIndex; // Index used by the Aggregation Driver when sending endpoint ID and Data Port number
    SOUNDWIRE_FUNCTION_INFORMATION FunctionId;
} SDCA_AGGREGATION_DEVICE, *PSDCA_AGGREGATION_DEVICE;

typedef struct _SDCA_AGGREGATION_DEVICES
{
    ULONG                       Size;
    UINT8                       FunctionCount;
    _Field_size_(FunctionCount)
    SDCA_AGGREGATION_DEVICE     FunctionIds[ANYSIZE_ARRAY];
} SDCA_AGGREGATION_DEVICES, *PSDCA_AGGREGATION_DEVICES;

// DECLARE_CONST_ACXOBJECTBAG_SOUNDWIRE_PROPERTY_NAME(DataPortNumbers);
// The DataPortNumbers is an optional Object Bag Blob entry the Streaming driver
// can add to the StreamBridge's OutStreamVarArguments for the SdcaAggregator to consume at
// stream creation time.
// DataPortNumbers will be an array of 1 or more Data Port Numbers, one data port number for
// each connected Audio Function (in a non-aggregated configuration this can be a single number)
// The array will assign a specific Data Port Number to the Audio Function at the same
// index in the SDCA_FUNCTION_INFORMATION_LIST returned by the SdcaAggregator
// The SdcaAggregator will assign the associated DPNo value to the object bag it forwards to
// the child aggregated audio paths (overwriting existing the DPNo value if any)

// Audio Module definition for retrieving the aggregation id to function id mapping
// {200ADFE5-8698-46D1-97AC-5C7CB707BBB5}
static const GUID AGGREGATION_ASSOCIATION = 
{ 0x200adfe5, 0x8698, 0x46d1, { 0x97, 0xac, 0x5c, 0x7c, 0xb7, 0x7, 0xbb, 0xb5 } };

#define AGGREGATION_ASSOCIATION_NAME L"Aggregation Association"
#define AGGREGATION_ASSOCIATION_VERSION_MAJOR  0x1
#define AGGREGATION_ASSOCIATION_VERSION_MINOR  0X0
#define AGGREGATION_ASSOCIATION_INSTANCEID     0X0

#define AGGREGATION_ID_MASK 0xFF
#define AGGREGATION_INSTANCE_ID_MASK  0x00FFFFFF

#define AGGREGATION_GET_AGGREGATIONID(InstanceId) \
    (ULONG(InstanceId) >> 24 & AGGREGATION_ID_MASK)

#define AGGREGATION_GET_AGGREGATED_INSTANCEID(AggDeviceId, BaseInstanceId) \
    ((ULONG(AggDeviceId & AGGREGATION_ID_MASK) << 24) | \
     (ULONG(BaseInstanceId & AGGREGATION_INSTANCE_ID_MASK)))

typedef struct _SDCA_AGGREGATION_ASSOCIATION
{
    UINT8 AggregationId;
    SOUNDWIRE_FUNCTION_INFORMATION FunctionId;
} SDCA_AGGREGATION_ASSOCIATION, *PSDCA_AGGREGATION_ASSOCIATION;

typedef struct _SDCA_AGGREGATION_ASSOCIATIONS
{
    ULONG                       Size; // the size of the structure including associations
    UINT8                       Count; // the number of entries in Association
    _Field_size_(FunctionCount)
    SDCA_AGGREGATION_ASSOCIATION     Association[ANYSIZE_ARRAY];
} SDCA_AGGREGATION_ASSOCIATIONS, *PSDCA_AGGREGATION_ASSOCIATIONS;

#endif // DEFINE_GUIDSTRUCT

#endif // _AUDIOAGGREGATION_H_
