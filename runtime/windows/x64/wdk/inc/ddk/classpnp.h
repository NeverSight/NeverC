/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    classpnp.h

Abstract:

    These are the structures and defines that are used in the
    SCSI class drivers.

--*/

#ifndef _CLASS_
#define _CLASS_

#include <ntdddisk.h>
#include <ntddcdrm.h>
#include <ntddtape.h>
#include <ntddscsi.h>
#include "ntddstor.h"

#include <stdio.h>

#include <scsi.h>

#if defined DebugPrint
    #undef DebugPrint
#endif

#ifdef TRY
    #undef TRY
#endif
#ifdef LEAVE
    #undef LEAVE
#endif
#ifdef FINALLY
    #undef FINALLY
#endif

#define TRY
#define LEAVE   goto __tryLabel;
#define FINALLY __tryLabel:

// #define ALLOCATE_SRB_FROM_POOL

//
// describes the well-known bit masks for ClassDebug, and describes the bits
// to enable in the debugger to view just those messages.  ClassDebugExternalX
// are reserved for third-party components' debugging use.  Anything above
// 16 will only be printed if the lower two bytes of ClassDebug are higher
// than the given level (no masking will be available).
//

typedef enum _CLASS_DEBUG_LEVEL {
    ClassDebugError        = 0,  // always printed
    ClassDebugWarning      = 1,  // set bit 0x00010000 in ClassDebug
    ClassDebugTrace        = 2,  // set bit 0x00020000 in ClassDebug
    ClassDebugInfo         = 3,  // set bit 0x00040000 in ClassDebug
#if 0
    ClassDebug Internal    = 4,  // set bit 0x00080000 in ClassDebug

    ClassDebug Internal    = 5,  // set bit 0x00100000 in ClassDebug
    ClassDebug Internal    = 6,  // set bit 0x00200000 in ClassDebug
    ClassDebug Internal    = 7,  // set bit 0x00400000 in ClassDebug
#endif // 0
    ClassDebugMediaLocks   = 8,  // set bit 0x00800000 in ClassDebug

    ClassDebugMCN          = 9,  // set bit 0x01000000 in ClassDebug
    ClassDebugDelayedRetry = 10, // set bit 0x02000000 in ClassDebug
    ClassDebugSenseInfo    = 11, // set bit 0x04000000 in ClassDebug
    ClassDebugRemoveLock   = 12, // set bit 0x08000000 in ClassDebug

    ClassDebugExternal4    = 13, // set bit 0x10000000 in ClassDebug
    ClassDebugExternal3    = 14, // set bit 0x20000000 in ClassDebug
    ClassDebugExternal2    = 15, // set bit 0x40000000 in ClassDebug
    ClassDebugExternal1    = 16  // set bit 0x80000000 in ClassDebug
} CLASS_DEBUG_LEVEL, *PCLASS_DEBUG_LEVEL;

#if DBG

#define DebugPrint(x) ClassDebugPrint x

#else

#define DebugPrint(x)

#endif // DBG

#define DEBUG_BUFFER_LENGTH 256

//
// Define our private SRB flags.  The high nibble of the flag field is
// reserved for class drivers's private use.
//

//
// Used to indicate that this request shouldn't invoke any power type operations
// like spinning up the drive.
//

#define SRB_CLASS_FLAGS_LOW_PRIORITY      0x10000000

//
// Used to indicate that the completion routine should not free the srb.
//

#define SRB_CLASS_FLAGS_PERSISTANT        0x20000000

//
// Used to indicate that an SRB is the result of a paging operation.
//

#define SRB_CLASS_FLAGS_PAGING            0x40000000

//
// Used to indicate the completion routine should free the MDL.
//

#define SRB_CLASS_FLAGS_FREE_MDL          0x80000000

//
// Random macros which should probably be in the system header files
// somewhere.
//

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

//
// Bit Flag Macros
//

#define SET_FLAG(Flags, Bit)    ((Flags) |= (Bit))
#define CLEAR_FLAG(Flags, Bit)  ((Flags) &= ~(Bit))
#define TEST_FLAG(Flags, Bit)   (((Flags) & (Bit)) != 0)

//
// neat little hacks to count number of bits set efficiently
//
__inline ULONG CountOfSetBitsUChar(UCHAR _X)
{ ULONG i = 0; while (_X) { _X &= _X - 1; i++; } return i; }
__inline ULONG CountOfSetBitsULong(ULONG _X)
{ ULONG i = 0; while (_X) { _X &= _X - 1; i++; } return i; }
__inline ULONG CountOfSetBitsULong32(ULONG32 _X)
{ ULONG i = 0; while (_X) { _X &= _X - 1; i++; } return i; }
__inline ULONG CountOfSetBitsULong64(ULONG64 _X)
{ ULONG i = 0; while (_X) { _X &= _X - 1; i++; } return i; }
__inline ULONG CountOfSetBitsUlongPtr(ULONG_PTR _X)
{ ULONG i = 0; while (_X) { _X &= _X - 1; i++; } return i; }


//
// Helper macros to verify data types and cleanup the code.
//

#define ASSERT_FDO(x) \
    ASSERT(((PCOMMON_DEVICE_EXTENSION) (x)->DeviceExtension)->IsFdo)

#define ASSERT_PDO(x) \
    ASSERT(!(((PCOMMON_DEVICE_EXTENSION) (x)->DeviceExtension)->IsFdo))

#define IS_CLEANUP_REQUEST(majorFunction)   \
        ((majorFunction == IRP_MJ_CLOSE) ||     \
         (majorFunction == IRP_MJ_CLEANUP) ||   \
         (majorFunction == IRP_MJ_SHUTDOWN))

#define DO_MCD(fdoExtension)                                        \
    (((fdoExtension)->MediaChangeDetectionInfo != NULL) &&          \
     ((fdoExtension)->MediaChangeDetectionInfo->MediaChangeDetectionDisableCount == 0))

#define IS_SCSIOP_READ(opCode)         \
      ((opCode == SCSIOP_READ6)   ||   \
       (opCode == SCSIOP_READ)    ||   \
       (opCode == SCSIOP_READ12)  ||   \
       (opCode == SCSIOP_READ16))

#define IS_SCSIOP_WRITE(opCode)         \
      ((opCode == SCSIOP_WRITE6)   ||   \
       (opCode == SCSIOP_WRITE)    ||   \
       (opCode == SCSIOP_WRITE12)  ||   \
       (opCode == SCSIOP_WRITE16))

#define IS_SCSIOP_READWRITE(opCode)  (IS_SCSIOP_READ(opCode) || IS_SCSIOP_WRITE(opCode))

#define ADJUST_FUA_FLAG(fdoExt) {                                                       \
    if (TEST_FLAG(fdoExt->DeviceFlags, DEV_WRITE_CACHE) &&                              \
        !TEST_FLAG(fdoExt->DeviceFlags, DEV_POWER_PROTECTED) &&                         \
        !TEST_FLAG(fdoExt->ScanForSpecialFlags, CLASS_SPECIAL_FUA_NOT_SUPPORTED) ) {    \
        fdoExt->CdbForceUnitAccess = TRUE;                                              \
    } else {                                                                            \
        fdoExt->CdbForceUnitAccess = FALSE;                                             \
    }                                                                                   \
}

#define FREE_POOL(_PoolPtr)     \
    if (_PoolPtr != NULL) {     \
        ExFreePool(_PoolPtr);   \
        _PoolPtr = NULL;        \
    }

#ifdef POOL_TAGGING
#undef ExAllocatePool
#undef ExAllocatePoolWithQuota
#define ExAllocatePool(a,b) ExAllocatePoolWithTag(a,b,'nUcS')
//#define ExAllocatePool(a,b) #assert(0)
#define ExAllocatePoolWithQuota(a,b) ExAllocatePoolWithQuotaTag(a,b,'nUcS')
#endif

#define CLASS_TAG_AUTORUN_DISABLE           'ALcS'
#define CLASS_TAG_FILE_OBJECT_EXTENSION     'FLcS'
#define CLASS_TAG_MEDIA_CHANGE_DETECTION    'MLcS'
#define CLASS_TAG_MOUNT                     'mLcS'
#define CLASS_TAG_RELEASE_QUEUE             'qLcS'
#define CLASS_TAG_POWER                     'WLcS'
#define CLASS_TAG_WMI                       'wLcS'
#define CLASS_TAG_FAILURE_PREDICT           'fLcS'
#define CLASS_TAG_DEVICE_CONTROL            'OIcS'
#define CLASS_TAG_MODE_DATA                 'oLcS'
#define CLASS_TAG_MULTIPATH                 'mPcS'

#define MAXIMUM_RETRIES 4

#define CLASS_DRIVER_EXTENSION_KEY ((PVOID) ClassInitialize)

struct _CLASS_INIT_DATA;
typedef struct _CLASS_INIT_DATA
                CLASS_INIT_DATA,
                *PCLASS_INIT_DATA;

//
// our first attempt at keeping private data actually private....
//

struct _CLASS_PRIVATE_FDO_DATA;
typedef struct _CLASS_PRIVATE_FDO_DATA
                CLASS_PRIVATE_FDO_DATA,
              *PCLASS_PRIVATE_FDO_DATA;

struct _CLASS_PRIVATE_PDO_DATA;
typedef struct _CLASS_PRIVATE_PDO_DATA
                CLASS_PRIVATE_PDO_DATA,
              *PCLASS_PRIVATE_PDO_DATA;

struct _CLASS_PRIVATE_COMMON_DATA;
typedef struct _CLASS_PRIVATE_COMMON_DATA
                CLASS_PRIVATE_COMMON_DATA,
              *PCLASS_PRIVATE_COMMON_DATA;

//
// Possible values for the IsRemoved flag
//

