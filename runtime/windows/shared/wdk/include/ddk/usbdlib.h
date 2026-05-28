/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    USBDLIB.H

Abstract:

   Services exported by USBD.

Environment:

    Kernel & user mode

Revision History:

    06-10-96 : created

--*/

#ifndef   __USBDLIB_H__
#define   __USBDLIB_H__

#ifdef OSR21_COMPAT
#pragma message("WARNING: OSR21_COMPAT SWITCH NOT SUPPORTED")
#endif

typedef struct _USBD_INTERFACE_LIST_ENTRY {
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    PUSBD_INTERFACE_INFORMATION Interface;
} USBD_INTERFACE_LIST_ENTRY, *PUSBD_INTERFACE_LIST_ENTRY;


//
// Macros for building URB requests
//

#define UsbBuildInterruptOrBulkTransferRequest(urb, \
                                               length, \
                                               pipeHandle, \
                                               transferBuffer, \
                                               transferBufferMDL, \
                                               transferBufferLength, \
                                               transferFlags, \
                                               link) { \
            (urb)->UrbHeader.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER; \
            (urb)->UrbHeader.Length = (length); \
            (urb)->UrbBulkOrInterruptTransfer.PipeHandle = (pipeHandle); \
            (urb)->UrbBulkOrInterruptTransfer.TransferBufferLength = (transferBufferLength); \
            (urb)->UrbBulkOrInterruptTransfer.TransferBufferMDL = (transferBufferMDL); \
            (urb)->UrbBulkOrInterruptTransfer.TransferBuffer = (transferBuffer); \
            (urb)->UrbBulkOrInterruptTransfer.TransferFlags = (transferFlags); \
            (urb)->UrbBulkOrInterruptTransfer.UrbLink = (link); }
            

#define UsbBuildGetDescriptorRequest(urb, \
                                     length, \
                                     descriptorType, \
                                     descriptorIndex, \
                                     languageId, \
                                     transferBuffer, \
                                     transferBufferMDL, \
                                     transferBufferLength, \
                                     link) { \
            (urb)->UrbHeader.Function =  URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE; \
            (urb)->UrbHeader.Length = (length); \
            (urb)->UrbControlDescriptorRequest.TransferBufferLength = (transferBufferLength); \
            (urb)->UrbControlDescriptorRequest.TransferBufferMDL = (transferBufferMDL); \
            (urb)->UrbControlDescriptorRequest.TransferBuffer = (transferBuffer); \
            (urb)->UrbControlDescriptorRequest.DescriptorType = (descriptorType); \
            (urb)->UrbControlDescriptorRequest.Index = (descriptorIndex); \
            (urb)->UrbControlDescriptorRequest.LanguageId = (languageId); \
            (urb)->UrbControlDescriptorRequest.UrbLink = (link); }



#define UsbBuildGetStatusRequest(urb, \
                                 op, \
                                 index, \
                                 transferBuffer, \
                                 transferBufferMDL, \
                                 link) { \
            (urb)->UrbHeader.Function =  (op); \
            (urb)->UrbHeader.Length = sizeof(struct _URB_CONTROL_GET_STATUS_REQUEST); \
            (urb)->UrbControlGetStatusRequest.TransferBufferLength = sizeof(USHORT); \
            (urb)->UrbControlGetStatusRequest.TransferBufferMDL = (transferBufferMDL); \
            (urb)->UrbControlGetStatusRequest.TransferBuffer = (transferBuffer); \
            (urb)->UrbControlGetStatusRequest.Index = (index); \
            (urb)->UrbControlGetStatusRequest.UrbLink = (link); }


#define UsbBuildFeatureRequest(urb, \
                               op, \
                               featureSelector, \
                               index, \
                               link) { \
            (urb)->UrbHeader.Function =  (op); \
            (urb)->UrbHeader.Length = sizeof(struct _URB_CONTROL_FEATURE_REQUEST); \
            (urb)->UrbControlFeatureRequest.FeatureSelector = (featureSelector); \
            (urb)->UrbControlFeatureRequest.Index = (index); \
            (urb)->UrbControlFeatureRequest.UrbLink = (link); }



#define UsbBuildSelectConfigurationRequest(urb, \
                                         length, \
                                         configurationDescriptor) { \
            (urb)->UrbHeader.Function =  URB_FUNCTION_SELECT_CONFIGURATION; \
            (urb)->UrbHeader.Length = (length); \
            (urb)->UrbSelectConfiguration.ConfigurationDescriptor = (configurationDescriptor);    }

