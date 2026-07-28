/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Abstract:

    This header defines the public STORAGE_ICE_KEY_MANAGEMENT_INTERFACE interface,
    which storage ICE drivers can expose to facilitate key management operations.

Environment:

    Kernel mode

--*/

#pragma once

#include <guiddef.h>

//
// GUID_DEVINTERFACE_STORAGE_ICE_KEY_MANAGEMENT is the device interface class for storage ICE devices.
//
// {e3528c0b-bca5-4963-91d5-0aaf03b93ad5}
//
DEFINE_GUID( GUID_DEVINTERFACE_STORAGE_ICE_KEY_MANAGEMENT, 0xe3528c0b, 0xbca5, 0x4963, 0x91, 0xd5, 0x0a, 0xaf, 0x03, 0xb9, 0x3a, 0xd5 );

//
// GUID_STORAGE_ICE_KEY_MANAGEMENT_INTERFACE is the interface that is exposed by storage ICE drivers.
//
// {1ce54cf7-ba48-4fa0-90e2-2aabb9a0619a}
//
DEFINE_GUID( GUID_STORAGE_ICE_KEY_MANAGEMENT_INTERFACE, 0x1ce54cf7, 0xba48, 0x4fa0, 0x90, 0xe2, 0x2a, 0xab, 0xb9, 0xa0, 0x61, 0x9a );

#pragma warning(push)
#pragma warning(disable:4201) // nameless struct/unions

typedef struct STORAGE_ICE_KEY_MANAGEMENT_CAPABILITIES {

    // Supported challenge size in bytes. Can be 0 if challenge is not supported
    // and not expected by key manager.
    USHORT ChallengeMaxSize;

    union {
        struct {
            // If a challenge is supported (ChallengeMaxSize > 0), this field indicates
            // if the key manager requires a challenge to be provided when wrapping a key.
            // If ChallengeMaxSize is 0, this bit is ignored.
            ULONG ChallengeRequired : 1;

            // Indicates if implementation has acquired FIPS module certification
            // for key wrapping operations.
            ULONG IsKeyWrappingFipsCompliant : 1;

            // Must be set to 0.
            ULONG Reserved : 30;
        };
        _Field_range_(>, 0) ULONG AsULong;
    } Flags;

    // Must be set to 0.
    ULONG Reserved[4];

} STORAGE_ICE_KEY_MANAGEMENT_CAPABILITIES;

#pragma warning(pop)

/*++

PQUERY_KEY_MANAGEMENT_CAPABILITIES

Routine Description:

    Returns information about the capabilities of the key management interface.

Arguments:

    InterfaceContext - The Context member of the STORAGE_ICE_KEY_MANAGEMENT_INTERFACE structure.
    KeyManagementCapabilities - A pointer to a buffer that receives the key management capabilities.

Return Value:

    STATUS_SUCCESS on success.
    STATUS_INVALID_PARAMETER if KeyManagementCapabilities is NULL.
    A NTSTATUS error code otherwise.

--*/
_IRQL_requires_(PASSIVE_LEVEL)
typedef
NTSTATUS
(__stdcall *PQUERY_KEY_MANAGEMENT_CAPABILITIES)(
    _In_ const PVOID InterfaceContext,
    _Out_ STORAGE_ICE_KEY_MANAGEMENT_CAPABILITIES* KeyManagementCapabilities
    );

typedef enum _STORAGE_ICE_KEY_TYPE {
    StorageIceKeyTypeDirectKey = 0,
    StorageIceKeyTypePlatformWrapped,
    StorageIceKeyTypePlutonWrapped,
    StorageIceKeyTypeMax
} STORAGE_ICE_KEY_TYPE;

#pragma warning(push)
#pragma warning(disable:4201) // nameless struct/unions

typedef struct STORAGE_ICE_KEY_DATA {

    // Namespace ID for the key
    GUID KeyNamespaceId;

    // Key type
    STORAGE_ICE_KEY_TYPE KeyType;

    // Key flags
    union {
        struct {
            USHORT ContainsChallenge : 1;

            // Must be set to 0.
            USHORT Reserved : 15;
        };
        _Field_range_(>, 0) USHORT AsUShort;
    } KeyFlags;

    // The key content size
    USHORT KeyContentSize;

    // Contains key content data
    _Field_size_bytes_(KeyContentSize) UCHAR KeyContent[ANYSIZE_ARRAY];
} STORAGE_ICE_KEY_DATA;

#pragma warning(pop)

typedef struct STORAGE_ICE_CHALLENGE_DATA {

    // The challenge content size
    USHORT ChallengeContentSize;

    // Contains challenge content
    _Field_size_bytes_(ChallengeContentSize) UCHAR ChallengeContent[ANYSIZE_ARRAY];
} STORAGE_ICE_CHALLENGE_DATA;

/*++

PWRAP_KEY

Routine Description:

    Wraps a key using the provided key content and supplemental data.

Arguments:

    InterfaceContext - The Context member of the STORAGE_ICE_KEY_MANAGEMENT_INTERFACE structure.
    KeyData - The key data to be wrapped.
    ChallengeData - The challenge data. Can be NULL if the reported capabilities reports max challenge size as 0.
    WrappedKeyDataSize - On input, provides size of the WrappedKeyData buffer or can be 0 when querying required size.
                         On output, contains actual size of WrappedKeyData buffer or required size.
    WrappedKeyData - A pointer to a buffer that receives the wrapped key data.
                     Can be NULL to query the WrappedKeyDataSize.

Return Value:

    STATUS_SUCCESS on success.
    STATUS_INVALID_PARAMETER if KeyData is NULL or ChallengeData is NULL when required.
    STATUS_BUFFER_TOO_SMALL if the WrappedKeyData buffer is too small or is NULL.
    A STATUS error code otherwise.

--*/
_IRQL_requires_(PASSIVE_LEVEL)
typedef
NTSTATUS
(__stdcall *PWRAP_KEY)(
    _In_ const PVOID InterfaceContext,
    _In_ const STORAGE_ICE_KEY_DATA* KeyData,
    _In_opt_ const STORAGE_ICE_CHALLENGE_DATA* ChallengeData,
    _Inout_ PULONG WrappedKeyDataSize,
    _Out_ STORAGE_ICE_KEY_DATA* WrappedKeyData
    );

/*++

PINSERT_CHALLENGE_DATA

Routine Description:

    Insert challenge data into the wrapped key content.

Arguments:

    InterfaceContext - The Context member of the STORAGE_ICE_KEY_MANAGEMENT_INTERFACE structure.
    ChallengeData - The challenge data.
    WrappedKeyDataSize - Provides the size of the WrappedKeyDataWithChallenge buffer.
    WrappedKeyDataWithChallenge - A pointer to a buffer containing the wrapped key data.

Return Value:

    STATUS_SUCCESS on success.
    STATUS_INVALID_PARAMETER if ChallengeData is NULL.
    STATUS_BUFFER_TOO_SMALL if the WrappedKeyDataWithChallenge buffer is too small or is NULL.
    A STATUS error code otherwise.

--*/
_IRQL_requires_(PASSIVE_LEVEL)
typedef
NTSTATUS
(__stdcall *PINSERT_CHALLENGE_DATA)(
    _In_ const PVOID InterfaceContext,
    _In_ const STORAGE_ICE_CHALLENGE_DATA* ChallengeData,
    _In_ const PULONG WrappedKeyDataSize,
    _Inout_ STORAGE_ICE_KEY_DATA* WrappedKeyDataWithChallenge
    );

/*++

PVALIDATE_UNWRAP_KEY

Routine Description:

    Verifes that the wrapped key is valid on the current system and can be unwrapped.
    If a challenge was used to wrap the key, the challenge data must have already
    been inserted. The wrapped key passed into this function is not intended to be
    used for any IO or encryption and is only used to verify the wrapped key
    belongs to the current system.

Arguments:

    InterfaceContext - The Context member of the STORAGE_ICE_KEY_MANAGEMENT_INTERFACE structure.
    WrappedKeyDataSize - Provides the size of the WrappedKeyData buffer.
    WrappedKeyData - A pointer to a buffer containing the wrapped key data. If a challenge
                     was used to wrap the key, the challenge data must have already been
                     inserted by the caller.

Return Value:

    STATUS_SUCCESS on success.
    STATUS_FVE_FAILED_TO_UNWRAP_HW_WRAPPED_KEY if the key cannot be unwrapped.
    STATUS_INVALID_PARAMETER if WrappedKeyData is NULL.
    A STATUS error code otherwise.

--*/
_IRQL_requires_(PASSIVE_LEVEL)
typedef
NTSTATUS
(__stdcall *PVALIDATE_UNWRAP_KEY)(
    _In_ const PVOID InterfaceContext,
    _In_ ULONG WrappedKeyDataSize,
    _In_reads_bytes_(WrappedKeyDataSize) const STORAGE_ICE_KEY_DATA* WrappedKeyData
    );

#define STORAGE_ICE_KEY_MANAGEMENT_INTERFACE_VERSION_1  1

typedef struct STORAGE_ICE_KEY_MANAGEMENT_INTERFACE {

    //
    // Generic interface header
    //
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;

    //
    // Required interface functions
    //

    PQUERY_KEY_MANAGEMENT_CAPABILITIES QueryKeyManagementCapabilities;

    //
    // Optional interface functions
    //

    PWRAP_KEY WrapKey;
    PINSERT_CHALLENGE_DATA InsertChallengeData;
    PVALIDATE_UNWRAP_KEY ValidateUnwrapKey;

} STORAGE_ICE_KEY_MANAGEMENT_INTERFACE;