#define NO_REMOVE 0
#define REMOVE_PENDING 1
#define REMOVE_COMPLETE 2


#define ClassAcquireRemoveLock(devobj, tag) \
    ClassAcquireRemoveLockEx(devobj, tag, __FILE__, __LINE__)

//
// Define start unit timeout to be 4 minutes.
//

#define START_UNIT_TIMEOUT  (60 * 4)

//
// Define media change test time to be 1 second for quicker response

#define MEDIA_CHANGE_DEFAULT_TIME    1

//
// Used to detect the loss of the autorun irp.
//

#define MEDIA_CHANGE_TIMEOUT_TIME  300

//
// Define the various states that media can be in for autorun.
//
typedef enum _MEDIA_CHANGE_DETECTION_STATE {
    MediaUnknown,
    MediaPresent,
    MediaNotPresent,
    MediaUnavailable   // e.g. cd-r media undergoing burn
} MEDIA_CHANGE_DETECTION_STATE, *PMEDIA_CHANGE_DETECTION_STATE;


struct _MEDIA_CHANGE_DETECTION_INFO;
typedef struct _MEDIA_CHANGE_DETECTION_INFO
    MEDIA_CHANGE_DETECTION_INFO, *PMEDIA_CHANGE_DETECTION_INFO;

//
// Structures for maintaining a dictionary list (list of objects
// referenced by a key value)
//

struct _DICTIONARY_HEADER;
typedef struct _DICTIONARY_HEADER DICTIONARY_HEADER, *PDICTIONARY_HEADER;

typedef struct _DICTIONARY {
    ULONGLONG Signature;
    PDICTIONARY_HEADER List;
    KSPIN_LOCK SpinLock;
} DICTIONARY, *PDICTIONARY;


//
// structures to simplify matching devices, ids, and hacks required for
// these ids.
//

typedef struct _CLASSPNP_SCAN_FOR_SPECIAL_INFO {

    //
    // * NULL pointers indicates that no match is required.
    // * empty string will only match an empty string.  non-existant strings
    //   in the device descriptor are considered empty strings for this match.
    //   (ie. "" will only match "")
    // * all other strings will do partial matches, based upon
    //   string provided (ie. "hi" will match "hitazen" and "higazui")
    // * array must end with all three PCHARs being set to NULL.
    //

    PCHAR      VendorId;
    PCHAR      ProductId;
    PCHAR      ProductRevision;

    //
    // marked as a ULONG_PTR to allow use as either a ptr to a data block
    // or 32 bits worth of flags. (64 bits on 64 bit systems)  no longer a
    // const so that it may be dynamically built.
    //

    ULONG_PTR  Data;

} CLASSPNP_SCAN_FOR_SPECIAL_INFO, *PCLASSPNP_SCAN_FOR_SPECIAL_INFO;




#ifdef ALLOCATE_SRB_FROM_POOL

#define ClasspAllocateSrb(ext)
    ExAllocatePoolWithTag(NonPagedPool,                 \
                          sizeof(SCSI_REQUEST_BLOCK),   \
                          'sBRS')

#define ClasspFreeSrb(ext, srb)     ExFreePool((srb));

#else

#define ClasspAllocateSrb(ext)                      \
    ExAllocateFromNPagedLookasideList(              \
        &((ext)->CommonExtension.SrbLookasideList))

#define ClasspFreeSrb(ext, srb)                     \
    ExFreeToNPagedLookasideList(                    \
        &((ext)->CommonExtension.SrbLookasideList), \
        (srb))

#endif


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_ERROR()

Routine Description:

    This routine is a callback into the driver to handle errors.  The queue
    shall not be unfrozen when this error handler is called, even though the
    SRB flags may mark the queue as having been frozen due to this SRB.

Irql:

    This routine will be called at KIRQL <= DISPATCH_LEVEL

Arguments:

    DeviceObject is the device object the error occurred on.

    Srb is the Srb that was being processed when the error occurred.

    Status may be overwritten by the routine if it decides that the error
        was benign, or otherwise wishes to change the returned status code
        for this command

    Retry may be overwritten to specify that this command should or should
        not be retried (if the callee supports retrying commands)

Return Value:

    status

--*/

__drv_maxIRQL(DISPATCH_LEVEL)
typedef
VOID
(*PCLASS_ERROR) (
    __in PDEVICE_OBJECT DeviceObject,
    __in PSCSI_REQUEST_BLOCK Srb,
    __out NTSTATUS *Status,
    __inout BOOLEAN *Retry
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_ADD_DEVICE()

Routine Description:

    This routine is a callback into the driver to create and initialize a new
    FDO for the corresponding PDO.  It may perform property queries on the PDO
    but cannot do any media access operations.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DriverObject is the class driver object this callback is registered for.

    PDO is the physical device object being added to.

Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_ADD_DEVICE) (
    __in PDRIVER_OBJECT DriverObject,
    __in PDEVICE_OBJECT Pdo
    );


/*++////////////////////////////////////////////////////////////////////////////

CLASS_POWER_DEVICE()

Routine Description:

    This routine is a callback into the driver to handle power up and
    power down requests.  Most drivers can set this to ClassPowerHandler,
    which will send a STOP_UNIT on powerdown, and a START_UNIT on powerup.
    ClassMinimalPowerHandler() may also be used to do nothing for power
    operations (except succeed them).  Please see the DDK for proper handling
    of IRP_MN_DEVICE_USAGE_NOTIFICATION for details regarding interaction
    of paging device notifications and the IRQL at which this routine will
    be called.

Irql:

    This routine will be called at PASSIVE_LEVEL if DO_POWER_PAGABLE is set.
    This code should NOT be pagable to prevent race conditions during the
    setting and clearing of the DO_POWER_PAGABLE bit.

Arguments:

    DeviceObject is the device that has the pending power request

    Irp is the power irp that needs to be handled

Return Value:

    status

--*/
typedef
NTSTATUS
(*PCLASS_POWER_DEVICE) (
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp
    );

