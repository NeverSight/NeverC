

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 7.00.0555 */
/* Compiler settings for softhidusbkif.idl:
    Oicf, W1, Zp8, env=Win32 (32b run), target_arch=X86 7.00.0555 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#pragma warning( disable: 4049 )  /* more than 64k source lines */


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 500
#endif

/* verify that the <rpcsal.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCSAL_H_VERSION__
#define __REQUIRED_RPCSAL_H_VERSION__ 100
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif // __RPCNDR_H_VERSION__


#ifndef __softhidusbkif_h__
#define __softhidusbkif_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __ISoftHIDProtocolXlator_FWD_DEFINED__
#define __ISoftHIDProtocolXlator_FWD_DEFINED__
typedef interface ISoftHIDProtocolXlator ISoftHIDProtocolXlator;
#endif 	/* __ISoftHIDProtocolXlator_FWD_DEFINED__ */


#ifndef __ISoftHidUsbDevice_FWD_DEFINED__
#define __ISoftHidUsbDevice_FWD_DEFINED__
typedef interface ISoftHidUsbDevice ISoftHidUsbDevice;
#endif 	/* __ISoftHidUsbDevice_FWD_DEFINED__ */


#ifndef __ISoftUSBHidDescriptor_FWD_DEFINED__
#define __ISoftUSBHidDescriptor_FWD_DEFINED__
typedef interface ISoftUSBHidDescriptor ISoftUSBHidDescriptor;
#endif 	/* __ISoftUSBHidDescriptor_FWD_DEFINED__ */


#ifndef __ISoftUSBPhysicalDescriptorSet0_FWD_DEFINED__
#define __ISoftUSBPhysicalDescriptorSet0_FWD_DEFINED__
typedef interface ISoftUSBPhysicalDescriptorSet0 ISoftUSBPhysicalDescriptorSet0;
#endif 	/* __ISoftUSBPhysicalDescriptorSet0_FWD_DEFINED__ */


#ifndef __ISoftUSBPhysicalDescriptorSet_FWD_DEFINED__
#define __ISoftUSBPhysicalDescriptorSet_FWD_DEFINED__
typedef interface ISoftUSBPhysicalDescriptorSet ISoftUSBPhysicalDescriptorSet;
#endif 	/* __ISoftUSBPhysicalDescriptorSet_FWD_DEFINED__ */


#ifndef __ISoftUSBPhysicalDescriptorItem_FWD_DEFINED__
#define __ISoftUSBPhysicalDescriptorItem_FWD_DEFINED__
typedef interface ISoftUSBPhysicalDescriptorItem ISoftUSBPhysicalDescriptorItem;
#endif 	/* __ISoftUSBPhysicalDescriptorItem_FWD_DEFINED__ */


#ifndef __ISoftUSBPhysicalDescriptor_FWD_DEFINED__
#define __ISoftUSBPhysicalDescriptor_FWD_DEFINED__
typedef interface ISoftUSBPhysicalDescriptor ISoftUSBPhysicalDescriptor;
#endif 	/* __ISoftUSBPhysicalDescriptor_FWD_DEFINED__ */


#ifndef __ISoftUSBPhysicalDescriptorSetList_FWD_DEFINED__
#define __ISoftUSBPhysicalDescriptorSetList_FWD_DEFINED__
typedef interface ISoftUSBPhysicalDescriptorSetList ISoftUSBPhysicalDescriptorSetList;
#endif 	/* __ISoftUSBPhysicalDescriptorSetList_FWD_DEFINED__ */


#ifndef __ISoftUSBPhysicalDescriptorItemList_FWD_DEFINED__
#define __ISoftUSBPhysicalDescriptorItemList_FWD_DEFINED__
typedef interface ISoftUSBPhysicalDescriptorItemList ISoftUSBPhysicalDescriptorItemList;
#endif 	/* __ISoftUSBPhysicalDescriptorItemList_FWD_DEFINED__ */


#ifndef __ISoftUSBHidFaultInjection_FWD_DEFINED__
#define __ISoftUSBHidFaultInjection_FWD_DEFINED__
typedef interface ISoftUSBHidFaultInjection ISoftUSBHidFaultInjection;
#endif 	/* __ISoftUSBHidFaultInjection_FWD_DEFINED__ */


#ifndef __SoftHIDProtocolXlator_FWD_DEFINED__
#define __SoftHIDProtocolXlator_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftHIDProtocolXlator SoftHIDProtocolXlator;
#else
typedef struct SoftHIDProtocolXlator SoftHIDProtocolXlator;
#endif /* __cplusplus */

#endif 	/* __SoftHIDProtocolXlator_FWD_DEFINED__ */


#ifndef __SoftHidUsbDevice_FWD_DEFINED__
#define __SoftHidUsbDevice_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftHidUsbDevice SoftHidUsbDevice;
#else
typedef struct SoftHidUsbDevice SoftHidUsbDevice;
#endif /* __cplusplus */

#endif 	/* __SoftHidUsbDevice_FWD_DEFINED__ */


#ifndef __SoftUSBHidDescriptor_FWD_DEFINED__
#define __SoftUSBHidDescriptor_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBHidDescriptor SoftUSBHidDescriptor;
#else
typedef struct SoftUSBHidDescriptor SoftUSBHidDescriptor;
#endif /* __cplusplus */

#endif 	/* __SoftUSBHidDescriptor_FWD_DEFINED__ */


#ifndef __SoftUSBPhysicalDescriptor_FWD_DEFINED__
#define __SoftUSBPhysicalDescriptor_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBPhysicalDescriptor SoftUSBPhysicalDescriptor;
#else
typedef struct SoftUSBPhysicalDescriptor SoftUSBPhysicalDescriptor;
#endif /* __cplusplus */

#endif 	/* __SoftUSBPhysicalDescriptor_FWD_DEFINED__ */


#ifndef __SoftUSBPhysicalDescriptorSet0_FWD_DEFINED__
#define __SoftUSBPhysicalDescriptorSet0_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBPhysicalDescriptorSet0 SoftUSBPhysicalDescriptorSet0;
#else
typedef struct SoftUSBPhysicalDescriptorSet0 SoftUSBPhysicalDescriptorSet0;
#endif /* __cplusplus */

#endif 	/* __SoftUSBPhysicalDescriptorSet0_FWD_DEFINED__ */


#ifndef __SoftUSBPhysicalDescriptorSet_FWD_DEFINED__
#define __SoftUSBPhysicalDescriptorSet_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBPhysicalDescriptorSet SoftUSBPhysicalDescriptorSet;
#else
typedef struct SoftUSBPhysicalDescriptorSet SoftUSBPhysicalDescriptorSet;
#endif /* __cplusplus */

#endif 	/* __SoftUSBPhysicalDescriptorSet_FWD_DEFINED__ */


