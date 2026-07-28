/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Abstract:

    This header defines the public NVME_ICE_INTERFACE interface, which NVMe ICE
    drivers can expose to facilitate offloading crypto for NVMe devices.

Environment:

    Kernel mode

--*/

#pragma once

#include <guiddef.h>
#include <nvme.h>
#ifndef _NTSTORPORTP_
#include <storport.h>
#endif // !defined _NTSTORPORTP_
#include <icekeymaninterface.h>

//
// GUID_DEVINTERFACE_NVME_ICE is the device interface class for NVMe ICE devices.
//
// {0d08b5d3-4ffe-4017-8c99-ea62f389648c}
//
DEFINE_GUID( GUID_DEVINTERFACE_NVME_ICE, 0x0d08b5d3, 0x4ffe, 0x4017, 0x8c, 0x99, 0xea, 0x62, 0xf3, 0x89, 0x64, 0x8c);

//
// GUID_NVME_ICE_INTERFACE is the interface that is exposed by NVMe ICE drivers.
//  This interface provides a way for drivers to offload crypto operations
//  to supporting NVMe ICE hardware using non-wrapped (clear/direct) keys.
//
// {ff05a26d-65df-4437-819b-3a7b20b41d42}
//
// Note: This interface is deprecated and will eventually be removed.
//
DEFINE_GUID( GUID_NVME_ICE_INTERFACE, 0xff05a26d, 0x65df, 0x4437, 0x81, 0x9b, 0x3a, 0x7b, 0x20, 0xb4, 0x1d, 0x42);

typedef struct NVME_PCI_ADDRESS {

    // PCI SBDF of device.
    ULONG Function: 3;
    ULONG Device: 5;
    ULONG Bus: 8;
    ULONG Segment: 16;

} NVME_PCI_ADDRESS;

#pragma warning(push)
#pragma warning(disable:4201) // nameless struct/unions

typedef union NVME_ICE_PAGE_SIZE_BITMASK {
    struct {
        UCHAR PageSize4K : 1;
        UCHAR PageSize2M : 1;
        UCHAR Reserved : 6;
    };
    _Field_range_(>, 0) UCHAR AsUchar;
} NVME_ICE_PAGE_SIZE_BITMASK;

typedef union NVME_ICE_DATA_ALIGNMENT_BITMASK {
    struct {
        UCHAR Alignment4B : 1;
        UCHAR Alignment8B : 1;
        UCHAR Alignment16B : 1;
        UCHAR Alignment32B : 1;
        UCHAR Reserved : 4;
    };
    _Field_range_(>, 0) UCHAR AsUchar;
} NVME_ICE_DATA_ALIGNMENT_BITMASK;

#define NVME_ICE_CAPABILITIES_VERSION_1               1

typedef struct NVME_ICE_CAPABILITIES {

    // Version of struct
    USHORT Version;

    // Size of struct
    USHORT Size;

    // Supported page sizes. Multiple bits may be set.
    NVME_ICE_PAGE_SIZE_BITMASK PageSizeBitmask;

    // Supported data alignments. Multiple bits may be set.
    NVME_ICE_DATA_ALIGNMENT_BITMASK DataTransferAlignmentBitmask;

    // Compliant security standards at the hardware level. Multiple bits may be set.
    STORAGE_SECURITY_COMPLIANCE_BITMASK SecurityComplianceBitmask;

    UCHAR Reserved;

    // Maximum supported transfer size in KB
    USHORT MaxTransferSizeKBytes;

    // Contains supported crypto configurations
    STOR_CRYPTO_CAPABILITIES_DATA CryptoCapabilitiesData;

} NVME_ICE_CAPABILITIES;

#pragma warning(pop)