/*++////////////////////////////////////////////////////////////////////////////

CLASS_START_DEVICE()

Routine Description:

    This routine is a callback into the driver to initialize the FDO or PDO for
    all requests, typically due to a IRP_MN_START_DEVICE.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject is the device object being started

Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_START_DEVICE) (
    __in PDEVICE_OBJECT DeviceObject
    );


/*++////////////////////////////////////////////////////////////////////////////

CLASS_STOP_DEVICE()

Routine Description:

    This routine is a callback into the driver to stop the device.
    For the storage stack, unless there are known issues, this routine
    need only return.  All queueing shall be handled by the lower device
    drivers.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject is the device object being stopped/query stopped.

    Type is the IRP_MN_ type that must be handled.

Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_STOP_DEVICE) (
    __in PDEVICE_OBJECT DeviceObject,
    __in UCHAR Type
    );


/*++////////////////////////////////////////////////////////////////////////////

CLASS_INIT_DEVICE()

Routine Description:

    This routine is a callback into the driver to do one-time initialization
    of new device objects.  It shall be called exactly once per device object,
    and it shall be called prior to CLASS_START_DEVICE() routine.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject is the device object to be initialized

Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_INIT_DEVICE) (
    __in PDEVICE_OBJECT DeviceObject
    );


/*++////////////////////////////////////////////////////////////////////////////

CLASS_ENUM_DEVICE()

Routine Description:

    This routine is a callback into the driver to update the list of PDOs for
    a given FDO.  See DISK.SYS's DiskEnumerateDevice for an example of use.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject is the FDO which is being enumerated.

Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_ENUM_DEVICE) (
    __in PDEVICE_OBJECT DeviceObject
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_READ_WRITE()

Routine Description:

    This routine is a callback into the driver to verify READ and WRITE irps.
    If the READ or WRITE request is failed, this routine shall set the Irp's
    IoStatus.Status to the returned error code and the IoStatus.Information
    field as appropriate for the given error.

Irql:

    This routine will be called at KIRQL <= DISPATCH_LEVEL

Arguments:

    DeviceObject is the device object being read from or written to

    Irp is the read or write request being processed

Return Value:

    status

--*/
__drv_maxIRQL(DISPATCH_LEVEL)
typedef
NTSTATUS
(*PCLASS_READ_WRITE) (
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_DEVICE_CONTROL()

Routine Description:

    This routine is a callback into the driver to

Irql:

    This routine will only be called at PASSIVE_LEVEL for storage IOCTLs.
    The code must therefore not be paged, but may call paged code for those
    ioctls which have been defined to be sent at PASSIVE_LEVEL, such as the
    storage IOCTLS.  Otherwise KIRQL <= DISPATCH_LEVEL.

Arguments:

    DeviceObject is the device object the IOCTL may be for

    Irp is the IOCTL request currently being processed

Return Value:

    status

--*/
__drv_maxIRQL(DISPATCH_LEVEL)
typedef
NTSTATUS
(*PCLASS_DEVICE_CONTROL) (
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_SHUTDOWN_FLUSH()

Routine Description:

    This routine is a callback into the driver to handle shutdown and flush
    irps.  These are sent by the system before it actually shuts down or when
    the file system does a flush.

    This routine may synchronize the device's media / cache and ensure the
    device is not locked if the system is in the process of shutting down.

Irql:

    This routine will be called at KIRQL <= DISPATCH_LEVEL

Arguments:

    DeviceObject is the device object that needs to be flushed

    Irp is the shutdown or flush request currently being processed

Return Value:

    status

--*/
__drv_maxIRQL(DISPATCH_LEVEL)
typedef
NTSTATUS
(*PCLASS_SHUTDOWN_FLUSH) (
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_CREATE_CLOSE()

Routine Description:

    This routine is a callback into the driver when the device is opened or
    closed.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject that is handling the request

    Irp is the create or close request currently being processed

Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_CREATE_CLOSE) (
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_QUERY_ID()

Routine Description:

    This routine generates the PNP id's for the device's enumerated PDOs.
    If the specified ID is one that cannot be generated, then the return
    status shall be STATUS_NOT_IMPLEMENTED so that classpnp shall not
    handle the request.  This routine shall allocate the buffer in the unicode
    string "IdString" upon success; it is the caller's responsibility to free
    this buffer when it is done.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject is the PDO to generate an ID for

    IdType is the type of ID to be generated

    UnicodeIdString is the string to place the results into

Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_QUERY_ID) (
    __in PDEVICE_OBJECT DeviceObject,
    __in BUS_QUERY_ID_TYPE IdType,
    __in PUNICODE_STRING IdString
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_REMOVE_DEVICE()

Routine Description:

    This routine is a callback into the driver to release any resources the
    device may have allocated for the device object.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject is the device object being removed/query removed/etc.

Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_REMOVE_DEVICE) (
    __in PDEVICE_OBJECT DeviceObject,
    __in UCHAR Type
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_UNLOAD()

Routine Description:

    This routine is a callback into the driver to unload itself.  It must free
    any resources allocated in the DriverEntry portion of the driver.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    X

    Irp is the IOCTL request currently being processed

Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
VOID
(*PCLASS_UNLOAD) (
    __in PDRIVER_OBJECT DriverObject
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_QUERY_PNP_CAPABILITIES()

Routine Description:

    ISSUE-2000/02/18-henrygab - description required

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    PhysicalDeviceObject is the PDO for which this query shall occur

    Capabilities is a structure that shall be modified by this routine
        to report the device's capabilities.

Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_QUERY_PNP_CAPABILITIES) (
    __in PDEVICE_OBJECT PhysicalDeviceObject,
    __in PDEVICE_CAPABILITIES Capabilities
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_TICK()

Routine Description:

    This routine is a callback into the driver that is called once per second.

Irql:

    This routine will be called at DISPATCH_LEVEL

Arguments:

    DeviceObject is the device object for which the timer has fired

Return Value:

    status

--*/
__drv_requiresIRQL(DISPATCH_LEVEL)
typedef
VOID
(*PCLASS_TICK) (
    __in PDEVICE_OBJECT DeviceObject
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_QUERY_WMI_REGINFO_EX()

Routine Description:

    This routine is a callback into the driver to retrieve information about
    the guids being registered.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject is the device whose registration information is needed

    *RegFlags returns with a set of flags that describe the guids being
        registered for this device. If the device wants enable and disable
        collection callbacks before receiving queries for the registered
        guids then it should return the WMIREG_FLAG_EXPENSIVE flag. Also the
        returned flags may specify WMIREG_FLAG_INSTANCE_PDO in which case
        the instance name is determined from the PDO associated with the
        device object. Note that the PDO must have an associated devnode. If
        WMIREG_FLAG_INSTANCE_PDO is not set then Name must return a unique
        name for the device.

    Name returns with the instance name for the guids if
        WMIREG_FLAG_INSTANCE_PDO is not set in the returned *RegFlags. The
        caller will call ExFreePool with the buffer returned.

    MofResourceName returns filled with a static string that contains
        the name of the MOF resource attached to the drivers image. The
        caller does not free the buffer as it is expected that the
        caller will use RtlInitializeUnicodeString to populate it.

Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_QUERY_WMI_REGINFO_EX) (
    __in PDEVICE_OBJECT DeviceObject,
    __out ULONG *RegFlags,
    __out PUNICODE_STRING Name,
    __out PUNICODE_STRING MofResouceName
    );

/*++////////////////////////////////////////////////////////////////////////////

PCLASS_QUERY_WMI_REGINFO()

Routine Description:

    This routine is a callback into the driver to retrieve information about
    the guids being registered.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject is the device whose registration information is needed

    *RegFlags returns with a set of flags that describe the guids being
        registered for this device. If the device wants enable and disable
        collection callbacks before receiving queries for the registered
        guids then it should return the WMIREG_FLAG_EXPENSIVE flag. Also the
        returned flags may specify WMIREG_FLAG_INSTANCE_PDO in which case
        the instance name is determined from the PDO associated with the
        device object. Note that the PDO must have an associated devnode. If
        WMIREG_FLAG_INSTANCE_PDO is not set then Name must return a unique
        name for the device.

    Name returns with the instance name for the guids if
        WMIREG_FLAG_INSTANCE_PDO is not set in the returned *RegFlags. The
        caller will call ExFreePool with the buffer returned.


Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_QUERY_WMI_REGINFO) (
    __in PDEVICE_OBJECT DeviceObject,
    __out ULONG *RegFlags,
    __out PUNICODE_STRING Name
    );

/*++////////////////////////////////////////////////////////////////////////////

PCLASS_QUERY_WMI_DATABLOCK()

Routine Description:

    This routine is a callback into the driver to query for the contents of
    a data block. When the driver has finished filling the data block it
    must call ClassWmiCompleteRequest to complete the irp. The driver can
    return STATUS_PENDING if the irp cannot be completed immediately.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject is the device whose data block is being queried

    Irp is the Irp that makes this request

    GuidIndex is the index into the list of guids provided when the
        device registered

    BufferAvail on has the maximum size available to write the data
        block.

    Buffer on return is filled with the returned data block


Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_QUERY_WMI_DATABLOCK) (
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp,
    __in ULONG GuidIndex,
    __in ULONG BufferAvail,
    __out_bcount(BufferAvail) PUCHAR Buffer
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_SET_WMI_DATABLOCK()

Routine Description:

    This routine is a callback into the driver to query for the contents of
    a data block. When the driver has finished filling the data block it
    must call ClassWmiCompleteRequest to complete the irp. The driver can
    return STATUS_PENDING if the irp cannot be completed immediately.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject is the device whose data block is being queried

    Irp is the Irp that makes this request

    GuidIndex is the index into the list of guids provided when the
        device registered

    BufferSize has the size of the data block passed

    Buffer has the new values for the data block


Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_SET_WMI_DATABLOCK) (
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp,
    __in ULONG GuidIndex,
    __in ULONG BufferSize,
    __in_bcount(BufferSize) PUCHAR Buffer
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_SET_WMI_DATAITEM()

Routine Description:

    This routine is a callback into the driver to query for the contents of
    a data block. When the driver has finished filling the data block it
    must call ClassWmiCompleteRequest to complete the irp. The driver can
    return STATUS_PENDING if the irp cannot be completed immediately.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject is the device whose data block is being queried

    Irp is the Irp that makes this request

    GuidIndex is the index into the list of guids provided when the
        device registered

    DataItemId has the id of the data item being set

    BufferSize has the size of the data item passed

    Buffer has the new values for the data item


Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_SET_WMI_DATAITEM) (
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp,
    __in ULONG GuidIndex,
    __in ULONG DataItemId,
    __in ULONG BufferSize,
    __in_bcount(BufferSize) PUCHAR Buffer
    );


/*++////////////////////////////////////////////////////////////////////////////

PCLASS_EXECUTE_WMI_METHOD()

Routine Description:

    This routine is a callback into the driver to execute a method. When the
    driver has finished filling the data block it must call
    ClassWmiCompleteRequest to complete the irp. The driver can
    return STATUS_PENDING if the irp cannot be completed immediately.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject is the device whose data block is being queried

    Irp is the Irp that makes this request

    GuidIndex is the index into the list of guids provided when the
        device registered

    MethodId has the id of the method being called

    InBufferSize has the size of the data block passed in as the input to
        the method.

    OutBufferSize on entry has the maximum size available to write the
        returned data block.

    Buffer is filled with the returned data block


Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_EXECUTE_WMI_METHOD) (
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp,
    __in ULONG GuidIndex,
    __in ULONG MethodId,
    __in ULONG InBufferSize,
    __in ULONG OutBufferSize,
    __in_xcount(max(InBufferSize, OutBufferSize)) PUCHAR Buffer
    );



//
// used by PCLASS_WMI_FUNCTION_CONTROL
//
typedef enum {
    EventGeneration,
    DataBlockCollection
} CLASSENABLEDISABLEFUNCTION;

/*++////////////////////////////////////////////////////////////////////////////

PCLASS_WMI_FUNCTION_CONTROL()

Routine Description:

    This routine is a callback into the driver to enabled or disable event
    generation or data block collection. A device should only expect a
    single enable when the first event or data consumer enables events or
    data collection and a single disable when the last event or data
    consumer disables events or data collection. Data blocks will only
    receive collection enable/disable if they were registered as requiring
    it.

Irql:

    This routine will be called at PASSIVE_LEVEL.
    Its code may be safely paged.

Arguments:

    DeviceObject is the device whose data block is being queried

    GuidIndex is the index into the list of guids provided when the
        device registered

    Function specifies which functionality is being enabled or disabled

    Enable is TRUE then the function is being enabled else disabled

Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
NTSTATUS
(*PCLASS_WMI_FUNCTION_CONTROL) (
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp,
    __in ULONG GuidIndex,
    __in CLASSENABLEDISABLEFUNCTION Function,
    __in BOOLEAN Enable
    );

/*++////////////////////////////////////////////////////////////////////////////

    This structure defines the history kept for a given transfer packet.
    It includes a srb status/sense data structure that is always either valid
    or zero-filled for the full 18 bytes, time sent/completed, and how long
    the retry delay was requested to be.
    
--*/
typedef struct _SRB_HISTORY_ITEM {
    LARGE_INTEGER TickCountSent;             //  0x00..0x07
    LARGE_INTEGER TickCountCompleted;        //  0x08..0x0F
    ULONG         MillisecondsDelayOnRetry;  //  0x10..0x13
    SENSE_DATA    NormalizedSenseData;       //  0x14..0x25 (0x12 bytes)
    UCHAR         SrbStatus;                 //  0x26
    UCHAR         ClassDriverUse;            //  0x27 -- one byte free (alignment)
} SRB_HISTORY_ITEM, *PSRB_HISTORY_ITEM;

typedef struct _SRB_HISTORY {
    ULONG_PTR        ClassDriverUse[4]; // for the class driver to use as they please
    __field_range(1,30000)
    ULONG            TotalHistoryCount;
    __field_range(0,TotalHistoryCount)
    ULONG            UsedHistoryCount;
    __field_ecount_part(TotalHistoryCount, UsedHistoryCount)
    SRB_HISTORY_ITEM History[1];
} SRB_HISTORY, *PSRB_HISTORY;

/*++////////////////////////////////////////////////////////////////////////////

PCLASS_INTERPRET_SENSE_INFO()

Routine Description:

    This routine is a callback into the driver to perform interpretation of the
    errors that may have occurred during an SRB transfer.  It closely matches the
    API set of ClassInterpretSenseInfo, with modifications to allow for a history
    of why the request was previously retried (and when), and changes the retry
    interval from being in seconds to being in milliseconds.  Finally, use of this
    extended API removes all retry logic from classpnp for these requests.  Thus,
    the provided routine must return FALSE when it determines the number of times
    the request should be retried has been exceeded.

Irql:

    This routine will be called at KIRQL <= DISPATCH_LEVEL

NOTE:

    Although it is not illegal to have both a PCLASS_INTERPRET_SENSE_INFO() and
    a PCLASS_ERROR() routine, the PCLASS_ERROR() function will only be called
    if the class driver (as part of its PCLASS_INTERPRET_SENSE_INFO() routine)
    calls into the legacy ClassInterpretSenseInfo().

Arguments:


Return Value:

    TRUE if the request should be retried
    FALSE if the request should be failed

--*/

// NOTE: Start with a smaller 100 second maximum, due to current assert in CLASSPNP
//       0x0000 00C9'2A69 C000 (864,000,000,000) is 24 hours in 100ns units
//       0x0000 0000'3B9A CA00 (  1,000,000,000) is 100 seconds in 100ns units
#define MAXIMUM_RETRY_FOR_SINGLE_IO_IN_100NS_UNITS (0x3B9ACA00)

__drv_maxIRQL(DISPATCH_LEVEL)
typedef
BOOLEAN
(*PCLASS_INTERPRET_SENSE_INFO) (
    __in      PDEVICE_OBJECT      Fdo,
    __in_opt  PIRP                OriginalRequest, // not always the same as in SRB
    __in      PSCSI_REQUEST_BLOCK Srb,
    __in      UCHAR               MajorFunctionCode,
    __in      ULONG               IoDeviceCode,
    __in      ULONG               PreviousRetryCount,
    // const except for bits explicitly set aside for class driver to update
    __in_opt  SRB_HISTORY *       RequestHistory,
    __out     NTSTATUS    *       Status,
    __out __range(0,MAXIMUM_RETRY_FOR_SINGLE_IO_IN_100NS_UNITS)
              LONGLONG * RetryIn100nsUnits
    );

/*++////////////////////////////////////////////////////////////////////////////

PCLASS_COMPRESS_HISTORY_DATA()

Routine Description:

    This routine is a callback into the driver to perform "compression" of the
    history data that is saved during retry of SRBs.  The function will only be
    called when the history array is full (UsedHistoryCount == TotalHistoryCount).
    This function must reduce the overall UsedHistoryCount by at least one
    element (and update the UsedHistoryCount field appropriately).
    .  that may have occurred during an SRB transfer.  It closely matches the
    API set of ClassInterpretSenseInfo, with modifications to allow for a history
    of why the request was previously retried (and when), and changes the retry
    interval from being in seconds to being in milliseconds.  Finally, use of this
    extended API removes all retry logic from classpnp for these requests.  Thus,
    the provided routine must return FALSE when it determines the number of times
    the request should be retried has been exceeded.

Irql:

    This routine will be called at KIRQL <= DISPATCH_LEVEL

NOTE:

    Although it is not illegal to have both a PCLASS_INTERPRET_SENSE_INFO() and
    a PCLASS_ERROR() routine, the PCLASS_ERROR() function will only be called
    if the class driver (as part of its PCLASS_INTERPRET_SENSE_INFO() routine)
    calls into the legacy ClassInterpretSenseInfo().

Arguments:


--*/
__drv_maxIRQL(DISPATCH_LEVEL)
typedef
VOID
(*PCLASS_COMPRESS_RETRY_HISTORY_DATA) (
    __in     PDEVICE_OBJECT DeviceObject,
    __inout  PSRB_HISTORY   RequestHistory
    );

//
// Restricted - May only append to this structure for backwards compatibility
//
typedef struct {
    GUID Guid;               // Guid to registered
    ULONG InstanceCount;     // Count of Instances of Datablock
    ULONG Flags;             // Additional flags (see WMIREGINFO in wmistr.h)
} GUIDREGINFO, *PGUIDREGINFO;


//
// Restricted - May only append to this structure for backwards compatibility
//
typedef struct _CLASS_WMI_INFO {
    ULONG GuidCount;
    PGUIDREGINFO GuidRegInfo;

    PCLASS_QUERY_WMI_REGINFO      ClassQueryWmiRegInfo;
    PCLASS_QUERY_WMI_DATABLOCK    ClassQueryWmiDataBlock;
    PCLASS_SET_WMI_DATABLOCK      ClassSetWmiDataBlock;
    PCLASS_SET_WMI_DATAITEM       ClassSetWmiDataItem;
    PCLASS_EXECUTE_WMI_METHOD     ClassExecuteWmiMethod;
    PCLASS_WMI_FUNCTION_CONTROL   ClassWmiFunctionControl;
} CLASS_WMI_INFO, *PCLASS_WMI_INFO;


//
// Restricted - May only append to this structure for backwards compatibility
//
typedef struct _CLASS_DEV_INFO {

    //
    // Bytes needed by the class driver
    // for it's extension.
    // If this is zero, the driver does not expect to have any PDO's
    //

    ULONG DeviceExtensionSize;

    DEVICE_TYPE DeviceType;

    UCHAR StackSize;

    //
    // Device Characteristics flags
    //  eg.:
    //
    //  FILE_REMOVABLE_MEDIA
    //  FILE_READ_ONLY_DEVICE
    //  FILE_FLOPPY_DISKETTE
    //  FILE_WRITE_ONCE_MEDIA
    //  FILE_REMOTE_DEVICE
    //  FILE_DEVICE_IS_MOUNTED
    //  FILE_VIRTUAL_VOLUME
    //

    ULONG DeviceCharacteristics;

    PCLASS_ERROR                    ClassError;
    PCLASS_READ_WRITE               ClassReadWriteVerification;
    PCLASS_DEVICE_CONTROL           ClassDeviceControl;
    PCLASS_SHUTDOWN_FLUSH           ClassShutdownFlush;
    PCLASS_CREATE_CLOSE             ClassCreateClose;

    PCLASS_INIT_DEVICE              ClassInitDevice;
    PCLASS_START_DEVICE             ClassStartDevice;
    PCLASS_POWER_DEVICE             ClassPowerDevice;
    PCLASS_STOP_DEVICE              ClassStopDevice;
    PCLASS_REMOVE_DEVICE            ClassRemoveDevice;

    PCLASS_QUERY_PNP_CAPABILITIES   ClassQueryPnpCapabilities;

    //
    // Registered Data Block info for wmi
    //
    CLASS_WMI_INFO                  ClassWmiInfo;

} CLASS_DEV_INFO, *PCLASS_DEV_INFO;

//
// Restricted - May only append to this structure for backwards compatibility
//
struct _CLASS_INIT_DATA {

    //
    // This structure size - version checking.
    //

    ULONG InitializationDataSize;

    //
    // Specific init data for functional and physical device objects.
    //

    CLASS_DEV_INFO FdoData;
    CLASS_DEV_INFO PdoData;

    //
    // Device-specific driver routines
    //

    PCLASS_ADD_DEVICE             ClassAddDevice;
    PCLASS_ENUM_DEVICE            ClassEnumerateDevice;

    PCLASS_QUERY_ID               ClassQueryId;

    PDRIVER_STARTIO               ClassStartIo;
    PCLASS_UNLOAD                 ClassUnload;

    PCLASS_TICK                   ClassTick;
};

//
// this is a private structure, but must be kept here
// to properly compile size of FUNCTIONAL_DEVICE_EXTENSION
//
typedef struct _FILE_OBJECT_EXTENSION {
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    ULONG LockCount;
    ULONG McnDisableCount;
} FILE_OBJECT_EXTENSION, *PFILE_OBJECT_EXTENSION;

typedef struct _CLASS_WORKING_SET
{
    __field_range(sizeof(CLASS_WORKING_SET),sizeof(CLASS_WORKING_SET))
    ULONG Size;          // Must be sizeof(CLASS_WORKING_SET)

    __field_range(0,2048) // NOTE: This range can be made larger more easily than it can be reduced
    ULONG XferPacketsWorkingSetMaximum;
    __field_range(0,2048)
    ULONG XferPacketsWorkingSetMinimum;
} CLASS_WORKING_SET, *PCLASS_WORKING_SET;
#define CLASS_WORKING_SET_MAXIMUM 2048

typedef struct _CLASS_INTERPRET_SENSE_INFO2
{
    __field_range(sizeof(CLASS_INTERPRET_SENSE_INFO),sizeof(CLASS_INTERPRET_SENSE_INFO))
    ULONG Size;          // Must be sizeof(CLASS_INTERPRET_SENSE_INFO)

    __field_range(1,30000)
    ULONG HistoryCount;  // The number of SRB_HISTORY units that will be used

    __callback PCLASS_COMPRESS_RETRY_HISTORY_DATA Compress;
    __callback PCLASS_INTERPRET_SENSE_INFO        Interpret;

} CLASS_INTERPRET_SENSE_INFO2, *PCLASS_INTERPRET_SENSE_INFO2;
// A compile-time check of the 30,000 limit not overflowing ULONG size...
// Note that it is not expected that a release (FRE) driver will normally
// have such a large history, instead using the compression function.
#define CLASS_INTERPRET_SENSE_INFO2_MAXIMUM_HISTORY_COUNT 30000
C_ASSERT( (MAXULONG - sizeof(SRB_HISTORY)) / 30000 >= sizeof(SRB_HISTORY_ITEM) );


//
// Restricted - May only append to this structure for backwards compatibility
//
typedef struct _CLASS_DRIVER_EXTENSION {

    UNICODE_STRING RegistryPath;

    CLASS_INIT_DATA InitData;

    ULONG DeviceCount;

#if (NTDDI_VERSION >= NTDDI_WINXP)
    PCLASS_QUERY_WMI_REGINFO_EX   ClassFdoQueryWmiRegInfoEx;
    PCLASS_QUERY_WMI_REGINFO_EX   ClassPdoQueryWmiRegInfoEx;
#endif

#if (NTDDI_VERSION >= NTDDI_VISTA)
    REGHANDLE EtwHandle;

    PDRIVER_DISPATCH DeviceMajorFunctionTable[IRP_MJ_MAXIMUM_FUNCTION + 1];
    PDRIVER_DISPATCH MpDeviceMajorFunctionTable[IRP_MJ_MAXIMUM_FUNCTION + 1];

    //
    // Support for cdrom class drivers to extend
    // the interpret sense information routine
    // and retry history per-packet.  Need to 
    // setup during DriverEntry, so must be placed here.
    //
    PCLASS_INTERPRET_SENSE_INFO2 InterpretSenseInfo;

    //
    // Support for overriding the min/max number
    // of packets to keep allocated.  Need to
    // setup during DriverEntry, so must be placed here.
    //
    PCLASS_WORKING_SET WorkingSet;

#endif

} CLASS_DRIVER_EXTENSION, *PCLASS_DRIVER_EXTENSION;

typedef struct _COMMON_DEVICE_EXTENSION COMMON_DEVICE_EXTENSION, *PCOMMON_DEVICE_EXTENSION;
typedef struct _FUNCTIONAL_DEVICE_EXTENSION FUNCTIONAL_DEVICE_EXTENSION, *PFUNCTIONAL_DEVICE_EXTENSION;
typedef struct _PHYSICAL_DEVICE_EXTENSION PHYSICAL_DEVICE_EXTENSION, *PPHYSICAL_DEVICE_EXTENSION;

//
// Restricted - May only append to this structure for backwards compatibility
//
typedef struct _COMMON_DEVICE_EXTENSION {

    //
    // Version control field
    //
    // Note - this MUST be the first thing in the device extension
    // for any class driver using classpnp or a later version.
    //

    ULONG Version;

    //
    // Back pointer to device object
    //
    // NOTE - this MUST be the second field in the common device extension.
    // Users of this structure will include it in a union with the DeviceObject
    // pointer so they can reference this with a bit of syntactic sugar.
    //

    PDEVICE_OBJECT DeviceObject;

    //
    // Pointer to lower device object - send all requests through this
    //

    PDEVICE_OBJECT LowerDeviceObject;

    //
    // Pointer to the partition zero device extension.
    // There are several flags stored there that pdo
    // routines need to access
    //

    PFUNCTIONAL_DEVICE_EXTENSION PartitionZeroExtension;

    //
    // Pointer to the initialization data for this driver.  This is more
    // efficient than constantly getting the driver extension.
    //

    PCLASS_DRIVER_EXTENSION DriverExtension;

    //
    // INTERLOCKED counter of the number of requests/function calls outstanding
    // which will need to use this device object.  When this count goes to
    // zero the RemoveEvent will be set.
    //
    // This variable is only manipulated by ClassIncrementRemoveLock and
    // ClassDecrementRemoveLock.
    //

    LONG RemoveLock;

    //
    // This event will be signalled when it is safe to remove the device object
    //

    KEVENT RemoveEvent;

    //
    // The spinlock and the list are only used in checked builds to track
    // who has acquired the remove lock.  Free systems will leave these
    // initialized to ff
    //

    KSPIN_LOCK RemoveTrackingSpinlock;

    PVOID RemoveTrackingList;

    LONG RemoveTrackingUntrackedCount;

    //
    // Pointer to the driver specific data area
    //

    PVOID DriverData;

#if _MSC_VER >= 1200
#pragma warning(push)
#endif
#pragma warning(disable:4214) // bit field types other than int
#pragma warning(disable:4201) // nameless struct/union

    //
    // Flag indicates whether this device object is
    // an FDO or a PDO
    //

    struct {
        BOOLEAN IsFdo : 1;
        BOOLEAN IsInitialized : 1;

        //
        // Flag indicating whether the lookaside listhead for srbs has been
        // initialized.
        //

        BOOLEAN IsSrbLookasideListInitialized : 1;
    };

#if _MSC_VER >= 1200
#pragma warning(pop)
#endif

    //
    // Contains the IRP_MN_CODE of the last state-changing pnp irps we
    // recieved (XXX_STOP, XXX_REMOVE, START, etc...).  Used in concert
    // with IsRemoved.
    //

    UCHAR PreviousState;
    UCHAR CurrentState;

    //
    // interlocked flag indicating that the device has been removed.
    //

    ULONG IsRemoved;

    //
    // The name of the object
    //
    UNICODE_STRING DeviceName;

    //
    // The next child device (or if this is an FDO, the first child device).
    //

    PPHYSICAL_DEVICE_EXTENSION ChildList;

    //
    // Number of the partition or -1L if not partitionable.
    //

    ULONG PartitionNumber;

    //
    // Length of partition in bytes
    //

    LARGE_INTEGER PartitionLength;

    //
    // Number of bytes before start of partition
    //

    LARGE_INTEGER StartingOffset;

    //
    // Dev-Info structure for this type of device object
    // Contains call-out routines for the class driver.
    //

    PCLASS_DEV_INFO DevInfo;

    //
    // Count of page files going through this device object
    // and event to synchronize them with.
    //

    ULONG PagingPathCount;
    ULONG DumpPathCount;
    ULONG HibernationPathCount;
    KEVENT PathCountEvent;

#ifndef ALLOCATE_SRB_FROM_POOL
    //
    // Lookaside listhead for srbs.
    //

    NPAGED_LOOKASIDE_LIST SrbLookasideList;
#endif

    //
    // Interface name string returned by IoRegisterDeviceInterface.
    //

    UNICODE_STRING MountedDeviceInterfaceName;


    //
    // Registered Data Block info for wmi
    //
    ULONG GuidCount;
    PGUIDREGINFO GuidRegInfo;

    //
    // File object dictionary for this device object.  Extensions are stored
    // in here rather than off the actual file object.
    //

    DICTIONARY FileObjectDictionary;

    //
    // The following will be in the released product as reserved.
    // Leave these at the end of the structure.
    //

#if (NTDDI_VERSION >= NTDDI_WINXP)
    PCLASS_PRIVATE_COMMON_DATA PrivateCommonData;
#else
    ULONG_PTR Reserved1;
#endif

#if (NTDDI_VERSION >= NTDDI_VISTA)
    //
    // Pointer to the dispatch table for this object
    //

    PDRIVER_DISPATCH *DispatchTable;
#else
    ULONG_PTR Reserved2;
#endif

    ULONG_PTR Reserved3;
    ULONG_PTR Reserved4;

} COMMON_DEVICE_EXTENSION, *PCOMMON_DEVICE_EXTENSION;

typedef enum {
    FailurePredictionNone = 0,     // No failure detection polling needed
    FailurePredictionIoctl,        // Do failure detection via IOCTL
    FailurePredictionSmart,        // Do failure detection via SMART
    FailurePredictionSense         // Do failure detection via sense data
} FAILURE_PREDICTION_METHOD, *PFAILURE_PREDICTION_METHOD;

//
// Default failure prediction polling interval is every hour
//

#define DEFAULT_FAILURE_PREDICTION_PERIOD 60 * 60 * 1

//
// The failure prediction structure is internal to classpnp - drivers do not
// need to know what it contains.
//

struct _FAILURE_PREDICTION_INFO;
typedef struct _FAILURE_PREDICTION_INFO *PFAILURE_PREDICTION_INFO;

#if _MSC_VER >= 1200
#pragma warning(push)
#endif
#pragma warning(disable:4214) // bit field types other than int

//
// this is to allow for common code to handle
// every option.
//

typedef struct _CLASS_POWER_OPTIONS {
    ULONG PowerDown              :  1;
    ULONG LockQueue              :  1;
    ULONG HandleSpinDown         :  1;
    ULONG HandleSpinUp           :  1;
    ULONG Reserved               : 27;
} CLASS_POWER_OPTIONS, *PCLASS_POWER_OPTIONS;

#if _MSC_VER >= 1200
#pragma warning(pop)
#endif

//
// this is a private structure, but must be kept here
// to properly compile size of FUNCTIONAL_DEVICE_EXTENSION
//
typedef enum {
    PowerDownDeviceInitial,
    PowerDownDeviceLocked,
    PowerDownDeviceStopped,
    PowerDownDeviceOff,
    PowerDownDeviceUnlocked
} CLASS_POWER_DOWN_STATE;

//
// same as above, but with an extra state for whistler
// should be ok to change the above structure, but that
// would break someone somewhere who ignore the PRIVATE
// nature of the structure.
//

typedef enum {
    PowerDownDeviceInitial2,
    PowerDownDeviceLocked2,
    PowerDownDeviceFlushed2,
    PowerDownDeviceStopped2,
    PowerDownDeviceOff2,
    PowerDownDeviceUnlocked2
} CLASS_POWER_DOWN_STATE2;

//
// this is a private enum, but must be kept here
// to properly compile size of FUNCTIONAL_DEVICE_EXTENSION
//
typedef enum {
    PowerUpDeviceInitial,
    PowerUpDeviceLocked,
    PowerUpDeviceOn,
    PowerUpDeviceStarted,
    PowerUpDeviceUnlocked
} CLASS_POWER_UP_STATE;

//
// this is a private structure, but must be kept here
// to properly compile size of FUNCTIONAL_DEVICE_EXTENSION
//
typedef struct _CLASS_POWER_CONTEXT {

    union {
        CLASS_POWER_DOWN_STATE PowerDown;
        CLASS_POWER_DOWN_STATE2 PowerDown2; // whistler
        CLASS_POWER_UP_STATE PowerUp;
    } PowerChangeState;

    CLASS_POWER_OPTIONS Options;

    BOOLEAN InUse;
    BOOLEAN QueueLocked;

    NTSTATUS FinalStatus;

    ULONG RetryCount;
    ULONG RetryInterval;

    PIO_COMPLETION_ROUTINE CompletionRoutine;
    PDEVICE_OBJECT DeviceObject;
    PIRP Irp;

    SCSI_REQUEST_BLOCK Srb;

} CLASS_POWER_CONTEXT, *PCLASS_POWER_CONTEXT;

//
// Restricted - May only append to this structure for backwards compatibility
//
typedef struct _FUNCTIONAL_DEVICE_EXTENSION {

#if _MSC_VER >= 1200
#pragma warning(push)
#endif
#pragma warning(disable:4201) // nameless struct/union

    //
    // Common device extension header
    //

    union {
        struct {
            ULONG Version;
            PDEVICE_OBJECT DeviceObject;
        };
        COMMON_DEVICE_EXTENSION CommonExtension;
    };

#if _MSC_VER >= 1200
#pragma warning(pop)
#endif

    //
    // Pointer to the physical device object we attached to - use this
    // for Pnp calls which need a PDO
    //

    PDEVICE_OBJECT LowerPdo;

    //
    // Device capabilities
    //

    PSTORAGE_DEVICE_DESCRIPTOR DeviceDescriptor;

    //
    // SCSI port driver capabilities
    //

    PSTORAGE_ADAPTER_DESCRIPTOR AdapterDescriptor;

    //
    // Current Power state of the device
    //

    DEVICE_POWER_STATE DevicePowerState;

    //
    // DM Driver for IDE drives hack (ie. OnTrack)
    // Bytes to skew all requests
    //

    ULONG DMByteSkew;

    //
    // DM Driver for IDE drives hack (ie. OnTrack)
    // Sectors to skew all requests.
    //

    ULONG DMSkew;

    //
    // DM Driver for IDE drives hack (ie. OnTrack)
    // Flag to indicate whether DM driver has been located on an IDE drive.
    //

    BOOLEAN DMActive;

    //
    // Buffer for drive parameters returned in IO device control.
    //

    DISK_GEOMETRY DiskGeometry;

    //
    // Request Sense Buffer
    //

    PSENSE_DATA SenseData;

    //
    // Request timeout in seconds;
    //

    ULONG TimeOutValue;

    //
    // System device number
    //

    ULONG DeviceNumber;

    //
    // Add default Srb Flags.
    //

    ULONG SrbFlags;

    //
    // Total number of SCSI protocol errors on the device.
    //

    ULONG ErrorCount;

    //
    // Lock count for removable media.
    //

    LONG LockCount;
    LONG ProtectedLockCount;
    LONG InternalLockCount;

    KEVENT EjectSynchronizationEvent;

    //
    // Values for the flags are below.
    //

    USHORT  DeviceFlags;

    //
    // Log2 of sector size
    //

    UCHAR SectorShift;

    //
    // Flags to optimize CDB handling.
    //

#if (NTDDI_VERSION >= NTDDI_VISTA)
    UCHAR CdbForceUnitAccess;
#else
    UCHAR ReservedByte;
#endif

    //
    // Indicates that the necessary data structures for media change
    // detection have been initialized.
    //

    PMEDIA_CHANGE_DETECTION_INFO MediaChangeDetectionInfo;

    PKEVENT Unused1;
    HANDLE  Unused2;

    //
    // File system context. Used for kernel-mode requests to disable autorun.
    //

    FILE_OBJECT_EXTENSION KernelModeMcnContext;

    //
    // Count of media changes.  This field is only valid for the root partition
    // (ie. if PhysicalDevice == NULL).
    //

    ULONG MediaChangeCount;

    //
    // Storage for a handle to the directory the PDO's are placed in
    //

    HANDLE DeviceDirectory;

    //
    // Storage for a release queue request.
    //

    KSPIN_LOCK ReleaseQueueSpinLock;

    PIRP ReleaseQueueIrp;

    SCSI_REQUEST_BLOCK ReleaseQueueSrb;

    BOOLEAN ReleaseQueueNeeded;

    BOOLEAN ReleaseQueueInProgress;

    BOOLEAN ReleaseQueueIrpFromPool;
    //
    // Failure detection storage
    //

    BOOLEAN FailurePredicted;

    ULONG FailureReason;
    PFAILURE_PREDICTION_INFO FailurePredictionInfo;

    BOOLEAN PowerDownInProgress;

    //
    // Interlock for ensuring we don't recurse during enumeration.
    //

    ULONG EnumerationInterlock;

    //
    // Synchronization object for manipulating the child list.
    //

    KEVENT ChildLock;

    //
    // The thread which currently owns the ChildLock.  This is used to
    // avoid recursive acquisition.
    //

    PKTHREAD ChildLockOwner;

    //
    // The number of times this event has been acquired.
    //

    ULONG ChildLockAcquisitionCount;

    //
    // Flags for special behaviour required by
    // different hardware, such as never spinning down
    // or disabling advanced features such as write cache
    //

    ULONG ScanForSpecialFlags;

    //
    // For delayed retry of power requests at DPC level
    //

    KDPC PowerRetryDpc;
    KTIMER PowerRetryTimer;

    //
    // Context structure for power operations.  Since we can only have
    // one D irp at any time in the stack we don't need to worry about
    // allocating multiple of these structures.
    //

    CLASS_POWER_CONTEXT PowerContext;

#if (NTDDI_VERSION <= NTDDI_WIN2K)

#if (SPVER(NTDDI_VERSION) < 2))

    ULONG_PTR Reserved1;
    ULONG_PTR Reserved2;
    ULONG_PTR Reserved3;
    ULONG_PTR Reserved4;

#else

    //
    // Indicates the number of successfully completed
    // requests, if error throttling has been applied.
    //
    ULONG CompletionSuccessCount;

    //
    // When too many errors occur and features are turned off
    // the old SrbFlags are saved here, so that if the condition
    // is fixed, we can restore them to their proper state.
    //
    ULONG SavedSrbFlags;

    //
    // Once recovery has been initiated, cache the old error count value.
    // If new errors occur, go back to the feature set as was earlier used.
    //
    ULONG SavedErrorCount;

    //
    // For future expandability
    // leave these at the end of the structure.
    //

    ULONG_PTR Reserved1;
#endif

#else

    //
    // Hold new private data that only classpnp should modify
    // in this structure.
    //

    PCLASS_PRIVATE_FDO_DATA PrivateFdoData;

    //
    // For future expandability
    // leave these at the end of the structure.
    //

    ULONG_PTR Reserved2;
    ULONG_PTR Reserved3;
    ULONG_PTR Reserved4;
#endif

} FUNCTIONAL_DEVICE_EXTENSION, *PFUNCTIONAL_DEVICE_EXTENSION;

//
// The following CLASS_SPECIAL_ flags are set in ScanForSpecialFlags
// in the FdoExtension
//

// Never Spin Up/Down the drive (may not handle properly)
#define CLASS_SPECIAL_DISABLE_SPIN_DOWN                 0x00000001
#define CLASS_SPECIAL_DISABLE_SPIN_UP                   0x00000002

// Don't bother to lock the queue when powering down
// (used mostly to send a quick stop to a cdrom to abort audio playback)
#define CLASS_SPECIAL_NO_QUEUE_LOCK                     0x00000008

// Disable write cache due to known bugs
#define CLASS_SPECIAL_DISABLE_WRITE_CACHE               0x00000010

//
// Special interpretation of "device not ready / cause not reportable" for
// devices which don't tell us they need to be spun up manually after they
// spin themselves down behind our back.
//
// The down side of this is that if the drive chooses to report
// "device not ready / cause not reportable" to mean "no media in device"
// or any other error which really does require user intervention NT will
// waste a large amount of time trying to spin up a disk which can't be spun
// up.
//

#define CLASS_SPECIAL_CAUSE_NOT_REPORTABLE_HACK         0x00000020

#if ((NTDDI_VERSION == NTDDI_WIN2KSP3) || (OSVER(NTDDI_VERSION) == NTDDI_WINXP))
// Disabling the write cache is not supported on this device
#define CLASS_SPECIAL_DISABLE_WRITE_CACHE_NOT_SUPPORTED 0x00000040  // Obsolete
#endif
#define CLASS_SPECIAL_MODIFY_CACHE_UNSUCCESSFUL         0x00000040
#define CLASS_SPECIAL_FUA_NOT_SUPPORTED                 0x00000080

#define CLASS_SPECIAL_VALID_MASK                        0x000000FB
#define CLASS_SPECIAL_RESERVED         (~CLASS_SPECIAL_VALID_MASK)


//
// Restricted - May only append to this structure for backwards compatibility
//
typedef struct _PHYSICAL_DEVICE_EXTENSION {

#if _MSC_VER >= 1200
#pragma warning(push)
#endif
#pragma warning(disable:4201) // nameless struct/union

    //
    // Common extension data
    //

    union {
        struct {
            ULONG Version;
            PDEVICE_OBJECT DeviceObject;
        };
        COMMON_DEVICE_EXTENSION CommonExtension;
    };

#if _MSC_VER >= 1200
#pragma warning(pop)
#endif

    //
    // Indicates that the pdo no longer physically exits.
    //

    BOOLEAN IsMissing;

    //
    // Indicates that the PDO has been handed out to the PNP system.
    //

    BOOLEAN IsEnumerated;

    //
    // Hold new private data that only classpnp should modify
    // in this structure.
    //

#if (NTDDI_VERSION >= NTDDI_WINXP)
    PCLASS_PRIVATE_PDO_DATA PrivatePdoData;
#else
    ULONG_PTR Reserved1;
#endif

    //
    // for future expandability
    // leave these at the end of the structure.
    //

    ULONG_PTR Reserved2;
    ULONG_PTR Reserved3;
    ULONG_PTR Reserved4;

} PHYSICAL_DEVICE_EXTENSION, *PPHYSICAL_DEVICE_EXTENSION;

//
// Indicates that the device has write caching enabled.
//

#define DEV_WRITE_CACHE     0x00000001

//
// Build SCSI 1 or SCSI 2 CDBs
//

#define DEV_USE_SCSI1       0x00000002

//
// Indicates whether is is safe to send StartUnit commands
// to this device. It will only be off for some removeable devices.
//

#define DEV_SAFE_START_UNIT 0x00000004

//
// Indicates whether it is unsafe to send SCSIOP_MECHANISM_STATUS commands to
// this device.  Some devices don't like these 12 byte commands
//

#define DEV_NO_12BYTE_CDB   0x00000008

//
// Indicates that the device is connected to a backup power supply
// and hence write-through and synch cache requests may be ignored
//

#define DEV_POWER_PROTECTED 0x00000010

//
// Indicates that the device supports 16 byte CDBs
//

#define DEV_USE_16BYTE_CDB  0x00000020


//
// Define context structure for asynchronous completions.
//

typedef struct _COMPLETION_CONTEXT {
    PDEVICE_OBJECT DeviceObject;
    SCSI_REQUEST_BLOCK Srb;
}COMPLETION_CONTEXT, *PCOMPLETION_CONTEXT;


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
__checkReturn
SCSIPORT_API
ULONG
ClassInitialize(
    __in  PVOID            Argument1,
    __in  PVOID            Argument2,
    __in  PCLASS_INIT_DATA InitializationData
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/

//
// List of the GUIDs supported by ClassInitializeEx() and the structure
// type used for the data parameter for that GUID.
//
// {00E34B11-2444-4745-A53D-620100CD82F7} == CLASS_QUERY_WMI_REGINFO_EX_LIST
// {509a8c5f-71d7-48f6-821e-173c49bf2f18} == CLASS_INTERPRET_SENSE_INFO2

#define GUID_CLASSPNP_QUERY_REGINFOEX { 0x00e34b11, 0x2444, 0x4745, { 0xa5, 0x3d, 0x62, 0x01, 0x00, 0xcd, 0x82, 0xf7 } }
#define GUID_CLASSPNP_SENSEINFO2      { 0x509a8c5f, 0x71d7, 0x48f6, { 0x82, 0x1e, 0x17, 0x3c, 0x49, 0xbf, 0x2f, 0x18 } }
#define GUID_CLASSPNP_WORKING_SET     { 0x105701b0, 0x9e9b, 0x47cb, { 0x97, 0x80, 0x81, 0x19, 0x8a, 0xf7, 0xb5, 0x24 } }

//
// The structure specifies callbacks that are used instead of the
// PCLASS_QUERY_WMI_REGINFO callbacks.
//
typedef struct _CLASS_QUERY_WMI_REGINFO_EX_LIST
{
    ULONG Size;          // Should be sizeof(CLASS_QUERY_REGINFO_EX_LIST)

    __callback PCLASS_QUERY_WMI_REGINFO_EX   ClassFdoQueryWmiRegInfoEx;
    __callback PCLASS_QUERY_WMI_REGINFO_EX   ClassPdoQueryWmiRegInfoEx;

} CLASS_QUERY_WMI_REGINFO_EX_LIST, *PCLASS_QUERY_WMI_REGINFO_EX_LIST;

__drv_maxIRQL(PASSIVE_LEVEL)
__checkReturn
SCSIPORT_API
ULONG
ClassInitializeEx(
    __in  PDRIVER_OBJECT   DriverObject,
    __in  LPGUID           Guid,
    __in  PVOID            Data
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
__checkReturn
__drv_valueIs(<0;==0)
SCSIPORT_API
NTSTATUS
ClassCreateDeviceObject(
    __in PDRIVER_OBJECT       DriverObject,
    __in_z PCCHAR               ObjectNameBuffer,
    __in PDEVICE_OBJECT       LowerDeviceObject,
    __in BOOLEAN              IsFdo,
    __out
    __drv_out_deref(
        __drv_allocatesMem(Mem)
        __drv_aliasesMem
        __on_failure(__null))
    PDEVICE_OBJECT      *DeviceObject
    );

/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
__checkReturn
SCSIPORT_API
NTSTATUS
ClassReadDriveCapacity(
    __in PDEVICE_OBJECT DeviceObject
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
VOID
ClassReleaseQueue(
    __in PDEVICE_OBJECT DeviceObject
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
VOID
ClassSplitRequest(
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp,
    __in ULONG MaximumBytes
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
NTSTATUS
ClassDeviceControl(
    __in PDEVICE_OBJECT DeviceObject,
    __inout PIRP Irp
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
IO_COMPLETION_ROUTINE ClassIoComplete;


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
IO_COMPLETION_ROUTINE ClassIoCompleteAssociated;


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
BOOLEAN
ClassInterpretSenseInfo(
    __in PDEVICE_OBJECT DeviceObject,
    __in PSCSI_REQUEST_BLOCK Srb,
    __in UCHAR MajorFunctionCode,
    __in ULONG IoDeviceCode,
    __in ULONG RetryCount,
    __out NTSTATUS *Status,
    __out_opt __range(0,100) ULONG *RetryInterval
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
VOID
ClassSendDeviceIoControlSynchronous(
    __in ULONG IoControlCode,
    __in PDEVICE_OBJECT TargetDeviceObject,
    __inout_xcount_opt(max(InputBufferLength, OutputBufferLength)) PVOID Buffer,
    __in ULONG InputBufferLength,
    __in ULONG OutputBufferLength,
    __in BOOLEAN InternalDeviceIoControl,
    __out PIO_STATUS_BLOCK IoStatus
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
NTSTATUS
ClassSendIrpSynchronous(
    __in PDEVICE_OBJECT TargetDeviceObject,
    __in PIRP Irp
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
NTSTATUS
ClassForwardIrpSynchronous(
    __in PCOMMON_DEVICE_EXTENSION CommonExtension,
    __in PIRP Irp
    );

/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
NTSTATUS
ClassSendSrbSynchronous(
    __in PDEVICE_OBJECT DeviceObject,
    __inout PSCSI_REQUEST_BLOCK Srb,
    __in_bcount_opt(BufferLength) PVOID BufferAddress,
    __in ULONG BufferLength,
    __in BOOLEAN WriteToDevice
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
NTSTATUS
ClassSendSrbAsynchronous(
    __in PDEVICE_OBJECT DeviceObject,
    __inout PSCSI_REQUEST_BLOCK Srb,
    __in PIRP Irp,
    __in_bcount_opt(BufferLength) __drv_aliasesMem PVOID BufferAddress,
    __in ULONG BufferLength,
    __in BOOLEAN WriteToDevice
    );

/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
NTSTATUS
ClassBuildRequest(
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
ULONG
ClassModeSense(
    __in PDEVICE_OBJECT DeviceObject,
    __in_bcount(Length) PCHAR ModeSenseBuffer,
    __in ULONG Length,
    __in UCHAR PageMode
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
PVOID
ClassFindModePage(
    __in_bcount(Length) PCHAR ModeSenseBuffer,
    __in ULONG Length,
    __in UCHAR PageMode,
    __in BOOLEAN Use6Byte
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
NTSTATUS
ClassClaimDevice(
    __in PDEVICE_OBJECT LowerDeviceObject,
    __in BOOLEAN Release
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
__drv_dispatchType(IRP_MJ_SCSI)
DRIVER_DISPATCH ClassInternalIoControl;

/*++

Internal function - described in classpnp\utils.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
VOID
ClassInitializeSrbLookasideList(
    __inout PCOMMON_DEVICE_EXTENSION CommonExtension,
    __in ULONG NumberElements
    );

/*++

Internal function - described in classpnp\utils.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
VOID
ClassDeleteSrbLookasideList(
    __inout PCOMMON_DEVICE_EXTENSION CommonExtension
    );

/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
ULONG
ClassQueryTimeOutRegistryValue(
    __in PDEVICE_OBJECT DeviceObject
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
NTSTATUS
ClassGetDescriptor(
    __in PDEVICE_OBJECT DeviceObject,
    __in PSTORAGE_PROPERTY_ID PropertyId,
    __deref_out PVOID *Descriptor
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
VOID
ClassInvalidateBusRelations(
    __in PDEVICE_OBJECT Fdo
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
VOID
ClassMarkChildrenMissing(
    __in PFUNCTIONAL_DEVICE_EXTENSION Fdo
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
BOOLEAN
ClassMarkChildMissing(
    __in PPHYSICAL_DEVICE_EXTENSION PdoExtension,
    __in BOOLEAN AcquireChildLock
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
VOID
ClassDebugPrint(
    __in CLASS_DEBUG_LEVEL DebugPrintLevel,
    __in_z PCCHAR DebugMessage,
    ...
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
__drv_aliasesMem
__drv_maxIRQL(DISPATCH_LEVEL)
SCSIPORT_API
PCLASS_DRIVER_EXTENSION
ClassGetDriverExtension(
    __in PDRIVER_OBJECT DriverObject
    );


/*++

Internal function - described in classpnp\lock.c in ddk sources

--*/
SCSIPORT_API
VOID
ClassCompleteRequest(
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp,
    __in CCHAR PriorityBoost
    );


/*++

Internal function - described in classpnp\lock.c in ddk sources

--*/
SCSIPORT_API
VOID
ClassReleaseRemoveLock(
    __in PDEVICE_OBJECT DeviceObject,
         PIRP Tag
    );


/*++

Internal function - described in classpnp\lock.c in ddk sources

--*/
SCSIPORT_API
ULONG
ClassAcquireRemoveLockEx(
    __in PDEVICE_OBJECT DeviceObject,
         PVOID Tag,
    __in PCSTR File,
    __in ULONG Line
    );




/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
VOID
ClassUpdateInformationInRegistry(
    __in PDEVICE_OBJECT   Fdo,
    __in PCHAR            DeviceName,
    __in ULONG            DeviceNumber,
    __in_bcount_opt(InquiryDataLength) PINQUIRYDATA InquiryData,
    __in ULONG            InquiryDataLength
    );


/*++

Internal function - described in classpnp\classwmi.c in ddk sources

--*/
SCSIPORT_API
NTSTATUS
ClassWmiCompleteRequest(
    __in PDEVICE_OBJECT DeviceObject,
    __inout PIRP Irp,
    __in NTSTATUS Status,
    __in ULONG BufferUsed,
    __in CCHAR PriorityBoost
    );


/*++

Internal function - described in classpnp\classwmi.c in ddk sources

--*/
__drv_maxIRQL(DISPATCH_LEVEL)
SCSIPORT_API
NTSTATUS
ClassWmiFireEvent(
    __in PDEVICE_OBJECT DeviceObject,
    __in LPGUID Guid,
    __in ULONG InstanceIndex,
    __in ULONG EventDataSize,
    __in_bcount(EventDataSize) PVOID EventData
    );


/*++

Internal function - described in classpnp\autorun.c in ddk sources

--*/
SCSIPORT_API
VOID
ClassResetMediaChangeTimer(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension
    );


/*++

Internal function - described in classpnp\autorun.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
VOID
ClassInitializeMediaChangeDetection(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension,
    __in PUCHAR EventPrefix
    );


/*++

Internal function - described in classpnp\autorun.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
NTSTATUS
ClassInitializeTestUnitPolling(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension,
    __in BOOLEAN AllowDriveToSleep
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
PVPB
ClassGetVpb(
    __in PDEVICE_OBJECT DeviceObject
    );


/*++

Internal function - described in classpnp\power.c in ddk sources

--*/
__control_entrypoint(DeviceDriver)
SCSIPORT_API
NTSTATUS
ClassSpinDownPowerHandler(
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp
    );


/*++

Internal function - described in classpnp\power.c in ddk sources

--*/
NTSTATUS
ClassStopUnitPowerHandler(
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp
    );


/*++

Internal function - described in classpnp\autorun.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
NTSTATUS
ClassSetFailurePredictionPoll(
    __inout PFUNCTIONAL_DEVICE_EXTENSION FdoExtension,
    __in FAILURE_PREDICTION_METHOD FailurePredictionMethod,
    __in ULONG PollingPeriod
    );


/*++

Internal function - described in classpnp\autorun.c in ddk sources

--*/
__drv_maxIRQL(DISPATCH_LEVEL)
VOID
ClassNotifyFailurePredicted(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension,
    __in_bcount(BufferSize) PUCHAR Buffer,
    __in ULONG BufferSize,
    __in BOOLEAN LogError,
    __in ULONG UniqueErrorValue,
    __in UCHAR PathId,
    __in UCHAR TargetId,
    __in UCHAR Lun
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
VOID
ClassAcquireChildLock(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
VOID
ClassReleaseChildLock(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
IO_COMPLETION_ROUTINE ClassSignalCompletion;


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
VOID
ClassSendStartUnit(
    __in PDEVICE_OBJECT DeviceObject
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
NTSTATUS
ClassRemoveDevice(
    __in PDEVICE_OBJECT DeviceObject,
    __in UCHAR RemoveType
    );


/*++

Internal function - described in classpnp\class.c in ddk sources

--*/
SCSIPORT_API
IO_COMPLETION_ROUTINE ClassAsynchronousCompletion;


/*++

Internal function - described in classpnp\autorun.c in ddk sources

--*/
SCSIPORT_API
VOID
ClassCheckMediaState(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension
    );


/*++

Internal function - described in classpnp\autorun.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
VOID
ClassSetMediaChangeState(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension,
    __in MEDIA_CHANGE_DETECTION_STATE State,
    __in BOOLEAN Wait
    );


/*++

Internal function - described in classpnp\autorun.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
VOID
ClassEnableMediaChangeDetection(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension
    );


/*++

Internal function - described in classpnp\autorun.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
VOID
ClassDisableMediaChangeDetection(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension
    );


/*++

Internal function - described in classpnp\autorun.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
SCSIPORT_API
VOID
ClassCleanupMediaChangeDetection(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension
    );


/*++

Internal function - described in classpnp\utils.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
VOID
ClassGetDeviceParameter(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension,
    __in_opt PWSTR SubkeyName,
    __in PWSTR ParameterName,
    __inout PULONG ParameterValue
    );


/*++

Internal function - described in classpnp\utils.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
NTSTATUS
ClassSetDeviceParameter(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension,
    __in_opt PWSTR SubkeyName,
    __in PWSTR ParameterName,
    __in ULONG ParameterValue
    );

#if (NTDDI_VERSION >= NTDDI_VISTA)

/*++

Internal function - described in classpnp\create.c in ddk sources

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
PFILE_OBJECT_EXTENSION
ClassGetFsContext(
    __in PCOMMON_DEVICE_EXTENSION CommonExtension,
    __in PFILE_OBJECT FileObject
    );

/*++

Internal function - described in classpnp\autorun.c in ddk sources

--*/
__drv_maxIRQL(DISPATCH_LEVEL)
VOID
ClassSendNotification(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension,
    __in const GUID * Guid,
    __in ULONG  ExtraDataSize,
    __in_bcount_opt(ExtraDataSize) PVOID  ExtraData
    );

#endif

//
// could be #define, but this allows typechecking
//

__inline
BOOLEAN
PORT_ALLOCATED_SENSE(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension,
    __in PSCSI_REQUEST_BLOCK Srb
    )
{
    return ((BOOLEAN)((TEST_FLAG(Srb->SrbFlags, SRB_FLAGS_PORT_DRIVER_ALLOCSENSE) &&
             TEST_FLAG(Srb->SrbFlags, SRB_FLAGS_FREE_SENSE_BUFFER)) &&
            (Srb->SenseInfoBuffer != FdoExtension->SenseData))
            );
}

__inline
VOID
FREE_PORT_ALLOCATED_SENSE_BUFFER(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension,
    __in PSCSI_REQUEST_BLOCK Srb
    )
{
    ASSERT(TEST_FLAG(Srb->SrbFlags, SRB_FLAGS_PORT_DRIVER_ALLOCSENSE));
    ASSERT(TEST_FLAG(Srb->SrbFlags, SRB_FLAGS_FREE_SENSE_BUFFER));
    ASSERT(Srb->SenseInfoBuffer != FdoExtension->SenseData);

    ExFreePool(Srb->SenseInfoBuffer);
    Srb->SenseInfoBuffer = FdoExtension->SenseData;
    Srb->SenseInfoBufferLength = SENSE_BUFFER_SIZE; // should be variable?
    CLEAR_FLAG(Srb->SrbFlags, SRB_FLAGS_FREE_SENSE_BUFFER);
    return;
}



/*++////////////////////////////////////////////////////////////////////////////

PCLASS_SCAN_FOR_SPECIAL_HANDLER()

Routine Description:

    This routine is a callback into the driver to set device-specific
    flags based upon matches made to the device's inquiry data.  Drivers
    register for this callback using ClassRegisterScanForSpecial().

Irql:

    This routine will be called at KIRQL == PASSIVE_LEVEL

Arguments:

    DeviceObject is the device object the error occurred on.

    Srb is the Srb that was being processed when the error occurred.

    Status may be overwritten by the routine if it decides that the error
        was benign, or otherwise wishes to change the returned status code
        for this command

    Retry may be overwritten to specify that this command should or should
        not be retried (if the callee supports retrying commands)

Return Value:

    status

--*/
__drv_maxIRQL(PASSIVE_LEVEL)
typedef
VOID
(*PCLASS_SCAN_FOR_SPECIAL_HANDLER) (
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension,
    __in ULONG_PTR Data
    );

__drv_maxIRQL(PASSIVE_LEVEL)
VOID
ClassScanForSpecial(
    __in PFUNCTIONAL_DEVICE_EXTENSION FdoExtension,
    __in CLASSPNP_SCAN_FOR_SPECIAL_INFO DeviceList[],
    __in PCLASS_SCAN_FOR_SPECIAL_HANDLER Function
    );

#endif /* _CLASS_ */