#ifndef __SoftUSBPhysicalDescriptorItem_FWD_DEFINED__
#define __SoftUSBPhysicalDescriptorItem_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBPhysicalDescriptorItem SoftUSBPhysicalDescriptorItem;
#else
typedef struct SoftUSBPhysicalDescriptorItem SoftUSBPhysicalDescriptorItem;
#endif /* __cplusplus */

#endif 	/* __SoftUSBPhysicalDescriptorItem_FWD_DEFINED__ */


#ifndef __SoftUSBPhysicalDescriptorSetList_FWD_DEFINED__
#define __SoftUSBPhysicalDescriptorSetList_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBPhysicalDescriptorSetList SoftUSBPhysicalDescriptorSetList;
#else
typedef struct SoftUSBPhysicalDescriptorSetList SoftUSBPhysicalDescriptorSetList;
#endif /* __cplusplus */

#endif 	/* __SoftUSBPhysicalDescriptorSetList_FWD_DEFINED__ */


#ifndef __SoftUSBPhysicalDescriptorItemList_FWD_DEFINED__
#define __SoftUSBPhysicalDescriptorItemList_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBPhysicalDescriptorItemList SoftUSBPhysicalDescriptorItemList;
#else
typedef struct SoftUSBPhysicalDescriptorItemList SoftUSBPhysicalDescriptorItemList;
#endif /* __cplusplus */

#endif 	/* __SoftUSBPhysicalDescriptorItemList_FWD_DEFINED__ */


#ifndef __SoftUSBHidFaultInjection_FWD_DEFINED__
#define __SoftUSBHidFaultInjection_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBHidFaultInjection SoftUSBHidFaultInjection;
#else
typedef struct SoftUSBHidFaultInjection SoftUSBHidFaultInjection;
#endif /* __cplusplus */

#endif 	/* __SoftUSBHidFaultInjection_FWD_DEFINED__ */