/*++

PQUERY_CAPABILITIES

Routine Description:

    Returns information about the capabilities of a SoC that supports NVMe ICE.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    PciAddress       - The PCI SBDF of the NVMe device.
    Capabilities     - The capabilities of the SoC. Can be NULL to query CapabilitiesSize.
    CapabilitiesSize - On input, provides size of the Capabilities struct or can be
                       0 when querying required size. On output, contains actual size of
                       Capabilities struct or required size.

Return Value:

    STOR_STATUS_SUCCESS on success.
    STOR_STATUS_BUFFER_TOO_SMALL if Capabilities is NULL or if CapabilitiesSize is too small.
    STOR_STATUS_INVALID_DEVICE_REQUEST if NVMe device is not supported.
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_(PASSIVE_LEVEL)
typedef
ULONG
(__stdcall *PQUERY_CAPABILITIES)(
    _In_ const PVOID InterfaceContext,
    _In_ const NVME_PCI_ADDRESS* PciAddress,
    _Out_ NVME_ICE_CAPABILITIES* Capabilities,
    _Inout_ PULONG CapabilitiesSize
    );

#define NVME_ICE_CONFIGURE_CAPABILITY_DATA_VERSION_1  1

typedef struct NVME_ICE_CONFIGURE_CAPABILITY_DATA {

    // IN: Version of struct
    USHORT Version;

    // IN: Size of struct
    USHORT Size;

    // IN: Crypto capability to configure as reported by QueryCapabilities.
    USHORT ReportedCryptoCapabilityIndex;

    // IN: Chosen maximum supported transfer size in KB
    USHORT MaxTransferSizeKBytes;

    // IN: Chosen size of an LBA in bytes
    ULONG LBASize;

    // IN: Chosen size of PRP Page Size in bytes
    ULONG PRPPageSize;

    // OUT: Index of configured capability to be used with ProgramKey.
    USHORT ConfiguredCryptoCapabilityIndex;

} NVME_ICE_CONFIGURE_CAPABILITY_DATA;

/*++

PCONFIGURE_CAPABILITY

Routine Description:

    Configures a capability returned by QueryCapabilities. Subsequent calls with identical
    input parameters must return the same ConfiguredCryptoCapabilityIndex. If this function needs
    to perform any memory allocations then it must ensure that those memory allocations do not fail.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    CapabilityData   - The configuration data to be set to the provided capability.

Return Value:

    STOR_STATUS_SUCCESS on success.
    STOR_STATUS_BUSY on transient error. This suggests a retry.
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_max_(DISPATCH_LEVEL)
typedef
ULONG
(__stdcall *PCONFIGURE_CAPABILITY)(
    _In_ const PVOID InterfaceContext,
    _Inout_ NVME_ICE_CONFIGURE_CAPABILITY_DATA* CapabilityData
    );

#define NVME_ICE_PROGRAM_KEY_DATA_VERSION_1           1

typedef struct NVME_ICE_PROGRAM_KEY_DATA {

    // IN: Version of struct
    USHORT Version;

    // IN: Size of struct
    USHORT Size;

    // IN: The key's size
    USHORT KeyContentSize;

    // IN: Capability to use for this key as provided by ConfigureCapability.
    USHORT ConfiguredCryptoCapabilityIndex;

    // IN_OUT: If NULL, driver is expected to return an unused key handle.
    //         If non-NULL, driver is expected to replace key associated with provided handle with new key.
    PVOID KeyHandle;

    // IN: Contains the key
    _Field_size_bytes_(KeyContentSize) UCHAR KeyContent[ANYSIZE_ARRAY];

} NVME_ICE_PROGRAM_KEY_DATA;

/*++

PPROGRAM_KEY

Routine Description:

    Programs a key to a SoC that supports NVMe ICE. If this function needs to perform any memory
    allocations then it must ensure that those memory allocations do not fail.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    KeyData          - The key data to be programmed to the SoC.
    KeyDataSize      - Provides size of KeyData struct.

Return Value:

    STOR_STATUS_SUCCESS on success.
    STOR_STATUS_BUSY on transient error. This suggests a retry.
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_max_(DISPATCH_LEVEL)
typedef
ULONG
(__stdcall *PPROGRAM_KEY)(
    _In_ const PVOID InterfaceContext,
    _Inout_ NVME_ICE_PROGRAM_KEY_DATA* KeyData,
    _In_ PULONG KeyDataSize
    );

#define NVME_ICE_IO_DESCRIPTOR_VERSION_1              1

typedef struct NVME_ICE_IO_DESCRIPTOR {

    // IN: Version of struct
    USHORT Version;

    // IN: Size of struct
    USHORT Size;

    // IN: Number of LBAs in request
    _Field_range_(>, 0) ULONG LBACount;

    // IN: Number of PRPs in request
    _Field_range_(>, 0) ULONG PRPCount;

    // IN: Key handle to use for this I/O as provided by ProgramKey.
    PVOID KeyHandle;

    // IN: Initialization Vector to use for crypto
    UCHAR IV[16];

    // OUT: Context to be passed back to driver on completion. If no work is
    //      required, set value to NULL.
    PVOID IOContext;

    //
    // PRP1, PRP2, and PRPEntries are all IN_OUT. They contain the page addresses for
    // this request, but have different usages depending on the value of PRPCount.
    //
    // If PRPCount == 1:
    //     PRP1 points to the singular PRP. PRP2 and PRPEntries are NULL.
    // If PRPCount == 2:
    //     PRP1 and PRP2 each point to PRPs. PRPEntries is NULL.
    // If PRPCount > 2:
    //     PRP1 points to a singular PRP and PRPEntries points to an array of the remaining
    //     PRPCount-1 PRPs. PRP2 is NULL.
    //
    PNVME_PRP_ENTRY PRP1;
    PNVME_PRP_ENTRY PRP2;
    PNVME_PRP_ENTRY PRPEntries;

} NVME_ICE_IO_DESCRIPTOR;

/*++

PIO_START

Routine Description:

    Starts a crypto I/O on a SoC that supports NVMe ICE. If this function needs to perform any memory
    allocations then it must ensure that those memory allocations do not fail.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    IODescriptor     - Parameters to be used for crypto I/O.

Return Value:

    STOR_STATUS_SUCCESS on success.
    STOR_STATUS_BUSY on transient error. This suggests a retry.
    STOR_STATUS_RESET_REQUIRED if hardware needs to be reset. (new STOR_STATUS value)
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_max_(DISPATCH_LEVEL)
typedef
ULONG
(__stdcall *PIO_START)(
    _In_ PVOID InterfaceContext,
    _Inout_ NVME_ICE_IO_DESCRIPTOR* IODescriptor
    );

/*++

PIO_COMPLETE

Routine Description:

    Completes a crypto I/O on a SoC that supports NVMe ICE. If this function needs to perform any memory
    allocations then it must ensure that those memory allocations do not fail.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    IOContext        - Specifies which I/O to complete as returned in NVME_ICE_IO_DESCRIPTOR::IoContext.

Return Value:

    STOR_STATUS_SUCCESS on success.
    STOR_STATUS_RESET_REQUIRED if hardware needs to be reset. (new STOR_STATUS value)
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_(DISPATCH_LEVEL)
typedef
ULONG
(__stdcall *PIO_COMPLETE)(
    _In_ PVOID InterfaceContext,
    _In_ PVOID IOContext
    );

//
// NOTE: This interface is deprecated and will be removed.
// Use and implement NVME_ICE_INTERFACE_V2
//

#define NVME_ICE_INTERFACE_VERSION_1                  1

typedef struct NVME_ICE_INTERFACE {

    //
    // Generic interface header
    //
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;

    //
    // Required NVMe ICE interface functions
    //
    PQUERY_CAPABILITIES QueryCapabilities;
    PCONFIGURE_CAPABILITY ConfigureCapability;
    PPROGRAM_KEY ProgramKey;

    //
    // Optional NVMe ICE interface functions
    //
    PIO_START IOStart;
    PIO_COMPLETE IOComplete;

} NVME_ICE_INTERFACE;

//
// GUID_NVME_ICE_INTERFACE_V2 is the interface that is exposed by NVMe ICE drivers.
//
//  This interface is exposed via a separate interface GUID from the original
//  GUID_NVME_ICE_INTERFACE in order to be compatible with the WDF device interface
//  management system. GUID_NVME_ICE_INTERFACE is deprecated and will be removed.
//
//  NVME_ICE_INTERFACE_V2 supports:
//     * A simplified model for programming crypto keys, with support for "key wrapping"
//       to hide key material from the CPU.
//
//     * Support for dump and hibernate/resume workflows
//
// {cf5226e9-21f4-44ca-8d8f-7160f4df5237}
//
DEFINE_GUID( GUID_NVME_ICE_INTERFACE_V2, 0xcf5226e9, 0x21f4, 0x44ca, 0x8d, 0x8f, 0x71, 0x60, 0xf4, 0xdf, 0x52, 0x37);

#pragma warning(push)
#pragma warning(disable:4201) // nameless struct/unions

typedef struct NVME_ICE_CAPABILITIES_V2 {

    // Supported page sizes. Multiple bits may be set.
    NVME_ICE_PAGE_SIZE_BITMASK PageSizeBitmask;

    // Supported data alignments. Multiple bits may be set.
    NVME_ICE_DATA_ALIGNMENT_BITMASK DataTransferAlignmentBitmask;

    // Compliant security standards at the hardware level. Multiple bits may be set.
    STORAGE_SECURITY_COMPLIANCE_BITMASK SecurityComplianceBitmask;

    // Supported key types. Multiple bits may be set.
    STORAGE_CRYPTO_KEY_TYPE KeyTypeBitmask;

    // Maximum supported transfer size in KB
    USHORT MaxTransferSizeKBytes;

    // Maximum supported outstanding command count. Set to zero if no limit.
    USHORT MaxCommandCount;

    // Contains supported crypto configurations
    STOR_CRYPTO_CAPABILITIES_DATA CryptoCapabilitiesData;

} NVME_ICE_CAPABILITIES_V2;

#pragma warning(pop)

/*++

PQUERY_CAPABILITIES_V2

Routine Description:

    Returns information about the capabilities of a SoC that supports NVMe ICE.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    PciAddress       - The PCI SBDF of the NVMe device.
    Capabilities     - The capabilities of the SoC. Can be NULL to query CapabilitiesSize.
    CapabilitiesSize - On input, provides size of the Capabilities struct or can be
                       0 when querying required size. On output, contains actual size of
                       Capabilities struct or required size.

Return Value:

    STOR_STATUS_SUCCESS on success.
    STOR_STATUS_BUFFER_TOO_SMALL if Capabilities is NULL or if CapabilitiesSize is too small.
    STOR_STATUS_INVALID_DEVICE_REQUEST if NVMe device is not supported.
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_(PASSIVE_LEVEL)
typedef
ULONG
(__stdcall *PQUERY_CAPABILITIES_V2)(
    _In_ PVOID InterfaceContext,
    _In_ const NVME_PCI_ADDRESS* PciAddress,
    _Out_ NVME_ICE_CAPABILITIES_V2* Capabilities,
    _Inout_ PULONG CapabilitiesSize
    );

typedef struct NVME_ICE_PROGRAM_KEY_DATA_V2 {

    // IN: Crypto capability to configure as reported by QueryCapabilities.
    USHORT ReportedCryptoCapabilityIndex;

    // IN: Chosen maximum supported transfer size in KB
    USHORT MaxTransferSizeKBytes;

    // IN: Chosen size of an LBA in bytes
    ULONG LBASize;

    // IN: Chosen size of PRP Page Size in bytes
    ULONG PRPPageSize;

    // IN: Chosen key type
    STORAGE_ICE_KEY_TYPE KeyType;

    // IN: The key's size
    USHORT KeyContentSize;

    // IN_OUT: Driver returns the key index associated with the programed key 
    USHORT KeyIndex;

    // IN: Namespace ID for the key
    GUID KeyNamespaceId;

    // IN_OUT: If NULL, driver is expected to return an unused key handle.
    //         If non-NULL, driver is expected to replace key associated with provided handle with new key.
    PVOID KeyHandle;

    // IN: Contains the key
    _Field_size_bytes_(KeyContentSize) UCHAR KeyContent[ANYSIZE_ARRAY];

} NVME_ICE_PROGRAM_KEY_DATA_V2;

/*++

PPROGRAM_KEY_V2

Routine Description:

    Programs a key to a SoC that supports NVMe ICE. If this function needs to perform any memory
    allocations then it must ensure that those memory allocations do not fail.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    KeyData          - The key data to be programmed to the SoC.
    KeyDataSize      - Provides size of KeyData struct.

Return Value:

    STOR_STATUS_SUCCESS on success.
    STOR_STATUS_BUSY on transient error. This suggests a retry.
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_max_(DISPATCH_LEVEL)
typedef
ULONG
(__stdcall *PPROGRAM_KEY_V2)(
    _In_ PVOID InterfaceContext,
    _Inout_ NVME_ICE_PROGRAM_KEY_DATA_V2* KeyData,
    _In_ PULONG KeyDataSize
    );

typedef enum _NVME_ICE_DUMP_TYPE {
    NVME_ICE_DUMP_TYPE_UNKNOWN = 0,
    NVME_ICE_DUMP_TYPE_CRASH = 1,
    NVME_ICE_DUMP_TYPE_HIBERNATE = 2,
    NVME_ICE_DUMP_TYPE_HIBER_RESUME = 3,
} NVME_ICE_DUMP_TYPE;

/*++

PVNME_ICE_DUMP_INITIALIZE

Routine Description:

    This function is called during normal execution when the system
    is setting up dump support. This callback is an appropriate place
    to allocate any resources that are needed for a later crashdump.

    NOTE: This will be called once each for crash dump and hibernate.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE_V2 structure.

    DumpType - The type of dump being initialized.

Return Value:

    NTSTATUS.

--*/
_IRQL_requires_max_(PASSIVE_LEVEL)
typedef
NTSTATUS
(__stdcall *PNVME_ICE_DUMP_INITIALIZE)(
    _In_ PVOID InterfaceContext,
    _In_ NVME_ICE_DUMP_TYPE DumpType
    );

