/*++

Copyright (c) 2005-2006  Microsoft Corporation

Module Name:

    netdma.h

Abstract:

    This module contains function prototypes and definitons for memory to memory DMA
    providers

Author:

    
Revision history:
    Nov 2006: Added support for a subset of IOAT V2 features (page break and descriptor ring)
    Feb 2005: Original version
    
Environment:

    kernel mode only

--*/
#ifndef _NET_DMA_H
#define _NET_DMA_H

#if NTDDI_VERSION >= NTDDI_VISTASP1

#undef NET_DMA_EXPORT
#if defined(NET_DMA)
#define NET_DMA_EXPORT
#else
#define NET_DMA_EXPORT DECLSPEC_IMPORT
#endif

//
// flags used in NET_DMA_DESCRIPTOR->ControlFlags
//
#define NET_DMA_INTERRUPT_ON_COMPLETION     0x00000001
#define NET_DMA_SOURCE_NO_SNOOP             0x00000002
#define NET_DMA_DESTINATION_NO_SNOOP        0x00000004
#define NET_DMA_STATUS_UPDATE_ON_COMPLETION 0x00000008
#define NET_DMA_SERIALIZE_TRANSFER          0x00000010      // fence bit
#define NET_DMA_NULL_TRANSFER               0x00000020

//
// new flags supported in NETDMA V2
//

#define NET_DMA_SOURCE_PAGE_BREAK           0x00000040
#define NET_DMA_DESTINATION_PAGE_BREAK      0x00000080
#define NET_DMA_DESTINATION_DCA_ENABLE      0x00000200

//
// flags used in NET_DMA_DESCRIPTOR->ControlFlags with NET_DMA_OP_TYPE_CONTEXT_CHANGE
//
#define NET_DMA_DCA_CONTEXT_CHANGE          0x00000001


#define NET_DMA_OP_TYPE_STANDARD_DMA        0x00000000
#define NET_DMA_OP_TYPE_CONTEXT_CHANGE      0xFF000000


//
// The low 3 bits in "Completed Descriptor Address" are used
// to indicate the status of a DMA transfer. The physical and virtual address of 
// "Completed Descriptor Address" of a DMA channel are specified 
// by CompletionVirtualAddress and CompletionPhysicalAddress fields of 
// NET_DMA_CHANNEL_PARAMETERS at the time of allocating a DMA channel
//
#define NET_DMA_TRANSFER_STATUS_MASK        0x00000007

typedef enum _NET_DMA_TRANSFER_STATUS
{
    NetDmaTransferStatusActive = 0,     // 000 = Active, transfer completed successfully. This was -not- the last pending descriptor
    NetDmaTransferStatusIdle = 1,       // 001 = Idle, transfer completed successfully. This was the last pending descriptor
    NetDmaTransferStatusSuspend = 2,    // 010 = Suspend completed (no hard error). DMA channel is in Halt state
    NetDmaTransferStatusHalted = 3,     // 011 = Halted, operation aborted
    NetDmaTransferStatusArmed = 4       // 100 = Armed, first descriptor has not yet completed and Completed Descriptor Address is not valid
} NET_DMA_TRANSFER_STATUS, *PNET_DMA_TRANSFER_STATUS;
        
  
//
// NET_DMA_DESCRIPTOR data structure is used to submit a dma transfer to DMA engine
// DMA descriptors can be chained together. Descriptor structure is modeled after
// CB DMA descriptors. DMA descriptors are aligned on 64 bit boundary
//
typedef struct _NET_DMA_DESCRIPTOR
{
    union
    {
        ULONG               TransferSize;       // DMA Transfer size
        // DCA context = the APIC ID of the target CPU
        struct 
        {
            ULONG           DCAContext:32;
        }DCAContext32;
        struct
        {   
            ULONG           DCAContext:16;
            ULONG           Reserved:16;
        }DCAContext16;
        struct
        {
            ULONG           DCAContext:8;
            ULONG           Reserved:24;
        }DCAContext8;
    };
    
    ULONG               ControlFlags;           // see NET_DMA_xxx flags
    PHYSICAL_ADDRESS    SourceAddress;          // Source physical address
    PHYSICAL_ADDRESS    DestinationAddress;     // Destination physical address
    PHYSICAL_ADDRESS    NextDescriptor;         // Physical address of the next descriptor in the chain
    union
    {
        ULONG64             Reserved1;              // reserved for use by DMA engine (Provider)
        PHYSICAL_ADDRESS    NextSourceAddress;
    };
    union
    {
        ULONG64             Reserved2;              // reserved for use by DMA engine (Provider)
        PHYSICAL_ADDRESS    NextDestinationAddress;
    };
    ULONG64             UserContext1;           // Used by netdma driver and/or DMA client
    ULONG64             UserContext2;           // Used by netdma driver and/or DMA client
} NET_DMA_DESCRIPTOR, *PNET_DMA_DESCRIPTOR;


