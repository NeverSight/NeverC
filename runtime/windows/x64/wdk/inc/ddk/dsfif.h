

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 7.00.0555 */
/* Compiler settings for dsfif.idl:
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


#ifndef __dsfif_h__
#define __dsfif_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __IDSFResourceList_FWD_DEFINED__
#define __IDSFResourceList_FWD_DEFINED__
typedef interface IDSFResourceList IDSFResourceList;
#endif 	/* __IDSFResourceList_FWD_DEFINED__ */


#ifndef __IDSFFullResourceDescriptor_FWD_DEFINED__
#define __IDSFFullResourceDescriptor_FWD_DEFINED__
typedef interface IDSFFullResourceDescriptor IDSFFullResourceDescriptor;
#endif 	/* __IDSFFullResourceDescriptor_FWD_DEFINED__ */


#ifndef __IDSFDevice_FWD_DEFINED__
#define __IDSFDevice_FWD_DEFINED__
typedef interface IDSFDevice IDSFDevice;
#endif 	/* __IDSFDevice_FWD_DEFINED__ */


#ifndef __IDSFDeviceCaps_FWD_DEFINED__
#define __IDSFDeviceCaps_FWD_DEFINED__
typedef interface IDSFDeviceCaps IDSFDeviceCaps;
#endif 	/* __IDSFDeviceCaps_FWD_DEFINED__ */


#ifndef __IDSFDeviceEvents_FWD_DEFINED__
#define __IDSFDeviceEvents_FWD_DEFINED__
typedef interface IDSFDeviceEvents IDSFDeviceEvents;
#endif 	/* __IDSFDeviceEvents_FWD_DEFINED__ */


#ifndef __IDSF_FWD_DEFINED__
#define __IDSF_FWD_DEFINED__
typedef interface IDSF IDSF;
#endif 	/* __IDSF_FWD_DEFINED__ */


#ifndef __IDSFDevices_FWD_DEFINED__
#define __IDSFDevices_FWD_DEFINED__
typedef interface IDSFDevices IDSFDevices;
#endif 	/* __IDSFDevices_FWD_DEFINED__ */


#ifndef __IDSFLog_FWD_DEFINED__
#define __IDSFLog_FWD_DEFINED__
typedef interface IDSFLog IDSFLog;
#endif 	/* __IDSFLog_FWD_DEFINED__ */


#ifndef __IDSFVersion_FWD_DEFINED__
#define __IDSFVersion_FWD_DEFINED__
typedef interface IDSFVersion IDSFVersion;
#endif 	/* __IDSFVersion_FWD_DEFINED__ */


#ifndef __IDSFPropertyBag_FWD_DEFINED__
#define __IDSFPropertyBag_FWD_DEFINED__
typedef interface IDSFPropertyBag IDSFPropertyBag;
#endif 	/* __IDSFPropertyBag_FWD_DEFINED__ */


#ifndef __IDSFBus_FWD_DEFINED__
#define __IDSFBus_FWD_DEFINED__
typedef interface IDSFBus IDSFBus;
#endif 	/* __IDSFBus_FWD_DEFINED__ */


#ifndef __IDSFPartialResourceDescriptor_FWD_DEFINED__
#define __IDSFPartialResourceDescriptor_FWD_DEFINED__
typedef interface IDSFPartialResourceDescriptor IDSFPartialResourceDescriptor;
#endif 	/* __IDSFPartialResourceDescriptor_FWD_DEFINED__ */


#ifndef __DSFPartialResourceDescriptor_FWD_DEFINED__
#define __DSFPartialResourceDescriptor_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFPartialResourceDescriptor DSFPartialResourceDescriptor;
#else
typedef struct DSFPartialResourceDescriptor DSFPartialResourceDescriptor;
#endif /* __cplusplus */

#endif 	/* __DSFPartialResourceDescriptor_FWD_DEFINED__ */


#ifndef __IDSFPartialResourceList_FWD_DEFINED__
#define __IDSFPartialResourceList_FWD_DEFINED__
typedef interface IDSFPartialResourceList IDSFPartialResourceList;
#endif 	/* __IDSFPartialResourceList_FWD_DEFINED__ */


#ifndef __DSFPartialResourceList_FWD_DEFINED__
#define __DSFPartialResourceList_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFPartialResourceList DSFPartialResourceList;
#else
typedef struct DSFPartialResourceList DSFPartialResourceList;
#endif /* __cplusplus */

#endif 	/* __DSFPartialResourceList_FWD_DEFINED__ */


#ifndef __DSFFullResourceDescriptor_FWD_DEFINED__
#define __DSFFullResourceDescriptor_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFFullResourceDescriptor DSFFullResourceDescriptor;
#else
typedef struct DSFFullResourceDescriptor DSFFullResourceDescriptor;
#endif /* __cplusplus */

#endif 	/* __DSFFullResourceDescriptor_FWD_DEFINED__ */


#ifndef __DSFResourceList_FWD_DEFINED__
#define __DSFResourceList_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFResourceList DSFResourceList;
#else
typedef struct DSFResourceList DSFResourceList;
#endif /* __cplusplus */

#endif 	/* __DSFResourceList_FWD_DEFINED__ */


#ifndef __IDSFResDescPort_FWD_DEFINED__
#define __IDSFResDescPort_FWD_DEFINED__
typedef interface IDSFResDescPort IDSFResDescPort;
#endif 	/* __IDSFResDescPort_FWD_DEFINED__ */


#ifndef __DSFResDescPort_FWD_DEFINED__
#define __DSFResDescPort_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFResDescPort DSFResDescPort;
#else
typedef struct DSFResDescPort DSFResDescPort;
#endif /* __cplusplus */

#endif 	/* __DSFResDescPort_FWD_DEFINED__ */


#ifndef __IDSFResDescDevSpecific_FWD_DEFINED__
#define __IDSFResDescDevSpecific_FWD_DEFINED__
typedef interface IDSFResDescDevSpecific IDSFResDescDevSpecific;
#endif 	/* __IDSFResDescDevSpecific_FWD_DEFINED__ */


#ifndef __DSFResDescDevSpecific_FWD_DEFINED__
#define __DSFResDescDevSpecific_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFResDescDevSpecific DSFResDescDevSpecific;
#else
typedef struct DSFResDescDevSpecific DSFResDescDevSpecific;
#endif /* __cplusplus */

#endif 	/* __DSFResDescDevSpecific_FWD_DEFINED__ */


#ifndef __IDSFResDescDevPrivate_FWD_DEFINED__
#define __IDSFResDescDevPrivate_FWD_DEFINED__
typedef interface IDSFResDescDevPrivate IDSFResDescDevPrivate;
#endif 	/* __IDSFResDescDevPrivate_FWD_DEFINED__ */


#ifndef __DSFResDescDevPrivate_FWD_DEFINED__
#define __DSFResDescDevPrivate_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFResDescDevPrivate DSFResDescDevPrivate;
#else
typedef struct DSFResDescDevPrivate DSFResDescDevPrivate;
#endif /* __cplusplus */

#endif 	/* __DSFResDescDevPrivate_FWD_DEFINED__ */


#ifndef __IDSFResDescInterrupt_FWD_DEFINED__
#define __IDSFResDescInterrupt_FWD_DEFINED__
typedef interface IDSFResDescInterrupt IDSFResDescInterrupt;
#endif 	/* __IDSFResDescInterrupt_FWD_DEFINED__ */


#ifndef __DSFResDescInterrupt_FWD_DEFINED__
#define __DSFResDescInterrupt_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFResDescInterrupt DSFResDescInterrupt;
#else
typedef struct DSFResDescInterrupt DSFResDescInterrupt;
#endif /* __cplusplus */

#endif 	/* __DSFResDescInterrupt_FWD_DEFINED__ */


#ifndef __IDSFResDescMemory_FWD_DEFINED__
#define __IDSFResDescMemory_FWD_DEFINED__
typedef interface IDSFResDescMemory IDSFResDescMemory;
#endif 	/* __IDSFResDescMemory_FWD_DEFINED__ */


#ifndef __DSFResDescMemory_FWD_DEFINED__
#define __DSFResDescMemory_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFResDescMemory DSFResDescMemory;
#else
typedef struct DSFResDescMemory DSFResDescMemory;
#endif /* __cplusplus */

#endif 	/* __DSFResDescMemory_FWD_DEFINED__ */


#ifndef __IDSFResDescDMA_FWD_DEFINED__
#define __IDSFResDescDMA_FWD_DEFINED__
typedef interface IDSFResDescDMA IDSFResDescDMA;
#endif 	/* __IDSFResDescDMA_FWD_DEFINED__ */


#ifndef __DSFResDescDMA_FWD_DEFINED__
#define __DSFResDescDMA_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFResDescDMA DSFResDescDMA;
#else
typedef struct DSFResDescDMA DSFResDescDMA;
#endif /* __cplusplus */

#endif 	/* __DSFResDescDMA_FWD_DEFINED__ */


#ifndef __IDSFSystemDevice_FWD_DEFINED__
#define __IDSFSystemDevice_FWD_DEFINED__
typedef interface IDSFSystemDevice IDSFSystemDevice;
#endif 	/* __IDSFSystemDevice_FWD_DEFINED__ */


#ifndef __DSFSystemDevice_FWD_DEFINED__
#define __DSFSystemDevice_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFSystemDevice DSFSystemDevice;
#else
typedef struct DSFSystemDevice DSFSystemDevice;
#endif /* __cplusplus */

#endif 	/* __DSFSystemDevice_FWD_DEFINED__ */


#ifndef __DSFDevice_FWD_DEFINED__
#define __DSFDevice_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFDevice DSFDevice;
#else
typedef struct DSFDevice DSFDevice;
#endif /* __cplusplus */

#endif 	/* __DSFDevice_FWD_DEFINED__ */


#ifndef __DSF_FWD_DEFINED__
#define __DSF_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSF DSF;
#else
typedef struct DSF DSF;
#endif /* __cplusplus */

#endif 	/* __DSF_FWD_DEFINED__ */


#ifndef __DSFDevices_FWD_DEFINED__
#define __DSFDevices_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFDevices DSFDevices;
#else
typedef struct DSFDevices DSFDevices;
#endif /* __cplusplus */

#endif 	/* __DSFDevices_FWD_DEFINED__ */


#ifndef __DSFDeviceCaps_FWD_DEFINED__
#define __DSFDeviceCaps_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFDeviceCaps DSFDeviceCaps;
#else
typedef struct DSFDeviceCaps DSFDeviceCaps;
#endif /* __cplusplus */

#endif 	/* __DSFDeviceCaps_FWD_DEFINED__ */


#ifndef __DSFLog_FWD_DEFINED__
#define __DSFLog_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFLog DSFLog;
#else
typedef struct DSFLog DSFLog;
#endif /* __cplusplus */

#endif 	/* __DSFLog_FWD_DEFINED__ */


#ifndef __DSFVersion_FWD_DEFINED__
#define __DSFVersion_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFVersion DSFVersion;
#else
typedef struct DSFVersion DSFVersion;
#endif /* __cplusplus */

#endif 	/* __DSFVersion_FWD_DEFINED__ */


#ifndef __DSFPropertyBag_FWD_DEFINED__
#define __DSFPropertyBag_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFPropertyBag DSFPropertyBag;
#else
typedef struct DSFPropertyBag DSFPropertyBag;
#endif /* __cplusplus */

#endif 	/* __DSFPropertyBag_FWD_DEFINED__ */


#ifndef __IDSFDebug_FWD_DEFINED__
#define __IDSFDebug_FWD_DEFINED__
typedef interface IDSFDebug IDSFDebug;
#endif 	/* __IDSFDebug_FWD_DEFINED__ */


#ifndef __DSFDebug_FWD_DEFINED__
#define __DSFDebug_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFDebug DSFDebug;
#else
typedef struct DSFDebug DSFDebug;
#endif /* __cplusplus */

#endif 	/* __DSFDebug_FWD_DEFINED__ */


#ifndef __DSFBus_FWD_DEFINED__
#define __DSFBus_FWD_DEFINED__

#ifdef __cplusplus
typedef class DSFBus DSFBus;
#else
typedef struct DSFBus DSFBus;
#endif /* __cplusplus */

#endif 	/* __DSFBus_FWD_DEFINED__ */


#ifndef __IDSFPersist_FWD_DEFINED__
#define __IDSFPersist_FWD_DEFINED__
typedef interface IDSFPersist IDSFPersist;
#endif 	/* __IDSFPersist_FWD_DEFINED__ */