/*++

PVNME_ICE_DUMP_CLEANUP

Routine Description:

    This function is called when the system is cleaning up dump support.
    This is an appropriate place to free any resources that were allocated
    during DumpInitialize.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE_V2 structure.

    DumpType - The type of dump being initialized.

Return Value:

    None.

--*/
_IRQL_requires_max_(PASSIVE_LEVEL)
typedef
VOID
(__stdcall *PNVME_ICE_DUMP_CLEANUP)(
    _In_ PVOID InterfaceContext,
    _In_ NVME_ICE_DUMP_TYPE DumpType
    );

/*++

PVNME_ICE_MARK_HIBER_RESUME_MEMORY

Routine Description:

    The ICE driver must call this function to mark memory that it requires during
    hibernate and resume. This function is called from a crash/hibernate context
    and must not call any DDIs that are illegal in those contexts.

    Examples of marked memory:
    * Driver code pages (pass a length of 0 to mark the entire section).
    * Context structures needed to process DumpIOStart/DumpIOComplete/DumpProgramKeyV2.

Arguments:

    Address - The address of the memory to mark.

    Length - The length of the memory to mark. If zero is passed, the entire
        section containing the address is marked.

Return Value:

    None.

--*/
_IRQL_requires_(HIGH_LEVEL)
typedef
VOID
(__stdcall *PVNME_ICE_MARK_HIBER_RESUME_MEMORY)(
    _In_ PVOID Address,
    _In_ ULONG_PTR Length
    );