#ifdef __cplusplus
extern "C"{
#endif 



#ifndef __SOFTHIDUSBK_LIBRARY_DEFINED__
#define __SOFTHIDUSBK_LIBRARY_DEFINED__

/* library SOFTHIDUSBK */
/* [helpstringcontext][helpcontext][helpstring][helpstringdll][helpfile][version][lcid][uuid] */ 





















typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("1E4B562C-9C5D-4506-8FFA-F87CC84CF588") 
enum HID_DEVICETYPE
    {	DEVICE_TYPE_NONE	= 0,
	DEVICE_TYPE_KEYBOARD	= 1,
	DEVICE_TYPE_MOUSE	= 2
    } 	HID_DEVICETYPE;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("4486D19F-E669-4C26-8F30-30584B071AF1") 
enum HID_PHYSICAL_BIAS
    {	BIAS_NA	= 0,
	RIGHT_HAND	= 1,
	LEFT_HAND	= 2,
	BOTH_HANDS	= 3,
	EITHER_HAND	= 4
    } 	HID_PHYSICAL_BIAS;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("5B428086-ACA1-4051-A41A-F4768BDFF6D6") 
enum HID_PHYSICAL_DESIGNATOR
    {	NONE	= 0,
	HAND	= 0x1,
	EYEBALL	= 0x2,
	EYEBROW	= 0x3,
	EYELID	= 0x4,
	EAR	= 0x5,
	NOSE	= 0x6,
	MOUTH	= 0x7,
	UPPER_LIP	= 0x8,
	LOWER_LIP	= 0x9,
	JAW	= 0xa,
	NECK	= 0xb,
	UPPER_ARM	= 0xc,
	ELBOW	= 0xd,
	FOREARM	= 0xe,
	WRIST	= 0xf,
	PALM	= 0x10,
	THUMB	= 0x11,
	INDEX_FINGER	= 0x12,
	MIDDLE_FINGER	= 0x13,
	RING_FINGER	= 0x14,
	LITTLE_FINGER	= 0x15,
	HEAD	= 0x16,
	SHOULDER	= 0x17,
	HIP	= 0x18,
	WAIST	= 0x19,
	THIGH	= 0x1a,
	KNEE	= 0x1b,
	CALF	= 0x1c,
	ANKLE	= 0x1d,
	FOOT	= 0x1e,
	HEEL	= 0x1f,
	BALL_OF_FOOT	= 0x20,
	BIG_TOE	= 0x21,
	SECOND_TOE	= 0x22,
	THIRD_TOE	= 0x23,
	FOURTH_TOE	= 0x24,
	LITTLE_TOE	= 0x25,
	BROW	= 0x26,
	CHEEK	= 0x27
    } 	HID_PHYSICAL_DESIGNATOR;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("4531738B-4AAC-48E2-9B8E-F97A98AA2967") 
enum HID_PHYSICAL_QUALIFIER
    {	QUALIFIER_NA	= 0,
	RIGHT	= 1,
	LEFT	= 2,
	BOTH	= 3,
	EITHER	= 4,
	CENTER	= 5
    } 	HID_PHYSICAL_QUALIFIER;


EXTERN_C const IID LIBID_SOFTHIDUSBK;

#ifndef __ISoftHIDProtocolXlator_INTERFACE_DEFINED__
#define __ISoftHIDProtocolXlator_INTERFACE_DEFINED__

/* interface ISoftHIDProtocolXlator */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftHIDProtocolXlator;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("78C6932B-1B53-469F-9DE3-38114BE3B41E")
    ISoftHIDProtocolXlator : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceType( 
            /* [retval][out] */ __RPC__out HID_DEVICETYPE *pDevType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceType( 
            /* [in] */ HID_DEVICETYPE DevType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DSFDevice( 
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ IDSFDevice **ppiDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_HIDDescriptor( 
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBHidDescriptor **ppSoftUSBHIDDescriptor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_HIDDescriptor( 
            /* [in] */ __RPC__in_opt ISoftUSBHidDescriptor *pSoftUSBHIDDescriptor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT __stdcall putref_HIDDescriptor( 
            /* [in] */ __RPC__in_opt ISoftUSBHidDescriptor *pSoftUSBHIDDescriptor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall WriteReportDescriptor( 
            /* [in] */ __RPC__in SAFEARRAY * psaReportDescriptor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall ReadOutputReport( 
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaOutputReport) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall WriteInputReport( 
            /* [in] */ __RPC__in SAFEARRAY * psaInputReport) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall WriteFeatureReport( 
            /* [in] */ __RPC__in SAFEARRAY * psaFeatureReport) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftHIDProtocolXlatorVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftHIDProtocolXlator * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftHIDProtocolXlator * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftHIDProtocolXlator * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceType )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [retval][out] */ __RPC__out HID_DEVICETYPE *pDevType);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceType )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [in] */ HID_DEVICETYPE DevType);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DSFDevice )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ IDSFDevice **ppiDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_HIDDescriptor )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBHidDescriptor **ppSoftUSBHIDDescriptor);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_HIDDescriptor )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [in] */ __RPC__in_opt ISoftUSBHidDescriptor *pSoftUSBHIDDescriptor);
        
        /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT ( __stdcall *putref_HIDDescriptor )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [in] */ __RPC__in_opt ISoftUSBHidDescriptor *pSoftUSBHIDDescriptor);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *WriteReportDescriptor )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [in] */ __RPC__in SAFEARRAY * psaReportDescriptor);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *ReadOutputReport )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaOutputReport);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *WriteInputReport )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [in] */ __RPC__in SAFEARRAY * psaInputReport);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *WriteFeatureReport )( 
            __RPC__in ISoftHIDProtocolXlator * This,
            /* [in] */ __RPC__in SAFEARRAY * psaFeatureReport);
        
        END_INTERFACE
    } ISoftHIDProtocolXlatorVtbl;

    interface ISoftHIDProtocolXlator
    {
        CONST_VTBL struct ISoftHIDProtocolXlatorVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftHIDProtocolXlator_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftHIDProtocolXlator_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftHIDProtocolXlator_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftHIDProtocolXlator_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftHIDProtocolXlator_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftHIDProtocolXlator_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftHIDProtocolXlator_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftHIDProtocolXlator_get_DeviceType(This,pDevType)	\
    ( (This)->lpVtbl -> get_DeviceType(This,pDevType) ) 

#define ISoftHIDProtocolXlator_put_DeviceType(This,DevType)	\
    ( (This)->lpVtbl -> put_DeviceType(This,DevType) ) 

#define ISoftHIDProtocolXlator_get_DSFDevice(This,ppiDSFDevice)	\
    ( (This)->lpVtbl -> get_DSFDevice(This,ppiDSFDevice) ) 

#define ISoftHIDProtocolXlator_get_HIDDescriptor(This,ppSoftUSBHIDDescriptor)	\
    ( (This)->lpVtbl -> get_HIDDescriptor(This,ppSoftUSBHIDDescriptor) ) 

#define ISoftHIDProtocolXlator_put_HIDDescriptor(This,pSoftUSBHIDDescriptor)	\
    ( (This)->lpVtbl -> put_HIDDescriptor(This,pSoftUSBHIDDescriptor) ) 

#define ISoftHIDProtocolXlator_putref_HIDDescriptor(This,pSoftUSBHIDDescriptor)	\
    ( (This)->lpVtbl -> putref_HIDDescriptor(This,pSoftUSBHIDDescriptor) ) 

#define ISoftHIDProtocolXlator_WriteReportDescriptor(This,psaReportDescriptor)	\
    ( (This)->lpVtbl -> WriteReportDescriptor(This,psaReportDescriptor) ) 

#define ISoftHIDProtocolXlator_ReadOutputReport(This,ppsaOutputReport)	\
    ( (This)->lpVtbl -> ReadOutputReport(This,ppsaOutputReport) ) 

#define ISoftHIDProtocolXlator_WriteInputReport(This,psaInputReport)	\
    ( (This)->lpVtbl -> WriteInputReport(This,psaInputReport) ) 

#define ISoftHIDProtocolXlator_WriteFeatureReport(This,psaFeatureReport)	\
    ( (This)->lpVtbl -> WriteFeatureReport(This,psaFeatureReport) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftHIDProtocolXlator_INTERFACE_DEFINED__ */


#ifndef __ISoftHidUsbDevice_INTERFACE_DEFINED__
#define __ISoftHidUsbDevice_INTERFACE_DEFINED__

/* interface ISoftHidUsbDevice */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftHidUsbDevice;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("45332C25-A9E0-457B-AA5D-897A36B92ACB")
    ISoftHidUsbDevice : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_PhysicalDescriptor( 
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBPhysicalDescriptor **ppSoftUSBPhyDescriptor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_PhysicalDescriptor( 
            /* [in] */ __RPC__in_opt ISoftUSBPhysicalDescriptor *pSoftUSBPhyDescriptor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT __stdcall putref_PhysicalDescriptor( 
            /* [in] */ __RPC__in_opt ISoftUSBPhysicalDescriptor *pSoftUSBPhyDescriptor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_ReportDescriptor( 
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaSoftUSBReportDescriptor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_ReportDescriptor( 
            /* [in] */ __RPC__in SAFEARRAY * psaSoftUSBReportDescriptor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall QueueInputReport( 
            /* [in] */ ULONG cbData,
            /* [in] */ __RPC__in BYTE *pbData) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall DequeueOutputReport( 
            /* [out] */ __RPC__out ULONG *pcbData,
            /* [out] */ __RPC__deref_out_opt BYTE **ppbData) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall QueueFeatureReport( 
            /* [in] */ ULONG cbData,
            /* [in] */ __RPC__in BYTE *pbData) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftHidUsbDeviceVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftHidUsbDevice * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftHidUsbDevice * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftHidUsbDevice * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftHidUsbDevice * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftHidUsbDevice * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftHidUsbDevice * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftHidUsbDevice * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_PhysicalDescriptor )( 
            __RPC__in ISoftHidUsbDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBPhysicalDescriptor **ppSoftUSBPhyDescriptor);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_PhysicalDescriptor )( 
            __RPC__in ISoftHidUsbDevice * This,
            /* [in] */ __RPC__in_opt ISoftUSBPhysicalDescriptor *pSoftUSBPhyDescriptor);
        
        /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT ( __stdcall *putref_PhysicalDescriptor )( 
            __RPC__in ISoftHidUsbDevice * This,
            /* [in] */ __RPC__in_opt ISoftUSBPhysicalDescriptor *pSoftUSBPhyDescriptor);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_ReportDescriptor )( 
            __RPC__in ISoftHidUsbDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaSoftUSBReportDescriptor);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_ReportDescriptor )( 
            __RPC__in ISoftHidUsbDevice * This,
            /* [in] */ __RPC__in SAFEARRAY * psaSoftUSBReportDescriptor);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *QueueInputReport )( 
            __RPC__in ISoftHidUsbDevice * This,
            /* [in] */ ULONG cbData,
            /* [in] */ __RPC__in BYTE *pbData);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *DequeueOutputReport )( 
            __RPC__in ISoftHidUsbDevice * This,
            /* [out] */ __RPC__out ULONG *pcbData,
            /* [out] */ __RPC__deref_out_opt BYTE **ppbData);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *QueueFeatureReport )( 
            __RPC__in ISoftHidUsbDevice * This,
            /* [in] */ ULONG cbData,
            /* [in] */ __RPC__in BYTE *pbData);
        
        END_INTERFACE
    } ISoftHidUsbDeviceVtbl;

    interface ISoftHidUsbDevice
    {
        CONST_VTBL struct ISoftHidUsbDeviceVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftHidUsbDevice_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftHidUsbDevice_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftHidUsbDevice_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftHidUsbDevice_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftHidUsbDevice_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftHidUsbDevice_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftHidUsbDevice_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftHidUsbDevice_get_PhysicalDescriptor(This,ppSoftUSBPhyDescriptor)	\
    ( (This)->lpVtbl -> get_PhysicalDescriptor(This,ppSoftUSBPhyDescriptor) ) 

#define ISoftHidUsbDevice_put_PhysicalDescriptor(This,pSoftUSBPhyDescriptor)	\
    ( (This)->lpVtbl -> put_PhysicalDescriptor(This,pSoftUSBPhyDescriptor) ) 

#define ISoftHidUsbDevice_putref_PhysicalDescriptor(This,pSoftUSBPhyDescriptor)	\
    ( (This)->lpVtbl -> putref_PhysicalDescriptor(This,pSoftUSBPhyDescriptor) ) 

#define ISoftHidUsbDevice_get_ReportDescriptor(This,ppsaSoftUSBReportDescriptor)	\
    ( (This)->lpVtbl -> get_ReportDescriptor(This,ppsaSoftUSBReportDescriptor) ) 

#define ISoftHidUsbDevice_put_ReportDescriptor(This,psaSoftUSBReportDescriptor)	\
    ( (This)->lpVtbl -> put_ReportDescriptor(This,psaSoftUSBReportDescriptor) ) 

#define ISoftHidUsbDevice_QueueInputReport(This,cbData,pbData)	\
    ( (This)->lpVtbl -> QueueInputReport(This,cbData,pbData) ) 

#define ISoftHidUsbDevice_DequeueOutputReport(This,pcbData,ppbData)	\
    ( (This)->lpVtbl -> DequeueOutputReport(This,pcbData,ppbData) ) 

#define ISoftHidUsbDevice_QueueFeatureReport(This,cbData,pbData)	\
    ( (This)->lpVtbl -> QueueFeatureReport(This,cbData,pbData) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftHidUsbDevice_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBHidDescriptor_INTERFACE_DEFINED__
#define __ISoftUSBHidDescriptor_INTERFACE_DEFINED__

/* interface ISoftUSBHidDescriptor */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBHidDescriptor;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("7E4BAE25-09D9-4DB8-8CEC-6D089D87F5EE")
    ISoftUSBHidDescriptor : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Length( 
            /* [retval][out] */ __RPC__out BYTE *pbyLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DescriptorType( 
            /* [retval][out] */ __RPC__out BYTE *pbyDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_HID( 
            /* [retval][out] */ __RPC__out SHORT *psHID) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_HID( 
            /* [in] */ SHORT sHID) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_CountryCode( 
            /* [retval][out] */ __RPC__out BYTE *pbyCountryCode) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_CountryCode( 
            /* [in] */ BYTE byCountryCode) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_NumDescriptors( 
            /* [retval][out] */ __RPC__out BYTE *pbyNumDescriptors) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_NumDescriptors( 
            /* [in] */ BYTE byNumDescriptors) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_ClassDescriptorType( 
            /* [in] */ BYTE byIndex,
            /* [retval][out] */ __RPC__out BYTE *pbyClassDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_ClassDescriptorType( 
            /* [in] */ BYTE byIndex,
            /* [in] */ BYTE byClassDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DescriptorLength( 
            /* [in] */ BYTE byIndex,
            /* [retval][out] */ __RPC__out SHORT *psDescriptorLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DescriptorLength( 
            /* [in] */ BYTE byIndex,
            /* [in] */ SHORT sDescriptorLength) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBHidDescriptorVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBHidDescriptor * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBHidDescriptor * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBHidDescriptor * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Length )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [retval][out] */ __RPC__out BYTE *pbyLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DescriptorType )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [retval][out] */ __RPC__out BYTE *pbyDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_HID )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [retval][out] */ __RPC__out SHORT *psHID);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_HID )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [in] */ SHORT sHID);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_CountryCode )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [retval][out] */ __RPC__out BYTE *pbyCountryCode);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_CountryCode )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [in] */ BYTE byCountryCode);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_NumDescriptors )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [retval][out] */ __RPC__out BYTE *pbyNumDescriptors);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_NumDescriptors )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [in] */ BYTE byNumDescriptors);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_ClassDescriptorType )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [in] */ BYTE byIndex,
            /* [retval][out] */ __RPC__out BYTE *pbyClassDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_ClassDescriptorType )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [in] */ BYTE byIndex,
            /* [in] */ BYTE byClassDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DescriptorLength )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [in] */ BYTE byIndex,
            /* [retval][out] */ __RPC__out SHORT *psDescriptorLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DescriptorLength )( 
            __RPC__in ISoftUSBHidDescriptor * This,
            /* [in] */ BYTE byIndex,
            /* [in] */ SHORT sDescriptorLength);
        
        END_INTERFACE
    } ISoftUSBHidDescriptorVtbl;

    interface ISoftUSBHidDescriptor
    {
        CONST_VTBL struct ISoftUSBHidDescriptorVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBHidDescriptor_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBHidDescriptor_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBHidDescriptor_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBHidDescriptor_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBHidDescriptor_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBHidDescriptor_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBHidDescriptor_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBHidDescriptor_get_Length(This,pbyLength)	\
    ( (This)->lpVtbl -> get_Length(This,pbyLength) ) 

#define ISoftUSBHidDescriptor_get_DescriptorType(This,pbyDescriptorType)	\
    ( (This)->lpVtbl -> get_DescriptorType(This,pbyDescriptorType) ) 

#define ISoftUSBHidDescriptor_get_HID(This,psHID)	\
    ( (This)->lpVtbl -> get_HID(This,psHID) ) 

#define ISoftUSBHidDescriptor_put_HID(This,sHID)	\
    ( (This)->lpVtbl -> put_HID(This,sHID) ) 

#define ISoftUSBHidDescriptor_get_CountryCode(This,pbyCountryCode)	\
    ( (This)->lpVtbl -> get_CountryCode(This,pbyCountryCode) ) 

#define ISoftUSBHidDescriptor_put_CountryCode(This,byCountryCode)	\
    ( (This)->lpVtbl -> put_CountryCode(This,byCountryCode) ) 

#define ISoftUSBHidDescriptor_get_NumDescriptors(This,pbyNumDescriptors)	\
    ( (This)->lpVtbl -> get_NumDescriptors(This,pbyNumDescriptors) ) 

#define ISoftUSBHidDescriptor_put_NumDescriptors(This,byNumDescriptors)	\
    ( (This)->lpVtbl -> put_NumDescriptors(This,byNumDescriptors) ) 

#define ISoftUSBHidDescriptor_get_ClassDescriptorType(This,byIndex,pbyClassDescriptorType)	\
    ( (This)->lpVtbl -> get_ClassDescriptorType(This,byIndex,pbyClassDescriptorType) ) 

#define ISoftUSBHidDescriptor_put_ClassDescriptorType(This,byIndex,byClassDescriptorType)	\
    ( (This)->lpVtbl -> put_ClassDescriptorType(This,byIndex,byClassDescriptorType) ) 

#define ISoftUSBHidDescriptor_get_DescriptorLength(This,byIndex,psDescriptorLength)	\
    ( (This)->lpVtbl -> get_DescriptorLength(This,byIndex,psDescriptorLength) ) 

#define ISoftUSBHidDescriptor_put_DescriptorLength(This,byIndex,sDescriptorLength)	\
    ( (This)->lpVtbl -> put_DescriptorLength(This,byIndex,sDescriptorLength) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBHidDescriptor_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBPhysicalDescriptorSet0_INTERFACE_DEFINED__
#define __ISoftUSBPhysicalDescriptorSet0_INTERFACE_DEFINED__

/* interface ISoftUSBPhysicalDescriptorSet0 */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBPhysicalDescriptorSet0;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("F9573761-15AA-46D1-9FE0-16254E001F4C")
    ISoftUSBPhysicalDescriptorSet0 : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Number( 
            /* [retval][out] */ __RPC__out BYTE *pNumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Number( 
            /* [in] */ BYTE Number) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Length( 
            /* [retval][out] */ __RPC__out BYTE *pLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Length( 
            /* [in] */ BYTE Length) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBPhysicalDescriptorSet0Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet0 * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet0 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet0 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet0 * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet0 * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet0 * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBPhysicalDescriptorSet0 * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Number )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet0 * This,
            /* [retval][out] */ __RPC__out BYTE *pNumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Number )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet0 * This,
            /* [in] */ BYTE Number);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Length )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet0 * This,
            /* [retval][out] */ __RPC__out BYTE *pLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Length )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet0 * This,
            /* [in] */ BYTE Length);
        
        END_INTERFACE
    } ISoftUSBPhysicalDescriptorSet0Vtbl;

    interface ISoftUSBPhysicalDescriptorSet0
    {
        CONST_VTBL struct ISoftUSBPhysicalDescriptorSet0Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBPhysicalDescriptorSet0_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBPhysicalDescriptorSet0_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBPhysicalDescriptorSet0_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBPhysicalDescriptorSet0_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBPhysicalDescriptorSet0_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBPhysicalDescriptorSet0_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBPhysicalDescriptorSet0_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBPhysicalDescriptorSet0_get_Number(This,pNumber)	\
    ( (This)->lpVtbl -> get_Number(This,pNumber) ) 

#define ISoftUSBPhysicalDescriptorSet0_put_Number(This,Number)	\
    ( (This)->lpVtbl -> put_Number(This,Number) ) 

#define ISoftUSBPhysicalDescriptorSet0_get_Length(This,pLength)	\
    ( (This)->lpVtbl -> get_Length(This,pLength) ) 

#define ISoftUSBPhysicalDescriptorSet0_put_Length(This,Length)	\
    ( (This)->lpVtbl -> put_Length(This,Length) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBPhysicalDescriptorSet0_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBPhysicalDescriptorSet_INTERFACE_DEFINED__
#define __ISoftUSBPhysicalDescriptorSet_INTERFACE_DEFINED__

/* interface ISoftUSBPhysicalDescriptorSet */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBPhysicalDescriptorSet;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("1FB119F2-5613-4D6E-AE93-87584F8C4401")
    ISoftUSBPhysicalDescriptorSet : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Bias( 
            /* [retval][out] */ __RPC__out HID_PHYSICAL_BIAS *pBias) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Bias( 
            /* [in] */ HID_PHYSICAL_BIAS Bias) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Preference( 
            /* [retval][out] */ __RPC__out BYTE *pPreference) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Preference( 
            /* [in] */ BYTE Preference) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Items( 
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBPhysicalDescriptorItemList **ppItems) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBPhysicalDescriptorSetVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBPhysicalDescriptorSet * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Bias )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet * This,
            /* [retval][out] */ __RPC__out HID_PHYSICAL_BIAS *pBias);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Bias )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet * This,
            /* [in] */ HID_PHYSICAL_BIAS Bias);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Preference )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet * This,
            /* [retval][out] */ __RPC__out BYTE *pPreference);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Preference )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet * This,
            /* [in] */ BYTE Preference);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Items )( 
            __RPC__in ISoftUSBPhysicalDescriptorSet * This,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBPhysicalDescriptorItemList **ppItems);
        
        END_INTERFACE
    } ISoftUSBPhysicalDescriptorSetVtbl;

    interface ISoftUSBPhysicalDescriptorSet
    {
        CONST_VTBL struct ISoftUSBPhysicalDescriptorSetVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBPhysicalDescriptorSet_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBPhysicalDescriptorSet_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBPhysicalDescriptorSet_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBPhysicalDescriptorSet_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBPhysicalDescriptorSet_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBPhysicalDescriptorSet_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBPhysicalDescriptorSet_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBPhysicalDescriptorSet_get_Bias(This,pBias)	\
    ( (This)->lpVtbl -> get_Bias(This,pBias) ) 

#define ISoftUSBPhysicalDescriptorSet_put_Bias(This,Bias)	\
    ( (This)->lpVtbl -> put_Bias(This,Bias) ) 

#define ISoftUSBPhysicalDescriptorSet_get_Preference(This,pPreference)	\
    ( (This)->lpVtbl -> get_Preference(This,pPreference) ) 

#define ISoftUSBPhysicalDescriptorSet_put_Preference(This,Preference)	\
    ( (This)->lpVtbl -> put_Preference(This,Preference) ) 

#define ISoftUSBPhysicalDescriptorSet_get_Items(This,ppItems)	\
    ( (This)->lpVtbl -> get_Items(This,ppItems) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBPhysicalDescriptorSet_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBPhysicalDescriptorItem_INTERFACE_DEFINED__
#define __ISoftUSBPhysicalDescriptorItem_INTERFACE_DEFINED__

/* interface ISoftUSBPhysicalDescriptorItem */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBPhysicalDescriptorItem;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("904F9501-5208-409E-9F6C-67416876964A")
    ISoftUSBPhysicalDescriptorItem : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Designator( 
            /* [retval][out] */ __RPC__out HID_PHYSICAL_DESIGNATOR *pDesignator) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Designator( 
            /* [in] */ HID_PHYSICAL_DESIGNATOR Designator) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Flags( 
            /* [retval][out] */ __RPC__out BYTE *pFlags) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Flags( 
            /* [in] */ BYTE Flags) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Qualifier( 
            /* [retval][out] */ __RPC__out HID_PHYSICAL_QUALIFIER *pQualifier) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Qualifier( 
            /* [in] */ HID_PHYSICAL_QUALIFIER Qualifier) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Effort( 
            /* [retval][out] */ __RPC__out BYTE *pEffort) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Effort( 
            /* [in] */ BYTE Effort) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBPhysicalDescriptorItemVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBPhysicalDescriptorItem * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Designator )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This,
            /* [retval][out] */ __RPC__out HID_PHYSICAL_DESIGNATOR *pDesignator);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Designator )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This,
            /* [in] */ HID_PHYSICAL_DESIGNATOR Designator);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Flags )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This,
            /* [retval][out] */ __RPC__out BYTE *pFlags);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Flags )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This,
            /* [in] */ BYTE Flags);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Qualifier )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This,
            /* [retval][out] */ __RPC__out HID_PHYSICAL_QUALIFIER *pQualifier);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Qualifier )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This,
            /* [in] */ HID_PHYSICAL_QUALIFIER Qualifier);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Effort )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This,
            /* [retval][out] */ __RPC__out BYTE *pEffort);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Effort )( 
            __RPC__in ISoftUSBPhysicalDescriptorItem * This,
            /* [in] */ BYTE Effort);
        
        END_INTERFACE
    } ISoftUSBPhysicalDescriptorItemVtbl;

    interface ISoftUSBPhysicalDescriptorItem
    {
        CONST_VTBL struct ISoftUSBPhysicalDescriptorItemVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBPhysicalDescriptorItem_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBPhysicalDescriptorItem_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBPhysicalDescriptorItem_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBPhysicalDescriptorItem_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBPhysicalDescriptorItem_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBPhysicalDescriptorItem_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBPhysicalDescriptorItem_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBPhysicalDescriptorItem_get_Designator(This,pDesignator)	\
    ( (This)->lpVtbl -> get_Designator(This,pDesignator) ) 

#define ISoftUSBPhysicalDescriptorItem_put_Designator(This,Designator)	\
    ( (This)->lpVtbl -> put_Designator(This,Designator) ) 

#define ISoftUSBPhysicalDescriptorItem_get_Flags(This,pFlags)	\
    ( (This)->lpVtbl -> get_Flags(This,pFlags) ) 

#define ISoftUSBPhysicalDescriptorItem_put_Flags(This,Flags)	\
    ( (This)->lpVtbl -> put_Flags(This,Flags) ) 

#define ISoftUSBPhysicalDescriptorItem_get_Qualifier(This,pQualifier)	\
    ( (This)->lpVtbl -> get_Qualifier(This,pQualifier) ) 

#define ISoftUSBPhysicalDescriptorItem_put_Qualifier(This,Qualifier)	\
    ( (This)->lpVtbl -> put_Qualifier(This,Qualifier) ) 

#define ISoftUSBPhysicalDescriptorItem_get_Effort(This,pEffort)	\
    ( (This)->lpVtbl -> get_Effort(This,pEffort) ) 

#define ISoftUSBPhysicalDescriptorItem_put_Effort(This,Effort)	\
    ( (This)->lpVtbl -> put_Effort(This,Effort) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBPhysicalDescriptorItem_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBPhysicalDescriptor_INTERFACE_DEFINED__
#define __ISoftUSBPhysicalDescriptor_INTERFACE_DEFINED__

/* interface ISoftUSBPhysicalDescriptor */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBPhysicalDescriptor;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("0C192F43-ACC3-4CF7-A823-B14FCFF082A1")
    ISoftUSBPhysicalDescriptor : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Set0( 
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBPhysicalDescriptorSet0 **ppSet0) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Set0( 
            /* [in] */ __RPC__in_opt ISoftUSBPhysicalDescriptorSet0 *pSet0) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT __stdcall putref_Set0( 
            /* [in] */ __RPC__in_opt ISoftUSBPhysicalDescriptorSet0 *pSet0) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Sets( 
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBPhysicalDescriptorSetList **ppSets) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBPhysicalDescriptorVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBPhysicalDescriptor * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBPhysicalDescriptor * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBPhysicalDescriptor * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBPhysicalDescriptor * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBPhysicalDescriptor * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBPhysicalDescriptor * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBPhysicalDescriptor * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Set0 )( 
            __RPC__in ISoftUSBPhysicalDescriptor * This,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBPhysicalDescriptorSet0 **ppSet0);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Set0 )( 
            __RPC__in ISoftUSBPhysicalDescriptor * This,
            /* [in] */ __RPC__in_opt ISoftUSBPhysicalDescriptorSet0 *pSet0);
        
        /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT ( __stdcall *putref_Set0 )( 
            __RPC__in ISoftUSBPhysicalDescriptor * This,
            /* [in] */ __RPC__in_opt ISoftUSBPhysicalDescriptorSet0 *pSet0);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Sets )( 
            __RPC__in ISoftUSBPhysicalDescriptor * This,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBPhysicalDescriptorSetList **ppSets);
        
        END_INTERFACE
    } ISoftUSBPhysicalDescriptorVtbl;

    interface ISoftUSBPhysicalDescriptor
    {
        CONST_VTBL struct ISoftUSBPhysicalDescriptorVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBPhysicalDescriptor_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBPhysicalDescriptor_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBPhysicalDescriptor_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBPhysicalDescriptor_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBPhysicalDescriptor_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBPhysicalDescriptor_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBPhysicalDescriptor_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBPhysicalDescriptor_get_Set0(This,ppSet0)	\
    ( (This)->lpVtbl -> get_Set0(This,ppSet0) ) 

#define ISoftUSBPhysicalDescriptor_put_Set0(This,pSet0)	\
    ( (This)->lpVtbl -> put_Set0(This,pSet0) ) 

#define ISoftUSBPhysicalDescriptor_putref_Set0(This,pSet0)	\
    ( (This)->lpVtbl -> putref_Set0(This,pSet0) ) 

#define ISoftUSBPhysicalDescriptor_get_Sets(This,ppSets)	\
    ( (This)->lpVtbl -> get_Sets(This,ppSets) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBPhysicalDescriptor_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBPhysicalDescriptorSetList_INTERFACE_DEFINED__
#define __ISoftUSBPhysicalDescriptorSetList_INTERFACE_DEFINED__

/* interface ISoftUSBPhysicalDescriptorSetList */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBPhysicalDescriptorSetList;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("4CC5D8E4-49F4-4C34-ADA7-B81B4BC5C4D6")
    ISoftUSBPhysicalDescriptorSetList : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Count( 
            /* [retval][out] */ __RPC__out long *plCount) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Item( 
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBPhysicalDescriptorSet **ppSet) = 0;
        
        virtual /* [hidden][propget][id] */ HRESULT __stdcall get__NewEnum( 
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Add( 
            /* [in] */ __RPC__in_opt ISoftUSBPhysicalDescriptorSet *pSet,
            /* [optional][in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Remove( 
            /* [in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Clear( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBPhysicalDescriptorSetListVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBPhysicalDescriptorSetList * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBPhysicalDescriptorSetList * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBPhysicalDescriptorSetList * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBPhysicalDescriptorSetList * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBPhysicalDescriptorSetList * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBPhysicalDescriptorSetList * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBPhysicalDescriptorSetList * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Count )( 
            __RPC__in ISoftUSBPhysicalDescriptorSetList * This,
            /* [retval][out] */ __RPC__out long *plCount);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Item )( 
            __RPC__in ISoftUSBPhysicalDescriptorSetList * This,
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBPhysicalDescriptorSet **ppSet);
        
        /* [hidden][propget][id] */ HRESULT ( __stdcall *get__NewEnum )( 
            __RPC__in ISoftUSBPhysicalDescriptorSetList * This,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Add )( 
            __RPC__in ISoftUSBPhysicalDescriptorSetList * This,
            /* [in] */ __RPC__in_opt ISoftUSBPhysicalDescriptorSet *pSet,
            /* [optional][in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Remove )( 
            __RPC__in ISoftUSBPhysicalDescriptorSetList * This,
            /* [in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Clear )( 
            __RPC__in ISoftUSBPhysicalDescriptorSetList * This);
        
        END_INTERFACE
    } ISoftUSBPhysicalDescriptorSetListVtbl;

    interface ISoftUSBPhysicalDescriptorSetList
    {
        CONST_VTBL struct ISoftUSBPhysicalDescriptorSetListVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBPhysicalDescriptorSetList_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBPhysicalDescriptorSetList_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBPhysicalDescriptorSetList_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBPhysicalDescriptorSetList_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBPhysicalDescriptorSetList_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBPhysicalDescriptorSetList_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBPhysicalDescriptorSetList_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBPhysicalDescriptorSetList_get_Count(This,plCount)	\
    ( (This)->lpVtbl -> get_Count(This,plCount) ) 

#define ISoftUSBPhysicalDescriptorSetList_get_Item(This,Index,ppSet)	\
    ( (This)->lpVtbl -> get_Item(This,Index,ppSet) ) 

#define ISoftUSBPhysicalDescriptorSetList_get__NewEnum(This,ppunkEnum)	\
    ( (This)->lpVtbl -> get__NewEnum(This,ppunkEnum) ) 

#define ISoftUSBPhysicalDescriptorSetList_Add(This,pSet,Index)	\
    ( (This)->lpVtbl -> Add(This,pSet,Index) ) 

#define ISoftUSBPhysicalDescriptorSetList_Remove(This,Index)	\
    ( (This)->lpVtbl -> Remove(This,Index) ) 

#define ISoftUSBPhysicalDescriptorSetList_Clear(This)	\
    ( (This)->lpVtbl -> Clear(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBPhysicalDescriptorSetList_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBPhysicalDescriptorItemList_INTERFACE_DEFINED__
#define __ISoftUSBPhysicalDescriptorItemList_INTERFACE_DEFINED__

/* interface ISoftUSBPhysicalDescriptorItemList */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBPhysicalDescriptorItemList;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("24939051-5A21-432B-BF7C-D05037C116A4")
    ISoftUSBPhysicalDescriptorItemList : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Count( 
            /* [retval][out] */ __RPC__out long *plCount) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Item( 
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBPhysicalDescriptorItem **ppItem) = 0;
        
        virtual /* [hidden][propget][id] */ HRESULT __stdcall get__NewEnum( 
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Add( 
            /* [in] */ __RPC__in_opt ISoftUSBPhysicalDescriptorItem *pItem,
            /* [optional][in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Remove( 
            /* [in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Clear( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBPhysicalDescriptorItemListVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBPhysicalDescriptorItemList * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBPhysicalDescriptorItemList * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBPhysicalDescriptorItemList * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBPhysicalDescriptorItemList * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBPhysicalDescriptorItemList * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBPhysicalDescriptorItemList * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBPhysicalDescriptorItemList * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Count )( 
            __RPC__in ISoftUSBPhysicalDescriptorItemList * This,
            /* [retval][out] */ __RPC__out long *plCount);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Item )( 
            __RPC__in ISoftUSBPhysicalDescriptorItemList * This,
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBPhysicalDescriptorItem **ppItem);
        
        /* [hidden][propget][id] */ HRESULT ( __stdcall *get__NewEnum )( 
            __RPC__in ISoftUSBPhysicalDescriptorItemList * This,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Add )( 
            __RPC__in ISoftUSBPhysicalDescriptorItemList * This,
            /* [in] */ __RPC__in_opt ISoftUSBPhysicalDescriptorItem *pItem,
            /* [optional][in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Remove )( 
            __RPC__in ISoftUSBPhysicalDescriptorItemList * This,
            /* [in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Clear )( 
            __RPC__in ISoftUSBPhysicalDescriptorItemList * This);
        
        END_INTERFACE
    } ISoftUSBPhysicalDescriptorItemListVtbl;

    interface ISoftUSBPhysicalDescriptorItemList
    {
        CONST_VTBL struct ISoftUSBPhysicalDescriptorItemListVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBPhysicalDescriptorItemList_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBPhysicalDescriptorItemList_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBPhysicalDescriptorItemList_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBPhysicalDescriptorItemList_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBPhysicalDescriptorItemList_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBPhysicalDescriptorItemList_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBPhysicalDescriptorItemList_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBPhysicalDescriptorItemList_get_Count(This,plCount)	\
    ( (This)->lpVtbl -> get_Count(This,plCount) ) 

#define ISoftUSBPhysicalDescriptorItemList_get_Item(This,Index,ppItem)	\
    ( (This)->lpVtbl -> get_Item(This,Index,ppItem) ) 

#define ISoftUSBPhysicalDescriptorItemList_get__NewEnum(This,ppunkEnum)	\
    ( (This)->lpVtbl -> get__NewEnum(This,ppunkEnum) ) 

#define ISoftUSBPhysicalDescriptorItemList_Add(This,pItem,Index)	\
    ( (This)->lpVtbl -> Add(This,pItem,Index) ) 

#define ISoftUSBPhysicalDescriptorItemList_Remove(This,Index)	\
    ( (This)->lpVtbl -> Remove(This,Index) ) 

#define ISoftUSBPhysicalDescriptorItemList_Clear(This)	\
    ( (This)->lpVtbl -> Clear(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBPhysicalDescriptorItemList_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBHidFaultInjection_INTERFACE_DEFINED__
#define __ISoftUSBHidFaultInjection_INTERFACE_DEFINED__

/* interface ISoftUSBHidFaultInjection */
/* [object][helpstringcontext][helpcontext][helpstring][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBHidFaultInjection;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("36D2CC56-B545-4F13-BAEF-425F87E5BB37")
    ISoftUSBHidFaultInjection : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring] */ HRESULT __stdcall StartFaultInjection( void) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring] */ HRESULT __stdcall StopFaultInjection( void) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring] */ HRESULT __stdcall FaultNextNRequests( 
            /* [in] */ BYTE bRequests,
            /* [in] */ ULONG nTimesToFail) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring] */ HRESULT __stdcall FaultNextNIOPackets( 
            /* [in] */ BYTE bEpNumber,
            /* [in] */ BYTE bFlag,
            /* [in] */ ULONG nTimesToFail) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring] */ HRESULT __stdcall ChangeNextNIOPackets( 
            /* [in] */ BYTE bEpNumber,
            /* [in] */ BYTE bFlags,
            /* [in] */ SHORT wOffset,
            /* [in] */ __RPC__in BYTE *pBuffer,
            /* [in] */ SHORT wSize,
            /* [in] */ BYTE bMask,
            /* [in] */ ULONG nTimesToFail) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBHidFaultInjectionVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBHidFaultInjection * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBHidFaultInjection * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBHidFaultInjection * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBHidFaultInjection * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBHidFaultInjection * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBHidFaultInjection * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBHidFaultInjection * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring] */ HRESULT ( __stdcall *StartFaultInjection )( 
            __RPC__in ISoftUSBHidFaultInjection * This);
        
        /* [helpstringcontext][helpcontext][helpstring] */ HRESULT ( __stdcall *StopFaultInjection )( 
            __RPC__in ISoftUSBHidFaultInjection * This);
        
        /* [helpstringcontext][helpcontext][helpstring] */ HRESULT ( __stdcall *FaultNextNRequests )( 
            __RPC__in ISoftUSBHidFaultInjection * This,
            /* [in] */ BYTE bRequests,
            /* [in] */ ULONG nTimesToFail);
        
        /* [helpstringcontext][helpcontext][helpstring] */ HRESULT ( __stdcall *FaultNextNIOPackets )( 
            __RPC__in ISoftUSBHidFaultInjection * This,
            /* [in] */ BYTE bEpNumber,
            /* [in] */ BYTE bFlag,
            /* [in] */ ULONG nTimesToFail);
        
        /* [helpstringcontext][helpcontext][helpstring] */ HRESULT ( __stdcall *ChangeNextNIOPackets )( 
            __RPC__in ISoftUSBHidFaultInjection * This,
            /* [in] */ BYTE bEpNumber,
            /* [in] */ BYTE bFlags,
            /* [in] */ SHORT wOffset,
            /* [in] */ __RPC__in BYTE *pBuffer,
            /* [in] */ SHORT wSize,
            /* [in] */ BYTE bMask,
            /* [in] */ ULONG nTimesToFail);
        
        END_INTERFACE
    } ISoftUSBHidFaultInjectionVtbl;

    interface ISoftUSBHidFaultInjection
    {
        CONST_VTBL struct ISoftUSBHidFaultInjectionVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBHidFaultInjection_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBHidFaultInjection_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBHidFaultInjection_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBHidFaultInjection_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBHidFaultInjection_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBHidFaultInjection_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBHidFaultInjection_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBHidFaultInjection_StartFaultInjection(This)	\
    ( (This)->lpVtbl -> StartFaultInjection(This) ) 

#define ISoftUSBHidFaultInjection_StopFaultInjection(This)	\
    ( (This)->lpVtbl -> StopFaultInjection(This) ) 

#define ISoftUSBHidFaultInjection_FaultNextNRequests(This,bRequests,nTimesToFail)	\
    ( (This)->lpVtbl -> FaultNextNRequests(This,bRequests,nTimesToFail) ) 

#define ISoftUSBHidFaultInjection_FaultNextNIOPackets(This,bEpNumber,bFlag,nTimesToFail)	\
    ( (This)->lpVtbl -> FaultNextNIOPackets(This,bEpNumber,bFlag,nTimesToFail) ) 

#define ISoftUSBHidFaultInjection_ChangeNextNIOPackets(This,bEpNumber,bFlags,wOffset,pBuffer,wSize,bMask,nTimesToFail)	\
    ( (This)->lpVtbl -> ChangeNextNIOPackets(This,bEpNumber,bFlags,wOffset,pBuffer,wSize,bMask,nTimesToFail) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBHidFaultInjection_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_SoftHIDProtocolXlator;

#ifdef __cplusplus

class DECLSPEC_UUID("887C935C-84F7-4252-BFE3-33CBC16F3D47")
SoftHIDProtocolXlator;
#endif

EXTERN_C const CLSID CLSID_SoftHidUsbDevice;

#ifdef __cplusplus

class DECLSPEC_UUID("56545816-6EFE-45D0-BA2E-AC4426878CD5")
SoftHidUsbDevice;
#endif

EXTERN_C const CLSID CLSID_SoftUSBHidDescriptor;

#ifdef __cplusplus

class DECLSPEC_UUID("3FB17F94-282C-4091-BCAA-EAA0AC104946")
SoftUSBHidDescriptor;
#endif

EXTERN_C const CLSID CLSID_SoftUSBPhysicalDescriptor;

#ifdef __cplusplus

class DECLSPEC_UUID("D5DF9D05-B55C-45EC-BDC2-A9CC318CBBAB")
SoftUSBPhysicalDescriptor;
#endif

EXTERN_C const CLSID CLSID_SoftUSBPhysicalDescriptorSet0;

#ifdef __cplusplus

class DECLSPEC_UUID("921F4757-CD5A-4935-BB3E-616ABFE18BEA")
SoftUSBPhysicalDescriptorSet0;
#endif

EXTERN_C const CLSID CLSID_SoftUSBPhysicalDescriptorSet;

#ifdef __cplusplus

class DECLSPEC_UUID("EF7BF7AD-83D0-4160-9C07-39AD92A08A8D")
SoftUSBPhysicalDescriptorSet;
#endif

EXTERN_C const CLSID CLSID_SoftUSBPhysicalDescriptorItem;

#ifdef __cplusplus

class DECLSPEC_UUID("18217BCD-1DC8-4737-A0A3-95EC3432DBFE")
SoftUSBPhysicalDescriptorItem;
#endif

EXTERN_C const CLSID CLSID_SoftUSBPhysicalDescriptorSetList;

#ifdef __cplusplus

class DECLSPEC_UUID("E280E977-BD59-48A0-9B91-10D312157393")
SoftUSBPhysicalDescriptorSetList;
#endif

EXTERN_C const CLSID CLSID_SoftUSBPhysicalDescriptorItemList;

#ifdef __cplusplus

class DECLSPEC_UUID("8F779698-A4E3-47FE-8542-C312FE1F748A")
SoftUSBPhysicalDescriptorItemList;
#endif

EXTERN_C const CLSID CLSID_SoftUSBHidFaultInjection;

#ifdef __cplusplus

class DECLSPEC_UUID("D775D2E8-F328-43F4-B680-80DF4AE0CB6A")
SoftUSBHidFaultInjection;
#endif
#endif /* __SOFTHIDUSBK_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif



