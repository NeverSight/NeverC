// Copyright (C) Microsoft Corporation. All rights reserved.
#pragma once

EXTERN_C_START

//
// Possible data types
//

typedef enum _NDIS_PARAMETER_TYPE
{
    NdisParameterInteger,
    NdisParameterHexInteger,
    NdisParameterString,
    NdisParameterMultiString,
    NdisParameterBinary
} NDIS_PARAMETER_TYPE, *PNDIS_PARAMETER_TYPE;

typedef struct
{
    USHORT          Length;

    _Field_size_bytes_(Length)
    PVOID           Buffer;
} BINARY_DATA;

//
// To store configuration information
//
typedef struct _NDIS_CONFIGURATION_PARAMETER
{
    NDIS_PARAMETER_TYPE ParameterType;
    union
    {
        ULONG           IntegerData;
        NDIS_STRING     StringData;
        BINARY_DATA     BinaryData;
    } ParameterData;
} NDIS_CONFIGURATION_PARAMETER, *PNDIS_CONFIGURATION_PARAMETER;

EXTERN_C_END