typedef struct NVME_ICE_DUMP_START_PARAMETERS {

    // IN: The size of the structure, used for versioning.
    ULONG Size;

    // IN [optional]: The ICE driver must call this function to mark
    // memory that it requires during hibernate and resume.
    PVNME_ICE_MARK_HIBER_RESUME_MEMORY MarkHiberResumeMemoryCallback;

} NVME_ICE_DUMP_START_PARAMETERS;

/*++

PVNME_ICE_DUMP_START

Routine Description:

    This function is called when the system is about to read/write a crash dump or
    hiberfile. This function will be called after the dump storage adapter is
    reset.

    This function is called from a crash/hibernate context and must not call
    any DDIs that are illegal in those contexts.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE_V2 structure.

    DumpType - The type of dump being initialized.

    DumpStartParameters - An extensible set of parameters specific to the dump
        type that will commence.

Return Value:

    NTSTATUS.

--*/
_IRQL_requires_(HIGH_LEVEL)
typedef
NTSTATUS
(__stdcall *PNVME_ICE_DUMP_START)(
    _In_ PVOID InterfaceContext,
    _In_ NVME_ICE_DUMP_TYPE DumpType,
    _In_ NVME_ICE_DUMP_START_PARAMETERS *DumpStartParameters
    );

/*++

PVNME_ICE_DUMP_FINISH

Routine Description:

    This function is called when the system is finished reading/writing a crash dump
    or hiberfile. This function is called after all IOs are complete. It is called
    from a crash/hibernate context and must not call any DDIs that are illegal in
    those contexts.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE_V2 structure.

    DumpType - The type of dump being finished.

Return Value:

    None.

--*/
_IRQL_requires_(HIGH_LEVEL)
typedef
VOID
(__stdcall *PNVME_ICE_DUMP_FINISH)(
    _In_ PVOID InterfaceContext,
    _In_ NVME_ICE_DUMP_TYPE DumpType
    );