//
// DMA Provider entry points
//

typedef struct _NET_DMA_CHANNEL_CPU_AFFINITY
{
    ULONG   DmaChannel;
    ULONG   CpuNumber;
}NET_DMA_CHANNEL_CPU_AFFINITY,*PNET_DMA_CHANNEL_CPU_AFFINITY;

//
// DMA channel CPU affinity handler. NETDMA calls this entry point to set the affinity
// policy of DMA channels
//
typedef
NTSTATUS
(*DMA_CHANNELS_CPU_AFFINITY_HANDLER)(
    __in  PVOID   ProviderContext,                        // the Provider context passed to NetDmaRegisterProvider
    __in  PNET_DMA_CHANNEL_CPU_AFFINITY CpuAffinityArray, // see NET_DMA_CHANNEL_CPU_AFFINITY
    __in  ULONG   CpuAffinityArraySize                    // size of CpuAffinityArray
    );


//
// NET_DMA_CHANNEL_PARAMETERS is used to set up a DMA channel
// at the time of allocating the channel
//

#define NET_DMA_CHANNEL_REVISION_1  1

#define SIZEOF_NET_DMA_CHANNEL_PARAMETERS_REVISION_1                     \
        RTL_SIZEOF_THROUGH_FIELD(NET_DMA_CHANNEL_PARAMETERS, CpuNumber)

#if NTDDI_VERSION >= NTDDI_WIN7

#define NET_DMA_CHANNEL_REVISION_2  2

#define SIZEOF_NET_DMA_CHANNEL_PARAMETERS_REVISION_2                     \
        RTL_SIZEOF_THROUGH_FIELD(NET_DMA_CHANNEL_PARAMETERS, ProcessorAffinityMaskEx)

#endif

typedef struct _NET_DMA_CHANNEL_PARAMETERS
{
    USHORT              Revision;                   // Structure revision
    USHORT              Size;                       // size of this structure
    ULONG               Flags;                      // TBD
    PVOID               CompletionVirtualAddress;   // the virtual address where the engine will write the completion status
    PHYSICAL_ADDRESS    CompletionPhysicalAddress;  // the physical address where the engine will write the completion status
    ULONG               ProcessorAffinityMask;      // bitmap of CPUs that this channel could be associated with (deprecated in Win7)
    ULONG               ChannelPriority;            // per CB spec
    ULONG               CpuNumber;                  // provider will fill this with the CPU this channel is associated with
#if NTDDI_VERSION >= NTDDI_WIN7
    GROUP_AFFINITY      ProcessorAffinityMaskEx;    // group number and bitmap of CPUs that this channel could be associated with
#endif
} NET_DMA_CHANNEL_PARAMETERS, *PNET_DMA_CHANNEL_PARAMETERS;

