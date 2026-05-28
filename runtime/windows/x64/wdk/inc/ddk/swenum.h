/*++

    Copyright (c) 1997 Microsoft Corporation

Module Name:

    swenum.h

Abstract:
    Public header file and bus interface definition for the
    software device enumerator.

--*/

#if !defined( _SWENUM_ )

#define _SWENUM_

// Io controls

#define IOCTL_SWENUM_INSTALL_INTERFACE  CTL_CODE(FILE_DEVICE_BUS_EXTENDER, 0x000, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SWENUM_REMOVE_INTERFACE   CTL_CODE(FILE_DEVICE_BUS_EXTENDER, 0x001, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SWENUM_GET_BUS_ID         CTL_CODE(FILE_DEVICE_BUS_EXTENDER, 0x002, METHOD_NEITHER, FILE_READ_ACCESS)

//
// Io control related structures
//

typedef struct _SWENUM_INSTALL_INTERFACE {
    GUID    DeviceId;
    GUID    InterfaceId;
    WCHAR  ReferenceString[1];
    
} SWENUM_INSTALL_INTERFACE, *PSWENUM_INSTALL_INTERFACE;

#if defined( _KS_ )

#define STATIC_BUSID_SoftwareDeviceEnumerator STATIC_KSMEDIUMSETID_Standard
#define BUSID_SoftwareDeviceEnumerator KSMEDIUMSETID_Standard

#else // !_KS_

#define STATIC_BUSID_SoftwareDeviceEnumerator \
    0x4747B320L, 0x62CE, 0x11CF, 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00
#if defined(__cplusplus) && _MSC_VER >= 1100
struct __declspec(uuid("4747B320-62CE-11CF-A5D6-28DB04C10000")) BUSID_SoftwareDeviceEnumerator;
#define BUSID_SoftwareDeviceEnumerator __uuidof(struct BUSID_SoftwareDeviceEnumerator)
#else
DEFINE_GUIDEX(BUSID_SoftwareDeviceEnumerator);
#endif // !(defined(__cplusplus) && _MSC_VER >= 1100)

#endif // !_KS_

#if defined( _NTDDK_ )

typedef 
VOID 
(*PFNREFERENCEDEVICEOBJECT)( 
    __in PVOID Context
    );
    
typedef 
VOID 
(*PFNDEREFERENCEDEVICEOBJECT)( 
    __in PVOID Context
    );
    
typedef
NTSTATUS
(*PFNQUERYREFERENCESTRING)( 
    __in PVOID Context,
    __inout PWCHAR *String
    );

#define BUS_INTERFACE_SWENUM_VERSION    0x100
    
typedef struct _BUS_INTERFACE_SWENUM {
    //
    // Standard interface header
    //
    
    INTERFACE                   Interface;
    
    //
    // SWENUM bus interfaces
    //
    
    PFNREFERENCEDEVICEOBJECT    ReferenceDeviceObject;
    PFNDEREFERENCEDEVICEOBJECT  DereferenceDeviceObject;
    PFNQUERYREFERENCESTRING     QueryReferenceString;
    
} BUS_INTERFACE_SWENUM, *PBUS_INTERFACE_SWENUM;

#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)

#if defined( _KS_ )

KSDDKAPI
NTSTATUS
NTAPI
KsQuerySoftwareBusInterface(
    __in PDEVICE_OBJECT PnpDeviceObject,
    __out PBUS_INTERFACE_SWENUM BusInterface
    );

KSDDKAPI
NTSTATUS
NTAPI
KsReferenceSoftwareBusObject(
    __in KSDEVICE_HEADER  Header
    );

KSDDKAPI
VOID
NTAPI
KsDereferenceSoftwareBusObject(
    __in KSDEVICE_HEADER  Header
    );

__drv_maxIRQL(PASSIVE_LEVEL)
KSDDKAPI
NTSTATUS
NTAPI
KsCreateBusEnumObject(
    __in PWSTR BusIdentifier,
    __in PDEVICE_OBJECT BusDeviceObject,
    __in PDEVICE_OBJECT PhysicalDeviceObject,
    __in_opt PDEVICE_OBJECT PnpDeviceObject,
    __in_opt REFGUID InterfaceGuid,
    __in_opt PWSTR ServiceRelativePath
    );
    
KSDDKAPI
NTSTATUS
NTAPI
KsGetBusEnumIdentifier(
    __inout PIRP Irp
    );

__drv_maxIRQL(PASSIVE_LEVEL)
KSDDKAPI
NTSTATUS
NTAPI
KsGetBusEnumPnpDeviceObject(
    __in PDEVICE_OBJECT DeviceObject,
    __out PDEVICE_OBJECT *PnpDeviceObject
    );

__drv_maxIRQL(PASSIVE_LEVEL)
KSDDKAPI
NTSTATUS
NTAPI
KsInstallBusEnumInterface(
    __in PIRP Irp
    );

__drv_maxIRQL(PASSIVE_LEVEL)
KSDDKAPI
NTSTATUS
NTAPI
KsIsBusEnumChildDevice(
    __in PDEVICE_OBJECT DeviceObject,
    __out PBOOLEAN ChildDevice
    );
    
__drv_maxIRQL(PASSIVE_LEVEL)
KSDDKAPI
NTSTATUS
NTAPI
KsRemoveBusEnumInterface(
    __in PIRP Irp
    );
    
__drv_maxIRQL(PASSIVE_LEVEL)
KSDDKAPI
NTSTATUS
NTAPI
KsServiceBusEnumPnpRequest(
    __in PDEVICE_OBJECT DeviceObject,
    __inout PIRP Irp
    );
    
__drv_maxIRQL(PASSIVE_LEVEL)
KSDDKAPI
NTSTATUS
NTAPI
KsServiceBusEnumCreateRequest(
    __in PDEVICE_OBJECT DeviceObject,
    __inout PIRP Irp
    );
    
__drv_maxIRQL(PASSIVE_LEVEL)
KSDDKAPI
NTSTATUS
NTAPI
KsGetBusEnumParentFDOFromChildPDO(
    __in PDEVICE_OBJECT DeviceObject,
    __out PDEVICE_OBJECT *FunctionalDeviceObject
    );

#endif // _KS_

#if defined(__cplusplus)
}
#endif // defined(__cplusplus)

#endif // _NTDDK_

#endif // !_SWENUM_