#define UsbBuildSelectInterfaceRequest(urb, \
                                      length, \
                                      configurationHandle, \
                                      interfaceNumber, \
                                      alternateSetting) { \
            (urb)->UrbHeader.Function =  URB_FUNCTION_SELECT_INTERFACE; \
            (urb)->UrbHeader.Length = (length); \
            (urb)->UrbSelectInterface.Interface.AlternateSetting = (alternateSetting); \
            (urb)->UrbSelectInterface.Interface.InterfaceNumber = (interfaceNumber); \
            (urb)->UrbSelectInterface.Interface.Length = (length - sizeof(struct _URB_HEADER) - sizeof(USBD_CONFIGURATION_HANDLE)); \
            (urb)->UrbSelectInterface.ConfigurationHandle = (configurationHandle);  }


#define UsbBuildVendorRequest(urb, \
                              cmd, \
                              length, \
                              transferFlags, \
                              reservedbits, \
                              request, \
                              value, \
                              index, \
                              transferBuffer, \
                              transferBufferMDL, \
                              transferBufferLength, \
                              link) { \
            (urb)->UrbHeader.Function =  cmd; \
            (urb)->UrbHeader.Length = (length); \
            (urb)->UrbControlVendorClassRequest.TransferBufferLength = (transferBufferLength); \
            (urb)->UrbControlVendorClassRequest.TransferBufferMDL = (transferBufferMDL); \
            (urb)->UrbControlVendorClassRequest.TransferBuffer = (transferBuffer); \
            (urb)->UrbControlVendorClassRequest.RequestTypeReservedBits = (reservedbits); \
            (urb)->UrbControlVendorClassRequest.Request = (request); \
            (urb)->UrbControlVendorClassRequest.Value = (value); \
            (urb)->UrbControlVendorClassRequest.Index = (index); \
            (urb)->UrbControlVendorClassRequest.TransferFlags = (transferFlags); \
            (urb)->UrbControlVendorClassRequest.UrbLink = (link); }


// This is just a special vendor class request.
// Defined for windows XP and later

#if (NTDDI_VERSION >= NTDDI_WINXP)

#define UsbBuildOsFeatureDescriptorRequest(urb, \
                              length, \
                              interface, \
                              index, \
                              transferBuffer, \
                              transferBufferMDL, \
                              transferBufferLength, \
                              link) { \
            (urb)->UrbHeader.Function = URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR; \
            (urb)->UrbHeader.Length = (length); \
            (urb)->UrbOSFeatureDescriptorRequest.TransferBufferLength = (transferBufferLength); \
            (urb)->UrbOSFeatureDescriptorRequest.TransferBufferMDL = (transferBufferMDL); \
            (urb)->UrbOSFeatureDescriptorRequest.TransferBuffer = (transferBuffer); \
            (urb)->UrbOSFeatureDescriptorRequest.InterfaceNumber = (interface); \
            (urb)->UrbOSFeatureDescriptorRequest.MS_FeatureDescriptorIndex = (index); \
            (urb)->UrbOSFeatureDescriptorRequest.UrbLink = (link); }

#endif

//
// Get the USB status code
//

#define URB_STATUS(urb) ((urb)->UrbHeader.Status)

//
// Macros used for select interface and select configuration requests
//

//
// Calculates the size needed for a SELECT_CONFIGURATION URB request given
// the number of interfaces and the total number of pipes in all interfaces
// selected.
//


#define GET_SELECT_CONFIGURATION_REQUEST_SIZE(totalInterfaces, totalPipes) \
            (sizeof(struct _URB_SELECT_CONFIGURATION) + \
                ((totalInterfaces-1) * sizeof(USBD_INTERFACE_INFORMATION)) + \
                ((totalPipes-totalInterfaces)*sizeof(USBD_PIPE_INFORMATION)))

//
// Calculates the size needed for a SELECT_INTERFACE URB request given
// the number of pipes in the alternate interface selected.
//

#define GET_SELECT_INTERFACE_REQUEST_SIZE(totalPipes) \
            (sizeof(struct _URB_SELECT_INTERFACE) + \
             ((totalPipes-1)*sizeof(USBD_PIPE_INFORMATION)))

//
// Calculates the size of the interface information structure needed to describe
// a give interface based on the number of endpoints.
//


#define GET_USBD_INTERFACE_SIZE(numEndpoints) (sizeof(USBD_INTERFACE_INFORMATION) + \
                        (sizeof(USBD_PIPE_INFORMATION)*(numEndpoints)) \
                         - sizeof(USBD_PIPE_INFORMATION))

//
// Calculates the size of an iso urb request given the number of packets
//