//
// DMA channel Allocate handler. NETDMA calls this entry point to allocate a DMA channel
//
typedef
NTSTATUS
(*DMA_CHANNEL_ALLOCATE_HANDLER)(
    __in        PVOID   ProviderContext,                        // the Provider context passed to NetDmaRegisterProvider
    __in        PNET_DMA_CHANNEL_PARAMETERS ChannelParameters,  // see NET_DMA_CHANNEL_PARAMETERS
    __in        PVOID   NetDmaChannelHandle,                    // NETDMA handle for this channel. used by all calls from Provider to NETDMA regarding this channel
    __deref_out PVOID * pProviderChannelContext                 // on return holds Provider context for this channel. used in all calls from NETDMA to provider regarding this channel
    );

//
// NETDMA calls this entry point to free a DMA channel. When NETDMA calls this
// entry points, there are no outstanding DMA operations on this channel. Once
// NETDMA makes this call, it can not call the provider for any operation on the
// channel
// 

typedef
VOID
(*DMA_CHANNEL_FREE_HANDLER)(
    __in  PVOID   ProviderChannelContext               // Provider context for this channel (retuned from DMA_CHANNEL_ALLOCATE_HANDLER in pProviderChannelContext)
    );

//
// NETDMA calls this entry point to start a DMA transfer
//
typedef
NTSTATUS
(*DMA_START_HANDLER)(
    __in  PVOID                   ProviderChannelContext,     // Provider context for this channel
    __in  PNET_DMA_DESCRIPTOR     DescriptorVirtualAddress,   // pointer to the virtual address of the first descriptor
    __in  PHYSICAL_ADDRESS        DescriptorPhysicalAddress,  // pointer to the physical address of the first descriptor
    __in  ULONG                   DescriptorCount             // number of descriptors
    );

//
// NETDMA calls this entry point to suspend the DMA transfers
// currently in progress. The provider will return the
// physical address of the last DMA descriptor that it processed
//
typedef
NTSTATUS
(*DMA_SUSPEND_HANDLER)(
    __in        PVOID                   ProviderChannelContext,     // Provider context for this channel
    __deref_out PPHYSICAL_ADDRESS*      pLastDescriptor             // the physical address of the last processed descriptor
    );

//
// NETDMA calls this entry point to resume DMA operations on 
// a suspended DMA channel
//
typedef
NTSTATUS
(*DMA_RESUME_HANDLER)(
    __in  PVOID   ProviderChannelContext                      // Provider context for this channel
    );

//
// NETDMA calls this entry point to abort all DMA transfers
// that have been scheduled on a DMA channel
//
typedef
NTSTATUS
(*DMA_ABORT_HANDLER)(
    __in  PVOID   ProviderChannelContext                      // Provider context for this channel
    );

//
// NETDMA calls this entry point to append a chain of DMA descriptors
// to the last descriptor on a DMA channel
//
typedef
NTSTATUS
(*DMA_APPEND_HANDLER)(
    __in  PVOID                   ProviderChannelContext,     // Provider context for this channel
    __in  PNET_DMA_DESCRIPTOR     DescriptorVirtualAddress,   // pointer to the virtual address of the first descriptor to append
    __in  PHYSICAL_ADDRESS        DescriptorPhysicalAddress,  // pointer to the physical address of the first descriptor to append
    __in  ULONG                   DescriptorCount             // number of descriptors
    );

//
// TBD:
//
typedef
NTSTATUS
(*DMA_RESET_HANDLER)(
    __in  PVOID   ProviderChannelContext
    );


//
// DMA providers use NET_DMA_PROVIDER_CHARACTERISTICS structure in the call to 
// NetDmaRegisterProvider
//
#define NET_DMA_PROVIDER_REVISION_1  1
#define NET_DMA_PROVIDER_REVISION_2  2

//
// Flags for use in the NET_DMA_PROVIDER_CHARACTERISTICS structure
//
#define NET_DMA_PROVIDER_CHARACTERISTICS_DCA_SUPPORTED 0x00000001   