/*++

PDUMP_IO_START

Routine Description:

    Starts a crypto I/O during crashdump/hibernate on a SoC that supports NVMe ICE.

    This routine should not acquire any locks or call any DDIs that are illegal
    during crash.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    IODescriptor     - Parameters to be used for crypto I/O.

Return Value:

    STOR_STATUS_SUCCESS on success.
    STOR_STATUS_BUSY on transient error. This suggests a retry.
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_(HIGH_LEVEL)
typedef
ULONG
(__stdcall *PDUMP_IO_START)(
    _In_ const PVOID InterfaceContext,
    _Inout_ NVME_ICE_IO_DESCRIPTOR* IODescriptor
    );

/*++

PIO_COMPLETE

Routine Description:

    Completes a crypto I/O during dump/hibernate on a SoC that supports NVMe ICE.

    This routine should not acquire any locks or call any DDIs that are illegal
    during crash.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    IOContext        - Specifies which I/O to complete as returned in NVME_ICE_IO_DESCRIPTOR::IoContext.

Return Value:

    STOR_STATUS_SUCCESS on success.
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_(HIGH_LEVEL)
typedef
ULONG
(__stdcall *PDUMP_IO_COMPLETE)(
    _In_ const PVOID InterfaceContext,
    _In_ const PVOID IOContext
    );

/*++

PNVME_ICE_QUERY_STORAGE_DEVICE_SUPPORT

Routine Description:

    Returns whether the storage device is supported by the NVMe ICE driver.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    PciAddress       - The PCI SBDF of the NVMe device.

Return Value:

    STOR_STATUS_SUCCESS if the storage device is supported.
    STOR_STATUS_INVALID_DEVICE_REQUEST if the storage device is not supported.

--*/
_IRQL_requires_(PASSIVE_LEVEL)
typedef
ULONG
(__stdcall *PNVME_ICE_QUERY_STORAGE_DEVICE_SUPPORT)(
    _In_ PVOID InterfaceContext,
    _In_ const NVME_PCI_ADDRESS* PciAddress
    );