#define  GET_ISO_URB_SIZE(n) (sizeof(struct _URB_ISOCH_TRANSFER)+\
        sizeof(USBD_ISO_PACKET_DESCRIPTOR)*n)


#ifndef _USBD_

__drv_maxIRQL(DISPATCH_LEVEL) 
DECLSPEC_IMPORT
VOID
USBD_GetUSBDIVersion(
    __out PUSBD_VERSION_INFORMATION VersionInformation
    );


__drv_preferredFunction("USBD_ParseConfigurationDescriptorEx", "Obsolete") 
DECLSPEC_IMPORT
PUSB_INTERFACE_DESCRIPTOR
USBD_ParseConfigurationDescriptor(
    __in PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    __in UCHAR InterfaceNumber,
    __in UCHAR AlternateSetting
    );
/*++

Routine Description:

    This function is replaced by USBD_ParseConfigurationDescriptorEx

Arguments:

Return Value:


--*/


__drv_preferredFunction("USBD_CreateConfigurationRequestEx", "Obsolete") 
DECLSPEC_IMPORT
PURB
USBD_CreateConfigurationRequest(
    __in  PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    __out PUSHORT Siz
    );
/*++

Routine Description:

    This function is replaced by USBD_CreateConfigurationRequestEx

Arguments:


Return Value:


--*/


//
// These APIS replace USBD_CreateConfigurationRequest,
//                    USBD_ParseConfigurationDescriptor
//

__drv_maxIRQL(APC_LEVEL) 
DECLSPEC_IMPORT
PUSB_COMMON_DESCRIPTOR
USBD_ParseDescriptors(
    __in PVOID DescriptorBuffer,
    __in ULONG TotalLength,
    __in PVOID StartPosition,
    __in LONG DescriptorType
    );
/*++

Routine Description:

    Parses a group of standard USB configuration descriptors (returned from a device) for
    a specific descriptor type.

Arguments:

    DescriptorBuffer - pointer to a block of contiguous USB desscriptors
    TotalLength - size in bytes of the Descriptor buffer
    StartPosition - starting position in the buffer to begin parsing,
                    this must point to the begining of a USB descriptor.
    DescriptorType - USB descritor type to locate.


Return Value:

    pointer to a usb descriptor with a DescriptorType field matching the
            input parameter or NULL if not found.

--*/


__drv_maxIRQL(APC_LEVEL) 
DECLSPEC_IMPORT
PUSB_INTERFACE_DESCRIPTOR
USBD_ParseConfigurationDescriptorEx(
    __in PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    __in PVOID StartPosition,
    __in LONG InterfaceNumber,
    __in LONG AlternateSetting,
    __in LONG InterfaceClass,
    __in LONG InterfaceSubClass,
    __in LONG InterfaceProtocol
    );
/*++

Routine Description:

    Parses a standard USB configuration descriptor (returned from a device) for
    a specific interface, alternate setting class subclass or protocol codes

Arguments:

    ConfigurationDescriptor - pointer to USB configuration descriptor, returned
                            from a device (includes all interface and endpoint
                            descriptors).
    StartPosition - pointer to starting position within the configuration
                    descrptor to begin parsing -- this must be a valid usb
                    descriptor.
    InterfaceNumber - interface number to find, (-1) match any
    AlternateSetting - alt setting number to find, (-1) match any
    InterfaceClass - class to find, (-1) match any
    InterfaceSubClass - subclass to find, (-1) match any
    InterfaceProtocol - protocol to find, (-1) match any

Return Value:

    returns a pointer to the first interface descriptor matching the parameters
    passed in (starting from startposition) or NULL if no match is found.

--*/

__drv_maxIRQL(DISPATCH_LEVEL) 
DECLSPEC_IMPORT
PURB
USBD_CreateConfigurationRequestEx(
    __in PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    __in PUSBD_INTERFACE_LIST_ENTRY InterfaceList
    );
/*++

Routine Description:

    Allocates and initilaizes a urb of sufficient size to configure a device
    based on the list of interfaces passed in.

    The interface list is a contiguous array of USBD_INTERFACE_LIST_ENTRIES
    each pointing to a specific interface descriptor to be incorporated in
    the request, the list is terminated by a list entry with an
    InterfaceDescriptor pointer of NULL.

    On return the interface field of each list entry is filled in with a pointer
    to the USBD_INTERFACE_INFORMATION structure within the URB corrisponding to
    the same interface descriptor.

Arguments:

    ConfigurationDescriptor - pointer to USB configuration descriptor, returned
                            from a device (includes all interface and endpoint
                            descriptors).

    InterfaceList - list of interfaces we are interested in.

Return Value:

    Pointer to initailized select_configuration urb.

--*/