#if NTDDI_VERSION >= NTDDI_WIN7
#define NET_DMA_PROVIDER_SUPPORTS_PROC_GROUPS          0x00000002
#endif

//
// For back-compat with Vista headers
//
#if NTDDI_VERSION < NTDDI_WIN7
#define NET_DMA_PROVIDER_ATTRIBUTES_DCA_SUPPORTED NET_DMA_PROVIDER_CHARACTERISTICS_DCA_SUPPORTED
#endif



typedef struct _NET_DMA_PROVIDER_CHARACTERISTICS
{
    UCHAR                           MajorVersion;               // Major version of the DMA provider
    UCHAR                           MinorVersion;               // Minor version of the DMA provider
    USHORT                          Size;                       // the size of this structure
    ULONG                           Flags;                      // Possible values listed below
    PDEVICE_OBJECT                  PhysicalDeviceObject;       // The physical device object PnP associates with this device
    ULONG                           MaxDmaChannelCount;         // Maximum number of DMA channels
    DMA_CHANNELS_CPU_AFFINITY_HANDLER SetDmaChannelCpuAffinity; // Set channel CPU affinity handler
    DMA_CHANNEL_ALLOCATE_HANDLER    AllocateDmaChannel;         // Allocate DMA channel handler
    DMA_CHANNEL_FREE_HANDLER        FreeDmaChannel;             // Free DMA channel handler
    DMA_START_HANDLER               StartDma;                   // Start DMA handler
    DMA_SUSPEND_HANDLER             SuspendDma;                 // Suspend DMA handler
    DMA_RESUME_HANDLER              ResumeDma;                  // Resume DMA handler
    DMA_ABORT_HANDLER               AbortDma;                   // Abort DMA handler
    DMA_APPEND_HANDLER              AppendDma;                  // Append DMA handler
    DMA_RESET_HANDLER               ResetChannel;               // Reset channel handler
    UNICODE_STRING                  FriendlyName;               // Provider's friendly name
} NET_DMA_PROVIDER_CHARACTERISTICS, *PNET_DMA_PROVIDER_CHARACTERISTICS;

//
// DMA providers call NetDmaRegisterProvider to register an instance of a 
// DMA provider. An instance of a DMA provider is associated with a device
// (DMA engine). Providers usually make this call in their AddDevice entry point.
// Note: the reason that the call has to be made during AddDevice and not
// START IRP is because allocating MSI-X resources has to be done in
// FILTER_RESOURCE_REQUIREMENTS that is sent to the device drivers before
// the START IRP
//
__drv_requiresIRQL(PASSIVE_LEVEL)
NET_DMA_EXPORT
NTSTATUS
NetDmaRegisterProvider(
    __in  PVOID                           ProviderContext,                // Provider context for this device
    __in  PVOID *                         pNetDmaProviderHandle,          // upon return, this will hold NETDMA handle for this device
    __in  PNET_DMA_PROVIDER_CHARACTERISTICS ProviderCharacteristics       // provider's characteristics
    );

//
// DMA providers call NetDmaDeregisterProvider to deregister a provider
// that was previously registered by a call to NetDmaRegisterProvider
// the call is usually made while handling REMOVE IRP. 
//
__drv_requiresIRQL(PASSIVE_LEVEL)
NET_DMA_EXPORT
VOID
NetDmaDeregisterProvider(
    __in PVOID                        NetDmaProviderHandle                // Handle obtained by the call to NetDmaRegisterProvider
    );

//
// DMA providers use this structure in the call to NetDmaProviderStart
//