/*++

PNVME_ICE_QUERY_PLATFORM_CAPABILITIES

Routine Description:

    Returns information about the capabilities of the platform that supports NVMe ICE.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    Capabilities     - The capabilities of the platform. Can be NULL to query CapabilitiesSize.
    CapabilitiesSize - On input, provides size of the Capabilities struct or can be
                       0 when querying required size. On output, contains actual size of
                       Capabilities struct or required size.

Return Value:

    STOR_STATUS_SUCCESS on success.
    STOR_STATUS_BUFFER_TOO_SMALL if Capabilities is NULL or if CapabilitiesSize is too small.
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_(PASSIVE_LEVEL)
typedef
ULONG
(__stdcall *PNVME_ICE_QUERY_PLATFORM_CAPABILITIES)(
    _In_ PVOID InterfaceContext,
    _Out_ NVME_ICE_CAPABILITIES_V2* Capabilities,
    _Inout_ PULONG CapabilitiesSize
    );

typedef struct NVME_ICE_NVME_CAPABILITIES {

    // Maximum number of Submission Queues supported. Set to zero if no limit.
    USHORT MaxSQCount;

    // Maximum number of Completion Queues supported. Set to zero if no limit.
    USHORT MaxCQCount;

    // Max Namespace ID supported. Set to 0xFFFFFFFF if no limit.
    ULONG MaxNSID;

    // Max number of Exclusion Ranges supported. Set to zero if not supported.
    ULONG MaxERCount;

    // NVMe Command DWord location for CryptoEnabled bit. Set value to 0xFF if not supported.
    UCHAR CECDWNumber;
    UCHAR CECDWBitPosition;

    // NVME Command DWord location for Key Slot information. Set value to 0xFF if not supported.
    UCHAR KSICDWNumber;
    UCHAR KSICDWStartBit;
    UCHAR KSINumberOfBits;
 
    // NVME Command DWord locations for IV. Set value to 0xFF if not supported. IV0 is the least significant bytes.
    UCHAR IV0CDWNumber;
    UCHAR IV1CDWNumber;
    UCHAR IV2CDWNumber;
    UCHAR IV3CDWNumber;

#pragma warning(push)
#pragma warning(disable:4201) // nameless struct/unions
    union {
        struct {
            ULONG MarkLastPRPList : 1;
            ULONG Reserved        : 31;
        };
        ULONG AsUlong;
    } Flags;
#pragma warning(pop)

    UCHAR Reserved[3];

} NVME_ICE_NVME_CAPABILITIES;

/*++

PQUERY_NVME_CAPABILITIES

Routine Description:

    Returns information about the NVMe capabilities of a SoC that supports NVMe ICE.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    PciAddress       - The PCI SBDF of the NVMe device.
    NvmeCapabilities - The capabilities of the SoC.

Return Value:

    STOR_STATUS_SUCCESS on success.
    STOR_STATUS_INVALID_DEVICE_REQUEST if NVMe device is not supported.
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_(PASSIVE_LEVEL)
typedef
ULONG
(__stdcall *PQUERY_NVME_CAPABILITIES)(
    _In_ PVOID InterfaceContext,
    _In_ const NVME_PCI_ADDRESS* PciAddress,
    _Inout_ NVME_ICE_NVME_CAPABILITIES* NvmeCapabilities
    );

typedef struct NVME_ICE_NAMESPACE_CONFIG {

    // Namespace ID
    USHORT NamespaceId;

    // LBA size of this namespace
    USHORT LBASize;

} NVME_ICE_NAMESPACE_CONFIG;

typedef enum _NVME_ICE_QUEUE_TYPE {
    NVME_ICE_QUEUE_TYPE_UNKNOWN = 0,
    NVME_ICE_QUEUE_TYPE_ADMIN_SQ = 1,
    NVME_ICE_QUEUE_TYPE_ADMIN_CQ = 2,
    NVME_ICE_QUEUE_TYPE_IO_SQ = 3,
    NVME_ICE_QUEUE_TYPE_IO_CQ = 4,
    NVME_ICE_QUEUE_TYPE_MAX    
} NVME_ICE_QUEUE_TYPE;

typedef struct NVME_ICE_QUEUE_CONFIG {

    // Queue ID
    USHORT QueueId;

    UCHAR Reserved[2];

    // Queue size in bytes
    ULONG QueueSize;

    // Queue type
    NVME_ICE_QUEUE_TYPE QueueType;

    // Start address of the queue
    STOR_PHYSICAL_ADDRESS QueueAddress;

} NVME_ICE_QUEUE_CONFIG;

#define NVME_ICE_ENABLE_NVME_DEVICE_VERSION_1              1

typedef struct NVME_ICE_ENABLE_NVME_DEVICE {

    // Version
    USHORT Version;

    // Size of the whole structure
    ULONG Size;

    // Number of namespaces
    USHORT NamespaceCount;

    // Offset to an array of NVME_ICE_NAMESPACE_CONFIG. Zero value indicates no namespaces.
    USHORT NamespacesArrayOffset;

    // Number of queues
    USHORT QueueCount;

    // Offset to an array of NVME_ICE_QUEUE_CONFIG. Zero value indicates no queues.
    USHORT QueuesArrayOffset;

} NVME_ICE_ENABLE_NVME_DEVICE;

/*++

PNVME_ICE_ENABLE_NVME_SUPPORT

Routine Description:

    Enable the NVMe ICE hardware to support the specified NVMe device.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    PciAddress       - The PCI SBDF of the NVMe device.
    NvmeDevice       - A NVME_ICE_ENABLE_NVME_DEVICE struct.

Return Value:

    STOR_STATUS_SUCCESS on success.
    STOR_STATUS_INVALID_DEVICE_REQUEST if NVMe device is not supported.
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_(PASSIVE_LEVEL)
typedef
ULONG
(__stdcall *PNVME_ICE_ENABLE_NVME_SUPPORT)(
    _In_ PVOID InterfaceContext,
    _In_ const NVME_PCI_ADDRESS* PciAddress,
    _In_ NVME_ICE_ENABLE_NVME_DEVICE* NvmeDevice
    );

/*++

PNVME_ICE_NOTIFY_HARDWARE_RESET

Routine Description:

    Notify that the specified NVMe device has experience a hardware reset.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    PciAddress       - The PCI SBDF of the NVMe device.

Return Value:

    STOR_STATUS_SUCCESS on success.
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_max_(DISPATCH_LEVEL)
typedef
ULONG
(__stdcall *PNVME_ICE_NOTIFY_HARDWARE_RESET)(
    _In_ PVOID InterfaceContext,
    _In_ const NVME_PCI_ADDRESS* PciAddress
    );

typedef enum NVME_ICE_EXCLUSION_ACTION {
    NVME_ICE_EXCLUSION_ACTION_UNKNOWN = 0,
    NVME_ICE_EXCLUSION_ACTION_ADD = 1,
    NVME_ICE_EXCLUSION_ACTION_REMOVE = 2,
} NVME_ICE_EXCLUSION_ACTION;

#define NVME_ICE_ADDRESS_RANGE_VERSION_1              1

typedef struct NVME_ICE_ADDRESS_RANGE {

     // Version
    USHORT Version;

    // Size of the whole structure
    USHORT Size;

    STOR_PHYSICAL_ADDRESS RangeStart;
    ULONGLONG RangeLength;

    ULONG Readable: 1;
    ULONG Writable: 1;
    ULONG Reserved: 30;
} NVME_ICE_ADDRESS_RANGE;

/*++

PNVME_ICE_CONFIGURE_EXCLUSION_RANGES

Routine Description:

    Configure the exclusion ranges for the NVMe ICE hardware. One or more exclusion
    ranges can be added or removed. If adding an exclusion address range that has
    been already added before, STOR_STATUS_SUCCESS will be returned.

Arguments:

    InterfaceContext - The Context member of the NVME_ICE_INTERFACE structure.
    ExclusionRanges  - Array of NVME_ICE_EXCLUSION_RANGE structs.
    ExclusionRangeCount - Number of exclusion ranges in the array.
    ExclusionAction - Action to take on the exclusion ranges.

Return Value:

    STOR_STATUS_SUCCESS on success.
    STOR_STATUS_INSUFFICIENT_RESOURCES if the hardware cannot support the requested exclusion ranges.
    A STOR_STATUS error code otherwise.

--*/
_IRQL_requires_max_(DISPATCH_LEVEL)
typedef
ULONG
(__stdcall *PNVME_ICE_CONFIGURE_EXCLUSION_RANGES)(
    _In_ PVOID InterfaceContext,
    _In_count_(ExclusionRangeCount) NVME_ICE_ADDRESS_RANGE* ExclusionRanges,
    _In_ USHORT ExclusionRangeCount,
    _In_ NVME_ICE_EXCLUSION_ACTION ExclusionAction
    );