#ifdef __cplusplus
extern "C"{
#endif 



#ifndef __DSF_LIBRARY_DEFINED__
#define __DSF_LIBRARY_DEFINED__

/* library DSF */
/* [helpstringcontext][helpcontext][helpstring][helpstringdll][helpfile][version][lcid][uuid] */ 






















typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("993417C7-E892-4F80-9295-1321623DD50D") 
enum DSFSystemPowerState
    {	DSFPowerSystemUnspecified	= 0,
	DSFPowerSystemWorking	= 1,
	DSFPowerSystemSleeping1	= 2,
	DSFPowerSystemSleeping2	= 3,
	DSFPowerSystemSleeping3	= 4,
	DSFPowerSystemHibernate	= 5,
	DSFPowerSystemShutdown	= 6
    } 	DSFSystemPowerState;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("7B8AD659-B4EF-48C4-A189-9637BD56F086") 
enum DSFDevicePowerState
    {	DSFPowerDeviceUnspecified	= 0,
	DSFPowerDeviceD0	= 1,
	DSFPowerDeviceD1	= 2,
	DSFPowerDeviceD2	= 3,
	DSFPowerDeviceD3	= 4
    } 	DSFDevicePowerState;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("C936205A-C69B-45E4-8D19-6BE3CAC15E11") 
enum DSFBitOp
    {	DSFBitOff	= 0,
	DSFBitOn	= 1,
	DSFBitToggle	= 2
    } 	DSFBitOp;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("52CAB7A6-B8AC-474D-AB38-05A4AAF21FE7") 
enum DSFOpMode
    {	DSFCapture	= 0,
	DSFSimulate	= 1,
	DSFCaptureAndSimulate	= 2
    } 	DSFOpMode;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("7D4BA3AD-960C-4A3B-A6CC-3677AD9E0EDE") 
enum DSFResourceType
    {	DSFResourceTypeNull	= 0,
	DSFResourceTypePort	= 1,
	DSFResourceTypeInterrupt	= 2,
	DSFResourceTypeMemory	= 3,
	DSFResourceTypeDma	= 4,
	DSFResourceTypeDeviceSpecific	= 5,
	DSFResourceTypeBusNumber	= 6,
	DSFResourceTypeNonArbitrated	= 128,
	DSFResourceTypeConfigData	= 128,
	DSFResourceTypeDevicePrivate	= 129,
	DSFResourceTypePcCardConfig	= 130,
	DSFResourceTypeMfCardConfig	= 131,
	DSFResourceTypeConfigurationSpace	= 132,
	DSFResourceTypeAcpi	= 133,
	DSFResourceTypePci	= 134
    } 	DSFResourceType;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("D503F600-4EDD-4B7C-A676-0B50BDAED69E") 
enum DSFShareDisposition
    {	DSFShareDispositionUndetermined	= 0,
	DSFShareDispositionDeviceExclusive	= 1,
	DSFShareDispositionDriverExclusive	= 2,
	DSFShareDispositionShared	= 3
    } 	DSFShareDisposition;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("6FA31433-AA14-4EF7-8FBA-A91F326BA32B") 
enum DSFInterfaceType
    {	DSFInterfaceTypeUndefined	= -1,
	DSFInterfaceTypeInternal	= 0,
	DSFInterfaceTypeIsa	= 1,
	DSFInterfaceTypeEisa	= 2,
	DSFInterfaceTypeMicroChannel	= 3,
	DSFInterfaceTypeTurboChannel	= 4,
	DSFInterfaceTypePCIBus	= 5,
	DSFInterfaceTypeVMEBus	= 6,
	DSFInterfaceTypeNuBus	= 7,
	DSFInterfaceTypePCMCIABus	= 8,
	DSFInterfaceTypeCBus	= 9,
	DSFInterfaceTypeMPIBus	= 10,
	DSFInterfaceTypeMPSABus	= 11,
	DSFInterfaceTypeProcessorInternal	= 12,
	DSFInterfaceTypeInternalPowerBus	= 13,
	DSFInterfaceTypePNPISABus	= 14,
	DSFInterfaceTypePNPBus	= 15
    } 	DSFInterfaceType;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("78201517-0b6c-4208-b003-2f396640e5bc") 
enum DSFStateType
    {	DSFDefaultStateType	= 0
    } 	DSFStateType;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("86b50daf-5bb2-43f3-a9ca-e5bf444d9523") 
enum DSFFlagType
    {	DSFDebugFlags	= 0,
	DSFLogFlags	= 1,
	DSFStdOutFlags	= 2
    } 	DSFFlagType;

#define	DSFVersionMajor	( 1 )

#define	DSFVersionMinor	( 0 )


EXTERN_C const IID LIBID_DSF;

#ifndef __IDSFResourceList_INTERFACE_DEFINED__
#define __IDSFResourceList_INTERFACE_DEFINED__

/* interface IDSFResourceList */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFResourceList;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("418E0FF4-25F9-459A-B92B-6B3294E7135C")
    IDSFResourceList : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Count( 
            /* [retval][out] */ __RPC__out long *plCount) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Item( 
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt DSFFullResourceDescriptor	**ppDSFFullResourceDescriptor) = 0;
        
        virtual /* [hidden][propget][id] */ HRESULT __stdcall get__NewEnum( 
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Add( 
            /* [in] */ __RPC__in DSFFullResourceDescriptor	*pDSFFullResourceDescriptor,
            /* [optional][in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Remove( 
            /* [in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Clear( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFResourceListVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFResourceList * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFResourceList * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFResourceList * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFResourceList * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFResourceList * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFResourceList * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFResourceList * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Count )( 
            __RPC__in IDSFResourceList * This,
            /* [retval][out] */ __RPC__out long *plCount);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Item )( 
            __RPC__in IDSFResourceList * This,
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt DSFFullResourceDescriptor	**ppDSFFullResourceDescriptor);
        
        /* [hidden][propget][id] */ HRESULT ( __stdcall *get__NewEnum )( 
            __RPC__in IDSFResourceList * This,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Add )( 
            __RPC__in IDSFResourceList * This,
            /* [in] */ __RPC__in DSFFullResourceDescriptor	*pDSFFullResourceDescriptor,
            /* [optional][in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Remove )( 
            __RPC__in IDSFResourceList * This,
            /* [in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Clear )( 
            __RPC__in IDSFResourceList * This);
        
        END_INTERFACE
    } IDSFResourceListVtbl;

    interface IDSFResourceList
    {
        CONST_VTBL struct IDSFResourceListVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFResourceList_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFResourceList_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFResourceList_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFResourceList_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFResourceList_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFResourceList_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFResourceList_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFResourceList_get_Count(This,plCount)	\
    ( (This)->lpVtbl -> get_Count(This,plCount) ) 

#define IDSFResourceList_get_Item(This,Index,ppDSFFullResourceDescriptor)	\
    ( (This)->lpVtbl -> get_Item(This,Index,ppDSFFullResourceDescriptor) ) 

#define IDSFResourceList_get__NewEnum(This,ppunkEnum)	\
    ( (This)->lpVtbl -> get__NewEnum(This,ppunkEnum) ) 

#define IDSFResourceList_Add(This,pDSFFullResourceDescriptor,Index)	\
    ( (This)->lpVtbl -> Add(This,pDSFFullResourceDescriptor,Index) ) 

#define IDSFResourceList_Remove(This,Index)	\
    ( (This)->lpVtbl -> Remove(This,Index) ) 

#define IDSFResourceList_Clear(This)	\
    ( (This)->lpVtbl -> Clear(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFResourceList_INTERFACE_DEFINED__ */


#ifndef __IDSFFullResourceDescriptor_INTERFACE_DEFINED__
#define __IDSFFullResourceDescriptor_INTERFACE_DEFINED__

/* interface IDSFFullResourceDescriptor */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFFullResourceDescriptor;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E466F6A1-269F-4E18-94E6-7D2F02779417")
    IDSFFullResourceDescriptor : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_InterfaceType( 
            /* [retval][out] */ __RPC__out DSFInterfaceType *pType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_InterfaceType( 
            /* [in] */ DSFInterfaceType Type) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_BusNumber( 
            /* [retval][out] */ __RPC__out long *plBusNumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_BusNumber( 
            /* [in] */ long lBusNumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_PartialResourceList( 
            /* [retval][out] */ __RPC__deref_out_opt DSFPartialResourceList	**ppDSFPartialResourceList) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFFullResourceDescriptorVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFFullResourceDescriptor * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFFullResourceDescriptor * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFFullResourceDescriptor * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFFullResourceDescriptor * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFFullResourceDescriptor * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFFullResourceDescriptor * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFFullResourceDescriptor * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_InterfaceType )( 
            __RPC__in IDSFFullResourceDescriptor * This,
            /* [retval][out] */ __RPC__out DSFInterfaceType *pType);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_InterfaceType )( 
            __RPC__in IDSFFullResourceDescriptor * This,
            /* [in] */ DSFInterfaceType Type);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_BusNumber )( 
            __RPC__in IDSFFullResourceDescriptor * This,
            /* [retval][out] */ __RPC__out long *plBusNumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_BusNumber )( 
            __RPC__in IDSFFullResourceDescriptor * This,
            /* [in] */ long lBusNumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_PartialResourceList )( 
            __RPC__in IDSFFullResourceDescriptor * This,
            /* [retval][out] */ __RPC__deref_out_opt DSFPartialResourceList	**ppDSFPartialResourceList);
        
        END_INTERFACE
    } IDSFFullResourceDescriptorVtbl;

    interface IDSFFullResourceDescriptor
    {
        CONST_VTBL struct IDSFFullResourceDescriptorVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFFullResourceDescriptor_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFFullResourceDescriptor_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFFullResourceDescriptor_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFFullResourceDescriptor_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFFullResourceDescriptor_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFFullResourceDescriptor_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFFullResourceDescriptor_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFFullResourceDescriptor_get_InterfaceType(This,pType)	\
    ( (This)->lpVtbl -> get_InterfaceType(This,pType) ) 

#define IDSFFullResourceDescriptor_put_InterfaceType(This,Type)	\
    ( (This)->lpVtbl -> put_InterfaceType(This,Type) ) 

#define IDSFFullResourceDescriptor_get_BusNumber(This,plBusNumber)	\
    ( (This)->lpVtbl -> get_BusNumber(This,plBusNumber) ) 

#define IDSFFullResourceDescriptor_put_BusNumber(This,lBusNumber)	\
    ( (This)->lpVtbl -> put_BusNumber(This,lBusNumber) ) 

#define IDSFFullResourceDescriptor_get_PartialResourceList(This,ppDSFPartialResourceList)	\
    ( (This)->lpVtbl -> get_PartialResourceList(This,ppDSFPartialResourceList) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFFullResourceDescriptor_INTERFACE_DEFINED__ */


#ifndef __IDSFDevice_INTERFACE_DEFINED__
#define __IDSFDevice_INTERFACE_DEFINED__

/* interface IDSFDevice */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFDevice;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E3B2A057-2A23-4ABE-8188-9FB655131823")
    IDSFDevice : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Children( 
            /* [retval][out] */ __RPC__deref_out_opt DSFDevices	**ppDSFDevices) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Enabled( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEnabled) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Enabled( 
            /* [in] */ VARIANT_BOOL fvarEnabled) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Guid( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrGuid) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Guid( 
            /* [in] */ __RPC__in BSTR bstrGuid) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall HasObject( 
            /* [in] */ __RPC__in BSTR CLSID,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pvBool) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Hrm( 
            /* [in] */ DSFResourceType ResType,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkHrm) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Inserted( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarInserted) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Inserted( 
            /* [in] */ VARIANT_BOOL fvarInserted) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Name( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Name( 
            /* [in] */ __RPC__in BSTR bstrName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Next( 
            /* [retval][out] */ __RPC__deref_out_opt DSFDevice	**ppDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Next( 
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT __stdcall putref_Next( 
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Object( 
            /* [in] */ __RPC__in BSTR GUID,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkObject) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Object( 
            /* [in] */ __RPC__in BSTR GUID,
            /* [in] */ __RPC__in_opt IUnknown *punkObject) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT __stdcall putref_Object( 
            /* [in] */ __RPC__in BSTR GUID,
            /* [in] */ __RPC__in_opt IUnknown *punkObject) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_OpMode( 
            /* [retval][out] */ __RPC__out DSFOpMode *pMode) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_OpMode( 
            /* [in] */ DSFOpMode Mode) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Parent( 
            /* [retval][out] */ __RPC__deref_out_opt DSFDevice	**ppDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Parent( 
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT __stdcall putref_Parent( 
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Persistent( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarPersistent) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Persistent( 
            /* [in] */ VARIANT_BOOL fvarPersistent) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_State( 
            /* [retval][out] */ __RPC__out long *plState) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_SystemDevice( 
            /* [retval][out] */ __RPC__deref_out_opt IDSFSystemDevice **ppSystemDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall SetStateBit( 
            /* [in] */ long Bit,
            /* [in] */ DSFBitOp BitOp) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall StartFaultInjection( 
            /* [in] */ DSFResourceType ResType,
            /* [in] */ VARIANT Params,
            /* [retval][out] */ __RPC__out VARIANT *pvarContext) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall StopFaultInjection( 
            /* [in] */ DSFResourceType ResType,
            /* [in] */ VARIANT Context) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFDeviceVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFDevice * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFDevice * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFDevice * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFDevice * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Children )( 
            __RPC__in IDSFDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt DSFDevices	**ppDSFDevices);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Enabled )( 
            __RPC__in IDSFDevice * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEnabled);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Enabled )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ VARIANT_BOOL fvarEnabled);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Guid )( 
            __RPC__in IDSFDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrGuid);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Guid )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ __RPC__in BSTR bstrGuid);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *HasObject )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ __RPC__in BSTR CLSID,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pvBool);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Hrm )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ DSFResourceType ResType,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkHrm);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Inserted )( 
            __RPC__in IDSFDevice * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarInserted);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Inserted )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ VARIANT_BOOL fvarInserted);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Name )( 
            __RPC__in IDSFDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrName);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Name )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ __RPC__in BSTR bstrName);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Next )( 
            __RPC__in IDSFDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt DSFDevice	**ppDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Next )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT ( __stdcall *putref_Next )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Object )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ __RPC__in BSTR GUID,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkObject);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Object )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ __RPC__in BSTR GUID,
            /* [in] */ __RPC__in_opt IUnknown *punkObject);
        
        /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT ( __stdcall *putref_Object )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ __RPC__in BSTR GUID,
            /* [in] */ __RPC__in_opt IUnknown *punkObject);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_OpMode )( 
            __RPC__in IDSFDevice * This,
            /* [retval][out] */ __RPC__out DSFOpMode *pMode);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_OpMode )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ DSFOpMode Mode);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Parent )( 
            __RPC__in IDSFDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt DSFDevice	**ppDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Parent )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT ( __stdcall *putref_Parent )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Persistent )( 
            __RPC__in IDSFDevice * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarPersistent);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Persistent )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ VARIANT_BOOL fvarPersistent);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_State )( 
            __RPC__in IDSFDevice * This,
            /* [retval][out] */ __RPC__out long *plState);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_SystemDevice )( 
            __RPC__in IDSFDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt IDSFSystemDevice **ppSystemDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *SetStateBit )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ long Bit,
            /* [in] */ DSFBitOp BitOp);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *StartFaultInjection )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ DSFResourceType ResType,
            /* [in] */ VARIANT Params,
            /* [retval][out] */ __RPC__out VARIANT *pvarContext);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *StopFaultInjection )( 
            __RPC__in IDSFDevice * This,
            /* [in] */ DSFResourceType ResType,
            /* [in] */ VARIANT Context);
        
        END_INTERFACE
    } IDSFDeviceVtbl;

    interface IDSFDevice
    {
        CONST_VTBL struct IDSFDeviceVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFDevice_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFDevice_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFDevice_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFDevice_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFDevice_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFDevice_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFDevice_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFDevice_get_Children(This,ppDSFDevices)	\
    ( (This)->lpVtbl -> get_Children(This,ppDSFDevices) ) 

#define IDSFDevice_get_Enabled(This,pfvarEnabled)	\
    ( (This)->lpVtbl -> get_Enabled(This,pfvarEnabled) ) 

#define IDSFDevice_put_Enabled(This,fvarEnabled)	\
    ( (This)->lpVtbl -> put_Enabled(This,fvarEnabled) ) 

#define IDSFDevice_get_Guid(This,pbstrGuid)	\
    ( (This)->lpVtbl -> get_Guid(This,pbstrGuid) ) 

#define IDSFDevice_put_Guid(This,bstrGuid)	\
    ( (This)->lpVtbl -> put_Guid(This,bstrGuid) ) 

#define IDSFDevice_HasObject(This,CLSID,pvBool)	\
    ( (This)->lpVtbl -> HasObject(This,CLSID,pvBool) ) 

#define IDSFDevice_get_Hrm(This,ResType,ppunkHrm)	\
    ( (This)->lpVtbl -> get_Hrm(This,ResType,ppunkHrm) ) 

#define IDSFDevice_get_Inserted(This,pfvarInserted)	\
    ( (This)->lpVtbl -> get_Inserted(This,pfvarInserted) ) 

#define IDSFDevice_put_Inserted(This,fvarInserted)	\
    ( (This)->lpVtbl -> put_Inserted(This,fvarInserted) ) 

#define IDSFDevice_get_Name(This,pbstrName)	\
    ( (This)->lpVtbl -> get_Name(This,pbstrName) ) 

#define IDSFDevice_put_Name(This,bstrName)	\
    ( (This)->lpVtbl -> put_Name(This,bstrName) ) 

#define IDSFDevice_get_Next(This,ppDSFDevice)	\
    ( (This)->lpVtbl -> get_Next(This,ppDSFDevice) ) 

#define IDSFDevice_put_Next(This,pDSFDevice)	\
    ( (This)->lpVtbl -> put_Next(This,pDSFDevice) ) 

#define IDSFDevice_putref_Next(This,pDSFDevice)	\
    ( (This)->lpVtbl -> putref_Next(This,pDSFDevice) ) 

#define IDSFDevice_get_Object(This,GUID,ppunkObject)	\
    ( (This)->lpVtbl -> get_Object(This,GUID,ppunkObject) ) 

#define IDSFDevice_put_Object(This,GUID,punkObject)	\
    ( (This)->lpVtbl -> put_Object(This,GUID,punkObject) ) 

#define IDSFDevice_putref_Object(This,GUID,punkObject)	\
    ( (This)->lpVtbl -> putref_Object(This,GUID,punkObject) ) 

#define IDSFDevice_get_OpMode(This,pMode)	\
    ( (This)->lpVtbl -> get_OpMode(This,pMode) ) 

#define IDSFDevice_put_OpMode(This,Mode)	\
    ( (This)->lpVtbl -> put_OpMode(This,Mode) ) 

#define IDSFDevice_get_Parent(This,ppDSFDevice)	\
    ( (This)->lpVtbl -> get_Parent(This,ppDSFDevice) ) 

#define IDSFDevice_put_Parent(This,pDSFDevice)	\
    ( (This)->lpVtbl -> put_Parent(This,pDSFDevice) ) 

#define IDSFDevice_putref_Parent(This,pDSFDevice)	\
    ( (This)->lpVtbl -> putref_Parent(This,pDSFDevice) ) 

#define IDSFDevice_get_Persistent(This,pfvarPersistent)	\
    ( (This)->lpVtbl -> get_Persistent(This,pfvarPersistent) ) 

#define IDSFDevice_put_Persistent(This,fvarPersistent)	\
    ( (This)->lpVtbl -> put_Persistent(This,fvarPersistent) ) 

#define IDSFDevice_get_State(This,plState)	\
    ( (This)->lpVtbl -> get_State(This,plState) ) 

#define IDSFDevice_get_SystemDevice(This,ppSystemDevice)	\
    ( (This)->lpVtbl -> get_SystemDevice(This,ppSystemDevice) ) 

#define IDSFDevice_SetStateBit(This,Bit,BitOp)	\
    ( (This)->lpVtbl -> SetStateBit(This,Bit,BitOp) ) 

#define IDSFDevice_StartFaultInjection(This,ResType,Params,pvarContext)	\
    ( (This)->lpVtbl -> StartFaultInjection(This,ResType,Params,pvarContext) ) 

#define IDSFDevice_StopFaultInjection(This,ResType,Context)	\
    ( (This)->lpVtbl -> StopFaultInjection(This,ResType,Context) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFDevice_INTERFACE_DEFINED__ */


#ifndef __IDSFDeviceCaps_INTERFACE_DEFINED__
#define __IDSFDeviceCaps_INTERFACE_DEFINED__

/* interface IDSFDeviceCaps */
/* [object][helpstringcontext][helpcontext][helpstring][nonextensible][uuid] */ 


EXTERN_C const IID IID_IDSFDeviceCaps;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("766705D3-5109-460D-80B5-30D97B17D867")
    IDSFDeviceCaps : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Version( 
            /* [retval][out] */ __RPC__out short *psVersion) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Version( 
            /* [in] */ short sVersion) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceD1( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarDeviceD1) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceD1( 
            /* [in] */ VARIANT_BOOL fvarDeviceD1) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceD2( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarDeviceD2) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceD2( 
            /* [in] */ VARIANT_BOOL fvarDeviceD2) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_LockSupported( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarLockSupported) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_LockSupported( 
            /* [in] */ VARIANT_BOOL fvarLockSupported) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_EjectSupported( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEjectSupported) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_EjectSupported( 
            /* [in] */ VARIANT_BOOL fvarEjectSupported) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Removable( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarRemovable) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Removable( 
            /* [in] */ VARIANT_BOOL fvarRemovable) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DockDevice( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarDockDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DockDevice( 
            /* [in] */ VARIANT_BOOL fvarDockDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_UniqueID( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarUniqueID) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_UniqueID( 
            /* [in] */ VARIANT_BOOL fvarUniqueID) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_SilentInstall( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarSilentInstall) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_SilentInstall( 
            /* [in] */ VARIANT_BOOL fvarSilentInstall) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_RawDeviceOK( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarRawDeviceOK) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_RawDeviceOK( 
            /* [in] */ VARIANT_BOOL fvarRawDeviceOK) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_SurpriseRemovalOK( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarSurpriseRemovalOK) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_SurpriseRemovalOK( 
            /* [in] */ VARIANT_BOOL fvarSurpriseRemovalOK) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_WakeFromD0( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWakeFromD0) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_WakeFromD0( 
            /* [in] */ VARIANT_BOOL fvarWakeFromD0) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_WakeFromD1( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWakeFromD1) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_WakeFromD1( 
            /* [in] */ VARIANT_BOOL fvarWakeFromD1) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_WakeFromD2( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWakeFromD2) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_WakeFromD2( 
            /* [in] */ VARIANT_BOOL fvarWakeFromD2) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_WakeFromD3( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWakeFromD3) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_WakeFromD3( 
            /* [in] */ VARIANT_BOOL fvarWakeFromD3) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_HardwareDisabled( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarHardwareDisabled) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_HardwareDisabled( 
            /* [in] */ VARIANT_BOOL fvarHardwareDisabled) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_NonDynamic( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarNonDynamic) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_NonDynamic( 
            /* [in] */ VARIANT_BOOL fvarNonDynamic) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_WarmEjectSupported( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWarmEjectSupported) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_WarmEjectSupported( 
            /* [in] */ VARIANT_BOOL fvarWarmEjectSupported) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_NoDisplayInUI( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarNoDisplayInUI) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_NoDisplayInUI( 
            /* [in] */ VARIANT_BOOL fvarNoDisplayInUI) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Reserved( 
            /* [retval][out] */ __RPC__out short *psReserved) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Reserved( 
            /* [in] */ short sReserved) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Address( 
            /* [retval][out] */ __RPC__out long *plAddress) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Address( 
            /* [in] */ long lAddress) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_UINumber( 
            /* [retval][out] */ __RPC__out long *plUINumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_UINumber( 
            /* [in] */ long lUINumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceState( 
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaDeviceState) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceState( 
            /* [in] */ __RPC__in SAFEARRAY * psaDeviceState) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_SystemWake( 
            /* [retval][out] */ __RPC__out DSFSystemPowerState *pPowerState) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_SystemWake( 
            /* [in] */ DSFSystemPowerState PowerState) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceWake( 
            /* [retval][out] */ __RPC__out DSFDevicePowerState *pPowerState) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceWake( 
            /* [in] */ DSFDevicePowerState PowerState) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_D1Latency( 
            /* [retval][out] */ __RPC__out long *plD1Latency) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_D1Latency( 
            /* [in] */ long lD1Latency) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_D2Latency( 
            /* [retval][out] */ __RPC__out long *plD2Latency) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_D2Latency( 
            /* [in] */ long lD2Latency) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_D3Latency( 
            /* [retval][out] */ __RPC__out long *plD3Latency) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_D3Latency( 
            /* [in] */ long lD3Latency) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFDeviceCapsVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFDeviceCaps * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFDeviceCaps * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFDeviceCaps * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Version )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out short *psVersion);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Version )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ short sVersion);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceD1 )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarDeviceD1);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceD1 )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarDeviceD1);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceD2 )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarDeviceD2);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceD2 )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarDeviceD2);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_LockSupported )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarLockSupported);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_LockSupported )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarLockSupported);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_EjectSupported )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEjectSupported);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_EjectSupported )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarEjectSupported);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Removable )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarRemovable);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Removable )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarRemovable);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DockDevice )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarDockDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DockDevice )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarDockDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_UniqueID )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarUniqueID);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_UniqueID )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarUniqueID);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_SilentInstall )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarSilentInstall);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_SilentInstall )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarSilentInstall);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_RawDeviceOK )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarRawDeviceOK);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_RawDeviceOK )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarRawDeviceOK);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_SurpriseRemovalOK )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarSurpriseRemovalOK);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_SurpriseRemovalOK )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarSurpriseRemovalOK);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_WakeFromD0 )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWakeFromD0);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_WakeFromD0 )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarWakeFromD0);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_WakeFromD1 )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWakeFromD1);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_WakeFromD1 )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarWakeFromD1);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_WakeFromD2 )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWakeFromD2);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_WakeFromD2 )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarWakeFromD2);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_WakeFromD3 )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWakeFromD3);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_WakeFromD3 )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarWakeFromD3);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_HardwareDisabled )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarHardwareDisabled);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_HardwareDisabled )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarHardwareDisabled);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_NonDynamic )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarNonDynamic);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_NonDynamic )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarNonDynamic);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_WarmEjectSupported )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWarmEjectSupported);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_WarmEjectSupported )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarWarmEjectSupported);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_NoDisplayInUI )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarNoDisplayInUI);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_NoDisplayInUI )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ VARIANT_BOOL fvarNoDisplayInUI);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Reserved )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out short *psReserved);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Reserved )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ short sReserved);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Address )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out long *plAddress);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Address )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ long lAddress);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_UINumber )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out long *plUINumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_UINumber )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ long lUINumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceState )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaDeviceState);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceState )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ __RPC__in SAFEARRAY * psaDeviceState);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_SystemWake )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out DSFSystemPowerState *pPowerState);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_SystemWake )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ DSFSystemPowerState PowerState);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceWake )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out DSFDevicePowerState *pPowerState);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceWake )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ DSFDevicePowerState PowerState);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_D1Latency )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out long *plD1Latency);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_D1Latency )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ long lD1Latency);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_D2Latency )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out long *plD2Latency);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_D2Latency )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ long lD2Latency);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_D3Latency )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [retval][out] */ __RPC__out long *plD3Latency);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_D3Latency )( 
            __RPC__in IDSFDeviceCaps * This,
            /* [in] */ long lD3Latency);
        
        END_INTERFACE
    } IDSFDeviceCapsVtbl;

    interface IDSFDeviceCaps
    {
        CONST_VTBL struct IDSFDeviceCapsVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFDeviceCaps_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFDeviceCaps_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFDeviceCaps_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFDeviceCaps_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFDeviceCaps_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFDeviceCaps_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFDeviceCaps_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFDeviceCaps_get_Version(This,psVersion)	\
    ( (This)->lpVtbl -> get_Version(This,psVersion) ) 

#define IDSFDeviceCaps_put_Version(This,sVersion)	\
    ( (This)->lpVtbl -> put_Version(This,sVersion) ) 

#define IDSFDeviceCaps_get_DeviceD1(This,pfvarDeviceD1)	\
    ( (This)->lpVtbl -> get_DeviceD1(This,pfvarDeviceD1) ) 

#define IDSFDeviceCaps_put_DeviceD1(This,fvarDeviceD1)	\
    ( (This)->lpVtbl -> put_DeviceD1(This,fvarDeviceD1) ) 

#define IDSFDeviceCaps_get_DeviceD2(This,pfvarDeviceD2)	\
    ( (This)->lpVtbl -> get_DeviceD2(This,pfvarDeviceD2) ) 

#define IDSFDeviceCaps_put_DeviceD2(This,fvarDeviceD2)	\
    ( (This)->lpVtbl -> put_DeviceD2(This,fvarDeviceD2) ) 

#define IDSFDeviceCaps_get_LockSupported(This,pfvarLockSupported)	\
    ( (This)->lpVtbl -> get_LockSupported(This,pfvarLockSupported) ) 

#define IDSFDeviceCaps_put_LockSupported(This,fvarLockSupported)	\
    ( (This)->lpVtbl -> put_LockSupported(This,fvarLockSupported) ) 

#define IDSFDeviceCaps_get_EjectSupported(This,pfvarEjectSupported)	\
    ( (This)->lpVtbl -> get_EjectSupported(This,pfvarEjectSupported) ) 

#define IDSFDeviceCaps_put_EjectSupported(This,fvarEjectSupported)	\
    ( (This)->lpVtbl -> put_EjectSupported(This,fvarEjectSupported) ) 

#define IDSFDeviceCaps_get_Removable(This,pfvarRemovable)	\
    ( (This)->lpVtbl -> get_Removable(This,pfvarRemovable) ) 

#define IDSFDeviceCaps_put_Removable(This,fvarRemovable)	\
    ( (This)->lpVtbl -> put_Removable(This,fvarRemovable) ) 

#define IDSFDeviceCaps_get_DockDevice(This,pfvarDockDevice)	\
    ( (This)->lpVtbl -> get_DockDevice(This,pfvarDockDevice) ) 

#define IDSFDeviceCaps_put_DockDevice(This,fvarDockDevice)	\
    ( (This)->lpVtbl -> put_DockDevice(This,fvarDockDevice) ) 

#define IDSFDeviceCaps_get_UniqueID(This,pfvarUniqueID)	\
    ( (This)->lpVtbl -> get_UniqueID(This,pfvarUniqueID) ) 

#define IDSFDeviceCaps_put_UniqueID(This,fvarUniqueID)	\
    ( (This)->lpVtbl -> put_UniqueID(This,fvarUniqueID) ) 

#define IDSFDeviceCaps_get_SilentInstall(This,pfvarSilentInstall)	\
    ( (This)->lpVtbl -> get_SilentInstall(This,pfvarSilentInstall) ) 

#define IDSFDeviceCaps_put_SilentInstall(This,fvarSilentInstall)	\
    ( (This)->lpVtbl -> put_SilentInstall(This,fvarSilentInstall) ) 

#define IDSFDeviceCaps_get_RawDeviceOK(This,pfvarRawDeviceOK)	\
    ( (This)->lpVtbl -> get_RawDeviceOK(This,pfvarRawDeviceOK) ) 

#define IDSFDeviceCaps_put_RawDeviceOK(This,fvarRawDeviceOK)	\
    ( (This)->lpVtbl -> put_RawDeviceOK(This,fvarRawDeviceOK) ) 

#define IDSFDeviceCaps_get_SurpriseRemovalOK(This,pfvarSurpriseRemovalOK)	\
    ( (This)->lpVtbl -> get_SurpriseRemovalOK(This,pfvarSurpriseRemovalOK) ) 

#define IDSFDeviceCaps_put_SurpriseRemovalOK(This,fvarSurpriseRemovalOK)	\
    ( (This)->lpVtbl -> put_SurpriseRemovalOK(This,fvarSurpriseRemovalOK) ) 

#define IDSFDeviceCaps_get_WakeFromD0(This,pfvarWakeFromD0)	\
    ( (This)->lpVtbl -> get_WakeFromD0(This,pfvarWakeFromD0) ) 

#define IDSFDeviceCaps_put_WakeFromD0(This,fvarWakeFromD0)	\
    ( (This)->lpVtbl -> put_WakeFromD0(This,fvarWakeFromD0) ) 

#define IDSFDeviceCaps_get_WakeFromD1(This,pfvarWakeFromD1)	\
    ( (This)->lpVtbl -> get_WakeFromD1(This,pfvarWakeFromD1) ) 

#define IDSFDeviceCaps_put_WakeFromD1(This,fvarWakeFromD1)	\
    ( (This)->lpVtbl -> put_WakeFromD1(This,fvarWakeFromD1) ) 

#define IDSFDeviceCaps_get_WakeFromD2(This,pfvarWakeFromD2)	\
    ( (This)->lpVtbl -> get_WakeFromD2(This,pfvarWakeFromD2) ) 

#define IDSFDeviceCaps_put_WakeFromD2(This,fvarWakeFromD2)	\
    ( (This)->lpVtbl -> put_WakeFromD2(This,fvarWakeFromD2) ) 

#define IDSFDeviceCaps_get_WakeFromD3(This,pfvarWakeFromD3)	\
    ( (This)->lpVtbl -> get_WakeFromD3(This,pfvarWakeFromD3) ) 

#define IDSFDeviceCaps_put_WakeFromD3(This,fvarWakeFromD3)	\
    ( (This)->lpVtbl -> put_WakeFromD3(This,fvarWakeFromD3) ) 

#define IDSFDeviceCaps_get_HardwareDisabled(This,pfvarHardwareDisabled)	\
    ( (This)->lpVtbl -> get_HardwareDisabled(This,pfvarHardwareDisabled) ) 

#define IDSFDeviceCaps_put_HardwareDisabled(This,fvarHardwareDisabled)	\
    ( (This)->lpVtbl -> put_HardwareDisabled(This,fvarHardwareDisabled) ) 

#define IDSFDeviceCaps_get_NonDynamic(This,pfvarNonDynamic)	\
    ( (This)->lpVtbl -> get_NonDynamic(This,pfvarNonDynamic) ) 

#define IDSFDeviceCaps_put_NonDynamic(This,fvarNonDynamic)	\
    ( (This)->lpVtbl -> put_NonDynamic(This,fvarNonDynamic) ) 

#define IDSFDeviceCaps_get_WarmEjectSupported(This,pfvarWarmEjectSupported)	\
    ( (This)->lpVtbl -> get_WarmEjectSupported(This,pfvarWarmEjectSupported) ) 

#define IDSFDeviceCaps_put_WarmEjectSupported(This,fvarWarmEjectSupported)	\
    ( (This)->lpVtbl -> put_WarmEjectSupported(This,fvarWarmEjectSupported) ) 

#define IDSFDeviceCaps_get_NoDisplayInUI(This,pfvarNoDisplayInUI)	\
    ( (This)->lpVtbl -> get_NoDisplayInUI(This,pfvarNoDisplayInUI) ) 

#define IDSFDeviceCaps_put_NoDisplayInUI(This,fvarNoDisplayInUI)	\
    ( (This)->lpVtbl -> put_NoDisplayInUI(This,fvarNoDisplayInUI) ) 

#define IDSFDeviceCaps_get_Reserved(This,psReserved)	\
    ( (This)->lpVtbl -> get_Reserved(This,psReserved) ) 

#define IDSFDeviceCaps_put_Reserved(This,sReserved)	\
    ( (This)->lpVtbl -> put_Reserved(This,sReserved) ) 

#define IDSFDeviceCaps_get_Address(This,plAddress)	\
    ( (This)->lpVtbl -> get_Address(This,plAddress) ) 

#define IDSFDeviceCaps_put_Address(This,lAddress)	\
    ( (This)->lpVtbl -> put_Address(This,lAddress) ) 

#define IDSFDeviceCaps_get_UINumber(This,plUINumber)	\
    ( (This)->lpVtbl -> get_UINumber(This,plUINumber) ) 

#define IDSFDeviceCaps_put_UINumber(This,lUINumber)	\
    ( (This)->lpVtbl -> put_UINumber(This,lUINumber) ) 

#define IDSFDeviceCaps_get_DeviceState(This,ppsaDeviceState)	\
    ( (This)->lpVtbl -> get_DeviceState(This,ppsaDeviceState) ) 

#define IDSFDeviceCaps_put_DeviceState(This,psaDeviceState)	\
    ( (This)->lpVtbl -> put_DeviceState(This,psaDeviceState) ) 

#define IDSFDeviceCaps_get_SystemWake(This,pPowerState)	\
    ( (This)->lpVtbl -> get_SystemWake(This,pPowerState) ) 

#define IDSFDeviceCaps_put_SystemWake(This,PowerState)	\
    ( (This)->lpVtbl -> put_SystemWake(This,PowerState) ) 

#define IDSFDeviceCaps_get_DeviceWake(This,pPowerState)	\
    ( (This)->lpVtbl -> get_DeviceWake(This,pPowerState) ) 

#define IDSFDeviceCaps_put_DeviceWake(This,PowerState)	\
    ( (This)->lpVtbl -> put_DeviceWake(This,PowerState) ) 

#define IDSFDeviceCaps_get_D1Latency(This,plD1Latency)	\
    ( (This)->lpVtbl -> get_D1Latency(This,plD1Latency) ) 

#define IDSFDeviceCaps_put_D1Latency(This,lD1Latency)	\
    ( (This)->lpVtbl -> put_D1Latency(This,lD1Latency) ) 

#define IDSFDeviceCaps_get_D2Latency(This,plD2Latency)	\
    ( (This)->lpVtbl -> get_D2Latency(This,plD2Latency) ) 

#define IDSFDeviceCaps_put_D2Latency(This,lD2Latency)	\
    ( (This)->lpVtbl -> put_D2Latency(This,lD2Latency) ) 

#define IDSFDeviceCaps_get_D3Latency(This,plD3Latency)	\
    ( (This)->lpVtbl -> get_D3Latency(This,plD3Latency) ) 

#define IDSFDeviceCaps_put_D3Latency(This,lD3Latency)	\
    ( (This)->lpVtbl -> put_D3Latency(This,lD3Latency) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFDeviceCaps_INTERFACE_DEFINED__ */


#ifndef __IDSFDeviceEvents_INTERFACE_DEFINED__
#define __IDSFDeviceEvents_INTERFACE_DEFINED__

/* interface IDSFDeviceEvents */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFDeviceEvents;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("3E706BA6-2C8F-4441-8931-1ACEB318112F")
    IDSFDeviceEvents : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Disabled( void) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Enabled( void) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Inserted( void) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall OpModeChange( 
            /* [in] */ DSFOpMode NewOpMode) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Removed( void) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall ResourcesSet( void) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall StartFaultInjection( 
            /* [in] */ DSFResourceType ResType,
            /* [in] */ VARIANT Params,
            /* [retval][out] */ __RPC__out VARIANT *pvarContext) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall StopFaultInjection( 
            /* [in] */ DSFResourceType ResType,
            /* [in] */ VARIANT Context) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall StateChange( 
            /* [in] */ long Bit,
            /* [in] */ short Value,
            /* [in] */ long NewState) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFDeviceEventsVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFDeviceEvents * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFDeviceEvents * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFDeviceEvents * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFDeviceEvents * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFDeviceEvents * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFDeviceEvents * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFDeviceEvents * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Disabled )( 
            __RPC__in IDSFDeviceEvents * This);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Enabled )( 
            __RPC__in IDSFDeviceEvents * This);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Inserted )( 
            __RPC__in IDSFDeviceEvents * This);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *OpModeChange )( 
            __RPC__in IDSFDeviceEvents * This,
            /* [in] */ DSFOpMode NewOpMode);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Removed )( 
            __RPC__in IDSFDeviceEvents * This);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *ResourcesSet )( 
            __RPC__in IDSFDeviceEvents * This);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *StartFaultInjection )( 
            __RPC__in IDSFDeviceEvents * This,
            /* [in] */ DSFResourceType ResType,
            /* [in] */ VARIANT Params,
            /* [retval][out] */ __RPC__out VARIANT *pvarContext);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *StopFaultInjection )( 
            __RPC__in IDSFDeviceEvents * This,
            /* [in] */ DSFResourceType ResType,
            /* [in] */ VARIANT Context);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *StateChange )( 
            __RPC__in IDSFDeviceEvents * This,
            /* [in] */ long Bit,
            /* [in] */ short Value,
            /* [in] */ long NewState);
        
        END_INTERFACE
    } IDSFDeviceEventsVtbl;

    interface IDSFDeviceEvents
    {
        CONST_VTBL struct IDSFDeviceEventsVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFDeviceEvents_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFDeviceEvents_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFDeviceEvents_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFDeviceEvents_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFDeviceEvents_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFDeviceEvents_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFDeviceEvents_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFDeviceEvents_Disabled(This)	\
    ( (This)->lpVtbl -> Disabled(This) ) 

#define IDSFDeviceEvents_Enabled(This)	\
    ( (This)->lpVtbl -> Enabled(This) ) 

#define IDSFDeviceEvents_Inserted(This)	\
    ( (This)->lpVtbl -> Inserted(This) ) 

#define IDSFDeviceEvents_OpModeChange(This,NewOpMode)	\
    ( (This)->lpVtbl -> OpModeChange(This,NewOpMode) ) 

#define IDSFDeviceEvents_Removed(This)	\
    ( (This)->lpVtbl -> Removed(This) ) 

#define IDSFDeviceEvents_ResourcesSet(This)	\
    ( (This)->lpVtbl -> ResourcesSet(This) ) 

#define IDSFDeviceEvents_StartFaultInjection(This,ResType,Params,pvarContext)	\
    ( (This)->lpVtbl -> StartFaultInjection(This,ResType,Params,pvarContext) ) 

#define IDSFDeviceEvents_StopFaultInjection(This,ResType,Context)	\
    ( (This)->lpVtbl -> StopFaultInjection(This,ResType,Context) ) 

#define IDSFDeviceEvents_StateChange(This,Bit,Value,NewState)	\
    ( (This)->lpVtbl -> StateChange(This,Bit,Value,NewState) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFDeviceEvents_INTERFACE_DEFINED__ */


#ifndef __IDSF_INTERFACE_DEFINED__
#define __IDSF_INTERFACE_DEFINED__

/* interface IDSF */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSF;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("959B9C1D-5644-4835-8297-F435B3FA80DD")
    IDSF : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Devices( 
            /* [retval][out] */ __RPC__deref_out_opt DSFDevices	**ppDSFDevices) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Log( 
            /* [retval][out] */ __RPC__deref_out_opt DSFLog	**ppDSFLog) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Version( 
            /* [retval][out] */ __RPC__deref_out_opt DSFVersion	**ppDSFVersion) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall CreateDevice( 
            /* [in] */ __RPC__in BSTR bstrDeviceID,
            /* [in] */ __RPC__in BSTR bstrInstanceID,
            /* [in] */ VARIANT HardwareIDs,
            /* [in] */ VARIANT CompatibleIDs,
            /* [in] */ __RPC__in DSFResourceList	*RawResources,
            /* [in] */ __RPC__in DSFResourceList	*XlatedResources,
            /* [in] */ __RPC__in DSFDeviceCaps	*DeviceCapabilities,
            /* [retval][out] */ __RPC__deref_out_opt DSFDevice	**ppDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall WriteState( 
            /* [in] */ DSFStateType Type,
            /* [in] */ __RPC__in BSTR Guid,
            /* [in] */ __RPC__in DSFPropertyBag	*pDSFPropertyBag) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall ReadState( 
            /* [in] */ DSFStateType Type,
            /* [in] */ __RPC__in BSTR Guid,
            /* [retval][out] */ __RPC__deref_out_opt DSFPropertyBag	**ppDSFPropertyBag) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall DeleteState( 
            /* [in] */ DSFStateType Type,
            /* [in] */ __RPC__in BSTR Guid) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall SetDriverFlags( 
            /* [in] */ __RPC__in BSTR DriverGuid,
            /* [in] */ DSFFlagType FlagType,
            /* [in] */ unsigned long Flags) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall EnableDriverLogging( 
            /* [in] */ __RPC__in BSTR DriverGuid,
            /* [in] */ VARIANT_BOOL Enable) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall HotPlug( 
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice,
            /* [in] */ __RPC__in BSTR bstrBus,
            /* [retval][out] */ __RPC__deref_out_opt IDSFBus **ppiDSFBus) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSF * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSF * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSF * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSF * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSF * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSF * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSF * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Devices )( 
            __RPC__in IDSF * This,
            /* [retval][out] */ __RPC__deref_out_opt DSFDevices	**ppDSFDevices);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Log )( 
            __RPC__in IDSF * This,
            /* [retval][out] */ __RPC__deref_out_opt DSFLog	**ppDSFLog);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Version )( 
            __RPC__in IDSF * This,
            /* [retval][out] */ __RPC__deref_out_opt DSFVersion	**ppDSFVersion);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *CreateDevice )( 
            __RPC__in IDSF * This,
            /* [in] */ __RPC__in BSTR bstrDeviceID,
            /* [in] */ __RPC__in BSTR bstrInstanceID,
            /* [in] */ VARIANT HardwareIDs,
            /* [in] */ VARIANT CompatibleIDs,
            /* [in] */ __RPC__in DSFResourceList	*RawResources,
            /* [in] */ __RPC__in DSFResourceList	*XlatedResources,
            /* [in] */ __RPC__in DSFDeviceCaps	*DeviceCapabilities,
            /* [retval][out] */ __RPC__deref_out_opt DSFDevice	**ppDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *WriteState )( 
            __RPC__in IDSF * This,
            /* [in] */ DSFStateType Type,
            /* [in] */ __RPC__in BSTR Guid,
            /* [in] */ __RPC__in DSFPropertyBag	*pDSFPropertyBag);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *ReadState )( 
            __RPC__in IDSF * This,
            /* [in] */ DSFStateType Type,
            /* [in] */ __RPC__in BSTR Guid,
            /* [retval][out] */ __RPC__deref_out_opt DSFPropertyBag	**ppDSFPropertyBag);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *DeleteState )( 
            __RPC__in IDSF * This,
            /* [in] */ DSFStateType Type,
            /* [in] */ __RPC__in BSTR Guid);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *SetDriverFlags )( 
            __RPC__in IDSF * This,
            /* [in] */ __RPC__in BSTR DriverGuid,
            /* [in] */ DSFFlagType FlagType,
            /* [in] */ unsigned long Flags);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *EnableDriverLogging )( 
            __RPC__in IDSF * This,
            /* [in] */ __RPC__in BSTR DriverGuid,
            /* [in] */ VARIANT_BOOL Enable);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *HotPlug )( 
            __RPC__in IDSF * This,
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice,
            /* [in] */ __RPC__in BSTR bstrBus,
            /* [retval][out] */ __RPC__deref_out_opt IDSFBus **ppiDSFBus);
        
        END_INTERFACE
    } IDSFVtbl;

    interface IDSF
    {
        CONST_VTBL struct IDSFVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSF_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSF_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSF_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSF_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSF_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSF_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSF_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSF_get_Devices(This,ppDSFDevices)	\
    ( (This)->lpVtbl -> get_Devices(This,ppDSFDevices) ) 

#define IDSF_get_Log(This,ppDSFLog)	\
    ( (This)->lpVtbl -> get_Log(This,ppDSFLog) ) 

#define IDSF_get_Version(This,ppDSFVersion)	\
    ( (This)->lpVtbl -> get_Version(This,ppDSFVersion) ) 

#define IDSF_CreateDevice(This,bstrDeviceID,bstrInstanceID,HardwareIDs,CompatibleIDs,RawResources,XlatedResources,DeviceCapabilities,ppDSFDevice)	\
    ( (This)->lpVtbl -> CreateDevice(This,bstrDeviceID,bstrInstanceID,HardwareIDs,CompatibleIDs,RawResources,XlatedResources,DeviceCapabilities,ppDSFDevice) ) 

#define IDSF_WriteState(This,Type,Guid,pDSFPropertyBag)	\
    ( (This)->lpVtbl -> WriteState(This,Type,Guid,pDSFPropertyBag) ) 

#define IDSF_ReadState(This,Type,Guid,ppDSFPropertyBag)	\
    ( (This)->lpVtbl -> ReadState(This,Type,Guid,ppDSFPropertyBag) ) 

#define IDSF_DeleteState(This,Type,Guid)	\
    ( (This)->lpVtbl -> DeleteState(This,Type,Guid) ) 

#define IDSF_SetDriverFlags(This,DriverGuid,FlagType,Flags)	\
    ( (This)->lpVtbl -> SetDriverFlags(This,DriverGuid,FlagType,Flags) ) 

#define IDSF_EnableDriverLogging(This,DriverGuid,Enable)	\
    ( (This)->lpVtbl -> EnableDriverLogging(This,DriverGuid,Enable) ) 

#define IDSF_HotPlug(This,pDSFDevice,bstrBus,ppiDSFBus)	\
    ( (This)->lpVtbl -> HotPlug(This,pDSFDevice,bstrBus,ppiDSFBus) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSF_INTERFACE_DEFINED__ */


#ifndef __IDSFDevices_INTERFACE_DEFINED__
#define __IDSFDevices_INTERFACE_DEFINED__

/* interface IDSFDevices */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFDevices;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E75FB264-EA8A-4167-892B-F6F315249518")
    IDSFDevices : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Count( 
            /* [retval][out] */ __RPC__out long *plCount) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Item( 
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt DSFDevice	**ppDSFDevice) = 0;
        
        virtual /* [hidden][propget][id] */ HRESULT __stdcall get__NewEnum( 
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Refresh( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFDevicesVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFDevices * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFDevices * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFDevices * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFDevices * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFDevices * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFDevices * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFDevices * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Count )( 
            __RPC__in IDSFDevices * This,
            /* [retval][out] */ __RPC__out long *plCount);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Item )( 
            __RPC__in IDSFDevices * This,
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt DSFDevice	**ppDSFDevice);
        
        /* [hidden][propget][id] */ HRESULT ( __stdcall *get__NewEnum )( 
            __RPC__in IDSFDevices * This,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Refresh )( 
            __RPC__in IDSFDevices * This);
        
        END_INTERFACE
    } IDSFDevicesVtbl;

    interface IDSFDevices
    {
        CONST_VTBL struct IDSFDevicesVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFDevices_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFDevices_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFDevices_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFDevices_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFDevices_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFDevices_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFDevices_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFDevices_get_Count(This,plCount)	\
    ( (This)->lpVtbl -> get_Count(This,plCount) ) 

#define IDSFDevices_get_Item(This,Index,ppDSFDevice)	\
    ( (This)->lpVtbl -> get_Item(This,Index,ppDSFDevice) ) 

#define IDSFDevices_get__NewEnum(This,ppunkEnum)	\
    ( (This)->lpVtbl -> get__NewEnum(This,ppunkEnum) ) 

#define IDSFDevices_Refresh(This)	\
    ( (This)->lpVtbl -> Refresh(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFDevices_INTERFACE_DEFINED__ */


#ifndef __IDSFLog_INTERFACE_DEFINED__
#define __IDSFLog_INTERFACE_DEFINED__

/* interface IDSFLog */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFLog;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("B7902A6F-4CC1-4ABE-8E7A-6EFF79BBBACA")
    IDSFLog : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Write( 
            /* [in] */ __RPC__in BSTR Text) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Enabled( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEnabled) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Enabled( 
            /* [in] */ VARIANT_BOOL fvarEnabled) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFLogVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFLog * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFLog * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFLog * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFLog * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFLog * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFLog * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFLog * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Write )( 
            __RPC__in IDSFLog * This,
            /* [in] */ __RPC__in BSTR Text);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Enabled )( 
            __RPC__in IDSFLog * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEnabled);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Enabled )( 
            __RPC__in IDSFLog * This,
            /* [in] */ VARIANT_BOOL fvarEnabled);
        
        END_INTERFACE
    } IDSFLogVtbl;

    interface IDSFLog
    {
        CONST_VTBL struct IDSFLogVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFLog_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFLog_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFLog_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFLog_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFLog_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFLog_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFLog_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFLog_Write(This,Text)	\
    ( (This)->lpVtbl -> Write(This,Text) ) 

#define IDSFLog_get_Enabled(This,pfvarEnabled)	\
    ( (This)->lpVtbl -> get_Enabled(This,pfvarEnabled) ) 

#define IDSFLog_put_Enabled(This,fvarEnabled)	\
    ( (This)->lpVtbl -> put_Enabled(This,fvarEnabled) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFLog_INTERFACE_DEFINED__ */


#ifndef __IDSFVersion_INTERFACE_DEFINED__
#define __IDSFVersion_INTERFACE_DEFINED__

/* interface IDSFVersion */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFVersion;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("F65DF1DA-32B4-411A-B989-1B43B5C428F2")
    IDSFVersion : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Major( 
            /* [retval][out] */ __RPC__out long *plMajor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Minor( 
            /* [retval][out] */ __RPC__out long *plMinor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_OSMajor( 
            /* [retval][out] */ __RPC__out long *plOSMajor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_OSMinor( 
            /* [retval][out] */ __RPC__out long *plOSMinor) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFVersionVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFVersion * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFVersion * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFVersion * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFVersion * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFVersion * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFVersion * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFVersion * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Major )( 
            __RPC__in IDSFVersion * This,
            /* [retval][out] */ __RPC__out long *plMajor);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Minor )( 
            __RPC__in IDSFVersion * This,
            /* [retval][out] */ __RPC__out long *plMinor);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_OSMajor )( 
            __RPC__in IDSFVersion * This,
            /* [retval][out] */ __RPC__out long *plOSMajor);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_OSMinor )( 
            __RPC__in IDSFVersion * This,
            /* [retval][out] */ __RPC__out long *plOSMinor);
        
        END_INTERFACE
    } IDSFVersionVtbl;

    interface IDSFVersion
    {
        CONST_VTBL struct IDSFVersionVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFVersion_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFVersion_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFVersion_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFVersion_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFVersion_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFVersion_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFVersion_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFVersion_get_Major(This,plMajor)	\
    ( (This)->lpVtbl -> get_Major(This,plMajor) ) 

#define IDSFVersion_get_Minor(This,plMinor)	\
    ( (This)->lpVtbl -> get_Minor(This,plMinor) ) 

#define IDSFVersion_get_OSMajor(This,plOSMajor)	\
    ( (This)->lpVtbl -> get_OSMajor(This,plOSMajor) ) 

#define IDSFVersion_get_OSMinor(This,plOSMinor)	\
    ( (This)->lpVtbl -> get_OSMinor(This,plOSMinor) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFVersion_INTERFACE_DEFINED__ */


#ifndef __IDSFPropertyBag_INTERFACE_DEFINED__
#define __IDSFPropertyBag_INTERFACE_DEFINED__

/* interface IDSFPropertyBag */
/* [object][helpstringcontext][helpcontext][helpstring][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFPropertyBag;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("ac2ed126-da20-48db-817c-61689122d1e0")
    IDSFPropertyBag : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Write( 
            /* [in] */ __RPC__in BSTR Name,
            /* [in] */ VARIANT Value) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Read( 
            /* [in] */ __RPC__in BSTR Name,
            /* [retval][out] */ __RPC__out VARIANT *Value) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Remove( 
            /* [in] */ __RPC__in BSTR Name) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Clear( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFPropertyBagVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFPropertyBag * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFPropertyBag * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFPropertyBag * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFPropertyBag * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFPropertyBag * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFPropertyBag * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFPropertyBag * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Write )( 
            __RPC__in IDSFPropertyBag * This,
            /* [in] */ __RPC__in BSTR Name,
            /* [in] */ VARIANT Value);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Read )( 
            __RPC__in IDSFPropertyBag * This,
            /* [in] */ __RPC__in BSTR Name,
            /* [retval][out] */ __RPC__out VARIANT *Value);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Remove )( 
            __RPC__in IDSFPropertyBag * This,
            /* [in] */ __RPC__in BSTR Name);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Clear )( 
            __RPC__in IDSFPropertyBag * This);
        
        END_INTERFACE
    } IDSFPropertyBagVtbl;

    interface IDSFPropertyBag
    {
        CONST_VTBL struct IDSFPropertyBagVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFPropertyBag_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFPropertyBag_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFPropertyBag_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFPropertyBag_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFPropertyBag_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFPropertyBag_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFPropertyBag_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFPropertyBag_Write(This,Name,Value)	\
    ( (This)->lpVtbl -> Write(This,Name,Value) ) 

#define IDSFPropertyBag_Read(This,Name,Value)	\
    ( (This)->lpVtbl -> Read(This,Name,Value) ) 

#define IDSFPropertyBag_Remove(This,Name)	\
    ( (This)->lpVtbl -> Remove(This,Name) ) 

#define IDSFPropertyBag_Clear(This)	\
    ( (This)->lpVtbl -> Clear(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFPropertyBag_INTERFACE_DEFINED__ */


#ifndef __IDSFBus_INTERFACE_DEFINED__
#define __IDSFBus_INTERFACE_DEFINED__

/* interface IDSFBus */
/* [object][helpstringcontext][helpcontext][helpstring][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFBus;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E927C266-5364-449E-AE52-D6A782AFDA9C")
    IDSFBus : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Name( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_GUID( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrGuid) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall HotPlug( 
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Unplug( 
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFBusVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFBus * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFBus * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFBus * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFBus * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFBus * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFBus * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFBus * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Name )( 
            __RPC__in IDSFBus * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrName);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_GUID )( 
            __RPC__in IDSFBus * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrGuid);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *HotPlug )( 
            __RPC__in IDSFBus * This,
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Unplug )( 
            __RPC__in IDSFBus * This,
            /* [in] */ __RPC__in DSFDevice	*pDSFDevice);
        
        END_INTERFACE
    } IDSFBusVtbl;

    interface IDSFBus
    {
        CONST_VTBL struct IDSFBusVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFBus_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFBus_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFBus_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFBus_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFBus_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFBus_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFBus_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFBus_get_Name(This,pbstrName)	\
    ( (This)->lpVtbl -> get_Name(This,pbstrName) ) 

#define IDSFBus_get_GUID(This,pbstrGuid)	\
    ( (This)->lpVtbl -> get_GUID(This,pbstrGuid) ) 

#define IDSFBus_HotPlug(This,pDSFDevice)	\
    ( (This)->lpVtbl -> HotPlug(This,pDSFDevice) ) 

#define IDSFBus_Unplug(This,pDSFDevice)	\
    ( (This)->lpVtbl -> Unplug(This,pDSFDevice) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFBus_INTERFACE_DEFINED__ */


#ifndef __IDSFPartialResourceDescriptor_INTERFACE_DEFINED__
#define __IDSFPartialResourceDescriptor_INTERFACE_DEFINED__

/* interface IDSFPartialResourceDescriptor */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFPartialResourceDescriptor;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("227948DE-4F1F-4689-8D9F-ED044417C5A9")
    IDSFPartialResourceDescriptor : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Type( 
            /* [retval][out] */ __RPC__out DSFResourceType *pType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Type( 
            /* [in] */ DSFResourceType ResType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_ShareDisposition( 
            /* [retval][out] */ __RPC__out DSFShareDisposition *pShareType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_ShareDisposition( 
            /* [in] */ DSFShareDisposition ShareType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Flags( 
            /* [retval][out] */ __RPC__out short *psFlags) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Flags( 
            /* [in] */ short sFlags) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Descriptor( 
            /* [retval][out] */ __RPC__deref_out_opt IDispatch **ppdispDescriptor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Descriptor( 
            /* [in] */ __RPC__in_opt IDispatch *pdispDescriptor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_HRM( 
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkHRM) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_HRM( 
            /* [in] */ __RPC__in_opt IUnknown *punkHRM) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFPartialResourceDescriptorVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFPartialResourceDescriptor * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFPartialResourceDescriptor * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFPartialResourceDescriptor * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Type )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [retval][out] */ __RPC__out DSFResourceType *pType);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Type )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [in] */ DSFResourceType ResType);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_ShareDisposition )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [retval][out] */ __RPC__out DSFShareDisposition *pShareType);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_ShareDisposition )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [in] */ DSFShareDisposition ShareType);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Flags )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [retval][out] */ __RPC__out short *psFlags);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Flags )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [in] */ short sFlags);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Descriptor )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [retval][out] */ __RPC__deref_out_opt IDispatch **ppdispDescriptor);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Descriptor )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [in] */ __RPC__in_opt IDispatch *pdispDescriptor);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_HRM )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkHRM);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_HRM )( 
            __RPC__in IDSFPartialResourceDescriptor * This,
            /* [in] */ __RPC__in_opt IUnknown *punkHRM);
        
        END_INTERFACE
    } IDSFPartialResourceDescriptorVtbl;

    interface IDSFPartialResourceDescriptor
    {
        CONST_VTBL struct IDSFPartialResourceDescriptorVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFPartialResourceDescriptor_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFPartialResourceDescriptor_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFPartialResourceDescriptor_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFPartialResourceDescriptor_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFPartialResourceDescriptor_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFPartialResourceDescriptor_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFPartialResourceDescriptor_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFPartialResourceDescriptor_get_Type(This,pType)	\
    ( (This)->lpVtbl -> get_Type(This,pType) ) 

#define IDSFPartialResourceDescriptor_put_Type(This,ResType)	\
    ( (This)->lpVtbl -> put_Type(This,ResType) ) 

#define IDSFPartialResourceDescriptor_get_ShareDisposition(This,pShareType)	\
    ( (This)->lpVtbl -> get_ShareDisposition(This,pShareType) ) 

#define IDSFPartialResourceDescriptor_put_ShareDisposition(This,ShareType)	\
    ( (This)->lpVtbl -> put_ShareDisposition(This,ShareType) ) 

#define IDSFPartialResourceDescriptor_get_Flags(This,psFlags)	\
    ( (This)->lpVtbl -> get_Flags(This,psFlags) ) 

#define IDSFPartialResourceDescriptor_put_Flags(This,sFlags)	\
    ( (This)->lpVtbl -> put_Flags(This,sFlags) ) 

#define IDSFPartialResourceDescriptor_get_Descriptor(This,ppdispDescriptor)	\
    ( (This)->lpVtbl -> get_Descriptor(This,ppdispDescriptor) ) 

#define IDSFPartialResourceDescriptor_put_Descriptor(This,pdispDescriptor)	\
    ( (This)->lpVtbl -> put_Descriptor(This,pdispDescriptor) ) 

#define IDSFPartialResourceDescriptor_get_HRM(This,ppunkHRM)	\
    ( (This)->lpVtbl -> get_HRM(This,ppunkHRM) ) 

#define IDSFPartialResourceDescriptor_put_HRM(This,punkHRM)	\
    ( (This)->lpVtbl -> put_HRM(This,punkHRM) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFPartialResourceDescriptor_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_DSFPartialResourceDescriptor;

#ifdef __cplusplus

class DECLSPEC_UUID("7E3FC012-1DB1-4135-AAAA-7FA6ED1CF620")
DSFPartialResourceDescriptor;
#endif

#ifndef __IDSFPartialResourceList_INTERFACE_DEFINED__
#define __IDSFPartialResourceList_INTERFACE_DEFINED__

/* interface IDSFPartialResourceList */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFPartialResourceList;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("C388EB82-EA52-4459-84D9-69B7E9846EC0")
    IDSFPartialResourceList : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Version( 
            /* [retval][out] */ __RPC__out short *psVersion) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Version( 
            /* [in] */ short sVersion) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Revision( 
            /* [retval][out] */ __RPC__out short *psRevision) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Revision( 
            /* [in] */ short sRevision) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Count( 
            /* [retval][out] */ __RPC__out long *plCount) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Item( 
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt DSFPartialResourceDescriptor	**ppDSFPartialResourceDescriptor) = 0;
        
        virtual /* [hidden][propget][id] */ HRESULT __stdcall get__NewEnum( 
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Add( 
            /* [in] */ __RPC__in DSFPartialResourceDescriptor	*pDSFPartialResourceDescriptor,
            /* [optional][in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Remove( 
            /* [in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Clear( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFPartialResourceListVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFPartialResourceList * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFPartialResourceList * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFPartialResourceList * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFPartialResourceList * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFPartialResourceList * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFPartialResourceList * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFPartialResourceList * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Version )( 
            __RPC__in IDSFPartialResourceList * This,
            /* [retval][out] */ __RPC__out short *psVersion);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Version )( 
            __RPC__in IDSFPartialResourceList * This,
            /* [in] */ short sVersion);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Revision )( 
            __RPC__in IDSFPartialResourceList * This,
            /* [retval][out] */ __RPC__out short *psRevision);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Revision )( 
            __RPC__in IDSFPartialResourceList * This,
            /* [in] */ short sRevision);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Count )( 
            __RPC__in IDSFPartialResourceList * This,
            /* [retval][out] */ __RPC__out long *plCount);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Item )( 
            __RPC__in IDSFPartialResourceList * This,
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt DSFPartialResourceDescriptor	**ppDSFPartialResourceDescriptor);
        
        /* [hidden][propget][id] */ HRESULT ( __stdcall *get__NewEnum )( 
            __RPC__in IDSFPartialResourceList * This,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Add )( 
            __RPC__in IDSFPartialResourceList * This,
            /* [in] */ __RPC__in DSFPartialResourceDescriptor	*pDSFPartialResourceDescriptor,
            /* [optional][in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Remove )( 
            __RPC__in IDSFPartialResourceList * This,
            /* [in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Clear )( 
            __RPC__in IDSFPartialResourceList * This);
        
        END_INTERFACE
    } IDSFPartialResourceListVtbl;

    interface IDSFPartialResourceList
    {
        CONST_VTBL struct IDSFPartialResourceListVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFPartialResourceList_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFPartialResourceList_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFPartialResourceList_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFPartialResourceList_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFPartialResourceList_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFPartialResourceList_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFPartialResourceList_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFPartialResourceList_get_Version(This,psVersion)	\
    ( (This)->lpVtbl -> get_Version(This,psVersion) ) 

#define IDSFPartialResourceList_put_Version(This,sVersion)	\
    ( (This)->lpVtbl -> put_Version(This,sVersion) ) 

#define IDSFPartialResourceList_get_Revision(This,psRevision)	\
    ( (This)->lpVtbl -> get_Revision(This,psRevision) ) 

#define IDSFPartialResourceList_put_Revision(This,sRevision)	\
    ( (This)->lpVtbl -> put_Revision(This,sRevision) ) 

#define IDSFPartialResourceList_get_Count(This,plCount)	\
    ( (This)->lpVtbl -> get_Count(This,plCount) ) 

#define IDSFPartialResourceList_get_Item(This,Index,ppDSFPartialResourceDescriptor)	\
    ( (This)->lpVtbl -> get_Item(This,Index,ppDSFPartialResourceDescriptor) ) 

#define IDSFPartialResourceList_get__NewEnum(This,ppunkEnum)	\
    ( (This)->lpVtbl -> get__NewEnum(This,ppunkEnum) ) 

#define IDSFPartialResourceList_Add(This,pDSFPartialResourceDescriptor,Index)	\
    ( (This)->lpVtbl -> Add(This,pDSFPartialResourceDescriptor,Index) ) 

#define IDSFPartialResourceList_Remove(This,Index)	\
    ( (This)->lpVtbl -> Remove(This,Index) ) 

#define IDSFPartialResourceList_Clear(This)	\
    ( (This)->lpVtbl -> Clear(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFPartialResourceList_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_DSFPartialResourceList;

#ifdef __cplusplus

class DECLSPEC_UUID("DC1C8B82-254C-4AD9-8CE0-CA477FC56DB1")
DSFPartialResourceList;
#endif

EXTERN_C const CLSID CLSID_DSFFullResourceDescriptor;

#ifdef __cplusplus

class DECLSPEC_UUID("CECFEC54-035C-4103-B64F-A200AB9F32C2")
DSFFullResourceDescriptor;
#endif

EXTERN_C const CLSID CLSID_DSFResourceList;

#ifdef __cplusplus

class DECLSPEC_UUID("3596C463-2D1E-40B7-8DC7-7BB32F9E2DC4")
DSFResourceList;
#endif

#ifndef __IDSFResDescPort_INTERFACE_DEFINED__
#define __IDSFResDescPort_INTERFACE_DEFINED__

/* interface IDSFResDescPort */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFResDescPort;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("82850DB2-0F99-4529-9F6E-6C1273C6027C")
    IDSFResDescPort : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_StartLow( 
            /* [retval][out] */ __RPC__out long *plStartLow) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_StartLow( 
            /* [in] */ long lStartLow) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_StartHigh( 
            /* [retval][out] */ __RPC__out long *plStartHigh) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_StartHigh( 
            /* [in] */ long lStartHigh) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Length( 
            /* [retval][out] */ __RPC__out long *plLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Length( 
            /* [in] */ long lLength) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFResDescPortVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFResDescPort * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFResDescPort * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFResDescPort * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFResDescPort * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFResDescPort * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFResDescPort * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFResDescPort * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_StartLow )( 
            __RPC__in IDSFResDescPort * This,
            /* [retval][out] */ __RPC__out long *plStartLow);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_StartLow )( 
            __RPC__in IDSFResDescPort * This,
            /* [in] */ long lStartLow);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_StartHigh )( 
            __RPC__in IDSFResDescPort * This,
            /* [retval][out] */ __RPC__out long *plStartHigh);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_StartHigh )( 
            __RPC__in IDSFResDescPort * This,
            /* [in] */ long lStartHigh);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Length )( 
            __RPC__in IDSFResDescPort * This,
            /* [retval][out] */ __RPC__out long *plLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Length )( 
            __RPC__in IDSFResDescPort * This,
            /* [in] */ long lLength);
        
        END_INTERFACE
    } IDSFResDescPortVtbl;

    interface IDSFResDescPort
    {
        CONST_VTBL struct IDSFResDescPortVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFResDescPort_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFResDescPort_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFResDescPort_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFResDescPort_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFResDescPort_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFResDescPort_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFResDescPort_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFResDescPort_get_StartLow(This,plStartLow)	\
    ( (This)->lpVtbl -> get_StartLow(This,plStartLow) ) 

#define IDSFResDescPort_put_StartLow(This,lStartLow)	\
    ( (This)->lpVtbl -> put_StartLow(This,lStartLow) ) 

#define IDSFResDescPort_get_StartHigh(This,plStartHigh)	\
    ( (This)->lpVtbl -> get_StartHigh(This,plStartHigh) ) 

#define IDSFResDescPort_put_StartHigh(This,lStartHigh)	\
    ( (This)->lpVtbl -> put_StartHigh(This,lStartHigh) ) 

#define IDSFResDescPort_get_Length(This,plLength)	\
    ( (This)->lpVtbl -> get_Length(This,plLength) ) 

#define IDSFResDescPort_put_Length(This,lLength)	\
    ( (This)->lpVtbl -> put_Length(This,lLength) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFResDescPort_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_DSFResDescPort;

#ifdef __cplusplus

class DECLSPEC_UUID("680D2930-B314-4B4F-87F0-14E8ECB1982F")
DSFResDescPort;
#endif

#ifndef __IDSFResDescDevSpecific_INTERFACE_DEFINED__
#define __IDSFResDescDevSpecific_INTERFACE_DEFINED__

/* interface IDSFResDescDevSpecific */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFResDescDevSpecific;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("914D27BF-0AB4-44c2-873D-84D163B39DD5")
    IDSFResDescDevSpecific : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DataSize( 
            /* [retval][out] */ __RPC__out long *plDataSize) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DataSize( 
            /* [in] */ long lDataSize) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Reserved1( 
            /* [retval][out] */ __RPC__out long *plReserved1) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Reserved1( 
            /* [in] */ long lReserved1) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Reserved2( 
            /* [retval][out] */ __RPC__out long *plReserved2) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Reserved2( 
            /* [in] */ long lReserved2) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFResDescDevSpecificVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFResDescDevSpecific * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFResDescDevSpecific * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFResDescDevSpecific * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFResDescDevSpecific * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFResDescDevSpecific * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFResDescDevSpecific * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFResDescDevSpecific * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DataSize )( 
            __RPC__in IDSFResDescDevSpecific * This,
            /* [retval][out] */ __RPC__out long *plDataSize);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DataSize )( 
            __RPC__in IDSFResDescDevSpecific * This,
            /* [in] */ long lDataSize);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Reserved1 )( 
            __RPC__in IDSFResDescDevSpecific * This,
            /* [retval][out] */ __RPC__out long *plReserved1);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Reserved1 )( 
            __RPC__in IDSFResDescDevSpecific * This,
            /* [in] */ long lReserved1);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Reserved2 )( 
            __RPC__in IDSFResDescDevSpecific * This,
            /* [retval][out] */ __RPC__out long *plReserved2);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Reserved2 )( 
            __RPC__in IDSFResDescDevSpecific * This,
            /* [in] */ long lReserved2);
        
        END_INTERFACE
    } IDSFResDescDevSpecificVtbl;

    interface IDSFResDescDevSpecific
    {
        CONST_VTBL struct IDSFResDescDevSpecificVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFResDescDevSpecific_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFResDescDevSpecific_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFResDescDevSpecific_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFResDescDevSpecific_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFResDescDevSpecific_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFResDescDevSpecific_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFResDescDevSpecific_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFResDescDevSpecific_get_DataSize(This,plDataSize)	\
    ( (This)->lpVtbl -> get_DataSize(This,plDataSize) ) 

#define IDSFResDescDevSpecific_put_DataSize(This,lDataSize)	\
    ( (This)->lpVtbl -> put_DataSize(This,lDataSize) ) 

#define IDSFResDescDevSpecific_get_Reserved1(This,plReserved1)	\
    ( (This)->lpVtbl -> get_Reserved1(This,plReserved1) ) 

#define IDSFResDescDevSpecific_put_Reserved1(This,lReserved1)	\
    ( (This)->lpVtbl -> put_Reserved1(This,lReserved1) ) 

#define IDSFResDescDevSpecific_get_Reserved2(This,plReserved2)	\
    ( (This)->lpVtbl -> get_Reserved2(This,plReserved2) ) 

#define IDSFResDescDevSpecific_put_Reserved2(This,lReserved2)	\
    ( (This)->lpVtbl -> put_Reserved2(This,lReserved2) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFResDescDevSpecific_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_DSFResDescDevSpecific;

#ifdef __cplusplus

class DECLSPEC_UUID("75679715-3926-4211-A5A6-6A333023D5BA")
DSFResDescDevSpecific;
#endif

#ifndef __IDSFResDescDevPrivate_INTERFACE_DEFINED__
#define __IDSFResDescDevPrivate_INTERFACE_DEFINED__

/* interface IDSFResDescDevPrivate */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFResDescDevPrivate;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("C455E6E1-F93D-47bd-9042-95B7AB0A7D2B")
    IDSFResDescDevPrivate : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Data0( 
            /* [retval][out] */ __RPC__out long *plData0) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Data0( 
            /* [in] */ long lData0) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Data1( 
            /* [retval][out] */ __RPC__out long *plData1) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Data1( 
            /* [in] */ long lData1) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Data2( 
            /* [retval][out] */ __RPC__out long *plData2) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Data2( 
            /* [in] */ long lData2) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFResDescDevPrivateVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFResDescDevPrivate * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFResDescDevPrivate * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFResDescDevPrivate * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFResDescDevPrivate * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFResDescDevPrivate * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFResDescDevPrivate * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFResDescDevPrivate * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Data0 )( 
            __RPC__in IDSFResDescDevPrivate * This,
            /* [retval][out] */ __RPC__out long *plData0);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Data0 )( 
            __RPC__in IDSFResDescDevPrivate * This,
            /* [in] */ long lData0);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Data1 )( 
            __RPC__in IDSFResDescDevPrivate * This,
            /* [retval][out] */ __RPC__out long *plData1);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Data1 )( 
            __RPC__in IDSFResDescDevPrivate * This,
            /* [in] */ long lData1);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Data2 )( 
            __RPC__in IDSFResDescDevPrivate * This,
            /* [retval][out] */ __RPC__out long *plData2);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Data2 )( 
            __RPC__in IDSFResDescDevPrivate * This,
            /* [in] */ long lData2);
        
        END_INTERFACE
    } IDSFResDescDevPrivateVtbl;

    interface IDSFResDescDevPrivate
    {
        CONST_VTBL struct IDSFResDescDevPrivateVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFResDescDevPrivate_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFResDescDevPrivate_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFResDescDevPrivate_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFResDescDevPrivate_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFResDescDevPrivate_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFResDescDevPrivate_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFResDescDevPrivate_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFResDescDevPrivate_get_Data0(This,plData0)	\
    ( (This)->lpVtbl -> get_Data0(This,plData0) ) 

#define IDSFResDescDevPrivate_put_Data0(This,lData0)	\
    ( (This)->lpVtbl -> put_Data0(This,lData0) ) 

#define IDSFResDescDevPrivate_get_Data1(This,plData1)	\
    ( (This)->lpVtbl -> get_Data1(This,plData1) ) 

#define IDSFResDescDevPrivate_put_Data1(This,lData1)	\
    ( (This)->lpVtbl -> put_Data1(This,lData1) ) 

#define IDSFResDescDevPrivate_get_Data2(This,plData2)	\
    ( (This)->lpVtbl -> get_Data2(This,plData2) ) 

#define IDSFResDescDevPrivate_put_Data2(This,lData2)	\
    ( (This)->lpVtbl -> put_Data2(This,lData2) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFResDescDevPrivate_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_DSFResDescDevPrivate;

#ifdef __cplusplus

class DECLSPEC_UUID("F03EAE50-D772-4125-82E7-087B91ADF213")
DSFResDescDevPrivate;
#endif

#ifndef __IDSFResDescInterrupt_INTERFACE_DEFINED__
#define __IDSFResDescInterrupt_INTERFACE_DEFINED__

/* interface IDSFResDescInterrupt */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFResDescInterrupt;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("20F6EF07-E851-464A-B1BC-549B941682B0")
    IDSFResDescInterrupt : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Level( 
            /* [retval][out] */ __RPC__out long *plLevel) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Level( 
            /* [in] */ long lLevel) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Vector( 
            /* [retval][out] */ __RPC__out long *plVector) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Vector( 
            /* [in] */ long lVector) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Affinity( 
            /* [retval][out] */ __RPC__out long *plAffinity) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Affinity( 
            /* [in] */ long lAffinity) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFResDescInterruptVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFResDescInterrupt * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFResDescInterrupt * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFResDescInterrupt * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFResDescInterrupt * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFResDescInterrupt * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFResDescInterrupt * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFResDescInterrupt * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Level )( 
            __RPC__in IDSFResDescInterrupt * This,
            /* [retval][out] */ __RPC__out long *plLevel);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Level )( 
            __RPC__in IDSFResDescInterrupt * This,
            /* [in] */ long lLevel);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Vector )( 
            __RPC__in IDSFResDescInterrupt * This,
            /* [retval][out] */ __RPC__out long *plVector);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Vector )( 
            __RPC__in IDSFResDescInterrupt * This,
            /* [in] */ long lVector);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Affinity )( 
            __RPC__in IDSFResDescInterrupt * This,
            /* [retval][out] */ __RPC__out long *plAffinity);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Affinity )( 
            __RPC__in IDSFResDescInterrupt * This,
            /* [in] */ long lAffinity);
        
        END_INTERFACE
    } IDSFResDescInterruptVtbl;

    interface IDSFResDescInterrupt
    {
        CONST_VTBL struct IDSFResDescInterruptVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFResDescInterrupt_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFResDescInterrupt_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFResDescInterrupt_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFResDescInterrupt_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFResDescInterrupt_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFResDescInterrupt_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFResDescInterrupt_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFResDescInterrupt_get_Level(This,plLevel)	\
    ( (This)->lpVtbl -> get_Level(This,plLevel) ) 

#define IDSFResDescInterrupt_put_Level(This,lLevel)	\
    ( (This)->lpVtbl -> put_Level(This,lLevel) ) 

#define IDSFResDescInterrupt_get_Vector(This,plVector)	\
    ( (This)->lpVtbl -> get_Vector(This,plVector) ) 

#define IDSFResDescInterrupt_put_Vector(This,lVector)	\
    ( (This)->lpVtbl -> put_Vector(This,lVector) ) 

#define IDSFResDescInterrupt_get_Affinity(This,plAffinity)	\
    ( (This)->lpVtbl -> get_Affinity(This,plAffinity) ) 

#define IDSFResDescInterrupt_put_Affinity(This,lAffinity)	\
    ( (This)->lpVtbl -> put_Affinity(This,lAffinity) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFResDescInterrupt_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_DSFResDescInterrupt;

#ifdef __cplusplus

class DECLSPEC_UUID("B68DF23C-9DB6-4C0A-B927-94F0AFE080C4")
DSFResDescInterrupt;
#endif

#ifndef __IDSFResDescMemory_INTERFACE_DEFINED__
#define __IDSFResDescMemory_INTERFACE_DEFINED__

/* interface IDSFResDescMemory */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFResDescMemory;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("48ADC411-19EE-4E06-9772-882B487BB8C0")
    IDSFResDescMemory : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_StartLow( 
            /* [retval][out] */ __RPC__out long *plStartLow) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_StartLow( 
            /* [in] */ long lStartLow) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_StartHigh( 
            /* [retval][out] */ __RPC__out long *plStartHigh) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_StartHigh( 
            /* [in] */ long lStartHigh) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Length( 
            /* [retval][out] */ __RPC__out long *plLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Length( 
            /* [in] */ long lLength) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFResDescMemoryVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFResDescMemory * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFResDescMemory * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFResDescMemory * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFResDescMemory * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFResDescMemory * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFResDescMemory * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFResDescMemory * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_StartLow )( 
            __RPC__in IDSFResDescMemory * This,
            /* [retval][out] */ __RPC__out long *plStartLow);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_StartLow )( 
            __RPC__in IDSFResDescMemory * This,
            /* [in] */ long lStartLow);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_StartHigh )( 
            __RPC__in IDSFResDescMemory * This,
            /* [retval][out] */ __RPC__out long *plStartHigh);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_StartHigh )( 
            __RPC__in IDSFResDescMemory * This,
            /* [in] */ long lStartHigh);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Length )( 
            __RPC__in IDSFResDescMemory * This,
            /* [retval][out] */ __RPC__out long *plLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Length )( 
            __RPC__in IDSFResDescMemory * This,
            /* [in] */ long lLength);
        
        END_INTERFACE
    } IDSFResDescMemoryVtbl;

    interface IDSFResDescMemory
    {
        CONST_VTBL struct IDSFResDescMemoryVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFResDescMemory_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFResDescMemory_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFResDescMemory_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFResDescMemory_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFResDescMemory_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFResDescMemory_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFResDescMemory_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFResDescMemory_get_StartLow(This,plStartLow)	\
    ( (This)->lpVtbl -> get_StartLow(This,plStartLow) ) 

#define IDSFResDescMemory_put_StartLow(This,lStartLow)	\
    ( (This)->lpVtbl -> put_StartLow(This,lStartLow) ) 

#define IDSFResDescMemory_get_StartHigh(This,plStartHigh)	\
    ( (This)->lpVtbl -> get_StartHigh(This,plStartHigh) ) 

#define IDSFResDescMemory_put_StartHigh(This,lStartHigh)	\
    ( (This)->lpVtbl -> put_StartHigh(This,lStartHigh) ) 

#define IDSFResDescMemory_get_Length(This,plLength)	\
    ( (This)->lpVtbl -> get_Length(This,plLength) ) 

#define IDSFResDescMemory_put_Length(This,lLength)	\
    ( (This)->lpVtbl -> put_Length(This,lLength) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFResDescMemory_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_DSFResDescMemory;

#ifdef __cplusplus

class DECLSPEC_UUID("FDB94131-E0D2-41E7-A43F-72258F098281")
DSFResDescMemory;
#endif

#ifndef __IDSFResDescDMA_INTERFACE_DEFINED__
#define __IDSFResDescDMA_INTERFACE_DEFINED__

/* interface IDSFResDescDMA */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFResDescDMA;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("9DA9B4D3-C73F-42B8-8CA2-0E5E3FF7198E")
    IDSFResDescDMA : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Channel( 
            /* [retval][out] */ __RPC__out long *plChannel) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Channel( 
            /* [in] */ long lChannel) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Port( 
            /* [retval][out] */ __RPC__out long *plPort) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Port( 
            /* [in] */ long lPort) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Reserved( 
            /* [retval][out] */ __RPC__out long *plReserved) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Reserved( 
            /* [in] */ long lReserved) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFResDescDMAVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFResDescDMA * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFResDescDMA * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFResDescDMA * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFResDescDMA * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFResDescDMA * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFResDescDMA * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFResDescDMA * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Channel )( 
            __RPC__in IDSFResDescDMA * This,
            /* [retval][out] */ __RPC__out long *plChannel);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Channel )( 
            __RPC__in IDSFResDescDMA * This,
            /* [in] */ long lChannel);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Port )( 
            __RPC__in IDSFResDescDMA * This,
            /* [retval][out] */ __RPC__out long *plPort);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Port )( 
            __RPC__in IDSFResDescDMA * This,
            /* [in] */ long lPort);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Reserved )( 
            __RPC__in IDSFResDescDMA * This,
            /* [retval][out] */ __RPC__out long *plReserved);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Reserved )( 
            __RPC__in IDSFResDescDMA * This,
            /* [in] */ long lReserved);
        
        END_INTERFACE
    } IDSFResDescDMAVtbl;

    interface IDSFResDescDMA
    {
        CONST_VTBL struct IDSFResDescDMAVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFResDescDMA_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFResDescDMA_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFResDescDMA_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFResDescDMA_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFResDescDMA_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFResDescDMA_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFResDescDMA_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFResDescDMA_get_Channel(This,plChannel)	\
    ( (This)->lpVtbl -> get_Channel(This,plChannel) ) 

#define IDSFResDescDMA_put_Channel(This,lChannel)	\
    ( (This)->lpVtbl -> put_Channel(This,lChannel) ) 

#define IDSFResDescDMA_get_Port(This,plPort)	\
    ( (This)->lpVtbl -> get_Port(This,plPort) ) 

#define IDSFResDescDMA_put_Port(This,lPort)	\
    ( (This)->lpVtbl -> put_Port(This,lPort) ) 

#define IDSFResDescDMA_get_Reserved(This,plReserved)	\
    ( (This)->lpVtbl -> get_Reserved(This,plReserved) ) 

#define IDSFResDescDMA_put_Reserved(This,lReserved)	\
    ( (This)->lpVtbl -> put_Reserved(This,lReserved) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFResDescDMA_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_DSFResDescDMA;

#ifdef __cplusplus

class DECLSPEC_UUID("449D593F-EFE0-414E-A5D8-2333B223090E")
DSFResDescDMA;
#endif

#ifndef __IDSFSystemDevice_INTERFACE_DEFINED__
#define __IDSFSystemDevice_INTERFACE_DEFINED__

/* interface IDSFSystemDevice */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_IDSFSystemDevice;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("8041F860-55E4-4709-A96C-FA51EBC8F895")
    IDSFSystemDevice : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Address( 
            /* [retval][out] */ __RPC__out long *plAddress) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Address( 
            /* [in] */ long lAddress) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_BusNumber( 
            /* [retval][out] */ __RPC__out long *plBusNumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_BusNumber( 
            /* [in] */ long lBusNumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_BusTypeGUID( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrGUID) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_BusTypeGUID( 
            /* [in] */ __RPC__in BSTR bstrGUID) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Capabilities( 
            /* [retval][out] */ __RPC__deref_out_opt DSFDeviceCaps	**ppDSFDeviceCaps) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Capabilities( 
            /* [in] */ __RPC__in DSFDeviceCaps	*pDSFDeviceCaps) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT __stdcall putref_Capabilities( 
            /* [in] */ __RPC__in DSFDeviceCaps	*pDSFDeviceCaps) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_ClassGUID( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrGUID) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_ClassGUID( 
            /* [in] */ __RPC__in BSTR bstrGUID) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_ClassName( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_ClassName( 
            /* [in] */ __RPC__in BSTR bstrName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_CompatibleIDs( 
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaIDs) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_CompatibleIDs( 
            /* [in] */ __RPC__in SAFEARRAY * psaIDs) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceDescription( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrDescription) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceDescription( 
            /* [in] */ __RPC__in BSTR bstrDescription) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceID( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrID) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceID( 
            /* [in] */ __RPC__in BSTR bstrID) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DriverKeyName( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrKeyName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DriverKeyName( 
            /* [in] */ __RPC__in BSTR bstrKeyName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_EnumeratorName( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_EnumeratorName( 
            /* [in] */ __RPC__in BSTR bstrName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_FriendlyName( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_FriendlyName( 
            /* [in] */ __RPC__in BSTR bstrName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_HardwareIDs( 
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaIDs) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_HardwareIDs( 
            /* [in] */ __RPC__in SAFEARRAY * psaIDs) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_InstanceID( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrID) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_InstanceID( 
            /* [in] */ __RPC__in BSTR bstrID) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_LegacyBusType( 
            /* [retval][out] */ __RPC__out DSFInterfaceType *pBusType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_LegacyBusType( 
            /* [in] */ DSFInterfaceType BusType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_LocationInfo( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrInfo) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_LocationInfo( 
            /* [in] */ __RPC__in BSTR bstrInfo) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Manufacturer( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Manufacturer( 
            /* [in] */ __RPC__in BSTR bstrName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_PDOName( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrPDOName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_PDOName( 
            /* [in] */ __RPC__in BSTR bstrPDOName) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_RawResources( 
            /* [retval][out] */ __RPC__deref_out_opt DSFResourceList	**ppDSFResourceList) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_RawResources( 
            /* [in] */ __RPC__in DSFResourceList	*pDSFResourceList) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT __stdcall putref_RawResources( 
            /* [in] */ __RPC__in DSFResourceList	*pDSFResourceList) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_UINumber( 
            /* [retval][out] */ __RPC__out long *plNumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_UINumber( 
            /* [in] */ long lNumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_XlatedResources( 
            /* [retval][out] */ __RPC__deref_out_opt DSFResourceList	**ppDSFResourceList) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_XlatedResources( 
            /* [in] */ __RPC__in DSFResourceList	*pDSFResourceList) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT __stdcall putref_XlatedResources( 
            /* [in] */ __RPC__in DSFResourceList	*pDSFResourceList) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFSystemDeviceVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFSystemDevice * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFSystemDevice * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in IDSFSystemDevice * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IDSFSystemDevice * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Address )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__out long *plAddress);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Address )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ long lAddress);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_BusNumber )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__out long *plBusNumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_BusNumber )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ long lBusNumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_BusTypeGUID )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrGUID);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_BusTypeGUID )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in BSTR bstrGUID);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Capabilities )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt DSFDeviceCaps	**ppDSFDeviceCaps);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Capabilities )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in DSFDeviceCaps	*pDSFDeviceCaps);
        
        /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT ( __stdcall *putref_Capabilities )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in DSFDeviceCaps	*pDSFDeviceCaps);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_ClassGUID )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrGUID);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_ClassGUID )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in BSTR bstrGUID);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_ClassName )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrName);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_ClassName )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in BSTR bstrName);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_CompatibleIDs )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaIDs);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_CompatibleIDs )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in SAFEARRAY * psaIDs);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceDescription )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrDescription);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceDescription )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in BSTR bstrDescription);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceID )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrID);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceID )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in BSTR bstrID);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DriverKeyName )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrKeyName);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DriverKeyName )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in BSTR bstrKeyName);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_EnumeratorName )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrName);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_EnumeratorName )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in BSTR bstrName);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_FriendlyName )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrName);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_FriendlyName )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in BSTR bstrName);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_HardwareIDs )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaIDs);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_HardwareIDs )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in SAFEARRAY * psaIDs);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_InstanceID )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrID);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_InstanceID )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in BSTR bstrID);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_LegacyBusType )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__out DSFInterfaceType *pBusType);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_LegacyBusType )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ DSFInterfaceType BusType);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_LocationInfo )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrInfo);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_LocationInfo )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in BSTR bstrInfo);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Manufacturer )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrName);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Manufacturer )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in BSTR bstrName);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_PDOName )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrPDOName);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_PDOName )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in BSTR bstrPDOName);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_RawResources )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt DSFResourceList	**ppDSFResourceList);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_RawResources )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in DSFResourceList	*pDSFResourceList);
        
        /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT ( __stdcall *putref_RawResources )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in DSFResourceList	*pDSFResourceList);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_UINumber )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__out long *plNumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_UINumber )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ long lNumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_XlatedResources )( 
            __RPC__in IDSFSystemDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt DSFResourceList	**ppDSFResourceList);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_XlatedResources )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in DSFResourceList	*pDSFResourceList);
        
        /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT ( __stdcall *putref_XlatedResources )( 
            __RPC__in IDSFSystemDevice * This,
            /* [in] */ __RPC__in DSFResourceList	*pDSFResourceList);
        
        END_INTERFACE
    } IDSFSystemDeviceVtbl;

    interface IDSFSystemDevice
    {
        CONST_VTBL struct IDSFSystemDeviceVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFSystemDevice_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFSystemDevice_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFSystemDevice_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFSystemDevice_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define IDSFSystemDevice_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define IDSFSystemDevice_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define IDSFSystemDevice_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define IDSFSystemDevice_get_Address(This,plAddress)	\
    ( (This)->lpVtbl -> get_Address(This,plAddress) ) 

#define IDSFSystemDevice_put_Address(This,lAddress)	\
    ( (This)->lpVtbl -> put_Address(This,lAddress) ) 

#define IDSFSystemDevice_get_BusNumber(This,plBusNumber)	\
    ( (This)->lpVtbl -> get_BusNumber(This,plBusNumber) ) 

#define IDSFSystemDevice_put_BusNumber(This,lBusNumber)	\
    ( (This)->lpVtbl -> put_BusNumber(This,lBusNumber) ) 

#define IDSFSystemDevice_get_BusTypeGUID(This,pbstrGUID)	\
    ( (This)->lpVtbl -> get_BusTypeGUID(This,pbstrGUID) ) 

#define IDSFSystemDevice_put_BusTypeGUID(This,bstrGUID)	\
    ( (This)->lpVtbl -> put_BusTypeGUID(This,bstrGUID) ) 

#define IDSFSystemDevice_get_Capabilities(This,ppDSFDeviceCaps)	\
    ( (This)->lpVtbl -> get_Capabilities(This,ppDSFDeviceCaps) ) 

#define IDSFSystemDevice_put_Capabilities(This,pDSFDeviceCaps)	\
    ( (This)->lpVtbl -> put_Capabilities(This,pDSFDeviceCaps) ) 

#define IDSFSystemDevice_putref_Capabilities(This,pDSFDeviceCaps)	\
    ( (This)->lpVtbl -> putref_Capabilities(This,pDSFDeviceCaps) ) 

#define IDSFSystemDevice_get_ClassGUID(This,pbstrGUID)	\
    ( (This)->lpVtbl -> get_ClassGUID(This,pbstrGUID) ) 

#define IDSFSystemDevice_put_ClassGUID(This,bstrGUID)	\
    ( (This)->lpVtbl -> put_ClassGUID(This,bstrGUID) ) 

#define IDSFSystemDevice_get_ClassName(This,pbstrName)	\
    ( (This)->lpVtbl -> get_ClassName(This,pbstrName) ) 

#define IDSFSystemDevice_put_ClassName(This,bstrName)	\
    ( (This)->lpVtbl -> put_ClassName(This,bstrName) ) 

#define IDSFSystemDevice_get_CompatibleIDs(This,ppsaIDs)	\
    ( (This)->lpVtbl -> get_CompatibleIDs(This,ppsaIDs) ) 

#define IDSFSystemDevice_put_CompatibleIDs(This,psaIDs)	\
    ( (This)->lpVtbl -> put_CompatibleIDs(This,psaIDs) ) 

#define IDSFSystemDevice_get_DeviceDescription(This,pbstrDescription)	\
    ( (This)->lpVtbl -> get_DeviceDescription(This,pbstrDescription) ) 

#define IDSFSystemDevice_put_DeviceDescription(This,bstrDescription)	\
    ( (This)->lpVtbl -> put_DeviceDescription(This,bstrDescription) ) 

#define IDSFSystemDevice_get_DeviceID(This,pbstrID)	\
    ( (This)->lpVtbl -> get_DeviceID(This,pbstrID) ) 

#define IDSFSystemDevice_put_DeviceID(This,bstrID)	\
    ( (This)->lpVtbl -> put_DeviceID(This,bstrID) ) 

#define IDSFSystemDevice_get_DriverKeyName(This,pbstrKeyName)	\
    ( (This)->lpVtbl -> get_DriverKeyName(This,pbstrKeyName) ) 

#define IDSFSystemDevice_put_DriverKeyName(This,bstrKeyName)	\
    ( (This)->lpVtbl -> put_DriverKeyName(This,bstrKeyName) ) 

#define IDSFSystemDevice_get_EnumeratorName(This,pbstrName)	\
    ( (This)->lpVtbl -> get_EnumeratorName(This,pbstrName) ) 

#define IDSFSystemDevice_put_EnumeratorName(This,bstrName)	\
    ( (This)->lpVtbl -> put_EnumeratorName(This,bstrName) ) 

#define IDSFSystemDevice_get_FriendlyName(This,pbstrName)	\
    ( (This)->lpVtbl -> get_FriendlyName(This,pbstrName) ) 

#define IDSFSystemDevice_put_FriendlyName(This,bstrName)	\
    ( (This)->lpVtbl -> put_FriendlyName(This,bstrName) ) 

#define IDSFSystemDevice_get_HardwareIDs(This,ppsaIDs)	\
    ( (This)->lpVtbl -> get_HardwareIDs(This,ppsaIDs) ) 

#define IDSFSystemDevice_put_HardwareIDs(This,psaIDs)	\
    ( (This)->lpVtbl -> put_HardwareIDs(This,psaIDs) ) 

#define IDSFSystemDevice_get_InstanceID(This,pbstrID)	\
    ( (This)->lpVtbl -> get_InstanceID(This,pbstrID) ) 

#define IDSFSystemDevice_put_InstanceID(This,bstrID)	\
    ( (This)->lpVtbl -> put_InstanceID(This,bstrID) ) 

#define IDSFSystemDevice_get_LegacyBusType(This,pBusType)	\
    ( (This)->lpVtbl -> get_LegacyBusType(This,pBusType) ) 

#define IDSFSystemDevice_put_LegacyBusType(This,BusType)	\
    ( (This)->lpVtbl -> put_LegacyBusType(This,BusType) ) 

#define IDSFSystemDevice_get_LocationInfo(This,pbstrInfo)	\
    ( (This)->lpVtbl -> get_LocationInfo(This,pbstrInfo) ) 

#define IDSFSystemDevice_put_LocationInfo(This,bstrInfo)	\
    ( (This)->lpVtbl -> put_LocationInfo(This,bstrInfo) ) 

#define IDSFSystemDevice_get_Manufacturer(This,pbstrName)	\
    ( (This)->lpVtbl -> get_Manufacturer(This,pbstrName) ) 

#define IDSFSystemDevice_put_Manufacturer(This,bstrName)	\
    ( (This)->lpVtbl -> put_Manufacturer(This,bstrName) ) 

#define IDSFSystemDevice_get_PDOName(This,pbstrPDOName)	\
    ( (This)->lpVtbl -> get_PDOName(This,pbstrPDOName) ) 

#define IDSFSystemDevice_put_PDOName(This,bstrPDOName)	\
    ( (This)->lpVtbl -> put_PDOName(This,bstrPDOName) ) 

#define IDSFSystemDevice_get_RawResources(This,ppDSFResourceList)	\
    ( (This)->lpVtbl -> get_RawResources(This,ppDSFResourceList) ) 

#define IDSFSystemDevice_put_RawResources(This,pDSFResourceList)	\
    ( (This)->lpVtbl -> put_RawResources(This,pDSFResourceList) ) 

#define IDSFSystemDevice_putref_RawResources(This,pDSFResourceList)	\
    ( (This)->lpVtbl -> putref_RawResources(This,pDSFResourceList) ) 

#define IDSFSystemDevice_get_UINumber(This,plNumber)	\
    ( (This)->lpVtbl -> get_UINumber(This,plNumber) ) 

#define IDSFSystemDevice_put_UINumber(This,lNumber)	\
    ( (This)->lpVtbl -> put_UINumber(This,lNumber) ) 

#define IDSFSystemDevice_get_XlatedResources(This,ppDSFResourceList)	\
    ( (This)->lpVtbl -> get_XlatedResources(This,ppDSFResourceList) ) 

#define IDSFSystemDevice_put_XlatedResources(This,pDSFResourceList)	\
    ( (This)->lpVtbl -> put_XlatedResources(This,pDSFResourceList) ) 

#define IDSFSystemDevice_putref_XlatedResources(This,pDSFResourceList)	\
    ( (This)->lpVtbl -> putref_XlatedResources(This,pDSFResourceList) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFSystemDevice_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_DSFSystemDevice;

#ifdef __cplusplus

class DECLSPEC_UUID("1AE675C7-9C3D-429c-94C9-137A00246470")
DSFSystemDevice;
#endif

EXTERN_C const CLSID CLSID_DSFDevice;

#ifdef __cplusplus

class DECLSPEC_UUID("8B6626F7-F57B-4C47-AE2F-4813FB4D40D6")
DSFDevice;
#endif

EXTERN_C const CLSID CLSID_DSF;

#ifdef __cplusplus

class DECLSPEC_UUID("44DD6DC0-6427-4C02-8D5C-1179C50C65D7")
DSF;
#endif

EXTERN_C const CLSID CLSID_DSFDevices;

#ifdef __cplusplus

class DECLSPEC_UUID("60FA4E76-ED77-4458-ABCF-56E23113FE61")
DSFDevices;
#endif

EXTERN_C const CLSID CLSID_DSFDeviceCaps;

#ifdef __cplusplus

class DECLSPEC_UUID("26FF1048-7125-45F8-ACA8-F13C5E883ED1")
DSFDeviceCaps;
#endif

EXTERN_C const CLSID CLSID_DSFLog;

#ifdef __cplusplus

class DECLSPEC_UUID("A880A214-56A0-426A-844A-7AACCF7C3641")
DSFLog;
#endif

EXTERN_C const CLSID CLSID_DSFVersion;

#ifdef __cplusplus

class DECLSPEC_UUID("9B89C450-8E7C-4B02-B4A0-B581A9999B47")
DSFVersion;
#endif

EXTERN_C const CLSID CLSID_DSFPropertyBag;

#ifdef __cplusplus

class DECLSPEC_UUID("c283c5ec-4ba5-46ec-9efc-e9f1bffe7c70")
DSFPropertyBag;
#endif

#ifndef __IDSFDebug_INTERFACE_DEFINED__
#define __IDSFDebug_INTERFACE_DEFINED__

/* interface IDSFDebug */
/* [object][helpstringcontext][helpcontext][helpstring][uuid] */ 


EXTERN_C const IID IID_IDSFDebug;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("69b999b5-1f70-418b-83ac-900a289a07f9")
    IDSFDebug : public IUnknown
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring] */ HRESULT __stdcall SetObjectFlags( 
            /* [in] */ DSFFlagType FlagType,
            /* [in] */ unsigned long Flags) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFDebugVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFDebug * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFDebug * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFDebug * This);
        
        /* [helpstringcontext][helpcontext][helpstring] */ HRESULT ( __stdcall *SetObjectFlags )( 
            __RPC__in IDSFDebug * This,
            /* [in] */ DSFFlagType FlagType,
            /* [in] */ unsigned long Flags);
        
        END_INTERFACE
    } IDSFDebugVtbl;

    interface IDSFDebug
    {
        CONST_VTBL struct IDSFDebugVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFDebug_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFDebug_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFDebug_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFDebug_SetObjectFlags(This,FlagType,Flags)	\
    ( (This)->lpVtbl -> SetObjectFlags(This,FlagType,Flags) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFDebug_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_DSFDebug;

#ifdef __cplusplus

class DECLSPEC_UUID("f6d08a7f-ef55-406b-be39-2fe6f613ef3d")
DSFDebug;
#endif

EXTERN_C const CLSID CLSID_DSFBus;

#ifdef __cplusplus

class DECLSPEC_UUID("0DEF0513-18CE-4AA4-8C7C-70D37206B4F7")
DSFBus;
#endif

#ifndef __IDSFPersist_INTERFACE_DEFINED__
#define __IDSFPersist_INTERFACE_DEFINED__

/* interface IDSFPersist */
/* [object][helpstringcontext][helpcontext][helpstring][uuid] */ 


EXTERN_C const IID IID_IDSFPersist;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("66CBAEEA-2F16-4685-AC93-71CC713E6A6A")
    IDSFPersist : public IUnknown
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring] */ HRESULT __stdcall get_DSFDevice( 
            /* [retval][out] */ __RPC__deref_out_opt IDSFDevice **ppiDSFDevice) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDSFPersistVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in IDSFPersist * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in IDSFPersist * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in IDSFPersist * This);
        
        /* [helpstringcontext][helpcontext][helpstring] */ HRESULT ( __stdcall *get_DSFDevice )( 
            __RPC__in IDSFPersist * This,
            /* [retval][out] */ __RPC__deref_out_opt IDSFDevice **ppiDSFDevice);
        
        END_INTERFACE
    } IDSFPersistVtbl;

    interface IDSFPersist
    {
        CONST_VTBL struct IDSFPersistVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDSFPersist_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDSFPersist_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDSFPersist_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDSFPersist_get_DSFDevice(This,ppiDSFDevice)	\
    ( (This)->lpVtbl -> get_DSFDevice(This,ppiDSFDevice) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDSFPersist_INTERFACE_DEFINED__ */

#endif /* __DSF_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif



