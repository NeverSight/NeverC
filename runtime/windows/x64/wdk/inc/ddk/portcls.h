/*****************************************************************************
 * portcls.h - WDM Streaming port class driver
 *****************************************************************************
 * Copyright (c) Microsoft Corporation. All rights reserved.
 */

#ifndef _PORTCLS_H_
#define _PORTCLS_H_

#ifdef __cplusplus
// WDM.H does not play well with C++.
extern "C"
{
#include <wdm.h>
}
#else
#include <wdm.h>
#endif

#ifndef IRP_MN_FILTER_RESOURCE_REQUIREMENTS
#define IRP_MN_FILTER_RESOURCE_REQUIREMENTS 0x0D
#endif

#include <windef.h>
#define NOBITMAP
#include <mmreg.h>
#undef NOBITMAP
#include <ks.h>
#include <ksmedia.h>
#include <punknown.h>

#if (NTDDI_VERSION >= NTDDI_WINXP)
#include <drmk.h>
#endif

#if defined(PC_NO_IMPORTS)
#define PORTCLASSAPI EXTERN_C
#else
#define PORTCLASSAPI EXTERN_C __declspec(dllimport)
#endif

#define _100NS_UNITS_PER_SECOND 10000000L
#define PORT_CLASS_DEVICE_EXTENSION_SIZE (64*sizeof(ULONG_PTR))

#if (NTDDI_VERSION >= NTDDI_WINXP)
//
//  N.B.: If you are having problems building your driver,
//        #define PC_OLD_NAMES in your sources file.
//        This flag is no longer turned on by default.
//
//#ifndef PC_NEW_NAMES
//#define PC_OLD_NAMES
//#endif
#elif (NTDDI_VERSION >= NTDDI_WIN2K)
#ifndef PC_NEW_NAMES
#define PC_OLD_NAMES
#endif
#endif

#define IID_IAdapterPowerManagment IID_IAdapterPowerManagement
#define IID_IAdapterPowerManagment2 IID_IAdapterPowerManagement2
#define PADAPTERPOWERMANAGMENT PADAPTERPOWERMANAGEMENT
#define PADAPTERPOWERMANAGMENT2 PADAPTERPOWERMANAGEMENT2

/*****************************************************************************
 * Interface identifiers.
 */