#define NVME_ICE_INTERFACE_VERSION_2                  2

typedef struct NVME_ICE_INTERFACE_V2 {

    //
    // Generic interface header
    //
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;

    //
    // Required NVMe ICE V2 interface functions
    //

    PQUERY_CAPABILITIES_V2 QueryCapabilitiesV2;
    PPROGRAM_KEY_V2 ProgramKeyV2;

    PPROGRAM_KEY_V2 DumpProgramKeyV2;

    //
    // Optional NVMe ICE V2 interface functions
    //
    PIO_START IOStart;
    PIO_COMPLETE IOComplete;

    //
    // Note: The DumpIOStart/DumpIOComplete interfaces
    // will only be called after DumpProgramKeyV2 has been called.
    //
    PDUMP_IO_START DumpIOStart;
    PDUMP_IO_COMPLETE DumpIOComplete;

    PNVME_ICE_DUMP_INITIALIZE DumpInitialize;
    PNVME_ICE_DUMP_CLEANUP DumpCleanup;

    PNVME_ICE_DUMP_START DumpStart;
    PNVME_ICE_DUMP_FINISH DumpFinish;

    PNVME_ICE_QUERY_STORAGE_DEVICE_SUPPORT QueryStorageDeviceSupport;
    PNVME_ICE_QUERY_PLATFORM_CAPABILITIES QueryPlatformCapabilities;

    //
    // Optional interface functions
    //
    PQUERY_NVME_CAPABILITIES QueryNVMeCapabilities;
    PNVME_ICE_ENABLE_NVME_SUPPORT EnableNvmeSupport;
    PNVME_ICE_CONFIGURE_EXCLUSION_RANGES ConfigureExclusionRanges;
    PNVME_ICE_NOTIFY_HARDWARE_RESET NotifyHardwareReset;

} NVME_ICE_INTERFACE_V2;
