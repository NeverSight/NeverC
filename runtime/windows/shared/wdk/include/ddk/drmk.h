#ifndef _DRMK_H_
#define _DRMK_H_

#ifdef __cplusplus
extern "C"
{
#endif
    
	
typedef struct tagDRMRIGHTS {
    BOOL  CopyProtect;
    ULONG Reserved;
    BOOL  DigitalOutputDisable;
} DRMRIGHTS , *PDRMRIGHTS;
typedef const DRMRIGHTS *PCDRMRIGHTS;

#define DEFINE_DRMRIGHTS_DEFAULT(DrmRights) const DRMRIGHTS DrmRights = {FALSE, 0, FALSE}


// {1915C967-3299-48cb-A3E4-69FD1D1B306E}
DEFINE_GUID(IID_IDrmAudioStream,
	    0x1915c967, 0x3299, 0x48cb, 0xa3, 0xe4, 0x69, 0xfd, 0x1d, 0x1b, 0x30, 0x6e);

DECLARE_INTERFACE_(IDrmAudioStream, IUnknown)
{
    // IUnknown methods
    STDMETHOD_(NTSTATUS, QueryInterface)(THIS_
        __in  REFIID InterfaceId,
        __out PVOID* Interface
        ) PURE;
        
    STDMETHOD_(ULONG,AddRef)(THIS) PURE;
    
    STDMETHOD_(ULONG,Release)(THIS) PURE;
    
    // IDrmAudioStream methods                       
    STDMETHOD_(NTSTATUS,SetContentId)(THIS_
	__in ULONG ContentId,
        __in PCDRMRIGHTS DrmRights
        ) PURE;
};

typedef IDrmAudioStream *PDRMAUDIOSTREAM;

#define IMP_IDrmAudioStream\
    STDMETHODIMP_(NTSTATUS) SetContentId\
    (   __in      ULONG	    ContentId,\
        __in      PCDRMRIGHTS DrmRights\
    );

typedef struct tagDRMFORWARD {
    DWORD          Flags;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT   FileObject;
    PVOID          Context;
} DRMFORWARD, *PDRMFORWARD;
typedef const DRMFORWARD *PCDRMFORWARD;

__drv_maxIRQL(PASSIVE_LEVEL)
NTSTATUS
NTAPI
DrmAddContentHandlers(
    __in ULONG ContentId,
    __in_ecount(NumHandlers) PVOID* paHandlers,
    __in ULONG NumHandlers
    );

typedef
NTSTATUS
(NTAPI *PFNDRMADDCONTENTHANDLERS)(
    __in ULONG ContentId,
    __in PVOID* paHandlers,
    __in ULONG NumHandlers
    );

__drv_maxIRQL(PASSIVE_LEVEL)
NTSTATUS
NTAPI
DrmCreateContentMixed(
    __in PULONG paContentId,
    __in ULONG cContentId,
    __out PULONG pMixedContentId
    );

typedef
NTSTATUS
(NTAPI *PFNDRMCREATECONTENTMIXED)(
    __in PULONG paContentId,
    __in ULONG cContentId,
    __out PULONG pMixedContentId
    );

__drv_maxIRQL(PASSIVE_LEVEL)
NTSTATUS
NTAPI
DrmDestroyContent(
    __in ULONG ContentId
    );

typedef
NTSTATUS
(NTAPI *PFNDRMDESTROYCONTENT)(
    __in ULONG ContentId
    );

NTSTATUS
NTAPI
DrmForwardContentToDeviceObject(
    __in ULONG ContentId,
    __in_opt PVOID Reserved,
    __in PCDRMFORWARD DrmForward
    );

typedef
NTSTATUS
(NTAPI *PFNDRMFORWARDCONTENTTODEVICEOBJECT)(
    __in ULONG ContentId,
    __in PVOID Reserved,
    __in PCDRMFORWARD DrmForward
    );


__drv_maxIRQL(PASSIVE_LEVEL) 
__drv_preferredFunction("DrmForwardContentToDeviceObject", "Obsolete")
NTSTATUS
NTAPI
DrmForwardContentToFileObject(
    __in ULONG ContentId,
    __in PFILE_OBJECT FileObject
    );

typedef
NTSTATUS
(NTAPI *PFNDRMFORWARDCONTENTTOFILEOBJECT)(
    __in ULONG ContentId,
    __in PFILE_OBJECT FileObject
    );

__drv_maxIRQL(PASSIVE_LEVEL)
NTSTATUS
NTAPI
DrmForwardContentToInterface(
    __in ULONG ContentId,
    __in PUNKNOWN pUnknown,
    __in ULONG NumMethods);

typedef
NTSTATUS
(NTAPI *PFNDRMFORWARDCONTENTTOINTERFACE)(
    __in ULONG ContentId,
    __in PUNKNOWN pUnknown,
    __in ULONG NumMethods);

__drv_maxIRQL(PASSIVE_LEVEL)
NTSTATUS
NTAPI
DrmGetContentRights(
    __in ULONG ContentId,
    __out PDRMRIGHTS DrmRights
    );

typedef
NTSTATUS
(NTAPI *PFNDRMGETCONTENTRIGHTS)(
    __in ULONG ContentId,
    __out PDRMRIGHTS DrmRights
    );


//
// Structures for use with KSPROPERY_DRMAUDIOSTREAM_CONTENTID
//

typedef struct {
    ULONG     ContentId;
    DRMRIGHTS DrmRights;
} KSDRMAUDIOSTREAM_CONTENTID, *PKSDRMAUDIOSTREAM_CONTENTID;

typedef struct {
    KSPROPERTY                         Property;
    PVOID                              Context;
    // DRM API callback functions
    PFNDRMADDCONTENTHANDLERS            DrmAddContentHandlers;
    PFNDRMCREATECONTENTMIXED            DrmCreateContentMixed;
    PFNDRMDESTROYCONTENT                DrmDestroyContent;
    PFNDRMFORWARDCONTENTTODEVICEOBJECT  DrmForwardContentToDeviceObject;
    PFNDRMFORWARDCONTENTTOFILEOBJECT    DrmForwardContentToFileObject;
    PFNDRMFORWARDCONTENTTOINTERFACE     DrmForwardContentToInterface;
    PFNDRMGETCONTENTRIGHTS              DrmGetContentRights;
} KSP_DRMAUDIOSTREAM_CONTENTID, *PKSP_DRMAUDIOSTREAM_CONTENTID;


#ifdef __cplusplus
}
#endif

#endif