__drv_maxIRQL(PASSIVE_LEVEL) 
__declspec(dllexport)
ULONG
USBD_GetInterfaceLength(
    __in PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor,
    __in PUCHAR BufferEnd
    );
/*++

Routine Description:

    Returns the length (in bytes) of a given interface descriptor
    including all endpoint and class descriptors


Arguments:

    InterfaceDescriptor

    BufferEnd - Pointer to the end of the buffer containing the descriptors

Return Value:

    length of descriptors

--*/


__drv_maxIRQL(PASSIVE_LEVEL) 
__declspec(dllexport)
VOID
USBD_RegisterHcFilter(
    __in PDEVICE_OBJECT DeviceObject,
    __in PDEVICE_OBJECT FilterDeviceObject
    );

/*++

Routine Description:

    Called by an HC filter driver after it attaches to the top
    of the host controller driver stack.

Arguments:

    DeviceObject - current top of stack

    FilterDeviceObject - device object for the filter driver

Return Value:

    none

--*/

__drv_maxIRQL(APC_LEVEL) 
__drv_preferredFunction("(see documentation)", "Obsolete") 
__declspec(dllexport)
NTSTATUS
USBD_GetPdoRegistryParameter(
    __in PDEVICE_OBJECT PhysicalDeviceObject,
    __inout_bcount(ParameterLength) PVOID Parameter,
    __in ULONG ParameterLength,
    __in_bcount(KeyNameLength) PWSTR KeyName,
    __in ULONG KeyNameLength
    );
/*++

Routine Description:

Arguments:

Return Value:

--*/

__drv_preferredFunction("(see documentation)", "Obsolete") 
__declspec(dllexport)
NTSTATUS
USBD_QueryBusTime(
    __in PDEVICE_OBJECT RootHubPdo,
    __out PULONG CurrentFrame
    );
/*++

Routine Description:

    Returns the current frame, callable at any IRQL

Arguments:

Return Value:

--*/

/*
    Defined for Windows XP and later
*/

#if (NTDDI_VERSION >= NTDDI_WINXP)

__drv_maxIRQL(DISPATCH_LEVEL) 
__drv_preferredFunction("(see documentation)", "Obsolete") 
DECLSPEC_IMPORT
ULONG
USBD_CalculateUsbBandwidth(
    __in ULONG MaxPacketSize,
    __in UCHAR EndpointType,
    __in BOOLEAN LowSpeed
    );
/*++

Routine Description:

    Returns bus bw consumed based on the endpoint type and
    packet size

Arguments:

Return Value:

--*/
#endif

#if (NTDDI_VERSION >= NTDDI_VISTA)

__drv_maxIRQL(DISPATCH_LEVEL) 
DECLSPEC_IMPORT
USBD_STATUS
USBD_ValidateConfigurationDescriptor(
    __in_bcount(BufferLength) PUSB_CONFIGURATION_DESCRIPTOR ConfigDesc,
    __in ULONG BufferLength,
    __in USHORT Level,
    __out PUCHAR *Offset,
    __in_opt ULONG Tag);
/*++

Routine Description:

    Validates a USB Configuration Descriptor

Arguments:

    ConfigDesc:  Pointer to the USB Configuration descriptor to be validated
    
    BufferLength: Known size of buffer pointed to by ConfigDesc (NOT NECESSARILY wTotalLength)
    
    Level: Level of checking desired:
           1: Basic Config Descriptor header check only
           
           2: Full pass-through of the config descriptor checking for the following:
                
                a) Unique endpoint addresses and interface numbers
                c) Number of interfaces contained in the descriptor
                d) Ensures the bLength values of the USB descriptors do not
                   exceed the length of the buffer.
                c) Basic validation of information in a USB_INTERFACE_ASSOCIATION_DESCRIPTOR
               
           3: Includes all of the validation for levels 1-2 plus the following:
            
                a) Validation of the number of endpoints in each interface
                b) Enforcement of the USB spec descriptor bLength sizes.
                c) Check to see if all interface numbers are in 
                sequential(not necessarily consecutive) order.
             

   Offset: if the USBD_STATUS returned is not USBD_STATUS_SUCCESS, offet will
   be set to the address within the ConfigDesc buffer where the failure occured.

   Tag:  Driver-defined heap pool tag to be used by usbdlib.
   
 Return Value:  
 
    One of the USBD_STATUS codes declared in usb.h
    
--*/

#endif /* (NTDDI_VERSION >= NTDDI_VISTA) */

#endif /* _USBD_ */

#endif /* __USBDLIB_H__ */

