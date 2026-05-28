/*++

Copyright (c) Microsoft Corporation. All rights reserved.

Module Name:

    ksi.h

Abstract:

    Windows Driver Model/Connection and Streaming Architecture (WDM-CSA)
    core internal definitions.

--*/

#if !defined(_KSI_)
#define _KSI_

#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)

#if defined(_NTDDK_)

typedef struct {
    LONGLONG Frequency;
    LONGLONG LastDueTime;
    LONGLONG RunningTimeDelta;
    LONGLONG LastRunningTime;
    KSPIN_LOCK TimeAccessLock;
    LIST_ENTRY EventQueue;
    KSPIN_LOCK EventQueueLock;
    KTIMER QueueTimer;
    KDPC QueueDpc;
    LONG ReferenceCount;
    KSSTATE State;
    LONGLONG SuspendDelta;
    LONGLONG SuspendTime;
    PFNKSSETTIMER SetTimer;
    PFNKSCANCELTIMER CancelTimer;
    PFNKSCLOCK_CORRELATEDTIME CorrelatedTime;
    PVOID Context;
    KSRESOLUTION Resolution;
    KEVENT FreeEvent;
    LONG ExternalTimeReferenceCount;
    BOOLEAN ExternalTimeValid;
    LONGLONG LastStreamTime;
} KSIDEFAULTCLOCK, *PKSIDEFAULTCLOCK;

typedef struct {
    KSOBJECT_HEADER Header;
    PKSIDEFAULTCLOCK DefaultClock;
    ULONG Reserved;
} KSCLOCKINSTANCE, *PKSCLOCKINSTANCE;

__drv_maxIRQL(PASSIVE_LEVEL)
__checkReturn
KSDDKAPI
NTSTATUS
NTAPI
KsiPropertyDefaultClockGetTime(
    __in PIRP Irp,
    __in PKSPROPERTY Property,
    __out PLONGLONG Time
    );

__drv_maxIRQL(PASSIVE_LEVEL)
__checkReturn
KSDDKAPI
NTSTATUS
NTAPI
KsiPropertyDefaultClockGetPhysicalTime(
    __in PIRP Irp,
    __in PKSPROPERTY Property,
    __out PLONGLONG Time
    );

__drv_maxIRQL(PASSIVE_LEVEL)
__checkReturn
KSDDKAPI
NTSTATUS
NTAPI
KsiPropertyDefaultClockGetCorrelatedTime(
    __in PIRP Irp,
    __in PKSPROPERTY Property,
    __out PKSCORRELATED_TIME Time
    );

__drv_maxIRQL(PASSIVE_LEVEL)
__checkReturn
KSDDKAPI
NTSTATUS
NTAPI
KsiPropertyDefaultClockGetCorrelatedPhysicalTime(
    __in PIRP Irp,
    __in PKSPROPERTY Property,
    __out PKSCORRELATED_TIME Time
    );

__drv_maxIRQL(PASSIVE_LEVEL)
__checkReturn
KSDDKAPI
NTSTATUS
NTAPI
KsiPropertyDefaultClockGetResolution(
    __in PIRP Irp,
    __in PKSPROPERTY Property,
    __out PKSRESOLUTION Resolution
    );

__drv_maxIRQL(PASSIVE_LEVEL)
__checkReturn
KSDDKAPI
NTSTATUS
NTAPI
KsiPropertyDefaultClockGetState(
    __in PIRP Irp,
    __in PKSPROPERTY Property,
    __out PKSSTATE State
    );

__drv_maxIRQL(PASSIVE_LEVEL)
__checkReturn
KSDDKAPI
NTSTATUS
NTAPI
KsiPropertyDefaultClockGetFunctionTable(
    __in PIRP Irp,
    __in PKSPROPERTY Property,
    __out PKSCLOCK_FUNCTIONTABLE FunctionTable
    );

__drv_maxIRQL(PASSIVE_LEVEL)
__checkReturn
KSDDKAPI
NTSTATUS
NTAPI
KsiDefaultClockAddMarkEvent(
    __in PIRP Irp,
    __in PKSEVENT_TIME_INTERVAL EventTime,
    __in PKSEVENT_ENTRY EventEntry
    );

__drv_maxIRQL(PASSIVE_LEVEL)
KSDDKAPI
BOOLEAN
NTAPI
KsiQueryObjectCreateItemsPresent(
    __in KSDEVICE_HEADER Header
    );

#endif // !defined(_NTDDK_)

#define STATIC_KSNAME_Server\
    0x3C0D501AL, 0x140B, 0x11D1, 0xB4, 0x0F, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96
DEFINE_GUIDSTRUCT("3C0D501A-140B-11D1-B40F-00A0C9223196", KSNAME_Server);
#define KSNAME_Server DEFINE_GUIDNAMED(KSNAME_Server)

#define KSSTRING_Server L"{3C0D501A-140B-11D1-B40F-00A0C9223196}"

#define STATIC_KSPROPSETID_Service \
    0x3C0D501BL, 0x140B, 0x11D1, 0xB4, 0x0F, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96
DEFINE_GUIDSTRUCT("3C0D501B-140B-11D1-B40F-00A0C9223196", KSPROPSETID_Service);
#define KSPROPSETID_Service DEFINE_GUIDNAMED(KSPROPSETID_Service)

typedef enum {
    KSPROPERTY_SERVICE_BUILDCACHE,
    KSPROPERTY_SERVICE_MERIT
} KSPROPERTY_SERVICE;

#define DEFINE_KSPROPERTY_ITEM_SERVICE_BUILDCACHE(SetHandler)\
    DEFINE_KSPROPERTY_ITEM(\
        KSPROPERTY_SERVICE_BUILDCACHE,\
        NULL,\
        sizeof(KSPROPERTY),\
        sizeof(L"\\\\?\\"),\
        (SetHandler),\
        NULL, 0, NULL, NULL, 0)

#define DEFINE_KSPROPERTY_ITEM_SERVICE_MERIT(SetHandler)\
    DEFINE_KSPROPERTY_ITEM(\
        KSPROPERTY_SERVICE_MERIT,\
        NULL,\
        sizeof(KSPROPERTY),\
        sizeof(ULONG) + sizeof(L"\\\\?\\"),\
        (SetHandler),\
        NULL, 0, NULL, NULL, 0)

#if defined(__cplusplus)
}
#endif // defined(__cplusplus)

#endif // !_KSI_