DEFINE_GUID(IID_IMiniport,
0xb4c90a24L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(IID_IPort,
0xb4c90a25L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(IID_IResourceList,
0x22C6AC60L, 0x851B, 0x11D0, 0x9A, 0x7F, 0x00, 0xAA, 0x00, 0x38, 0xAC, 0xFE);

#if (NTDDI_VERSION >= NTDDI_WINXP)
DEFINE_GUID(IID_IMusicTechnology,
0x80396C3CL, 0xCBCB, 0x409B, 0x9F, 0x65, 0x4F, 0x1E, 0x74, 0x67, 0xCD, 0xAF);
#endif

DEFINE_GUID(IID_IDmaChannel,
0x22C6AC61L, 0x851B, 0x11D0, 0x9A, 0x7F, 0x00, 0xAA, 0x00, 0x38, 0xAC, 0xFE);

//
// Take out IDmaChannelSlave for compilations.
//
#if (NTDDI_VERSION < NTDDI_VISTA)
DEFINE_GUID(IID_IDmaChannelSlave,
0x22C6AC62L, 0x851B, 0x11D0, 0x9A, 0x7F, 0x00, 0xAA, 0x00, 0x38, 0xAC, 0xFE);
#endif

DEFINE_GUID(IID_IInterruptSync,
0x22C6AC63L, 0x851B, 0x11D0, 0x9A, 0x7F, 0x00, 0xAA, 0x00, 0x38, 0xAC, 0xFE);
DEFINE_GUID(IID_IServiceSink,
0x22C6AC64L, 0x851B, 0x11D0, 0x9A, 0x7F, 0x00, 0xAA, 0x00, 0x38, 0xAC, 0xFE);
DEFINE_GUID(IID_IServiceGroup,
0x22C6AC65L, 0x851B, 0x11D0, 0x9A, 0x7F, 0x00, 0xAA, 0x00, 0x38, 0xAC, 0xFE);
DEFINE_GUID(IID_IRegistryKey,
0xE8DA4302l, 0xF304, 0x11D0, 0x95, 0x8B, 0x00, 0xC0, 0x4F, 0xB9, 0x25, 0xD3);
DEFINE_GUID(IID_IPortMidi,
0xb4c90a40L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(IID_IMiniportMidi,
0xb4c90a41L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(IID_IMiniportMidiStream,
0xb4c90a42L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(IID_IPortTopology,
0xb4c90a30L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(IID_IMiniportTopology,
0xb4c90a31L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(IID_IPortWaveCyclic,
0xb4c90a26L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(IID_IMiniportWaveCyclic,
0xb4c90a27L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(IID_IMiniportWaveCyclicStream,
0xb4c90a28L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(IID_IPortWavePci,
0xb4c90a50L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(IID_IPortWavePciStream,
0xb4c90a51L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(IID_IMiniportWavePci,
0xb4c90a52L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(IID_IMiniportWavePciStream,
0xb4c90a53L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);

#if (NTDDI_VERSION >= NTDDI_VISTA)
DEFINE_GUID(IID_IPortWaveRT,
0x339ff909, 0x68a9, 0x4310, 0xb0, 0x9b, 0x27, 0x4e, 0x96, 0xee, 0x4c, 0xbd);
DEFINE_GUID(IID_IPortWaveRTStream,
0x1809ce5a, 0x64bc, 0x4e62, 0xbd, 0x7d, 0x95, 0xbc, 0xe4, 0x3d, 0xe3, 0x93);
DEFINE_GUID(IID_IMiniportWaveRT,
0xf9fc4d6, 0x6061, 0x4f3c, 0xb1, 0xfc, 0x7, 0x5e, 0x35, 0xf7, 0x96, 0xa);
DEFINE_GUID(IID_IMiniportWaveRTStream,
0xac9ab, 0xfaab, 0x4f3d, 0x94, 0x55, 0x6f, 0xf8, 0x30, 0x6a, 0x74, 0xa0);
DEFINE_GUID(IID_IMiniportWaveRTStreamNotification,
0x23759128, 0x96f1, 0x423b, 0xab, 0x4d, 0x81, 0x63, 0x5b, 0xcf, 0x8c, 0xa1);

// Need compile only for post vista case
// Not sure what NTDDI_xxxx for Windows 7 
DEFINE_GUID(IID_IPortWMIRegistration, 
0xd80b18e7, 0x804c, 0x4e1e, 0x82, 0xd3, 0x24, 0x61, 0xec, 0x6, 0xe7, 0xc7);

// {C3D5E80C-7F55-40c5-88B2-6210D0CB2B59}
DEFINE_GUID(IID_IPortClsSubdeviceEx,  
0xc3d5e80c, 0x7f55, 0x40c5, 0x88, 0xb2, 0x62, 0x10, 0xd0, 0xcb, 0x2b, 0x59);

// {47BA0351-BC4B-4869-8134-B74FE17852D8}
DEFINE_GUID(IID_IPortClsPower, 
0x47ba0351, 0xbc4b, 0x4869, 0x81, 0x34, 0xb7, 0x4f, 0xe1, 0x78, 0x52, 0xd8);



// {29CC9AB1-E89D-413c-B6B2-F6D50005D063}
DEFINE_GUID(IID_IPinName, 
0x29cc9ab1, 0xe89d, 0x413c, 0xb6, 0xb2, 0xf6, 0xd5, 0x0, 0x5, 0xd0, 0x63);


#endif

DEFINE_GUID(IID_IAdapterPowerManagement,
0x793417D0L, 0x35FE, 0x11D1, 0xAD, 0x08, 0x00, 0xA0, 0xC9, 0x0A, 0xB1, 0xB0);
DEFINE_GUID(IID_IAdapterPowerManagement2, 
0xE0F92E5DL, 0x67F5, 0x48EE, 0xB5, 0x7A, 0x7D, 0x1E, 0x90, 0xC5, 0xF4, 0x3D);
DEFINE_GUID(IID_IPowerNotify,
0x3DD648B8L, 0x969F, 0x11D1, 0x95, 0xA9, 0x00, 0xC0, 0x4F, 0xB9, 0x25, 0xD3);
DEFINE_GUID(IID_IWaveCyclicClock,
0xdec1ec78L, 0x419a, 0x11d1, 0xad, 0x09, 0x00, 0xc0, 0x4f, 0xb9, 0x1b, 0xc4);
DEFINE_GUID(IID_IWavePciClock,
0xd5d7a256L, 0x5d10, 0x11d1, 0xad, 0xae, 0x00, 0xc0, 0x4f, 0xb9, 0x1b, 0xc4);
DEFINE_GUID(IID_IPortEvents,
0xA80F29C4L, 0x5498, 0x11D2, 0x95, 0xD9, 0x00, 0xC0, 0x4F, 0xB9, 0x25, 0xD3);

#if (NTDDI_VERSION >= NTDDI_WINXP)
DEFINE_GUID(IID_IDrmPort,
0x286D3DF8L, 0xCA22, 0x4E2E, 0xB9, 0xBC, 0x20, 0xB4, 0xF0, 0xE2, 0x01, 0xCE);
DEFINE_GUID(IID_IDrmPort2,
0x1ACCE59CL, 0x7311, 0x4B6B, 0x9F, 0xBA, 0xCC, 0x3B, 0xA5, 0x9A, 0xCD, 0xCE);
DEFINE_GUID(IID_IPortClsVersion,
0x7D89A7BBL, 0x869B, 0x4567, 0x8D, 0xBE, 0x1E, 0x16, 0x8C, 0xC8, 0x53, 0xDE);
DEFINE_GUID(IID_IPinCount,
0x5dadb7dcL, 0xa2cb, 0x4540, 0xa4, 0xa8, 0x42, 0x5e, 0xe4, 0xae, 0x90, 0x51);
DEFINE_GUID(IID_IPreFetchOffset,
0x7000f480L, 0xed44, 0x4e8b, 0xb3, 0x8a, 0x41, 0x2f, 0x8d, 0x7a, 0x50, 0x4d);
#endif

#if (NTDDI_VERSION >= NTDDI_WIN2003)
DEFINE_GUID(IID_IUnregisterSubdevice,
0x16738177L, 0xe199, 0x41f9, 0x9a, 0x87, 0xab, 0xb2, 0xa5, 0x43, 0x2f, 0x21);
DEFINE_GUID(IID_IUnregisterPhysicalConnection,
0x6c38e231L, 0x2a0d, 0x428d, 0x81, 0xf8, 0x07, 0xcc, 0x42, 0x8b, 0xb9, 0xa4);
#endif


/*****************************************************************************
 * Class identifiers.
 */

DEFINE_GUID(CLSID_PortMidi,
0xb4c90a43L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(CLSID_PortTopology,
0xb4c90a32L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(CLSID_PortWaveCyclic,
0xb4c90a2aL, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(CLSID_PortWavePci,
0xb4c90a54L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);

#if (NTDDI_VERSION >= NTDDI_VISTA)
DEFINE_GUID(CLSID_PortWaveRT,
0xcc9be57a, 0xeb9e, 0x42b4, 0x94, 0xfc, 0xc, 0xad, 0x3d, 0xbc, 0xe7, 0xfa);
#endif

DEFINE_GUID(CLSID_MiniportDriverFmSynth,
0xb4c90ae0L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(CLSID_MiniportDriverUart,
0xb4c90ae1L, 0x5791, 0x11d0, 0x86, 0xf9, 0x00, 0xa0, 0xc9, 0x11, 0xb5, 0x44);
DEFINE_GUID(CLSID_MiniportDriverFmSynthWithVol,
0xe5a3c139L, 0xf0f2, 0x11d1, 0x81, 0xaf, 0x00, 0x60, 0x08, 0x33, 0x16, 0xc1);


/*****************************************************************************
 * Interfaces
 */

#if !defined(DEFINE_ABSTRACT_UNKNOWN)

#define DEFINE_ABSTRACT_UNKNOWN()                               \
    STDMETHOD_(NTSTATUS, QueryInterface)(THIS_                  \
        REFIID InterfaceId,                                     \
        PVOID* Interface                                        \
        ) PURE;                                                 \
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;                        \
    STDMETHOD_(ULONG,Release)(THIS) PURE;

#endif //!defined(DEFINE_ABSTRACT_UNKNOWN)

#if !defined(DEFINE_ABSTRACT_PORT)

#ifdef PC_OLD_NAMES

#define DEFINE_ABSTRACT_PORT()                                      \
    STDMETHOD_(NTSTATUS,Init)                                       \
    (   THIS_                                                       \
        __in      PVOID           DeviceObject,                     \
        __in_opt  PVOID           Irp,                              \
        __in      PUNKNOWN        UnknownMiniport,                  \
        __in_opt  PUNKNOWN        UnknownAdapter,                   \
        __in      PRESOURCELIST   ResourceList                      \
    )   PURE;                                                       \
    STDMETHOD_(NTSTATUS,GetDeviceProperty)                          \
    (   THIS_                                                       \
        __in      DEVICE_REGISTRY_PROPERTY    DeviceProperty,       \
        __in      ULONG                       BufferLength,         \
        __out     PVOID                       PropertyBuffer,       \
        __out     PULONG                      ResultLength          \
    )   PURE;                                                       \
    STDMETHOD_(NTSTATUS,NewRegistryKey)                             \
    (   THIS_                                                       \
        __out     PREGISTRYKEY *      OutRegistryKey,               \
        __in_opt  PUNKNOWN            OuterUnknown,                 \
        __in      ULONG               RegistryKeyType,              \
        __in      ACCESS_MASK         DesiredAccess,                \
        __in_opt  POBJECT_ATTRIBUTES  ObjectAttributes,             \
        __in_opt  ULONG               CreateOptions,                \
        __out_opt PULONG              Disposition                   \
    )   PURE;

#else   //  !PC_OLD_NAMES

#define DEFINE_ABSTRACT_PORT()                                      \
    STDMETHOD_(NTSTATUS,Init)                                       \
    (   THIS_                                                       \
        __in      PDEVICE_OBJECT  DeviceObject,                     \
        __in_opt  PIRP            Irp,                              \
        __in      PUNKNOWN        UnknownMiniport,                  \
        __in_opt  PUNKNOWN        UnknownAdapter,                   \
        __in      PRESOURCELIST   ResourceList                      \
    )   PURE;                                                       \
    STDMETHOD_(NTSTATUS,GetDeviceProperty)                          \
    (   THIS_                                                       \
        __in      DEVICE_REGISTRY_PROPERTY    DeviceProperty,       \
        __in      ULONG                       BufferLength,         \
        __out     PVOID                       PropertyBuffer,       \
        __out     PULONG                      ResultLength          \
    )   PURE;                                                       \
    STDMETHOD_(NTSTATUS,NewRegistryKey)                             \
    (   THIS_                                                       \
        __out     PREGISTRYKEY *      OutRegistryKey,               \
        __in_opt  PUNKNOWN            OuterUnknown,                 \
        __in      ULONG               RegistryKeyType,              \
        __in      ACCESS_MASK         DesiredAccess,                \
        __in_opt  POBJECT_ATTRIBUTES  ObjectAttributes,             \
        __in_opt  ULONG               CreateOptions,                \
        __out_opt PULONG              Disposition                   \
    )   PURE;

#endif //   !PC_OLD_NAMES

#endif //!defined(DEFINE_ABSTRACT_PORT)


#if !defined(DEFINE_ABSTRACT_MINIPORT)

#define DEFINE_ABSTRACT_MINIPORT()                              \
    STDMETHOD_(NTSTATUS,GetDescription)                         \
    (   THIS_                                                   \
        __out     PPCFILTER_DESCRIPTOR *  Description           \
    )   PURE;                                                   \
    STDMETHOD_(NTSTATUS,DataRangeIntersection)                  \
    (   THIS_                                                   \
        __in      ULONG           PinId,                        \
        __in      PKSDATARANGE    DataRange,                    \
        __in      PKSDATARANGE    MatchingDataRange,            \
        __in      ULONG           OutputBufferLength,           \
        __out_opt PVOID           ResultantFormat,              \
        __out     PULONG          ResultantFormatLength         \
    )   PURE;

#endif //!defined(DEFINE_ABSTRACT_MINIPORT)

#if !defined(DEFINE_ABSTRACT_DMACHANNEL)

#define DEFINE_ABSTRACT_DMACHANNEL()                            \
    STDMETHOD_(NTSTATUS,AllocateBuffer)                         \
    (   THIS_                                                   \
        __in      ULONG               BufferSize,               \
        __in_opt  PPHYSICAL_ADDRESS   PhysicalAddressConstraint \
    )   PURE;                                                   \
    STDMETHOD_(void,FreeBuffer)                                 \
    (   THIS                                                    \
    )   PURE;                                                   \
    STDMETHOD_(ULONG,TransferCount)                             \
    (   THIS                                                    \
    )   PURE;                                                   \
    STDMETHOD_(ULONG,MaximumBufferSize)                         \
    (   THIS                                                    \
    )   PURE;                                                   \
    STDMETHOD_(ULONG,AllocatedBufferSize)                       \
    (   THIS                                                    \
    )   PURE;                                                   \
    STDMETHOD_(ULONG,BufferSize)                                \
    (   THIS                                                    \
    )   PURE;                                                   \
    STDMETHOD_(void,SetBufferSize)                              \
    (   THIS_                                                   \
        __in      ULONG   BufferSize                            \
    )   PURE;                                                   \
    STDMETHOD_(PVOID,SystemAddress)                             \
    (   THIS                                                    \
    )   PURE;                                                   \
    STDMETHOD_(PHYSICAL_ADDRESS,PhysicalAddress)                \
    (   THIS                                                    \
    )   PURE;                                                   \
    STDMETHOD_(PADAPTER_OBJECT,GetAdapterObject)                \
    (   THIS                                                    \
    )   PURE;                                                   \
    STDMETHOD_(void,CopyTo)                                     \
    (   THIS_                                                   \
        __inout_bcount(ByteCount)   PVOID   Destination,        \
        __in                        PVOID   Source,             \
        __in                        ULONG   ByteCount           \
    )   PURE;                                                   \
    STDMETHOD_(void,CopyFrom)                                   \
    (   THIS_                                                   \
        __inout_bcount(ByteCount)   PVOID   Destination,        \
        __in                        PVOID   Source,             \
        __in                        ULONG   ByteCount           \
    )   PURE;

#endif //!defined(DEFINE_ABSTRACT_DMACHANNEL)

//
// Take out definitions of IDmaChannelSlave.
//
#if (NTDDI_VERSION < NTDDI_VISTA)
#if !defined(DEFINE_ABSTRACT_DMACHANNELSLAVE)

#define DEFINE_ABSTRACT_DMACHANNELSLAVE()                       \
    STDMETHOD_(NTSTATUS,Start)                                  \
    (   THIS_                                                   \
        __in      ULONG               MapSize,                  \
        __in      BOOLEAN             WriteToDevice             \
    )   PURE;                                                   \
    STDMETHOD_(NTSTATUS,Stop)                                   \
    (   THIS                                                    \
    )   PURE;                                                   \
    STDMETHOD_(ULONG,ReadCounter)                               \
    (   THIS                                                    \
    )   PURE;                                                   \
    STDMETHOD_(NTSTATUS,WaitForTC)                              \
    (   THIS_                                                   \
        ULONG Timeout                                           \
    )   PURE;

#endif //!defined(DEFINE_ABSTRACT_DMACHANNELSLAVE)
#endif

#if !defined(DEFINE_ABSTRACT_DRMPORT)

#define DEFINE_ABSTRACT_DRMPORT()                              \
    STDMETHOD_(NTSTATUS,CreateContentMixed)                    \
    (   THIS_                                                  \
        __in  PULONG      paContentId,                         \
        __in  ULONG       cContentId,                          \
        __out PULONG      pMixedContentId                      \
    )   PURE;                                                  \
    STDMETHOD_(NTSTATUS,DestroyContent)                        \
    (   THIS_                                                  \
        __in ULONG        ContentId                            \
    )   PURE;                                                  \
    STDMETHOD_(NTSTATUS,ForwardContentToFileObject)            \
    (   THIS_                                                  \
        __in ULONG        ContentId,                           \
        __in PFILE_OBJECT FileObject                           \
    )   PURE;                                                  \
    STDMETHOD_(NTSTATUS,ForwardContentToInterface)             \
    (   THIS_                                                  \
        __in ULONG        ContentId,                           \
        __in PUNKNOWN     pUnknown,                            \
        __in ULONG        NumMethods                           \
    )   PURE;                                                  \
    STDMETHOD_(NTSTATUS,GetContentRights)                      \
    (   THIS_                                                  \
        __in  ULONG       ContentId,                           \
        __out PDRMRIGHTS  DrmRights                            \
    )   PURE;

#endif //!defined(DEFINE_ABSTRACT_DRMPORT)

#if !defined(DEFINE_ABSTRACT_MINIPORTWAVERTSTREAM)

#define DEFINE_ABSTRACT_MINIPORTWAVERTSTREAM()                 \
    STDMETHOD_(NTSTATUS,SetFormat)                             \
    (   THIS_                                                  \
        __in      PKSDATAFORMAT   DataFormat                   \
    )   PURE;                                                  \
    STDMETHOD_(NTSTATUS,SetState)                              \
    (   THIS_                                                  \
        __in      KSSTATE         State                        \
    )   PURE;                                                  \
    STDMETHOD_(NTSTATUS,GetPosition)                           \
    (   THIS_                                                  \
        __out     PKSAUDIO_POSITION   Position                 \
    )   PURE;                                                  \
    STDMETHOD_(NTSTATUS,AllocateAudioBuffer)                   \
    (   THIS_                                                  \
        __in  ULONG                   RequestedSize,           \
        __out PMDL                    *AudioBufferMdl,         \
        __out ULONG                   *ActualSize,             \
        __out ULONG                   *OffsetFromFirstPage,    \
        __out MEMORY_CACHING_TYPE     *CacheType               \
    ) PURE;                                                    \
    STDMETHOD_(VOID,FreeAudioBuffer)                           \
    (   THIS_                                                  \
        __in_opt PMDL                    AudioBufferMdl,          \
        __in     ULONG                   BufferSize               \
    ) PURE;                                                    \
    STDMETHOD_(VOID,GetHWLatency)                              \
    (   THIS_                                                  \
        __out KSRTAUDIO_HWLATENCY     *hwLatency               \
    ) PURE;                                                    \
    STDMETHOD_(NTSTATUS,GetPositionRegister)                   \
    (   THIS_                                                  \
        __out KSRTAUDIO_HWREGISTER    *Register                \
    ) PURE;                                                    \
    STDMETHOD_(NTSTATUS,GetClockRegister)                      \
    (   THIS_                                                  \
        __out KSRTAUDIO_HWREGISTER    *Register                \
    ) PURE;

#endif //!defined(DEFINE_ABSTRACT_MINIPORTWAVERTSTREAM)

#if !defined(DEFINE_ABSTRACT_ADAPTERPOWERMANAGEMENT)

#define DEFINE_ABSTRACT_ADAPTERPOWERMANAGEMENT()                \
    STDMETHOD_(void,PowerChangeState)                           \
    (   THIS_                                                   \
        __in      POWER_STATE     NewState                      \
    )   PURE;                                                   \
    STDMETHOD_(NTSTATUS,QueryPowerChangeState)                  \
    (   THIS_                                                   \
        __in      POWER_STATE     NewStateQuery                 \
    )   PURE;                                                   \
    STDMETHOD_(NTSTATUS,QueryDeviceCapabilities)                \
    (   THIS_                                                   \
        __in      PDEVICE_CAPABILITIES    PowerDeviceCaps       \
    )   PURE;

#endif //!defined(DEFINE_ABSTRACT_ADAPTERPOWERMANAGEMENT)

/*****************************************************************************
 * IResourceList
 *****************************************************************************
 * List of resources.
 */
DECLARE_INTERFACE_(IResourceList,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(ULONG,NumberOfEntries)
    (   THIS
    )   PURE;

    STDMETHOD_(ULONG,NumberOfEntriesOfType)
    (   THIS_
        __in      CM_RESOURCE_TYPE    Type
    )   PURE;

    STDMETHOD_(PCM_PARTIAL_RESOURCE_DESCRIPTOR,FindTranslatedEntry)
    (   THIS_
        __in      CM_RESOURCE_TYPE    Type,
        __in      ULONG               Index
    )   PURE;

    STDMETHOD_(PCM_PARTIAL_RESOURCE_DESCRIPTOR,FindUntranslatedEntry)
    (   THIS_
        __in      CM_RESOURCE_TYPE    Type,
        __in      ULONG               Index
    )   PURE;

    STDMETHOD_(NTSTATUS,AddEntry)
    (   THIS_
        __in      PCM_PARTIAL_RESOURCE_DESCRIPTOR Translated,
        __in      PCM_PARTIAL_RESOURCE_DESCRIPTOR Untranslated
    )   PURE;

    STDMETHOD_(NTSTATUS,AddEntryFromParent)
    (   THIS_
        __in      struct IResourceList *  Parent,
        __in      CM_RESOURCE_TYPE        Type,
        __in      ULONG                   Index
    )   PURE;

    STDMETHOD_(PCM_RESOURCE_LIST,TranslatedList)
    (   THIS
    )   PURE;

    STDMETHOD_(PCM_RESOURCE_LIST,UntranslatedList)
    (   THIS
    )   PURE;
};

typedef IResourceList *PRESOURCELIST;

#ifdef PC_IMPLEMENTATION
#define IMP_IResourceList\
    STDMETHODIMP_(ULONG)NumberOfEntries\
    (   void\
    );\
    STDMETHODIMP_(ULONG) NumberOfEntriesOfType\
    (   __in      CM_RESOURCE_TYPE    Type\
    );\
    STDMETHODIMP_(PCM_PARTIAL_RESOURCE_DESCRIPTOR) FindTranslatedEntry\
    (   __in      CM_RESOURCE_TYPE    Type,\
        __in      ULONG               Index\
    );\
    STDMETHODIMP_(PCM_PARTIAL_RESOURCE_DESCRIPTOR) FindUntranslatedEntry\
    (   __in      CM_RESOURCE_TYPE    Type,\
        __in      ULONG               Index\
    );\
    STDMETHODIMP_(NTSTATUS) AddEntry\
    (   __in      PCM_PARTIAL_RESOURCE_DESCRIPTOR Translated,\
        __in      PCM_PARTIAL_RESOURCE_DESCRIPTOR Untranslated\
    );\
    STDMETHODIMP_(NTSTATUS) AddEntryFromParent\
    (   __in      struct IResourceList *  Parent,\
        __in      CM_RESOURCE_TYPE        Type,\
        __in      ULONG                   Index\
    );\
    STDMETHODIMP_(PCM_RESOURCE_LIST) TranslatedList\
    (   void\
    );\
    STDMETHODIMP_(PCM_RESOURCE_LIST) UntranslatedList\
    (   void\
    )
#endif


#define NumberOfPorts()         NumberOfEntriesOfType(CmResourceTypePort)
#define FindTranslatedPort(n)   FindTranslatedEntry(CmResourceTypePort,(n))
#define FindUntranslatedPort(n) FindUntranslatedEntry(CmResourceTypePort,(n))
#define AddPortFromParent(p,n)  AddEntryFromParent((p),CmResourceTypePort,(n))

#define NumberOfInterrupts()         NumberOfEntriesOfType(CmResourceTypeInterrupt)
#define FindTranslatedInterrupt(n)   FindTranslatedEntry(CmResourceTypeInterrupt,(n))
#define FindUntranslatedInterrupt(n) FindUntranslatedEntry(CmResourceTypeInterrupt,(n))
#define AddInterruptFromParent(p,n)  AddEntryFromParent((p),CmResourceTypeInterrupt,(n))

#define NumberOfMemories()        NumberOfEntriesOfType(CmResourceTypeMemory)
#define FindTranslatedMemory(n)   FindTranslatedEntry(CmResourceTypeMemory,(n))
#define FindUntranslatedMemory(n) FindUntranslatedEntry(CmResourceTypeMemory,(n))
#define AddMemoryFromParent(p,n)  AddEntryFromParent((p),CmResourceTypeMemory,(n))

#define NumberOfDmas()         NumberOfEntriesOfType(CmResourceTypeDma)
#define FindTranslatedDma(n)   FindTranslatedEntry(CmResourceTypeDma,(n))
#define FindUntranslatedDma(n) FindUntranslatedEntry(CmResourceTypeDma,(n))
#define AddDmaFromParent(p,n)  AddEntryFromParent((p),CmResourceTypeDma,(n))

#define NumberOfDeviceSpecifics()         NumberOfEntriesOfType(CmResourceTypeDeviceSpecific)
#define FindTranslatedDeviceSpecific(n)   FindTranslatedEntry(CmResourceTypeDeviceSpecific,(n))
#define FindUntranslatedDeviceSpecific(n) FindUntranslatedEntry(CmResourceTypeDeviceSpecific,(n))
#define AddDeviceSpecificFromParent(p,n)  AddEntryFromParent((p),CmResourceTypeDeviceSpecific,(n))

#define NumberOfBusNumbers()         NumberOfEntriesOfType(CmResourceTypeBusNumber)
#define FindTranslatedBusNumber(n)   FindTranslatedEntry(CmResourceTypeBusNumber,(n))
#define FindUntranslatedBusNumber(n) FindUntranslatedEntry(CmResourceTypeBusNumber,(n))
#define AddBusNumberFromParent(p,n)  AddEntryFromParent((p),CmResourceTypeBusNumber,(n))

#define NumberOfDevicePrivates()         NumberOfEntriesOfType(CmResourceTypeDevicePrivate)
#define FindTranslatedDevicePrivate(n)   FindTranslatedEntry(CmResourceTypeDevicePrivate,(n))
#define FindUntranslatedDevicePrivate(n) FindUntranslatedEntry(CmResourceTypeDevicePrivate,(n))
#define AddDevicePrivateFromParent(p,n)  AddEntryFromParent((p),CmResourceTypeDevicePrivate,(n))

#define NumberOfAssignedResources()         NumberOfEntriesOfType(CmResourceTypeAssignedResource)
#define FindTranslatedAssignedResource(n)   FindTranslatedEntry(CmResourceTypeAssignedResource,(n))
#define FindUntranslatedAssignedResource(n) FindUntranslatedEntry(CmResourceTypeAssignedResource,(n))
#define AddAssignedResourceFromParent(p,n)  AddEntryFromParent((p),CmResourceTypeAssignedResource,(n))

#define NumberOfSubAllocateFroms()         NumberOfEntriesOfType(CmResourceTypeSubAllocateFrom)
#define FindTranslatedSubAllocateFrom(n)   FindTranslatedEntry(CmResourceTypeSubAllocateFrom,(n))
#define FindUntranslatedSubAllocateFrom(n) FindUntranslatedEntry(CmResourceTypeSubAllocateFrom,(n))
#define AddSubAllocateFromFromParent(p,n)  AddEntryFromParent((p),CmResourceTypeSubAllocateFrom,(n))

/*****************************************************************************
 * IDmaChannel
 *****************************************************************************
 * Interface for DMA channel.
 */
DECLARE_INTERFACE_(IDmaChannel,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()       //  For IUnknown

    DEFINE_ABSTRACT_DMACHANNEL()    //  For IDmaChannel
};

typedef IDmaChannel *PDMACHANNEL;

#ifdef PC_IMPLEMENTATION
#define IMP_IDmaChannel\
    STDMETHODIMP_(NTSTATUS) AllocateBuffer\
    (   __in      ULONG               BufferSize,\
        __in_opt  PPHYSICAL_ADDRESS   PhysicalAddressConstraint\
    );\
    STDMETHODIMP_(void) FreeBuffer\
    (   void\
    );\
    STDMETHODIMP_(ULONG) TransferCount\
    (   void\
    );\
    STDMETHODIMP_(ULONG) MaximumBufferSize\
    (   void\
    );\
    STDMETHODIMP_(ULONG) AllocatedBufferSize\
    (   void\
    );\
    STDMETHODIMP_(ULONG) BufferSize\
    (   void\
    );\
    STDMETHODIMP_(void) SetBufferSize\
    (   __in      ULONG   BufferSize\
    );\
    STDMETHODIMP_(PVOID) SystemAddress\
    (   void\
    );\
    STDMETHODIMP_(PHYSICAL_ADDRESS) PhysicalAddress\
    (   void\
    );\
    STDMETHODIMP_(PADAPTER_OBJECT) GetAdapterObject\
    (   void\
    );\
    STDMETHODIMP_(void) CopyTo\
    (   __inout_bcount(ByteCount)   PVOID   Destination,\
        __in                        PVOID   Source,\
        __in                        ULONG   ByteCount\
    );\
    STDMETHODIMP_(void) CopyFrom\
    (   __inout_bcount(ByteCount)   PVOID   Destination,\
        __in                        PVOID   Source,\
        __in                        ULONG   ByteCount\
    )
#endif

//
// For Longhorn we don't support DMA channel slave functions, but we need
// to define PDMACHANNELSLAVE so that we can compile NewSlaveDmaChannel
// which we need for forward compatibility.
//
#if (NTDDI_VERSION < NTDDI_VISTA)
/*****************************************************************************
 * IDmaChannelSlave
 *****************************************************************************
 * Interface for slave DMA channel.
 */
DECLARE_INTERFACE_(IDmaChannelSlave,IDmaChannel)
{
    DEFINE_ABSTRACT_UNKNOWN()           //  For IUnknown

    DEFINE_ABSTRACT_DMACHANNEL()        //  For IDmaChannel

    DEFINE_ABSTRACT_DMACHANNELSLAVE()   //  For IDmaChannelSlave
};

typedef IDmaChannelSlave *PDMACHANNELSLAVE;

#ifdef PC_IMPLEMENTATION
#define IMP_IDmaChannelSlave\
    IMP_IDmaChannel;\
    STDMETHODIMP_(NTSTATUS) Start\
    (   __in      ULONG               MapSize,\
        __in      BOOLEAN             WriteToDevice\
    );\
    STDMETHODIMP_(NTSTATUS) Stop\
    (   void\
    );\
    STDMETHODIMP_(ULONG) ReadCounter\
    (   void\
    );\
    STDMETHODIMP_(NTSTATUS) WaitForTC\
    (   ULONG Timeout\
    )
#endif
#else   // NTDDI_VERSION < NTDDI_VISTA
//
// This is a dummy definition for PDMACHANNELSLAVE.
//
typedef PVOID PDMACHANNELSLAVE;
#endif  // NTDDI_VERSION < NTDDI_VISTA

/*****************************************************************************
 * INTERRUPTSYNCMODE
 *****************************************************************************
 * Interrupt sync mode of operation.
 */
typedef enum
{
    InterruptSyncModeNormal = 1,    // One pass, stop when successful.
    InterruptSyncModeAll,           // One pass regardless of success.
    InterruptSyncModeRepeat         // Repeat until all return unsuccessful.
} INTERRUPTSYNCMODE;

/*****************************************************************************
 * PINTERRUPTSYNCROUTINE
 *****************************************************************************
 * Pointer to an interrupt synchronization routine.  Both interrupt service
 * routines and routines that are synchronized with ISRs use this type.
 */
typedef NTSTATUS
(*PINTERRUPTSYNCROUTINE)
(
    __in      struct IInterruptSync * InterruptSync,
    __in      PVOID                   DynamicContext
);

/*****************************************************************************
 * IInterruptSync
 *****************************************************************************
 * Interface for objects providing access synchronization with interrupts.
 */
DECLARE_INTERFACE_(IInterruptSync,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(NTSTATUS,CallSynchronizedRoutine)
    (   THIS_
        __in      PINTERRUPTSYNCROUTINE   Routine,
        __in      PVOID                   DynamicContext
    )   PURE;
    STDMETHOD_(PKINTERRUPT,GetKInterrupt)
    (   THIS
    )   PURE;
    STDMETHOD_(NTSTATUS,Connect)
    (   THIS
    )   PURE;
    STDMETHOD_(void,Disconnect)
    (   THIS
    )   PURE;
    STDMETHOD_(NTSTATUS,RegisterServiceRoutine)
    (   THIS_
        __in      PINTERRUPTSYNCROUTINE   Routine,
        __in      PVOID                   DynamicContext,
        __in      BOOLEAN                 First
    )   PURE;
};

typedef IInterruptSync *PINTERRUPTSYNC;

#ifdef PC_IMPLEMENTATION
#define IMP_IInterruptSync\
    STDMETHODIMP_(NTSTATUS) CallSynchronizedRoutine\
    (   __in      PINTERRUPTSYNCROUTINE   Routine,\
        __in      PVOID                   DynamicContext\
    );\
    STDMETHODIMP_(PKINTERRUPT) GetKInterrupt\
    (   void\
    );\
    STDMETHODIMP_(NTSTATUS) Connect\
    (   void\
    );\
    STDMETHODIMP_(void) Disconnect\
    (   void\
    );\
    STDMETHODIMP_(NTSTATUS) RegisterServiceRoutine\
    (   __in      PINTERRUPTSYNCROUTINE   Routine,\
        __in      PVOID                   DynamicContext,\
        __in      BOOLEAN                 First\
    )
#endif

/*****************************************************************************
 * IServiceSink
 *****************************************************************************
 * Interface for notification sinks for service groups.
 */
DECLARE_INTERFACE_(IServiceSink,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    //  For IServiceSink
    STDMETHOD_(void,RequestService)
    (   THIS
    )   PURE;
};

typedef IServiceSink *PSERVICESINK;

#ifdef PC_IMPLEMENTATION
#define IMP_IServiceSink\
    STDMETHODIMP_(void) RequestService\
    (   void\
    )
#endif

/*****************************************************************************
 * IServiceGroup
 *****************************************************************************
 * Interface for objects representing a group that is serviced collectively.
 */
DECLARE_INTERFACE_(IServiceGroup,IServiceSink)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    //  For IServiceSink
    STDMETHOD_(void,RequestService)
    (   THIS
    )   PURE;

    //  For IServiceGroup
    STDMETHOD_(NTSTATUS,AddMember)
    (   THIS_
        __in      PSERVICESINK    pServiceSink
    )   PURE;

    STDMETHOD_(void,RemoveMember)
    (   THIS_
        __in      PSERVICESINK    pServiceSink
    )   PURE;

    STDMETHOD_(void,SupportDelayedService)
    (   THIS
    )   PURE;

    STDMETHOD_(void,RequestDelayedService)
    (   THIS_
        __in      ULONGLONG   ullDelay
    )   PURE;

    STDMETHOD_(void,CancelDelayedService)
    (   THIS
    )   PURE;
};

typedef IServiceGroup *PSERVICEGROUP;

#ifdef PC_IMPLEMENTATION
#define IMP_IServiceGroup\
    IMP_IServiceSink;\
    STDMETHODIMP_(NTSTATUS) AddMember\
    (   __in  PSERVICESINK    pServiceSink\
    );\
    STDMETHODIMP_(void) RemoveMember\
    (   __in  PSERVICESINK    pServiceSink\
    );\
    STDMETHODIMP_(void) SupportDelayedService\
    (   void\
    );\
    STDMETHODIMP_(void) RequestDelayedService\
    (   __in  ULONGLONG   ullDelay\
    );\
    STDMETHODIMP_(void) CancelDelayedService\
    (   void\
    )
#endif

/*****************************************************************************
 * IRegistryKey
 *****************************************************************************
 * Interface for objects providing access to a registry key.
 */
DECLARE_INTERFACE_(IRegistryKey,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(NTSTATUS,QueryKey)
    (   THIS_
        __in      KEY_INFORMATION_CLASS   KeyInformationClass,
        __out     PVOID                   KeyInformation,
        __in      ULONG                   Length,
        __out     PULONG                  ResultLength
    )   PURE;

    STDMETHOD_(NTSTATUS,EnumerateKey)
    (   THIS_
        __in      ULONG                   Index,
        __in      KEY_INFORMATION_CLASS   KeyInformationClass,
        __out     PVOID                   KeyInformation,
        __in      ULONG                   Length,
        __out     PULONG                  ResultLength
    )   PURE;

    STDMETHOD_(NTSTATUS,QueryValueKey)
    (   THIS_
        __in      PUNICODE_STRING             ValueName,
        __in      KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
        __out     PVOID                       KeyValueInformation,
        __in      ULONG                       Length,
        __out     PULONG                      ResultLength
    )   PURE;

    STDMETHOD_(NTSTATUS,EnumerateValueKey)
    (   THIS_
        __in      ULONG                       Index,
        __in      KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
        __out     PVOID                       KeyValueInformation,
        __in      ULONG                       Length,
        __out     PULONG                      ResultLength
    )   PURE;

    STDMETHOD_(NTSTATUS,SetValueKey)
    (   THIS_
        __in_opt  PUNICODE_STRING     ValueName,
        __in      ULONG               Type,
        __in      PVOID               Data,
        __in      ULONG               DataSize
    )   PURE;

    STDMETHOD_(NTSTATUS,QueryRegistryValues)
    (   THIS_
        __in      PRTL_QUERY_REGISTRY_TABLE   QueryTable,
        __in_opt  PVOID                       Context 
    )   PURE;

    STDMETHOD_(NTSTATUS,NewSubKey)
    (   THIS_
        __out     IRegistryKey **     RegistrySubKey,
        __in      PUNKNOWN            OuterUnknown,
        __in      ACCESS_MASK         DesiredAccess,
        __in      PUNICODE_STRING     SubKeyName,
        __in      ULONG               CreateOptions,
        __out_opt PULONG              Disposition     
    )   PURE;

    STDMETHOD_(NTSTATUS,DeleteKey)
    (   THIS
    )   PURE;
};

typedef IRegistryKey *PREGISTRYKEY;

#ifdef PC_IMPLEMENTATION
#define IMP_IRegistryKey\
    STDMETHODIMP_(NTSTATUS) QueryKey\
    (   __in      KEY_INFORMATION_CLASS   KeyInformationClass,\
        __out     PVOID                   KeyInformation,\
        __in      ULONG                   Length,\
        __out     PULONG                  ResultLength\
    );\
    STDMETHODIMP_(NTSTATUS) EnumerateKey\
    (   __in      ULONG                   Index,\
        __in      KEY_INFORMATION_CLASS   KeyInformationClass,\
        __out     PVOID                   KeyInformation,\
        __in      ULONG                   Length,\
        __out     PULONG                  ResultLength\
    );\
    STDMETHODIMP_(NTSTATUS) QueryValueKey\
    (   __in      PUNICODE_STRING             ValueName,\
        __in      KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,\
        __out     PVOID                       KeyValueInformation,\
        __in      ULONG                       Length,\
        __out     PULONG                      ResultLength\
    );\
    STDMETHODIMP_(NTSTATUS) EnumerateValueKey\
    (   __in      ULONG                       Index,\
        __in      KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,\
        __out     PVOID                       KeyValueInformation,\
        __in      ULONG                       Length,\
        __out     PULONG                      ResultLength\
    );\
    STDMETHODIMP_(NTSTATUS) SetValueKey\
    (   __in_opt  PUNICODE_STRING     ValueName,\
        __in      ULONG               Type,\
        __in      PVOID               Data,\
        __in      ULONG               DataSize\
    );\
    STDMETHODIMP_(NTSTATUS) QueryRegistryValues\
    (   __in      PRTL_QUERY_REGISTRY_TABLE   QueryTable,\
        __in_opt  PVOID                       Context\
    );\
    STDMETHODIMP_(NTSTATUS) NewSubKey\
    (   __out     IRegistryKey **     RegistrySubKey,\
        __in      PUNKNOWN            OuterUnknown,\
        __in      ACCESS_MASK         DesiredAccess,\
        __in      PUNICODE_STRING     SubKeyName,\
        __in      ULONG               CreateOptions,\
        __out_opt PULONG              Disposition\
    );\
    STDMETHODIMP_(NTSTATUS) DeleteKey\
    (   void\
    )
#endif

#if (NTDDI_VERSION >= NTDDI_WINXP)
/*****************************************************************************
 * IMusicTechnology
 *****************************************************************************
 * Interface for setting MusicTechnology.
 */
DECLARE_INTERFACE_(IMusicTechnology,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    //  For IMusicTechnology
    STDMETHOD_(NTSTATUS,SetTechnology)
    (   THIS_
        __in      const GUID *    Technology
    )   PURE;
};

typedef IMusicTechnology *PMUSICTECHNOLOGY;

#define IMP_IMusicTechnology\
    STDMETHODIMP_(NTSTATUS) SetTechnology\
    (   __in      const GUID *    Technology\
    )
#endif

typedef struct _PCPROPERTY_REQUEST PCPROPERTY_REQUEST, *PPCPROPERTY_REQUEST;
typedef struct _PCMETHOD_REQUEST PCMETHOD_REQUEST, *PPCMETHOD_REQUEST;
typedef struct _PCEVENT_REQUEST PCEVENT_REQUEST, *PPCEVENT_REQUEST;

/*****************************************************************************
 * PCPFNPROPERTY_HANDLER
 *****************************************************************************
 * Property handler function prototype.
 *
 * All property accesses and support queries for a given property on a given
 * filter, pin or node are routed to a single handler.  The parameter contains
 * complete information regarding the request.  The handler may return
 * STATUS_PENDING, in which case it must eventually call
 * PcCompletePendingPropertyRequest() to complete the request.
 */
typedef
NTSTATUS
(*PCPFNPROPERTY_HANDLER)
(
    __in      PPCPROPERTY_REQUEST PropertyRequest
);

/*****************************************************************************
 * PCPFNMETHOD_HANDLER
 *****************************************************************************
 * Method handler function prototype.
 *
 * All method calls and support queries for a given method on a given filter,
 * pin or node are routed to a single handler.  The parameter contains
 * complete information regarding the request.  The handler may return
 * STATUS_PENDING, in which case it must eventually call
 * PcCompletePendingMethodRequest() to complete the request.
 */
typedef
NTSTATUS
(*PCPFNMETHOD_HANDLER)
(
    __in      PPCMETHOD_REQUEST   MethodRequest
);

/*****************************************************************************
 * PCPFNEVENT_HANDLER
 *****************************************************************************
 * Event handler function prototype.
 *
 * All event add and remove requests and all event support queries for a
 * given event on a given filter, pin or node are routed to a single handler.
 * The parameter contains complete information regarding the request.  The
 * handler may return STATUS_PENDING, in which case it must eventually call
 * PcCompletePendingEventRequest() to complete the request.
 */
typedef
NTSTATUS
(*PCPFNEVENT_HANDLER)
(
    __in      PPCEVENT_REQUEST    EventRequest
);

/*****************************************************************************
 * PCPROPERTY_ITEM
 *****************************************************************************
 * Property table entry.
 *
 * A property item describes a property supported by a given filter, pin or
 * node.  The flags indicate what operations regarding the property are
 * supported and specify selected options with respect to the port's handling
 * of property requests.
 */
typedef struct
{
    const GUID *            Set;
    ULONG                   Id;
    ULONG                   Flags;
#define PCPROPERTY_ITEM_FLAG_GET            KSPROPERTY_TYPE_GET
#define PCPROPERTY_ITEM_FLAG_SET            KSPROPERTY_TYPE_SET
#define PCPROPERTY_ITEM_FLAG_BASICSUPPORT   KSPROPERTY_TYPE_BASICSUPPORT
//not supported #define PCPROPERTY_ITEM_FLAG_RELATIONS      KSPROPERTY_TYPE_RELATIONS
#define PCPROPERTY_ITEM_FLAG_SERIALIZERAW   KSPROPERTY_TYPE_SERIALIZERAW
#define PCPROPERTY_ITEM_FLAG_UNSERIALIZERAW KSPROPERTY_TYPE_UNSERIALIZERAW
#define PCPROPERTY_ITEM_FLAG_SERIALIZESIZE  KSPROPERTY_TYPE_SERIALIZESIZE
#define PCPROPERTY_ITEM_FLAG_SERIALIZE\
        (PCPROPERTY_ITEM_FLAG_SERIALIZERAW\
        |PCPROPERTY_ITEM_FLAG_UNSERIALIZERAW\
        |PCPROPERTY_ITEM_FLAG_SERIALIZESIZE\
        )
#define PCPROPERTY_ITEM_FLAG_DEFAULTVALUES  KSPROPERTY_TYPE_DEFAULTVALUES
    PCPFNPROPERTY_HANDLER   Handler;
}
PCPROPERTY_ITEM, *PPCPROPERTY_ITEM;

/*****************************************************************************
 * PCMETHOD_ITEM
 *****************************************************************************
 * Method table entry.
 *
 * A method item describes a method supported by a given filter, pin or node.
 * The flags indicate what operations regarding the method are supported and
 * specify selected options with respect to the port's handling of method
 * requests.
 */
typedef struct
{
    const GUID *            Set;
    ULONG                   Id;
    ULONG                   Flags;
#define PCMETHOD_ITEM_FLAG_NONE         KSMETHOD_TYPE_NONE
#define PCMETHOD_ITEM_FLAG_READ         KSMETHOD_TYPE_READ
#define PCMETHOD_ITEM_FLAG_WRITE        KSMETHOD_TYPE_WRITE
#define PCMETHOD_ITEM_FLAG_MODIFY       KSMETHOD_TYPE_MODIFY
#define PCMETHOD_ITEM_FLAG_SOURCE       KSMETHOD_TYPE_SOURCE
#define PCMETHOD_ITEM_FLAG_BASICSUPPORT KSMETHOD_TYPE_BASICSUPPORT
    PCPFNMETHOD_HANDLER     Handler;
}
PCMETHOD_ITEM, *PPCMETHOD_ITEM;

/*****************************************************************************
 * PCEVENT_ITEM
 *****************************************************************************
 * Event table entry.
 *
 * An event item describes an event supported by a given filter, pin or node.
 * The flags indicate what operations regarding the event are supported and
 * specify selected options with respect to the port's handling of event
 * requests.
 */
typedef struct
{
    const GUID *            Set;
    ULONG                   Id;
    ULONG                   Flags;
#define PCEVENT_ITEM_FLAG_ENABLE        KSEVENT_TYPE_ENABLE
#define PCEVENT_ITEM_FLAG_ONESHOT       KSEVENT_TYPE_ONESHOT
#define PCEVENT_ITEM_FLAG_BASICSUPPORT  KSEVENT_TYPE_BASICSUPPORT
    PCPFNEVENT_HANDLER      Handler;
}
PCEVENT_ITEM, *PPCEVENT_ITEM;

/*****************************************************************************
 * PCPROPERTY_REQUEST
 *****************************************************************************
 * Property request submitted to a property handler.
 *
 * This is the form that a property request takes.  Although the major target
 * is generic, in the case of miniports, it will be a pointer to the miniport
 * object.  Likewise, the minor target is the stream or voice if the request
 * is specific to a stream or voice.  Otherwise, the minor target is NULL.
 * If the request is targeted at a node, the Node parameter will specify which
 * one, otherwise it will be ULONG(-1).  If the target is a node, the minor
 * target may be specified to indicate the stream or voice with which the
 * targeted node instance is associated.
 */
typedef struct _PCPROPERTY_REQUEST
{
    PUNKNOWN                MajorTarget;
    PUNKNOWN                MinorTarget;
    ULONG                   Node;
    const PCPROPERTY_ITEM * PropertyItem;
    ULONG                   Verb;
    ULONG                   InstanceSize;
    PVOID                   Instance;
    ULONG                   ValueSize;
    PVOID                   Value;
    PIRP                    Irp;
}
PCPROPERTY_REQUEST, *PPCPROPERTY_REQUEST;

/*****************************************************************************
 * PCMETHOD_REQUEST
 *****************************************************************************
 * Method request submitted to a property handler.
 *
 * Comments in the description of PCPROPERTY_REQUEST regarding the target
 * fields apply to this structure as well.
 */
typedef struct _PCMETHOD_REQUEST
{
    PUNKNOWN                MajorTarget;
    PUNKNOWN                MinorTarget;
    ULONG                   Node;
    const PCMETHOD_ITEM *   MethodItem;
    ULONG                   Verb;
    // TODO
}
PCMETHOD_REQUEST, *PPCMETHOD_REQUEST;

/*****************************************************************************
 * PCEVENT_REQUEST
 *****************************************************************************
 * Event request submitted to a property handler.
 *
 * Comments in the description of PCPROPERTY_REQUEST regarding the target
 * fields apply to this structure as well.
 */
typedef struct _PCEVENT_REQUEST
{
    PUNKNOWN                MajorTarget;
    PUNKNOWN                MinorTarget;
    ULONG                   Node;
    const PCEVENT_ITEM *    EventItem;
    PKSEVENT_ENTRY          EventEntry;
    ULONG                   Verb;
    PIRP                    Irp;
}
PCEVENT_REQUEST, *PPCEVENT_REQUEST;

#define PCEVENT_VERB_NONE          0
#define PCEVENT_VERB_ADD           1
#define PCEVENT_VERB_REMOVE        2
#define PCEVENT_VERB_SUPPORT       4

/*****************************************************************************
 * PCAUTOMATION_TABLE
 *****************************************************************************
 * Master table of properties, methods and events.
 *
 * Any of the item pointers may be NULL, in which case, corresponding counts
 * must be zero.  For item tables that are not zero length, the item size must
 * not be smaller than the size of the item structure defined by port class.
 * The item size may be larger, in which case the port class item structure is
 * assumed to be followed by private data.  Item sizes must be a multiple of
 * 8.
 */
typedef struct
{
    ULONG                               PropertyItemSize;
    ULONG                               PropertyCount;
    const PCPROPERTY_ITEM * Properties;
        ULONG                           MethodItemSize;
        ULONG                           MethodCount;
        const PCMETHOD_ITEM *   Methods;
        ULONG                           EventItemSize;
        ULONG                           EventCount;
        const PCEVENT_ITEM *    Events;
    ULONG                   Reserved;
}
PCAUTOMATION_TABLE, *PPCAUTOMATION_TABLE;

#define DEFINE_PCAUTOMATION_TABLE_PROP(AutomationTable,PropertyTable)\
const PCAUTOMATION_TABLE AutomationTable =\
{\
    sizeof(PropertyTable[0]),\
    SIZEOF_ARRAY(PropertyTable),\
    (const PCPROPERTY_ITEM *) PropertyTable,\
    0,0,NULL,\
    0,0,NULL,\
    0\
}

#define DEFINE_PCAUTOMATION_TABLE_PROP_EVENT(AutomationTable,PropertyTable,EventTable)\
const PCAUTOMATION_TABLE AutomationTable =\
{\
    sizeof(PropertyTable[0]),\
    SIZEOF_ARRAY(PropertyTable),\
    (const PCPROPERTY_ITEM *) PropertyTable,\
    0,0,NULL,\
    sizeof(EventTable[0]),\
    SIZEOF_ARRAY(EventTable),\
    (const PCEVENT_ITEM *) EventTable,\
    0\
}

#define DEFINE_PCAUTOMATION_TABLE_EVENT(AutomationTable,EventTable)\
const PCAUTOMATION_TABLE AutomationTable =\
{\
    0,0,NULL,\
    0,0,NULL,\
    sizeof(EventTable[0]),\
    SIZEOF_ARRAY(EventTable),\
    (const PCEVENT_ITEM *) EventTable,\
    0\
}

/*****************************************************************************
 * PCPIN_DESCRIPTOR for IMiniport::GetDescription()
 *****************************************************************************
 * Description of a pin on the filter implemented by the miniport.
 *
 * MaxGlobalInstanceCount and MaxFilterInstanceCount may be zero to indicate
 * that the pin may not be instantiated, ULONG(-1) to indicate the pin may be
 * allocated any number of times, or any other value to indicate a specific
 * number of times the pin may be allocated.  MinFilterInstanceCount may not
 * be ULONG(-1) because it specifies a definite lower bound on the number of
 * instances of a pin that must exist in order for a filter to function.
 *
 * The KS pin descriptor may have zero interfaces and zero mediums.  The list
 * of interfaces is ignored in all cases.  The medium list will default to
 * a list containing only the standard medium (device I/O).
 *
 * The automation table pointer may be NULL indicating that no automation is
 * supported.
 */
typedef struct
{
    ULONG                       MaxGlobalInstanceCount;
    ULONG                       MaxFilterInstanceCount;
    ULONG                       MinFilterInstanceCount;
    const PCAUTOMATION_TABLE *  AutomationTable;
    KSPIN_DESCRIPTOR            KsPinDescriptor;
}
PCPIN_DESCRIPTOR, *PPCPIN_DESCRIPTOR;

/*****************************************************************************
 * PCNODE_DESCRIPTOR for IMiniport::GetDescription()
 *****************************************************************************
 * Description of a node in the filter implemented by the miniport.
 *
 * The automation table pointer may be NULL indicating that no automation is
 * supported.  The name GUID pointer may be NULL indicating that the type GUID
 * should be used to determine the node name.
 */
typedef struct
{
    ULONG                       Flags;
        const PCAUTOMATION_TABLE *  AutomationTable;
        const GUID *                Type;
        const GUID *                Name;
}
PCNODE_DESCRIPTOR, *PPCNODE_DESCRIPTOR;

/*****************************************************************************
 * PCCONNECTION_DESCRIPTOR for IMiniport::GetDescription()
 *****************************************************************************
 * Description of a node connection in the topology of the filter implemented
 * by the miniport.
 */
typedef KSTOPOLOGY_CONNECTION
PCCONNECTION_DESCRIPTOR, *PPCCONNECTION_DESCRIPTOR;

/*****************************************************************************
 * PCFILTER_DESCRIPTOR for IMiniport::GetDescription()
 *****************************************************************************
 * Description of the of the filter implemented by a miniport, including
 * pins, nodes, connections and properties.
 *
 * The version number should be zero.
 */
typedef struct
{
    ULONG                           Version;
        const PCAUTOMATION_TABLE *      AutomationTable;
    ULONG                           PinSize;
    ULONG                           PinCount;
    const PCPIN_DESCRIPTOR *        Pins;
    ULONG                           NodeSize;
    ULONG                           NodeCount;
    const PCNODE_DESCRIPTOR *       Nodes;
    ULONG                           ConnectionCount;
    const PCCONNECTION_DESCRIPTOR * Connections;
    ULONG                           CategoryCount;
    const GUID *                    Categories;
}
PCFILTER_DESCRIPTOR, *PPCFILTER_DESCRIPTOR;

/*****************************************************************************
 * PCFILTER_NODE for IMiniport::GetTopology()
 *****************************************************************************
 * The placeholder for the FromNode or ToNode fields in connections which
 * describe connections to the filter's pins.
 */
#define PCFILTER_NODE KSFILTER_NODE

/*****************************************************************************
 * IMiniport
 *****************************************************************************
 * Interface common to all miniports.
 */
DECLARE_INTERFACE_(IMiniport,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_MINIPORT()  //  For IMiniport
};

typedef IMiniport *PMINIPORT;

#define IMP_IMiniport\
    STDMETHODIMP_(NTSTATUS) GetDescription\
    (   __out     PPCFILTER_DESCRIPTOR *  Description\
    );\
    STDMETHODIMP_(NTSTATUS) DataRangeIntersection\
    (   __in      ULONG           PinId,\
        __in      PKSDATARANGE    DataRange,\
        __in      PKSDATARANGE    MatchingDataRange,\
        __in      ULONG           OutputBufferLength,\
        __out_opt     PVOID           ResultantFormat,\
        __out     PULONG          ResultantFormatLength\
    )

/*****************************************************************************
 * IPort
 *****************************************************************************
 * Interface common to all port lower edges.
 */
DECLARE_INTERFACE_(IPort,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_PORT()      //  For IPort
};

typedef IPort *PPORT;

#ifdef PC_IMPLEMENTATION
#define IMP_IPort\
    STDMETHODIMP_(NTSTATUS) Init\
    (   __in      PDEVICE_OBJECT  DeviceObject,\
        __in_opt  PIRP            Irp,\
        __in      PUNKNOWN        UnknownMiniport,\
        __in_opt  PUNKNOWN        UnknownAdapter,\
        __in      PRESOURCELIST   ResourceList\
    );\
    STDMETHODIMP_(NTSTATUS) GetDeviceProperty\
    (   __in      DEVICE_REGISTRY_PROPERTY    DeviceProperty,\
        __in      ULONG                       BufferLength,\
        __out     PVOID                       PropertyBuffer,\
        __out     PULONG                      ResultLength\
    );\
    STDMETHODIMP_(NTSTATUS) NewRegistryKey\
    (   __out     PREGISTRYKEY *      OutRegistryKey,\
        __in_opt  PUNKNOWN            OuterUnknown,\
        __in      ULONG               RegistryKeyType,\
        __in      ACCESS_MASK         DesiredAccess,\
        __in_opt  POBJECT_ATTRIBUTES  ObjectAttributes,\
        __in_opt  ULONG               CreateOptions,\
        __out_opt PULONG              Disposition\
    )
#endif

/*****************************************************************************
 * IPortMidi
 *****************************************************************************
 * Interface for MIDI port lower edge.
 */
DECLARE_INTERFACE_(IPortMidi,IPort)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_PORT()      //  For IPort

    //  For IPortMidi
    STDMETHOD_(void,Notify)
    (   THIS_
        __in_opt  PSERVICEGROUP   ServiceGroup    
    )   PURE;

    STDMETHOD_(void,RegisterServiceGroup)
    (   THIS_
        __in      PSERVICEGROUP   ServiceGroup
    )   PURE;
};

typedef IPortMidi *PPORTMIDI;

#ifdef PC_IMPLEMENTATION
#define IMP_IPortMidi\
    IMP_IPort;\
    STDMETHODIMP_(void) Notify\
    (   __in_opt  PSERVICEGROUP   ServiceGroup\
    );\
    STDMETHODIMP_(void) RegisterServiceGroup\
    (   __in      PSERVICEGROUP   ServiceGroup\
    )
#endif

/*****************************************************************************
 * IMiniportMidiStream
 *****************************************************************************
 * Interface for MIDI miniport streams.
 */
DECLARE_INTERFACE_(IMiniportMidiStream,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(NTSTATUS,SetFormat)
    (   THIS_
        __in      PKSDATAFORMAT   DataFormat
    )   PURE;

    STDMETHOD_(NTSTATUS,SetState)
    (   THIS_
        __in      KSSTATE     State
    )   PURE;

    STDMETHOD_(NTSTATUS,Read)
    (   THIS_
        __in      PVOID       BufferAddress,
        __in      ULONG       BufferLength,
        __out     PULONG      BytesRead
    )   PURE;

    STDMETHOD_(NTSTATUS,Write)
    (   THIS_
        __in      PVOID       BufferAddress,
        __in      ULONG       BytesToWrite,
        __out     PULONG      BytesWritten
    )   PURE;
};

typedef IMiniportMidiStream *PMINIPORTMIDISTREAM;

#define IMP_IMiniportMidiStream\
    STDMETHODIMP_(NTSTATUS) SetFormat\
    (   __in      PKSDATAFORMAT   DataFormat\
    );\
    STDMETHODIMP_(NTSTATUS) SetState\
    (   __in      KSSTATE     State\
    );\
    STDMETHODIMP_(NTSTATUS) Read\
    (   __in      PVOID       BufferAddress,\
        __in      ULONG       BufferLength,\
        __out     PULONG      BytesRead\
    );\
    STDMETHODIMP_(NTSTATUS) Write\
    (   __in      PVOID       BufferAddress,\
        __in      ULONG       BytesToWrite,\
        __out     PULONG      BytesWritten\
    )

/*****************************************************************************
 * IMiniportMidi
 *****************************************************************************
 * Interface for MIDI miniports.
 */
DECLARE_INTERFACE_(IMiniportMidi,IMiniport)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_MINIPORT()  //  For IMiniport

    //  For IMiniportMidi
    STDMETHOD_(NTSTATUS,Init)
    (   THIS_
        __in      PUNKNOWN        UnknownAdapter,
        __in      PRESOURCELIST   ResourceList,
        __in      PPORTMIDI       Port,
        __out     PSERVICEGROUP * ServiceGroup
    )   PURE;

    STDMETHOD_(void,Service)
    (   THIS
    )   PURE;

    STDMETHOD_(NTSTATUS,NewStream)
    (   THIS_
        __out     PMINIPORTMIDISTREAM *   Stream,
        __in_opt  PUNKNOWN                OuterUnknown,
        __in      POOL_TYPE               PoolType,
        __in      ULONG                   Pin,
        __in      BOOLEAN                 Capture,
        __in      PKSDATAFORMAT           DataFormat,
        __out     PSERVICEGROUP *         ServiceGroup
    )   PURE;
};

typedef IMiniportMidi *PMINIPORTMIDI;

#define IMP_IMiniportMidi\
    IMP_IMiniport;\
    STDMETHODIMP_(NTSTATUS) Init\
    (   __in      PUNKNOWN        UnknownAdapter,\
        __in      PRESOURCELIST   ResourceList,\
        __in      PPORTMIDI       Port,\
        __out     PSERVICEGROUP * ServiceGroup\
    );\
    STDMETHODIMP_(void) Service\
    (   void\
    );\
    STDMETHODIMP_(NTSTATUS) NewStream\
    (   __out     PMINIPORTMIDISTREAM *   Stream,\
        __in_opt  PUNKNOWN                OuterUnknown,\
        __in      POOL_TYPE               PoolType,\
        __in      ULONG                   Pin,\
        __in      BOOLEAN                 Capture,\
        __in      PKSDATAFORMAT           DataFormat,\
        __out     PSERVICEGROUP *         ServiceGroup\
    )

/*****************************************************************************
 * IPortDMus
 *****************************************************************************
 * See DMusicKS.h
 */

/*****************************************************************************
 * IMXF
 *****************************************************************************
 * See DMusicKS.h
 */

/*****************************************************************************
 * IAllocatorMXF
 *****************************************************************************
 * See DMusicKS.h
 */

/*****************************************************************************
 * IMiniportDMus
 *****************************************************************************
 * See DMusicKS.h
 */


/*****************************************************************************
 * IPortTopology
 *****************************************************************************
 * Interface for topology port lower edge.
 */
DECLARE_INTERFACE_(IPortTopology,IPort)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_PORT()      //  For IPort
};

typedef IPortTopology *PPORTTOPOLOGY;

#ifdef PC_IMPLEMENTATION
#define IMP_IPortTopology IMP_IPort
#endif

/*****************************************************************************
 * IMiniportTopology
 *****************************************************************************
 * Interface for topology miniports.
 */
DECLARE_INTERFACE_(IMiniportTopology,IMiniport)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_MINIPORT()  //  For IMiniport

    //  For IMiniportTopology
    STDMETHOD_(NTSTATUS,Init)
    (   THIS_
        __in      PUNKNOWN                UnknownAdapter,
        __in      PRESOURCELIST           ResourceList,
        __in      PPORTTOPOLOGY           Port
    )   PURE;
};

typedef IMiniportTopology *PMINIPORTTOPOLOGY;

#define IMP_IMiniportTopology\
    IMP_IMiniport;\
    STDMETHODIMP_(NTSTATUS) Init\
    (   __in      PUNKNOWN        UnknownAdapter,\
        __in      PRESOURCELIST   ResourceList,\
        __in      PPORTTOPOLOGY   Port\
    )

/*****************************************************************************
 * IPortWaveCyclic
 *****************************************************************************
 * Interface for cyclic wave port lower edge.
 */
DECLARE_INTERFACE_(IPortWaveCyclic,IPort)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_PORT()      //  For IPort

    //  For IPortWaveCyclic
    STDMETHOD_(void,Notify)
    (   THIS_
        __in      PSERVICEGROUP   ServiceGroup
    )   PURE;

    STDMETHOD_(NTSTATUS,NewSlaveDmaChannel)
    (   THIS_
        __out     PDMACHANNELSLAVE *  DmaChannel,
        __in      PUNKNOWN            OuterUnknown,
        __in      PRESOURCELIST       ResourceList,
        __in      ULONG               DmaIndex,
        __in      ULONG               MaximumLength,
        __in      BOOLEAN             DemandMode,
        __in      DMA_SPEED           DmaSpeed
    )   PURE;

    STDMETHOD_(NTSTATUS,NewMasterDmaChannel)
    (   THIS_
        __out     PDMACHANNEL *   DmaChannel,
        __in      PUNKNOWN        OuterUnknown,
        __in_opt  PRESOURCELIST   ResourceList,
        __in      ULONG           MaximumLength,
        __in      BOOLEAN         Dma32BitAddresses,
        __in      BOOLEAN         Dma64BitAddresses,
        __in      DMA_WIDTH       DmaWidth,
        __in      DMA_SPEED       DmaSpeed
    )   PURE;
};

typedef IPortWaveCyclic *PPORTWAVECYCLIC;

#ifdef PC_IMPLEMENTATION
#define IMP_IPortWaveCyclic\
    IMP_IPort;\
    STDMETHODIMP_(void) Notify\
    (   __in      PSERVICEGROUP   ServiceGroup\
    );\
    STDMETHODIMP_(NTSTATUS) NewSlaveDmaChannel\
    (   __out     PDMACHANNELSLAVE *  DmaChannel,\
        __in      PUNKNOWN            OuterUnknown,\
        __in      PRESOURCELIST       ResourceList,\
        __in      ULONG               DmaIndex,\
        __in      ULONG               MaximumLength,\
        __in      BOOLEAN             DemandMode,\
        __in      DMA_SPEED           DmaSpeed\
    );\
    STDMETHODIMP_(NTSTATUS) NewMasterDmaChannel\
    (   __out     PDMACHANNEL *   DmaChannel,\
        __in      PUNKNOWN        OuterUnknown,\
        __in_opt  PRESOURCELIST   ResourceList,\
        __in      ULONG           MaximumLength,\
        __in      BOOLEAN         Dma32BitAddresses,\
        __in      BOOLEAN         Dma64BitAddresses,\
        __in      DMA_WIDTH       DmaWidth,\
        __in      DMA_SPEED       DmaSpeed\
    )
#endif

/*****************************************************************************
 * IMiniportWaveCyclicStream
 *****************************************************************************
 * Interface for cyclic wave miniport streams.
 */
DECLARE_INTERFACE_(IMiniportWaveCyclicStream,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(NTSTATUS,SetFormat)
    (   THIS_
        __in      PKSDATAFORMAT   DataFormat
    )   PURE;

    STDMETHOD_(ULONG,SetNotificationFreq)
    (   THIS_
        __in      ULONG           Interval,
        __out     PULONG          FrameSize
    )   PURE;

    STDMETHOD_(NTSTATUS,SetState)
    (   THIS_
        __in      KSSTATE         State
    )   PURE;

    STDMETHOD_(NTSTATUS,GetPosition)
    (   THIS_
        __out     PULONG          Position
    )   PURE;

    STDMETHOD_(NTSTATUS,NormalizePhysicalPosition)
    (   THIS_
        __inout PLONGLONG       PhysicalPosition
    )   PURE;

    STDMETHOD_(void,Silence)
    (   THIS_
        __in      PVOID           Buffer,
        __in      ULONG           ByteCount
    )   PURE;
};

typedef IMiniportWaveCyclicStream *PMINIPORTWAVECYCLICSTREAM;

#define IMP_IMiniportWaveCyclicStream\
    STDMETHODIMP_(NTSTATUS) SetFormat\
    (   __in      PKSDATAFORMAT   DataFormat\
    );\
    STDMETHODIMP_(ULONG) SetNotificationFreq\
    (   __in      ULONG           Interval,\
        __out     PULONG          FrameSize\
    );\
    STDMETHODIMP_(NTSTATUS) SetState\
    (   __in      KSSTATE         State\
    );\
    STDMETHODIMP_(NTSTATUS) GetPosition\
    (   __out     PULONG          Position\
    );\
    STDMETHODIMP_(NTSTATUS) NormalizePhysicalPosition\
    (   __inout PLONGLONG        PhysicalPosition\
    );\
    STDMETHODIMP_(void) Silence\
    (   __in      PVOID           Buffer,\
        __in      ULONG           ByteCount\
    )

/*****************************************************************************
 * IMiniportWaveCyclic
 *****************************************************************************
 * Interface for cyclic wave miniports.
 */
DECLARE_INTERFACE_(IMiniportWaveCyclic,IMiniport)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_MINIPORT()  //  For IMiniport

    //  For IMiniportWaveCyclic
    STDMETHOD_(NTSTATUS,Init)
    (   THIS_
        __in      PUNKNOWN        UnknownAdapter,
        __in      PRESOURCELIST   ResourceList,
        __in      PPORTWAVECYCLIC Port
    )   PURE;

    STDMETHOD_(NTSTATUS,NewStream)
    (   THIS_
        __out     PMINIPORTWAVECYCLICSTREAM * Stream,
        __in_opt  PUNKNOWN                    OuterUnknown,
        __in      POOL_TYPE                   PoolType,
        __in      ULONG                       Pin,
        __in      BOOLEAN                     Capture,
        __in      PKSDATAFORMAT               DataFormat,
        __out     PDMACHANNEL *               DmaChannel,
        __out     PSERVICEGROUP *             ServiceGroup
    )   PURE;
};

typedef IMiniportWaveCyclic *PMINIPORTWAVECYCLIC;

#define IMP_IMiniportWaveCyclic\
    IMP_IMiniport;\
    STDMETHODIMP_(NTSTATUS) Init\
    (   __in      PUNKNOWN        UnknownAdapter,\
        __in      PRESOURCELIST   ResourceList,\
        __in      PPORTWAVECYCLIC Port\
    );\
    STDMETHODIMP_(NTSTATUS) NewStream\
    (   __out     PMINIPORTWAVECYCLICSTREAM * Stream,\
        __in_opt  PUNKNOWN                    OuterUnknown,\
        __in      POOL_TYPE                   PoolType,\
        __in      ULONG                       Pin,\
        __in      BOOLEAN                     Capture,\
        __in      PKSDATAFORMAT               DataFormat,\
        __out     PDMACHANNEL *               DmaChannel,\
        __out     PSERVICEGROUP *             ServiceGroup\
    )

/*****************************************************************************
 * IPortWavePci
 *****************************************************************************
 * Interface for PCI wave port lower edge.
 */
DECLARE_INTERFACE_(IPortWavePci,IPort)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_PORT()      //  For IPort

    //  For IPortWavePci
    STDMETHOD_(void,Notify)
    (   THIS_
        __in      PSERVICEGROUP       ServiceGroup
    )   PURE;

    STDMETHOD_(NTSTATUS,NewMasterDmaChannel)
    (   THIS_
        __out     PDMACHANNEL *       OutDmaChannel,
        __in_opt  PUNKNOWN            OuterUnknown,
        __in      POOL_TYPE           PoolType,
        __in_opt  PRESOURCELIST       ResourceList,
        __in      BOOLEAN             ScatterGather,
        __in      BOOLEAN             Dma32BitAddresses,
        __in      BOOLEAN             Dma64BitAddresses,
        __in      BOOLEAN             IgnoreCount,
        __in      DMA_WIDTH           DmaWidth,
        __in      DMA_SPEED           DmaSpeed,
        __in      ULONG               MaximumLength,
        __in      ULONG               DmaPort
    )   PURE;
};

typedef IPortWavePci *PPORTWAVEPCI;

#ifdef PC_IMPLEMENTATION
#define IMP_IPortWavePci\
    IMP_IPort;\
    STDMETHODIMP_(void) Notify\
    (   __in      PSERVICEGROUP       ServiceGroup\
    );\
    STDMETHODIMP_(NTSTATUS) NewMasterDmaChannel\
    (   __out     PDMACHANNEL *       OutDmaChannel,\
        __in_opt  PUNKNOWN            OuterUnknown,\
        __in      POOL_TYPE           PoolType,\
        __in_opt  PRESOURCELIST       ResourceList,\
        __in      BOOLEAN             ScatterGather,\
        __in      BOOLEAN             Dma32BitAddresses,\
        __in      BOOLEAN             Dma64BitAddresses,\
        __in      BOOLEAN             IgnoreCount,\
        __in      DMA_WIDTH           DmaWidth,\
        __in      DMA_SPEED           DmaSpeed,\
        __in      ULONG               MaximumLength,\
        __in      ULONG               DmaPort\
    )
#endif

/*****************************************************************************
 * IPortWavePciStream
 *****************************************************************************
 * Interface for PCI wave port pin lower edge.
 */
DECLARE_INTERFACE_(IPortWavePciStream,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(NTSTATUS,GetMapping)
    (   THIS_
        __in      PVOID               Tag,
        __out     PPHYSICAL_ADDRESS   PhysicalAddress,
        __out     PVOID *             VirtualAddress,
        __out     PULONG              ByteCount,
        __out     PULONG              Flags
    )   PURE;

    STDMETHOD_(NTSTATUS,ReleaseMapping)
    (   THIS_
        __in      PVOID               Tag
    )   PURE;

    STDMETHOD_(NTSTATUS,TerminatePacket)
    (   THIS
    )   PURE;
};

typedef IPortWavePciStream *PPORTWAVEPCISTREAM;

#ifdef PC_IMPLEMENTATION
#define IMP_IPortWavePciStream\
    STDMETHODIMP_(NTSTATUS) GetMapping\
    (   __in      PVOID               Tag,\
        __out     PPHYSICAL_ADDRESS   PhysicalAddress,\
        __out     PVOID *             VirtualAddress,\
        __out     PULONG              ByteCount,\
        __out     PULONG              Flags\
    );\
    STDMETHODIMP_(NTSTATUS) ReleaseMapping\
    (   __in      PVOID               Tag\
    );\
    STDMETHODIMP_(NTSTATUS) TerminatePacket\
    (   void\
    )
#endif

/*****************************************************************************
 * IMiniportWavePciStream
 *****************************************************************************
 * Interface for PCI wave miniport streams.
 */
DECLARE_INTERFACE_(IMiniportWavePciStream,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(NTSTATUS,SetFormat)
    (   THIS_
        __in      PKSDATAFORMAT   DataFormat
    )   PURE;

    STDMETHOD_(NTSTATUS,SetState)
    (   THIS_
        __in      KSSTATE         State
    )   PURE;

    STDMETHOD_(NTSTATUS,GetPosition)
    (   THIS_
        __out     PULONGLONG      Position
    )   PURE;

    STDMETHOD_(NTSTATUS,NormalizePhysicalPosition)
    (
        THIS_
        __inout PLONGLONG        PhysicalPosition
    )   PURE;

    STDMETHOD_(NTSTATUS,GetAllocatorFraming)
    (
        THIS_
        __out PKSALLOCATOR_FRAMING AllocatorFraming
    ) PURE;

    STDMETHOD_(NTSTATUS,RevokeMappings)
    (   THIS_
        __in      PVOID           FirstTag,
        __in      PVOID           LastTag,
        __out     PULONG          MappingsRevoked
    )   PURE;

    STDMETHOD_(void,MappingAvailable)
    (   THIS
    )   PURE;

    STDMETHOD_(void,Service)
    (   THIS
    )   PURE;
};

typedef IMiniportWavePciStream *PMINIPORTWAVEPCISTREAM;

#define IMP_IMiniportWavePciStream\
    STDMETHODIMP_(NTSTATUS) SetFormat\
    (   __in      PKSDATAFORMAT   DataFormat\
    );\
    STDMETHODIMP_(NTSTATUS) SetState\
    (   __in      KSSTATE         State\
    );\
    STDMETHODIMP_(NTSTATUS) GetPosition\
    (   __out     PULONGLONG      Position\
    );\
    STDMETHODIMP_(NTSTATUS) NormalizePhysicalPosition\
    (   __inout PLONGLONG        PhysicalPosition\
    );\
    STDMETHODIMP_(NTSTATUS) GetAllocatorFraming\
    (   __out PKSALLOCATOR_FRAMING AllocatorFraming\
    );\
    STDMETHODIMP_(NTSTATUS) RevokeMappings\
    (   __in      PVOID           FirstTag,\
        __in      PVOID           LastTag,\
        __out     PULONG          MappingsRevoked\
    );\
    STDMETHODIMP_(void) MappingAvailable\
    (   void\
    );\
    STDMETHODIMP_(void) Service\
    (   void\
    )

/*****************************************************************************
 * IMiniportWavePci
 *****************************************************************************
 * Interface for PCI wave miniports.
 */
DECLARE_INTERFACE_(IMiniportWavePci,IMiniport)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_MINIPORT()  //  For IMiniport

    //  For IMiniportWavePci
    STDMETHOD_(NTSTATUS,Init)
    (   THIS_
        __in      PUNKNOWN            UnknownAdapter,
        __in      PRESOURCELIST       ResourceList,
        __in      PPORTWAVEPCI        Port,
        __out     PSERVICEGROUP *     ServiceGroup
    )   PURE;

    STDMETHOD_(NTSTATUS,NewStream)
    (   THIS_
        __out     PMINIPORTWAVEPCISTREAM *    Stream,
        __in_opt  PUNKNOWN                    OuterUnknown,
        __in      POOL_TYPE                   PoolType,
        __in      PPORTWAVEPCISTREAM          PortStream,
        __in      ULONG                       Pin,
        __in      BOOLEAN                     Capture,
        __in      PKSDATAFORMAT               DataFormat,
        __out     PDMACHANNEL *               DmaChannel,
        __out     PSERVICEGROUP *             ServiceGroup
    )   PURE;

    STDMETHOD_(void,Service)
    (   THIS
    )   PURE;
};

typedef IMiniportWavePci *PMINIPORTWAVEPCI;

#define IMP_IMiniportWavePci\
    IMP_IMiniport;\
    STDMETHODIMP_(NTSTATUS) Init\
    (   __in      PUNKNOWN            UnknownAdapter,\
        __in      PRESOURCELIST       ResourceList,\
        __in      PPORTWAVEPCI        Port,\
        __out     PSERVICEGROUP *     ServiceGroup\
    );\
    STDMETHODIMP_(NTSTATUS) NewStream\
    (   __out     PMINIPORTWAVEPCISTREAM *    Stream,\
        __in_opt  PUNKNOWN                    OuterUnknown,\
        __in      POOL_TYPE                   PoolType,\
        __in      PPORTWAVEPCISTREAM          PortStream,\
        __in      ULONG                       Pin,\
        __in      BOOLEAN                     Capture,\
        __in      PKSDATAFORMAT               DataFormat,\
        __out     PDMACHANNEL *               DmaChannel,\
        __out     PSERVICEGROUP *             ServiceGroup\
    );\
    STDMETHODIMP_(void) Service\
    (   void\
    )

#if (NTDDI_VERSION >= NTDDI_VISTA)
/*****************************************************************************
 * IPortWaveRT
 *****************************************************************************
 * Interface for WaveRT port lower edge.
 */
DECLARE_INTERFACE_(IPortWaveRT,IPort)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_PORT()      //  For IPort
};

typedef IPortWaveRT *PPORTWAVERT;

#ifdef PC_IMPLEMENTATION
#define IMP_IPortWaveRT\
    IMP_IPort
#endif

/*****************************************************************************
 * IPortWaveRTStream
 *****************************************************************************
 * Interface for WaveRT stream port lower edge.
 */
DECLARE_INTERFACE_(IPortWaveRTStream,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(PMDL, AllocatePagesForMdl)
    (   THIS_
        __in      PHYSICAL_ADDRESS    HighAddress,
        __in      SIZE_T              TotalBytes
    )   PURE;

    STDMETHOD_(PMDL, AllocateContiguousPagesForMdl)
    (   THIS_
        __in      PHYSICAL_ADDRESS    LowAddress,
        __in      PHYSICAL_ADDRESS    HighAddress,
        __in      SIZE_T              TotalBytes
    )   PURE;

    STDMETHOD_(PVOID, MapAllocatedPages)
    (   THIS_
        __in      PMDL                    MemoryDescriptorList,
        __in      MEMORY_CACHING_TYPE     CacheType
    )   PURE;

    STDMETHOD_(VOID, UnmapAllocatedPages)
    (   THIS_
        __in      PVOID   BaseAddress,
        __in      PMDL    MemoryDescriptorList
    )   PURE;

    STDMETHOD_(VOID, FreePagesFromMdl)
    (   THIS_
        __in      PMDL    MemoryDescriptorList
    )   PURE;

    STDMETHOD_(ULONG, GetPhysicalPagesCount)
    (   THIS_
        __in      PMDL    MemoryDescriptorList
    )   PURE;

    STDMETHOD_(PHYSICAL_ADDRESS, GetPhysicalPageAddress)
    (   THIS_
        __in      PMDL    MemoryDescriptorList,
        __in      ULONG   Index
    )   PURE;
};

typedef IPortWaveRTStream *PPORTWAVERTSTREAM;

#ifdef PC_IMPLEMENTATION
#define IMP_IPortWaveRTStream\
    STDMETHODIMP_(PMDL) AllocatePagesForMdl\
    (\
        __in      PHYSICAL_ADDRESS    HighAddress,\
        __in      SIZE_T              TotalBytes\
    );\
    STDMETHODIMP_(PMDL) AllocateContiguousPagesForMdl\
    (\
        __in      PHYSICAL_ADDRESS    LowAddress,\
        __in      PHYSICAL_ADDRESS    HighAddress,\
        __in      SIZE_T              TotalBytes\
    );\
    STDMETHODIMP_(PVOID) MapAllocatedPages\
    (\
        __in      PMDL                    MemoryDescriptorList,\
        __in      MEMORY_CACHING_TYPE     CacheType\
    );\
    STDMETHODIMP_(VOID) UnmapAllocatedPages\
    (\
        __in      PVOID   BaseAddress,\
        __in      PMDL    MemoryDescriptorList\
    );\
    STDMETHODIMP_(VOID) FreePagesFromMdl\
    (\
        __in      PMDL    MemoryDescriptorList\
    );\
    STDMETHODIMP_(ULONG) GetPhysicalPagesCount\
    (\
        __in      PMDL    MemoryDescriptorList\
    );\
    STDMETHODIMP_(PHYSICAL_ADDRESS) GetPhysicalPageAddress\
    (\
        __in      PMDL    MemoryDescriptorList,\
        __in      ULONG   Index\
    )
#endif

/*****************************************************************************
 * IMiniportWaveRTStream
 *****************************************************************************
 * Interface for WaveRT miniport stream.
 */
DECLARE_INTERFACE_(IMiniportWaveRTStream,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown
    DEFINE_ABSTRACT_MINIPORTWAVERTSTREAM()   //  For IMiniportWaveRTStream
};

typedef IMiniportWaveRTStream *PMINIPORTWAVERTSTREAM;

#define IMP_IMiniportWaveRTStream\
    STDMETHODIMP_(NTSTATUS) SetFormat\
    (   __in      PKSDATAFORMAT   DataFormat\
    );\
    STDMETHODIMP_(NTSTATUS) SetState\
    (   __in      KSSTATE         State\
    );\
    STDMETHODIMP_(NTSTATUS) GetPosition\
    (   __out     PKSAUDIO_POSITION   Position\
    );\
    STDMETHODIMP_(NTSTATUS) AllocateAudioBuffer\
    (\
        __in      ULONG                   RequestedSize,\
        __out     PMDL                    *AudioBufferMdl,\
        __out     ULONG                   *ActualSize,\
        __out     ULONG                   *OffsetFromFirstPage,\
        __out     MEMORY_CACHING_TYPE     *CacheType\
    );\
    STDMETHODIMP_(VOID) FreeAudioBuffer\
    (\
        __in_opt  PMDL                    AudioBufferMdl,\
        __in  ULONG                   BufferSize\
    );\
    STDMETHODIMP_(VOID) GetHWLatency\
    (\
        __out KSRTAUDIO_HWLATENCY     *hwLatency\
    );\
    STDMETHODIMP_(NTSTATUS) GetPositionRegister\
    (\
        __out KSRTAUDIO_HWREGISTER    *Register\
    );\
    STDMETHODIMP_(NTSTATUS) GetClockRegister\
    (\
        __out KSRTAUDIO_HWREGISTER    *Register\
    )


/*****************************************************************************
 * IMiniportWaveRTStreamNotification
 *****************************************************************************
 * Interface for WaveRT miniport stream buffer notification extensions.
 */
DECLARE_INTERFACE_(IMiniportWaveRTStreamNotification,IMiniportWaveRTStream)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_MINIPORTWAVERTSTREAM()  //  For IMiniportWaveRTStream

    STDMETHOD_(NTSTATUS,AllocateBufferWithNotification)
    (   THIS_
        __in      ULONG                   NotificationCount,
        __in      ULONG                   RequestedSize,
        __out     PMDL                    *AudioBufferMdl,
        __out     ULONG                   *ActualSize,
        __out     ULONG                   *OffsetFromFirstPage,
        __out     MEMORY_CACHING_TYPE     *CacheType
    )   PURE;

    STDMETHOD_(VOID,FreeBufferWithNotification)
    (   THIS_
        __in      PMDL            AudioBufferMdl,
        __in      ULONG           BufferSize
    )   PURE;

    STDMETHOD_(NTSTATUS,RegisterNotificationEvent)
    (   THIS_
        __in      PKEVENT         NotificationEvent
    )   PURE;

    STDMETHOD_(NTSTATUS,UnregisterNotificationEvent)
    (   THIS_
        __in      PKEVENT         NotificationEvent
    )   PURE;
};

typedef IMiniportWaveRTStreamNotification *PMINIPORTWAVERTSTREAMNOTIFICATION;

#define IMP_IMiniportWaveRTStreamNotification\
    STDMETHODIMP_(NTSTATUS) AllocateBufferWithNotification\
    (\
        __in      ULONG                   NotificationCount,\
        __in      ULONG                   RequestedSize,\
        __out     PMDL                    *AudioBufferMdl,\
        __out     ULONG                   *ActualSize,\
        __out     ULONG                   *OffsetFromFirstPage,\
        __out     MEMORY_CACHING_TYPE     *CacheType\
    );\
    STDMETHODIMP_(VOID) FreeBufferWithNotification\
    (\
        __in      PMDL                    AudioBufferMdl,\
        __in      ULONG                   BufferSize\
    );\
    STDMETHODIMP_(NTSTATUS) RegisterNotificationEvent\
    (\
        __in      PKEVENT                 NotificationEvent\
    );\
    STDMETHODIMP_(NTSTATUS) UnregisterNotificationEvent\
    (\
        __in      PKEVENT                 NotificationEvent\
    )

/*****************************************************************************
 * IMiniportWaveRT
 *****************************************************************************
 * Interface for WaveRT miniports.
 */
DECLARE_INTERFACE_(IMiniportWaveRT,IMiniport)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_MINIPORT()  //  For IMiniport

    //  For IMiniportWaveRT
    STDMETHOD_(NTSTATUS,Init)
    (   THIS_
        __in      PUNKNOWN            UnknownAdapter,
        __in      PRESOURCELIST       ResourceList,
        __in      PPORTWAVERT             Port
    )   PURE;

    STDMETHOD_(NTSTATUS,NewStream)
    (   THIS_
        __out     PMINIPORTWAVERTSTREAM *         Stream,
        __in      PPORTWAVERTSTREAM               PortStream,
        __in      ULONG                       Pin,
        __in      BOOLEAN                     Capture,
        __in      PKSDATAFORMAT               DataFormat
    )   PURE;

    STDMETHOD_(NTSTATUS,GetDeviceDescription)
    (   THIS_
        __out     PDEVICE_DESCRIPTION     DeviceDescription
    )   PURE;
};

typedef IMiniportWaveRT *PMINIPORTWAVERT;

#define IMP_IMiniportWaveRT\
    IMP_IMiniport;\
    STDMETHODIMP_(NTSTATUS) Init\
    (   __in      PUNKNOWN            UnknownAdapter,\
        __in      PRESOURCELIST       ResourceList,\
        __in      PPORTWAVERT             Port\
    );\
    STDMETHODIMP_(NTSTATUS) NewStream\
    (   __out     PMINIPORTWAVERTSTREAM *         Stream,\
        __in      PPORTWAVERTSTREAM               PortStream,\
        __in      ULONG                       Pin,\
        __in      BOOLEAN                     Capture,\
        __in      PKSDATAFORMAT               DataFormat\
    );\
    STDMETHODIMP_(NTSTATUS) GetDeviceDescription\
    (   __out     PDEVICE_DESCRIPTION     DeviceDescription\
    )
#endif

/*****************************************************************************
 * IAdapterPowerManagement
 *****************************************************************************
 * An interface that adapters should implement and
 * register if they want power management messages.
 * Register this interface with PortCls via the
 * PcRegisterAdapterPowerManagement() call.
 *
 * NOTE: If you want to fill in the caps struct
 * for your device, register the interface
 * with PortCls in or before your AddDevice()
 * function. The OS queries devices before
 * StartDevice() gets called.
 */
DECLARE_INTERFACE_(IAdapterPowerManagement,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    // Called by PortCls to tell the device
    // to change to the new power state.
    //
    STDMETHOD_(void,PowerChangeState)
    (   THIS_
        __in      POWER_STATE     NewState
    )   PURE;

    // Called by PortCls to ask whether the device
    // can change to the requested power state.
    //
    STDMETHOD_(NTSTATUS,QueryPowerChangeState)
    (   THIS_
        __in      POWER_STATE     NewStateQuery
    )   PURE;

    // Called by PortCls to get the power management
    // capabilities of the device. See ACPI documentation
    // for data about the DEVICE_CAPABILITIES struct.
    //
    STDMETHOD_(NTSTATUS,QueryDeviceCapabilities)
    (   THIS_
        __in      PDEVICE_CAPABILITIES    PowerDeviceCaps
    )   PURE;
};

typedef IAdapterPowerManagement *PADAPTERPOWERMANAGEMENT;

#define IMP_IAdapterPowerManagement\
    STDMETHODIMP_(void) PowerChangeState\
    (   __in      POWER_STATE     NewState\
    );\
    STDMETHODIMP_(NTSTATUS) QueryPowerChangeState\
    (   __in      POWER_STATE     NewStateQuery\
    );\
    STDMETHODIMP_(NTSTATUS) QueryDeviceCapabilities\
    (   __in      PDEVICE_CAPABILITIES    PowerDeviceCaps\
    )

/*****************************************************************************
 * IAdapterPowerManagement2
 *****************************************************************************
 * An interface that adapters should implement and
 * register if they want power management messages.
 * Register this interface with PortCls via the
 * PcRegisterAdapterPowerManagement() call.
 *
 * NOTE: If you want to fill in the caps struct
 * for your device, register the interface
 * with PortCls in or before your AddDevice()
 * function. The OS queries devices before
 * StartDevice() gets called.
 */
DECLARE_INTERFACE_(IAdapterPowerManagement2,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown
    DEFINE_ABSTRACT_ADAPTERPOWERMANAGEMENT()

    // Called by PortCls to tell the device
    // to change to the new power state.
    //
    STDMETHOD_(void,PowerChangeState2)
    (   THIS_
        __in      DEVICE_POWER_STATE     NewDeviceState,
        __in      SYSTEM_POWER_STATE     NewSystemState
    )   PURE;
};

typedef IAdapterPowerManagement2 *PADAPTERPOWERMANAGEMENT2;

#define IMP_IAdapterPowerManagement2\
    IMP_IAdapterPowerManagement;\
    STDMETHODIMP_(void) PowerChangeState2\
    (   __in      DEVICE_POWER_STATE     NewDeviceState,\
        __in      SYSTEM_POWER_STATE     NewSystemState\
    );\

/*****************************************************************************
 * IPowerNotify
 *****************************************************************************
 * An OPTIONAL interface for miniports and pins to implement to
 * enable them to get device power state change notifications.
 */
DECLARE_INTERFACE_(IPowerNotify,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    // Called by the port to notify registered miniports
    // and pins of device power state changes, so that
    // appropriate context save/restore can take place.
    //
    STDMETHOD_(void,PowerChangeNotify)
    (   THIS_
        __in      POWER_STATE     PowerState
    )   PURE;
};

typedef IPowerNotify *PPOWERNOTIFY;

#define IMP_IPowerNotify\
    STDMETHODIMP_(void) PowerChangeNotify\
    (   __in  POWER_STATE     PowerState\
    )

#if (NTDDI_VERSION >= NTDDI_WINXP)
/*****************************************************************************
 * IPinCount
 *****************************************************************************
 * An OPTIONAL interface for miniports to implement to
 * enable them to get pin count queries, for dynamic pin counts.
 */
DECLARE_INTERFACE_(IPinCount,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    // Called by the port to notify registered miniports
    // of pin count queries, so that appropriate pin
    // count manipulation can take place.
    //
    STDMETHOD_(void,PinCount)
    (   THIS_
        __in      ULONG   PinId,
        __inout  PULONG  FilterNecessary,
        __inout  PULONG  FilterCurrent,
        __inout  PULONG  FilterPossible,
        __inout  PULONG  GlobalCurrent,
        __inout  PULONG  GlobalPossible
    )   PURE;
};

typedef IPinCount *PPINCOUNT;

#define IMP_IPinCount                       \
    STDMETHODIMP_(void) PinCount            \
    (   __in      ULONG   PinId,            \
        __inout  PULONG  FilterNecessary,   \
        __inout  PULONG  FilterCurrent,     \
        __inout  PULONG  FilterPossible,    \
        __inout  PULONG  GlobalCurrent,     \
        __inout  PULONG  GlobalPossible     \
    )
#endif

/*****************************************************************************
 * IPortEvents
 *****************************************************************************
 * An interface implemented by ports to provide
 * notification event helpers to miniports.
 */
DECLARE_INTERFACE_(IPortEvents,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(void,AddEventToEventList)
    (   THIS_
        __in  PKSEVENT_ENTRY      EventEntry
    )   PURE;
    STDMETHOD_(void,GenerateEventList)
    (   THIS_
        __in_opt GUID*   Set,
        __in     ULONG   EventId,
        __in     BOOL    PinEvent,
        __in     ULONG   PinId,
        __in     BOOL    NodeEvent,
        __in     ULONG   NodeId
    )   PURE;
};

typedef IPortEvents *PPORTEVENTS;

#define IMP_IPortEvents\
    STDMETHODIMP_(void) AddEventToEventList\
    (   __in  PKSEVENT_ENTRY  EventEntry\
    );\
    STDMETHODIMP_(void) GenerateEventList\
    (   __in_opt    GUID*   Set,\
        __in        ULONG   EventId,\
        __in        BOOL    PinEvent,\
        __in        ULONG   PinId,\
        __in        BOOL    NodeEvent,\
        __in        ULONG   NodeId\
    )

#if (NTDDI_VERSION >= NTDDI_WINXP)
/*****************************************************************************
 * IDrmPort
 *****************************************************************************
 * An optional interface implemented by ports
 * to provide DRM functionality to miniports.
 */
DECLARE_INTERFACE_(IDrmPort,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_DRMPORT()   //  For IDrmPort
};

typedef IDrmPort *PDRMPORT;

#define IMP_IDrmPort\
    STDMETHODIMP_(NTSTATUS) CreateContentMixed          \
    (   __in  PULONG      paContentId,                  \
        __in  ULONG       cContentId,                   \
        __out PULONG      pMixedContentId               \
    );                                                  \
    STDMETHODIMP_(NTSTATUS) DestroyContent              \
    (   __in ULONG        ContentId                     \
    );                                                  \
    STDMETHODIMP_(NTSTATUS) ForwardContentToFileObject  \
    (   __in ULONG        ContentId,                    \
        __in PFILE_OBJECT FileObject                    \
    );                                                  \
    STDMETHODIMP_(NTSTATUS) ForwardContentToInterface   \
    (   __in ULONG        ContentId,                    \
        __in PUNKNOWN     pUnknown,                     \
        __in ULONG        NumMethods                    \
    );                                                  \
    STDMETHODIMP_(NTSTATUS) GetContentRights            \
    (   __in  ULONG       ContentId,                    \
        __out PDRMRIGHTS  DrmRights                     \
    )

/*****************************************************************************
 * IDrmPort2
 *****************************************************************************
 * An optional interface implemented by ports
 * to provide DRM functionality to miniports.
 * This is identical to IDrmPort with the
 * addition of two new routines.
 */
DECLARE_INTERFACE_(IDrmPort2,IDrmPort)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    DEFINE_ABSTRACT_DRMPORT()   //  For IDrmPort

    STDMETHOD_(NTSTATUS,AddContentHandlers)
    (   THIS_
        __in ULONG        ContentId,
        __in PVOID      * paHandlers,
        __in ULONG        NumHandlers
    )   PURE;
    STDMETHOD_(NTSTATUS,ForwardContentToDeviceObject)
    (   THIS_
        __in ULONG          ContentId,
        __in PVOID          Reserved,
        __in PCDRMFORWARD   DrmForward
    )   PURE;
};

typedef IDrmPort2 *PDRMPORT2;

#define IMP_IDrmPort2                                   \
    IMP_IDrmPort;                                       \
    STDMETHODIMP_(NTSTATUS) AddContentHandlers          \
    (   __in ULONG        ContentId,                    \
        __in PVOID      * paHandlers,                   \
        __in ULONG        NumHandlers                   \
    );                                                  \
    STDMETHODIMP_(NTSTATUS) ForwardContentToDeviceObject\
    (   __in ULONG          ContentId,                  \
        __in PVOID          Reserved,                   \
        __in PCDRMFORWARD   DrmForward                  \
    )

/*****************************************************************************
 * IPortClsVersion
 *****************************************************************************
 * What version of PortCls is this?
 */
DECLARE_INTERFACE_(IPortClsVersion,IUnknown)
{
    STDMETHOD_(DWORD,GetVersion)
    (   THIS
    )   PURE;
};

typedef IPortClsVersion *PPORTCLSVERSION;


/*****************************************************************************
 * IPortClsSubdeviceEx
 *****************************************************************************
 * Provide access to new subdevice registration APIs
 * PcRegisterSubdeviceEx and PcEnableSubDeviceInterfaces
 */
DECLARE_INTERFACE_(IPortClsSubdeviceEx,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(NTSTATUS, UpdatePinDescriptor)           
    (                                                       
        __in      ULONG             _ulPinId,
        __in      ULONG             _ulFlags,
        __in      PPCPIN_DESCRIPTOR _pPinDescriptor
    ) PURE;
};

#define PCUPDATE_PIN_DESC_FLAG_DATARANGES 0x00000001

typedef IPortClsSubdeviceEx *PPORTCLSSubdeviceEx;

/*****************************************************************************
 * IPortClsPower
 *****************************************************************************
 * Used to access the adapter power management functions.
 */
DECLARE_INTERFACE_(IPortClsPower,IUnknown)
{
    STDMETHOD_(NTSTATUS,RegisterAdapterPowerManagement)
    (   
        __in      PUNKNOWN        _pUnknown,
        __in      PDEVICE_OBJECT  _DeviceObject
    )   PURE;
    STDMETHOD_(NTSTATUS,UnregisterAdapterPowerManagement)
    (   
        __in      PDEVICE_OBJECT  _DeviceObject
    )   PURE;
    STDMETHOD_(NTSTATUS,SetIdlePowerManagement)
    (
        __in      PDEVICE_OBJECT  _DeviceObject,
        __in      BOOLEAN         _bEnabled
    )   PURE;
};

typedef IPortClsPower *PPORTCLSPOWER;

// DO NOT ASSUME THAT EACH SUCCESSIVE ENUM IMPLIES A FEATURE SUPERSET!
// Example: Win2K has more audio features than Win98SE_QFE2.
//
enum
{
    kVersionInvalid = -1,

    kVersionWin98,        // IPortClsVersion is unsupported
    kVersionWin98SE,      // IPortClsVersion is unsupported
    kVersionWin2K,        // IPortClsVersion is unsupported

    kVersionWin98SE_QFE2, // IPortClsVersion is unsupported
                          // Hotfix Package 269601 (contains 242937 and 247565)

    kVersionWin2K_SP2,    // IPortClsVersion is supported

    kVersionWinME,        // IPortClsVersion is unsupported

    kVersionWin98SE_QFE3, // IPortClsVersion is supported
                          // Hotfix Package (not yet released, as of 6/15/2001)

    kVersionWinME_QFE1,   // IPortClsVersion is supported
                          // Hotfix Package (not yet released, as of 6/15/2001)

    kVersionWinXP,        // IPortClsVersion is supported
    kVersionWinXPSP1,     // IPortClsVersion is supported

    kVersionWinServer2003,// IPortClsVersion is supported

    kVersionWin2K_UAAQFE, // IPortClsVersion is supported
    kVersionWinXP_UAAQFE, // IPortClsVersion is supported
    kVersionWinServer2003_UAAQFE, // IPortClsVersion is supported

    kVersionWindowsLonghorn  // IPortClsVersion is supported

    // Additional enum values will be added here, in
    // *roughly* chronological (not feature set) order.
};

/*****************************************************************************
 * IPortWMIRegistration
 *****************************************************************************/
DECLARE_INTERFACE_(IPortWMIRegistration,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(NTSTATUS,RegisterWMIProvider)
    (   THIS_
        PDEVICE_OBJECT, 
        PVOID
    ) PURE;
    STDMETHOD_(NTSTATUS,UnregisterWMIProvider)
    (   
        THIS_
        PDEVICE_OBJECT
    ) PURE;
};

typedef IPortWMIRegistration *PPORTWMIREGISTRATION;

/*****************************************************************************
 * IPreFetchOffset
 *****************************************************************************
 * An interface implemented by the pin to implement prefetch characteristics
 * of bus master hardware - to specify the hardware queue size, determining
 * the pad between play cursor and write cursor.
 */
DECLARE_INTERFACE_(IPreFetchOffset,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(VOID,SetPreFetchOffset)
    (   THIS_
        __in  ULONG   PreFetchOffset
    )   PURE;
};

typedef IPreFetchOffset *PPREFETCHOFFSET;

#define IMP_IPreFetchOffset\
    STDMETHODIMP_(VOID) SetPreFetchOffset\
    (\
        __in  ULONG   PreFetchOffset\
    )
#endif

#if (NTDDI_VERSION >= NTDDI_WIN2003)
/*****************************************************************************
 * IUnregisterSubdevice
 *****************************************************************************
 * An interface implemented by the port to implement a method to remove the
 * registered subdevice.
 */
DECLARE_INTERFACE_(IUnregisterSubdevice,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(NTSTATUS,UnregisterSubdevice)
    (   THIS_
        __in  PDEVICE_OBJECT  DeviceObject,
        __in  PUNKNOWN        Unknown
    )   PURE;
};

typedef IUnregisterSubdevice *PUNREGISTERSUBDEVICE;

#define IMP_IUnregisterSubdevice\
    STDMETHODIMP_(NTSTATUS) UnregisterSubdevice\
    (\
        __in  PDEVICE_OBJECT  DeviceObject,\
        __in  PUNKNOWN        Unknown\
    )

/*****************************************************************************
 * IUnregisterPhysicalConnection
 *****************************************************************************
 * An interface implemented by the port to implement a method to remove the
 * registered physical connections.
 */
DECLARE_INTERFACE_(IUnregisterPhysicalConnection,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    STDMETHOD_(NTSTATUS,UnregisterPhysicalConnection)
    (   THIS_
        __in  PDEVICE_OBJECT  DeviceObject,
        __in  PUNKNOWN        FromUnknown,
        __in  ULONG           FromPin,
        __in  PUNKNOWN        ToUnknown,
        __in  ULONG           ToPin
    )   PURE;

    STDMETHOD_(NTSTATUS,UnregisterPhysicalConnectionToExternal)
    (   THIS_
        __in  PDEVICE_OBJECT  DeviceObject,
        __in  PUNKNOWN        FromUnknown,
        __in  ULONG           FromPin,
        __in  PUNICODE_STRING ToString,
        __in  ULONG           ToPin
    )   PURE;

    STDMETHOD_(NTSTATUS,UnregisterPhysicalConnectionFromExternal)
    (   THIS_
        __in  PDEVICE_OBJECT  DeviceObject,
        __in  PUNICODE_STRING FromString,
        __in  ULONG           FromPin,
        __in  PUNKNOWN        ToUnknown,
        __in  ULONG           ToPin
    )   PURE;
};

typedef IUnregisterPhysicalConnection *PUNREGISTERPHYSICALCONNECTION;

#define IMP_IUnregisterPhysicalConnection\
    STDMETHODIMP_(NTSTATUS) UnregisterPhysicalConnection\
    (\
        __in  PDEVICE_OBJECT  DeviceObject,\
        __in  PUNKNOWN        FromUnknown,\
        __in  ULONG           FromPin,\
        __in  PUNKNOWN        ToUnknown,\
        __in  ULONG           ToPin\
    );\
    STDMETHODIMP_(NTSTATUS) UnregisterPhysicalConnectionToExternal\
    (\
        __in  PDEVICE_OBJECT  DeviceObject,\
        __in  PUNKNOWN        FromUnknown,\
        __in  ULONG           FromPin,\
        __in  PUNICODE_STRING ToString,\
        __in  ULONG           ToPin\
    );\
    STDMETHODIMP_(NTSTATUS) UnregisterPhysicalConnectionFromExternal\
    (\
        __in  PDEVICE_OBJECT  DeviceObject,\
        __in  PUNICODE_STRING FromString,\
        __in  ULONG           FromPin,\
        __in  PUNKNOWN        ToUnknown,\
        __in  ULONG           ToPin\
    )
#endif


/*****************************************************************************
 * IPinName, 
 *****************************************************************************
 * An OPTIONAL interface for miniports to implement to
 * enable them to get pin name from miniport driver
 */
DECLARE_INTERFACE_(IPinName,IUnknown)
{
    DEFINE_ABSTRACT_UNKNOWN()   //  For IUnknown

    // Called by the port to notify registered miniports
    // for servicing verndor defined pin property
    //
    STDMETHOD_(NTSTATUS,GetPinName)
    (   THIS_
        __in      PIRP            Irp,     
        __in      PKSP_PIN        Pin,
        __out     PVOID           Data
    )   PURE;
};

typedef IPinName *PIPINNAME;

#define IMP_IPinName                        \
    STDMETHODIMP_(NTSTATUS) GetPinName      \
    (       __in      PIRP            Irp,  \
            __in      PKSP_PIN        Pin,  \
            __out     PVOID           Data  \
    );


/*****************************************************************************
 * Functions.
 */

/*****************************************************************************
 * PCPFNSTARTDEVICE
 *****************************************************************************
 * Type for start device callback.
 */
typedef
NTSTATUS
(*PCPFNSTARTDEVICE)
(
#ifdef PC_OLD_NAMES
    __in      PVOID           DeviceObject,
    __in      PVOID           Irp,
#else
    __in      PDEVICE_OBJECT  DeviceObject,
    __in      PIRP            Irp,
#endif
    __in      PRESOURCELIST   ResourceList
);

#if (NTDDI_VERSION < NTDDI_WINXP)
/*****************************************************************************
 * PCPFNIRPHANDLER
 *****************************************************************************
 * Type for IRP handlers.
 */
typedef
NTSTATUS
(*PCPFNIRPHANDLER)
(
    __in      PDEVICE_OBJECT  DeviceObject,
    __in      PIRP            Irp,
    __out     PULONG          Action
#define IRP_HANDLER_ACTION_DEFAULT 0
#define IRP_HANDLER_ACTION_FINISH  1
#define IRP_HANDLER_ACTION_FORWARD 2
);
#endif

/*****************************************************************************
 * PcInitializeAdapterDriver()
 *****************************************************************************
 * Initializes an adapter driver.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcInitializeAdapterDriver
(
    __in      PDRIVER_OBJECT      DriverObject,
    __in      PUNICODE_STRING     RegistryPathName,
    __in      PDRIVER_ADD_DEVICE  AddDevice
);

/*****************************************************************************
 * PcDispatchIrp()
 *****************************************************************************
 * Dispatch an IRP.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcDispatchIrp
(
    __in      PDEVICE_OBJECT  pDeviceObject,
    __in      PIRP            pIrp
);

/*****************************************************************************
 * PcAddAdapterDevice()
 *****************************************************************************
 * Adds an adapter device.  DeviceExtensionSize may be zero for default size.
 */
PORTCLASSAPI
NTSTATUS
__drv_fun(__drv_clearDoInit(yes))
NTAPI
PcAddAdapterDevice
(
    __in      PDRIVER_OBJECT      DriverObject,
    __in      PDEVICE_OBJECT      PhysicalDeviceObject,
    __in      PCPFNSTARTDEVICE    StartDevice,
    __in      ULONG               MaxObjects,
    __in      ULONG               DeviceExtensionSize
);

/*****************************************************************************
 * PcCompleteIrp()
 *****************************************************************************
 * Complete an IRP unless status is STATUS_PENDING.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcCompleteIrp
(
    __in      PDEVICE_OBJECT  pDeviceObject,
    __in      PIRP            pIrp,
    __in      NTSTATUS        ntStatus
);

/*****************************************************************************
 * PcForwardIrpSynchronous()
 *****************************************************************************
 * Forward a PnP IRP to the PDO.  The IRP is not completed at this level,
 * this function does not return until the lower driver has completed the IRP,
 * and DecrementPendingIrpCount() is not called.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcForwardIrpSynchronous
(
    __in      PDEVICE_OBJECT  DeviceObject,
    __in      PIRP            Irp
);

/*****************************************************************************
 * PcRegisterSubdevice()
 *****************************************************************************
 * Registers a subdevice.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcRegisterSubdevice
(
    __in      PDEVICE_OBJECT  DeviceObject,
    __in      PWSTR           Name,
    __in      PUNKNOWN        Unknown
);

/*****************************************************************************
 * PcRegisterPhysicalConnection()
 *****************************************************************************
 * Registers a physical connection between subdevices.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcRegisterPhysicalConnection
(
    __in      PDEVICE_OBJECT  DeviceObject,
    __in      PUNKNOWN        FromUnknown,
    __in      ULONG           FromPin,
    __in      PUNKNOWN        ToUnknown,
    __in      ULONG           ToPin
);

/*****************************************************************************
 * PcRegisterPhysicalConnectionToExternal()
 *****************************************************************************
 * Registers a physical connection from a subdevice to an external device.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcRegisterPhysicalConnectionToExternal
(
    __in      PDEVICE_OBJECT  DeviceObject,
    __in      PUNKNOWN        FromUnknown,
    __in      ULONG           FromPin,
    __in      PUNICODE_STRING ToString,
    __in      ULONG           ToPin
);

/*****************************************************************************
 * PcRegisterPhysicalConnectionFromExternal()
 *****************************************************************************
 * Registers a physical connection to a subdevice from an external device.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcRegisterPhysicalConnectionFromExternal
(
    __in      PDEVICE_OBJECT  DeviceObject,
    __in      PUNICODE_STRING FromString,
    __in      ULONG           FromPin,
    __in      PUNKNOWN        ToUnknown,
    __in      ULONG           ToPin
);

/*****************************************************************************
 * PcNewPort()
 *****************************************************************************
 * Creates an instance of a port driver.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcNewPort
(
    __out     PPORT *     OutPort,
    __in      REFCLSID    ClassID
);

/*****************************************************************************
 * PcNewMiniport()
 *****************************************************************************
 * Creates an instance of a system-supplied miniport driver.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcNewMiniport
(
    __out     PMINIPORT * OutMiniPort,
    __in      REFCLSID    ClassID
);

#if (NTDDI_VERSION >= NTDDI_WINXP)
/*****************************************************************************
 * PcNewDmaChannel()
 *****************************************************************************
 * Creates a DMA channel.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcNewDmaChannel
(
    __out     PDMACHANNEL *       OutDmaChannel,
    __in_opt  PUNKNOWN            OuterUnknown,
    __in      POOL_TYPE           PoolType,
    __in      PDEVICE_DESCRIPTION DeviceDescription,
    __in      PDEVICE_OBJECT      DeviceObject
);
#endif

/*****************************************************************************
 * PcCompletePendingPropertyRequest()
 *****************************************************************************
 * Completes a pending property request.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcCompletePendingPropertyRequest
(
    __in      PPCPROPERTY_REQUEST PropertyRequest,
    __in      NTSTATUS            NtStatus
);

/*****************************************************************************
 * PcGetTimeInterval
 *****************************************************************************
 * Gets the system time interval
 */
PORTCLASSAPI
ULONGLONG
NTAPI
PcGetTimeInterval
(
    __in  ULONGLONG   Since
);

#define GTI_SECONDS(t)      (ULONGLONG(t)*10000000)
#define GTI_MILLISECONDS(t) (ULONGLONG(t)*10000)
#define GTI_MICROSECONDS(t) (ULONGLONG(t)*10)

/*****************************************************************************
 * PcNewResourceList()
 *****************************************************************************
 * Creates and initializes a resource list.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcNewResourceList
(
    __out     PRESOURCELIST *     OutResourceList,
    __in_opt  PUNKNOWN            OuterUnknown,
    __in      POOL_TYPE           PoolType,
    __in      PCM_RESOURCE_LIST   TranslatedResources,
    __in      PCM_RESOURCE_LIST   UntranslatedResources
);

/*****************************************************************************
 * PcNewResourceSublist()
 *****************************************************************************
 * Creates and initializes an empty resource list derived from another
 * resource list.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcNewResourceSublist
(
    __out     PRESOURCELIST *     OutResourceList,
    __in_opt  PUNKNOWN            OuterUnknown,
    __in      POOL_TYPE           PoolType,
    __in      PRESOURCELIST       ParentList,
    __in      ULONG               MaximumEntries
);

/*****************************************************************************
 * PcNewInterruptSync()
 *****************************************************************************
 * Creates and initializes an interrupt-level synchronization object.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcNewInterruptSync
(
    __out     PINTERRUPTSYNC *        OutInterruptSync,
    __in_opt  PUNKNOWN                OuterUnknown,
    __in      PRESOURCELIST           ResourceList,
    __in      ULONG                   ResourceIndex,
    __in      INTERRUPTSYNCMODE       Mode
);

/*****************************************************************************
 * PcNewServiceGroup()
 *****************************************************************************
 * Creates and initializes a service group object.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcNewServiceGroup
(
    __out     PSERVICEGROUP *     OutServiceGroup,
    __in_opt  PUNKNOWN            OuterUnknown
);

/*****************************************************************************
 * PcNewRegistryKey()
 *****************************************************************************
 * Creates and initializes a registry key object.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcNewRegistryKey
(
    __out       PREGISTRYKEY *      OutRegistryKey,
    __in_opt    PUNKNOWN            OuterUnknown,
    __in        ULONG               RegistryKeyType,
    __in        ACCESS_MASK         DesiredAccess,
    __in_opt    PVOID               DeviceObject,
    __in_opt    PVOID               SubDevice,
    __in_opt    POBJECT_ATTRIBUTES  ObjectAttributes,
    __in_opt    ULONG               CreateOptions,
    __out_opt   PULONG              Disposition    
);

/*****************************************************************************
 * RegistryKeyType for NewRegistryKey()
 *****************************************************************************
 * Enumeration of key types.
 */
enum
{
    GeneralRegistryKey,     // ObjectAttributes and CreateOptions are req'd.
    DeviceRegistryKey,      // Device Object is required
    DriverRegistryKey,      // Device Object is required
    HwProfileRegistryKey,   // Device Object is required
    DeviceInterfaceRegistryKey  // Device Object and SubDevice are required
};

/*****************************************************************************
 * PcGetDeviceProperty()
 *****************************************************************************
 * This returns the requested device property from the registry.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcGetDeviceProperty
(
    __in      PVOID                       DeviceObject,
    __in      DEVICE_REGISTRY_PROPERTY    DeviceProperty,
    __in      ULONG                       BufferLength,
    __out     PVOID                       PropertyBuffer,
    __out     PULONG                      ResultLength
);

/*****************************************************************************
 * PcRegisterAdapterPowerManagement()
 *****************************************************************************
 * Register the adapter's power management interface with PortCls.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcRegisterAdapterPowerManagement
(
    __in      PUNKNOWN    Unknown,
    __in      PVOID       pvContext1
);

/*****************************************************************************
 * PcUnregisterAdapterPowerManagement()
 *****************************************************************************
 * Unregister the adapter's power management interface with PortCls.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcUnregisterAdapterPowerManagement
(
    __in      PDEVICE_OBJECT  pDeviceObject
);

/*****************************************************************************
 * PcRequestNewPowerState()
 *****************************************************************************
 * This routine is used to request a new power state for the device.  It is
 * normally not needed by adapter drivers but is exported in order to
 * support unusual circumstances.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcRequestNewPowerState
(
    __in      PDEVICE_OBJECT      pDeviceObject,
    __in      DEVICE_POWER_STATE  RequestedNewState
);

/*****************************************************************************
 * PcRegisterIoTimeout()
 *****************************************************************************
 * This routine registers a driver-supplied callback associated with a given
 * device object (see IoInitializeTimer in the DDK).  This callback that will
 * be called approximately once per second while the device is active (see
 * IoStartTimer, and IoStopTimer in the DDK - these are called upon device
 * START and STOP).
 *
 * This routine must be called at PASSIVE_LEVEL.
 * pTimerRoutine can and will be called at DISPATCH_LEVEL; it must be non-paged.
 *
 */
__drv_maxIRQL(PASSIVE_LEVEL)
PORTCLASSAPI
NTSTATUS
NTAPI
PcRegisterIoTimeout
(
    __in      PDEVICE_OBJECT      pDeviceObject,
    __in      PIO_TIMER_ROUTINE   pTimerRoutine,
    __in      PVOID               pContext
);

/*****************************************************************************
 * PcUnregisterIoTimeout()
 *****************************************************************************
 * This routine unregisters a driver-supplied callback associated with a given
 * device object.  This callback must have been previously registered with
 * PcRegisterIoTimeout (with the same device object, timer routine and context).
 *
 * This routine must be called at PASSIVE_LEVEL.
 * pTimerRoutine can and will be called at DISPATCH_LEVEL; it must be non-paged.
 *
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcUnregisterIoTimeout
(
    __in      PDEVICE_OBJECT      pDeviceObject,
    __in      PIO_TIMER_ROUTINE   pTimerRoutine,
    __in      PVOID               pContext
);


#if (NTDDI_VERSION >= NTDDI_WINXP)
/*****************************************************************************
 * Pc DRM functions
 *****************************************************************************
 * These functions link directly to the kernel-mode Digital Rights Management
 * module.  They all must be called at PASSIVE_LEVEL.
 */
PORTCLASSAPI
NTSTATUS
NTAPI
PcAddContentHandlers
(
    __in      ULONG           ContentId,
    __in      PVOID         * paHandlers,
    __in      ULONG           NumHandlers
);

PORTCLASSAPI
NTSTATUS
NTAPI
PcCreateContentMixed
(
    __in      PULONG          paContentId,
    __in      ULONG           cContentId,
    __out     PULONG          pMixedContentId
);

PORTCLASSAPI
NTSTATUS
NTAPI
PcDestroyContent
(
    __in      ULONG           ContentId
);

PORTCLASSAPI
NTSTATUS
NTAPI
PcForwardContentToDeviceObject
(
    __in      ULONG           ContentId,
    __in      PVOID           Reserved,
    __in      PCDRMFORWARD    DrmForward
);

__drv_preferredFunction("DrmForwardContentToDeviceObject", "Obsolete") 
PORTCLASSAPI
NTSTATUS
NTAPI
PcForwardContentToFileObject
(
    __in      ULONG           ContentId,
    __in      PFILE_OBJECT    FileObject
);

PORTCLASSAPI
NTSTATUS
NTAPI
PcForwardContentToInterface
(
    __in      ULONG           ContentId,
    __in      PUNKNOWN        pUnknown,
    __in      ULONG           NumMethods
);

PORTCLASSAPI
NTSTATUS
NTAPI
PcGetContentRights
(
    __in      ULONG           ContentId,
    __out     PDRMRIGHTS      DrmRights
);
#endif

#ifdef PC_OLD_NAMES

#define InitializeAdapterDriver(c1,c2,a)        \
    PcInitializeAdapterDriver(PDRIVER_OBJECT(c1),PUNICODE_STRING(c2),PDRIVER_ADD_DEVICE(a))
#define AddAdapterDevice(c1,c2,s,m)             \
    PcAddAdapterDevice(PDRIVER_OBJECT(c1),PDEVICE_OBJECT(c2),s,m,0)
#define RegisterSubdevice(c1,c2,n,u)            \
    PcRegisterSubdevice(PDEVICE_OBJECT(c1),n,u)
#define RegisterPhysicalConnection(c1,c2,fs,fp,ts,tp) \
    PcRegisterPhysicalConnection(PDEVICE_OBJECT(c1),fs,fp,ts,tp)
#define RegisterPhysicalConnectionToExternal(c1,c2,fs,fp,ts,tp) \
    PcRegisterPhysicalConnectionToExternal(PDEVICE_OBJECT(c1),fs,fp,ts,tp)
#define RegisterPhysicalConnectionFromExternal(c1,c2,fs,fp,ts,tp) \
    PcRegisterPhysicalConnectionFromExternal(PDEVICE_OBJECT(c1),fs,fp,ts,tp)

#define NewPort                                 PcNewPort
#define NewMiniport                             PcNewMiniport
#define CompletePendingPropertyRequest          PcCompletePendingPropertyRequest

#if (NTDDI_VERSION < NTDDI_WINXP)
#define NewIrpStreamVirtual                     PcNewIrpStreamVirtual
#define NewIrpStreamPhysical                    PcNewIrpStreamPhysical
#endif

#define NewResourceList                         PcNewResourceList
#define NewResourceSublist                      PcNewResourceSublist
#define NewDmaChannel                           PcNewDmaChannel
#define NewServiceGroup                         PcNewServiceGroup
#define GetTimeInterval                         PcGetTimeInterval

#define WIN95COMPAT_ReadPortUChar(Port)         READ_PORT_UCHAR(Port)
#define WIN95COMPAT_WritePortUChar(Port,Value)  WRITE_PORT_UCHAR(Port,Value)
#define WIN95COMPAT_ReadPortUShort(Port)        READ_PORT_USHORT(Port)
#define WIN95COMPAT_WritePortUShort(Port,Value) WRITE_PORT_USHORT(Port,Value)

#endif  //PC_OLD_NAMES



#endif //_PORTCLS_H_