typedef struct _NET_DMA_PROVIDER_ATTRIBUTES
{
    UCHAR                           MajorHwVersion;             // Major version of the DMA provider
    UCHAR                           MinorHwVersion;             // Minor version of the DMA provider
    USHORT                          Size;                       // the size of this structure
    ULONG                           Flags;                      // TBD
    ULONG                           VendorId;                   // vendor ID
    ULONG                           DmaChannelCount;            // number of DMA channels
    ULONG                           MaximumTransferSize;        // Maximum DMA transfer size (minimum of 4K)
    PHYSICAL_ADDRESS                MaximumAddressSpace;        // Maximum physical adddress that can be addressed by this device
} NET_DMA_PROVIDER_ATTRIBUTES, *PNET_DMA_PROVIDER_ATTRIBUTES;

//
// DMA providers call NetDmaProviderStart to notify NETDMA that all the DMA channels
// on a provider are initialized  and ready to be used. DMA providers call this API in the
// context of handling START IRP.
//
NET_DMA_EXPORT
VOID
NetDmaProviderStart(
    __in PVOID                        NetDmaProviderHandle,      // Handle obtained by the call to NetDmaRegisterProvider
    __in PNET_DMA_PROVIDER_ATTRIBUTES ProviderAttributes         // the provider's device attributes    
    );

//
// DMA providers call NetDmaProviderStop to notify NETDMA that a previously started
// DMA engine is no longer available.
// DMA providers call this API in the context of handling REMOVE or STOP IRP. NETDMA will wait 
// for outstanding DMA operations to complete and frees all the DMA channels
// before returning back from this API
//
NET_DMA_EXPORT
VOID
NetDmaProviderStop(
    __in PVOID                NetDmaProviderHandle        // Handle obtained by the call to NetDmaRegisterProvider                
    );

//
// DMA providers call NetDmaIsr in their ISR handler
//
NET_DMA_EXPORT
VOID
NetDmaIsr(
    __in  PVOID               NetDmaChannelHandle,
    __in  PHYSICAL_ADDRESS    DmaDescriptor,
    __out PULONG              pCpuNumber
    );

//
// DMA providers call NetDmaInterruptDpc in their interrupt DPC handler
//
NET_DMA_EXPORT
VOID
NetDmaInterruptDpc(
    __in  PVOID               NetDmaChannelHandle,    // DMA channel
    __in_opt  PHYSICAL_ADDRESS    DmaDescriptor // optional if this is called for reasons other DMA completion
    );


NET_DMA_EXPORT
UINT
NetDmaGetVersion(
    VOID
    );


typedef enum _NET_DMA_PNP_NOTIFICATION_CODE
{
    NetDmaNotificationProviderRegistered = 1,   // a provider has registered. not used by providers
    NetDmaNotificationProviderArrival,          // a provider is ready for use. not used by providers
    NetDmaNotificationProviderRemoval,          // a provider is about to be removed. not used by providers
    NetDmaNotificationChannelArrival,           // a DMA channel has become available. not used by providers
    NetDmaNotificationProviderPowerDown,        // the DMA provider is going to D3. 
    NetDmaNotificationProviderPowerUp,          // the DMA provider is back to D0
    NetDmaNotificationMax
}NET_DMA_PNP_NOTIFICATION_CODE, *PNET_DMA_PNP_NOTIFICATION_CODE;

#define NET_DMA_PNP_NOTIFICATION_REVISION_1 1

typedef struct _NET_DMA_PNP_NOTIFICATION
{
    ULONG                           StructureRevision;
    ULONG                           StructureSize;
    NET_DMA_PNP_NOTIFICATION_CODE   NotificationCode;
    PVOID                           Buffer;
    ULONG                           BufferLength;
}NET_DMA_PNP_NOTIFICATION, *PNET_DMA_PNP_NOTIFICATION;

//
// this API is available to Providers version 1.1 and above
//
NET_DMA_EXPORT
VOID
NetDmaPnPEventNotify(
    __in  PVOID                       NetDmaProviderHandle,   // Provider handle
    __in  PNET_DMA_PNP_NOTIFICATION   PnPEvent                // PnP and Power Management event
    );

#endif // NTDDI_VISTASP1

#endif //_NET_DMA_H


