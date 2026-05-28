

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 7.00.0555 */
/* Compiler settings for softehciif.idl:
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


#ifndef __softehciif_h__
#define __softehciif_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __ISoftEHCI_FWD_DEFINED__
#define __ISoftEHCI_FWD_DEFINED__
typedef interface ISoftEHCI ISoftEHCI;
#endif 	/* __ISoftEHCI_FWD_DEFINED__ */


#ifndef __SoftEHCI_FWD_DEFINED__
#define __SoftEHCI_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftEHCI SoftEHCI;
#else
typedef struct SoftEHCI SoftEHCI;
#endif /* __cplusplus */

#endif 	/* __SoftEHCI_FWD_DEFINED__ */


#ifndef __ISoftEHCICtrlr_FWD_DEFINED__
#define __ISoftEHCICtrlr_FWD_DEFINED__
typedef interface ISoftEHCICtrlr ISoftEHCICtrlr;
#endif 	/* __ISoftEHCICtrlr_FWD_DEFINED__ */


#ifndef __SoftEHCICtrlr_FWD_DEFINED__
#define __SoftEHCICtrlr_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftEHCICtrlr SoftEHCICtrlr;
#else
typedef struct SoftEHCICtrlr SoftEHCICtrlr;
#endif /* __cplusplus */

#endif 	/* __SoftEHCICtrlr_FWD_DEFINED__ */


#ifndef __ISoftEHCIRootHubPorts_FWD_DEFINED__
#define __ISoftEHCIRootHubPorts_FWD_DEFINED__
typedef interface ISoftEHCIRootHubPorts ISoftEHCIRootHubPorts;
#endif 	/* __ISoftEHCIRootHubPorts_FWD_DEFINED__ */


#ifndef __SoftEHCIRootHubPorts_FWD_DEFINED__
#define __SoftEHCIRootHubPorts_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftEHCIRootHubPorts SoftEHCIRootHubPorts;
#else
typedef struct SoftEHCIRootHubPorts SoftEHCIRootHubPorts;
#endif /* __cplusplus */

#endif 	/* __SoftEHCIRootHubPorts_FWD_DEFINED__ */


#ifndef __ISoftEHCIRootHubPort_FWD_DEFINED__
#define __ISoftEHCIRootHubPort_FWD_DEFINED__
typedef interface ISoftEHCIRootHubPort ISoftEHCIRootHubPort;
#endif 	/* __ISoftEHCIRootHubPort_FWD_DEFINED__ */


#ifndef __SoftEHCIRootHubPort_FWD_DEFINED__
#define __SoftEHCIRootHubPort_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftEHCIRootHubPort SoftEHCIRootHubPort;
#else
typedef struct SoftEHCIRootHubPort SoftEHCIRootHubPort;
#endif /* __cplusplus */

#endif 	/* __SoftEHCIRootHubPort_FWD_DEFINED__ */


#ifdef __cplusplus
extern "C"{
#endif 



#ifndef __SoftEHCI_LIBRARY_DEFINED__
#define __SoftEHCI_LIBRARY_DEFINED__

/* library SoftEHCI */
/* [helpstringcontext][helpcontext][helpstring][version][lcid][uuid] */ 



typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("01647E9C-2B10-4620-BE92-758093F3C31A") 
enum EHCIRootHubIndicator
    {	EHCIRootHubOff	= 0,
	EHCIRootHubAmber	= 1,
	EHCIRootHubGreen	= 2,
	EHCIRootHubUndefined	= 3
    } 	EHCIRootHubIndicator;


EXTERN_C const IID LIBID_SoftEHCI;

#ifndef __ISoftEHCI_INTERFACE_DEFINED__
#define __ISoftEHCI_INTERFACE_DEFINED__

/* interface ISoftEHCI */
/* [object][helpstring][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftEHCI;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E99BC1D0-088B-4bd2-AE94-6DA3F2861FA2")
    ISoftEHCI : public IDispatch
    {
    public:
    };
    
#else 	/* C style interface */

    typedef struct ISoftEHCIVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftEHCI * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftEHCI * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftEHCI * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftEHCI * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftEHCI * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftEHCI * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftEHCI * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        END_INTERFACE
    } ISoftEHCIVtbl;

    interface ISoftEHCI
    {
        CONST_VTBL struct ISoftEHCIVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftEHCI_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftEHCI_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftEHCI_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftEHCI_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftEHCI_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftEHCI_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftEHCI_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftEHCI_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_SoftEHCI;

#ifdef __cplusplus

class DECLSPEC_UUID("3676BB7A-1618-4bfc-855C-63C92FD54ACD")
SoftEHCI;
#endif

#ifndef __ISoftEHCICtrlr_INTERFACE_DEFINED__
#define __ISoftEHCICtrlr_INTERFACE_DEFINED__

/* interface ISoftEHCICtrlr */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftEHCICtrlr;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("16017C34-A2BA-480B-8DE8-CD08756AD1F8")
    ISoftEHCICtrlr : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Ports( 
            /* [retval][out] */ __RPC__deref_out_opt SoftEHCIRootHubPorts	**ppPorts) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftEHCICtrlrVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftEHCICtrlr * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftEHCICtrlr * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftEHCICtrlr * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftEHCICtrlr * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftEHCICtrlr * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftEHCICtrlr * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftEHCICtrlr * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Ports )( 
            __RPC__in ISoftEHCICtrlr * This,
            /* [retval][out] */ __RPC__deref_out_opt SoftEHCIRootHubPorts	**ppPorts);
        
        END_INTERFACE
    } ISoftEHCICtrlrVtbl;

    interface ISoftEHCICtrlr
    {
        CONST_VTBL struct ISoftEHCICtrlrVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftEHCICtrlr_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftEHCICtrlr_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftEHCICtrlr_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftEHCICtrlr_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftEHCICtrlr_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftEHCICtrlr_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftEHCICtrlr_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftEHCICtrlr_get_Ports(This,ppPorts)	\
    ( (This)->lpVtbl -> get_Ports(This,ppPorts) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftEHCICtrlr_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_SoftEHCICtrlr;

#ifdef __cplusplus

class DECLSPEC_UUID("C2B7819E-632F-4ADD-A450-62E6F324DC70")
SoftEHCICtrlr;
#endif

#ifndef __ISoftEHCIRootHubPorts_INTERFACE_DEFINED__
#define __ISoftEHCIRootHubPorts_INTERFACE_DEFINED__

/* interface ISoftEHCIRootHubPorts */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftEHCIRootHubPorts;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("8202B252-20C9-47A1-9448-B8621D985CA1")
    ISoftEHCIRootHubPorts : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Count( 
            /* [retval][out] */ __RPC__out long *plCount) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Item( 
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt SoftEHCIRootHubPort	**ppSoftEHCIRootHubPort) = 0;
        
        virtual /* [hidden][propget][id] */ HRESULT __stdcall get__NewEnum( 
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftEHCIRootHubPortsVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftEHCIRootHubPorts * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftEHCIRootHubPorts * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftEHCIRootHubPorts * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftEHCIRootHubPorts * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftEHCIRootHubPorts * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftEHCIRootHubPorts * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftEHCIRootHubPorts * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Count )( 
            __RPC__in ISoftEHCIRootHubPorts * This,
            /* [retval][out] */ __RPC__out long *plCount);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Item )( 
            __RPC__in ISoftEHCIRootHubPorts * This,
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt SoftEHCIRootHubPort	**ppSoftEHCIRootHubPort);
        
        /* [hidden][propget][id] */ HRESULT ( __stdcall *get__NewEnum )( 
            __RPC__in ISoftEHCIRootHubPorts * This,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum);
        
        END_INTERFACE
    } ISoftEHCIRootHubPortsVtbl;

    interface ISoftEHCIRootHubPorts
    {
        CONST_VTBL struct ISoftEHCIRootHubPortsVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftEHCIRootHubPorts_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftEHCIRootHubPorts_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftEHCIRootHubPorts_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftEHCIRootHubPorts_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftEHCIRootHubPorts_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftEHCIRootHubPorts_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftEHCIRootHubPorts_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftEHCIRootHubPorts_get_Count(This,plCount)	\
    ( (This)->lpVtbl -> get_Count(This,plCount) ) 

#define ISoftEHCIRootHubPorts_get_Item(This,Index,ppSoftEHCIRootHubPort)	\
    ( (This)->lpVtbl -> get_Item(This,Index,ppSoftEHCIRootHubPort) ) 

#define ISoftEHCIRootHubPorts_get__NewEnum(This,ppunkEnum)	\
    ( (This)->lpVtbl -> get__NewEnum(This,ppunkEnum) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftEHCIRootHubPorts_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_SoftEHCIRootHubPorts;

#ifdef __cplusplus

class DECLSPEC_UUID("00DC66FB-A2E1-4BF9-9E6C-C3A1A60130EC")
SoftEHCIRootHubPorts;
#endif

#ifndef __ISoftEHCIRootHubPort_INTERFACE_DEFINED__
#define __ISoftEHCIRootHubPort_INTERFACE_DEFINED__

/* interface ISoftEHCIRootHubPort */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftEHCIRootHubPort;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("9A80EBED-8173-4417-9830-405EF2F0167A")
    ISoftEHCIRootHubPort : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_WakeOnOverCurrentEnable( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWake) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_WakeOnDisconnectEnable( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWake) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_WakeOnConnectEnable( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWake) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_TestCtrl( 
            /* [retval][out] */ __RPC__out BYTE *pbTestCtrl) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Indicator( 
            /* [retval][out] */ __RPC__out EHCIRootHubIndicator *pIndicator) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Owner( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOwner) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Power( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarPower) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Power( 
            /* [in] */ VARIANT_BOOL fvarPower) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_LineStatus( 
            /* [retval][out] */ __RPC__out BYTE *pbLineStatus) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_LineStatus( 
            /* [in] */ BYTE bLineStatus) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Reset( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarReset) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Suspend( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarSuspend) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Suspend( 
            /* [in] */ VARIANT_BOOL fvarSuspend) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_ForcePortResume( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarForcePortResume) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_ForcePortResume( 
            /* [in] */ VARIANT_BOOL fvarForcePortResume) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_OverCurrentChange( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOverCurrentChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_OverCurrentChange( 
            /* [in] */ VARIANT_BOOL fvarOverCurrentChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_OverCurrent( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOverCurrent) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_OverCurrent( 
            /* [in] */ VARIANT_BOOL fvarOverCurrent) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_EnableChange( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEnableChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_EnableChange( 
            /* [in] */ VARIANT_BOOL fvarEnableChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Enabled( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEnabled) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Enabled( 
            /* [in] */ VARIANT_BOOL fvarEnabled) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_ConnectStatusChange( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarConnectStatusChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_ConnectStatusChange( 
            /* [in] */ VARIANT_BOOL fvarConnectStatusChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_CurrentConnectStatus( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarStatus) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Status( 
            /* [retval][out] */ __RPC__out long *plStatus) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Device( 
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFDevice **ppDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall HotPlug( 
            /* [in] */ __RPC__in /* external definition not present */ DSFDevice *pDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Unplug( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftEHCIRootHubPortVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftEHCIRootHubPort * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftEHCIRootHubPort * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftEHCIRootHubPort * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_WakeOnOverCurrentEnable )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWake);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_WakeOnDisconnectEnable )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWake);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_WakeOnConnectEnable )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarWake);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_TestCtrl )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out BYTE *pbTestCtrl);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Indicator )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out EHCIRootHubIndicator *pIndicator);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Owner )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOwner);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Power )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarPower);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Power )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [in] */ VARIANT_BOOL fvarPower);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_LineStatus )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out BYTE *pbLineStatus);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_LineStatus )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [in] */ BYTE bLineStatus);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Reset )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarReset);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Suspend )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarSuspend);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Suspend )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [in] */ VARIANT_BOOL fvarSuspend);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_ForcePortResume )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarForcePortResume);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_ForcePortResume )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [in] */ VARIANT_BOOL fvarForcePortResume);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_OverCurrentChange )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOverCurrentChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_OverCurrentChange )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [in] */ VARIANT_BOOL fvarOverCurrentChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_OverCurrent )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOverCurrent);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_OverCurrent )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [in] */ VARIANT_BOOL fvarOverCurrent);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_EnableChange )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEnableChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_EnableChange )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [in] */ VARIANT_BOOL fvarEnableChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Enabled )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEnabled);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Enabled )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [in] */ VARIANT_BOOL fvarEnabled);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_ConnectStatusChange )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarConnectStatusChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_ConnectStatusChange )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [in] */ VARIANT_BOOL fvarConnectStatusChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_CurrentConnectStatus )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarStatus);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Status )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__out long *plStatus);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Device )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFDevice **ppDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *HotPlug )( 
            __RPC__in ISoftEHCIRootHubPort * This,
            /* [in] */ __RPC__in /* external definition not present */ DSFDevice *pDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Unplug )( 
            __RPC__in ISoftEHCIRootHubPort * This);
        
        END_INTERFACE
    } ISoftEHCIRootHubPortVtbl;

    interface ISoftEHCIRootHubPort
    {
        CONST_VTBL struct ISoftEHCIRootHubPortVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftEHCIRootHubPort_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftEHCIRootHubPort_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftEHCIRootHubPort_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftEHCIRootHubPort_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftEHCIRootHubPort_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftEHCIRootHubPort_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftEHCIRootHubPort_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftEHCIRootHubPort_get_WakeOnOverCurrentEnable(This,pfvarWake)	\
    ( (This)->lpVtbl -> get_WakeOnOverCurrentEnable(This,pfvarWake) ) 

#define ISoftEHCIRootHubPort_get_WakeOnDisconnectEnable(This,pfvarWake)	\
    ( (This)->lpVtbl -> get_WakeOnDisconnectEnable(This,pfvarWake) ) 

#define ISoftEHCIRootHubPort_get_WakeOnConnectEnable(This,pfvarWake)	\
    ( (This)->lpVtbl -> get_WakeOnConnectEnable(This,pfvarWake) ) 

#define ISoftEHCIRootHubPort_get_TestCtrl(This,pbTestCtrl)	\
    ( (This)->lpVtbl -> get_TestCtrl(This,pbTestCtrl) ) 

#define ISoftEHCIRootHubPort_get_Indicator(This,pIndicator)	\
    ( (This)->lpVtbl -> get_Indicator(This,pIndicator) ) 

#define ISoftEHCIRootHubPort_get_Owner(This,pfvarOwner)	\
    ( (This)->lpVtbl -> get_Owner(This,pfvarOwner) ) 

#define ISoftEHCIRootHubPort_get_Power(This,pfvarPower)	\
    ( (This)->lpVtbl -> get_Power(This,pfvarPower) ) 

#define ISoftEHCIRootHubPort_put_Power(This,fvarPower)	\
    ( (This)->lpVtbl -> put_Power(This,fvarPower) ) 

#define ISoftEHCIRootHubPort_get_LineStatus(This,pbLineStatus)	\
    ( (This)->lpVtbl -> get_LineStatus(This,pbLineStatus) ) 

#define ISoftEHCIRootHubPort_put_LineStatus(This,bLineStatus)	\
    ( (This)->lpVtbl -> put_LineStatus(This,bLineStatus) ) 

#define ISoftEHCIRootHubPort_get_Reset(This,pfvarReset)	\
    ( (This)->lpVtbl -> get_Reset(This,pfvarReset) ) 

#define ISoftEHCIRootHubPort_get_Suspend(This,pfvarSuspend)	\
    ( (This)->lpVtbl -> get_Suspend(This,pfvarSuspend) ) 

#define ISoftEHCIRootHubPort_put_Suspend(This,fvarSuspend)	\
    ( (This)->lpVtbl -> put_Suspend(This,fvarSuspend) ) 

#define ISoftEHCIRootHubPort_get_ForcePortResume(This,pfvarForcePortResume)	\
    ( (This)->lpVtbl -> get_ForcePortResume(This,pfvarForcePortResume) ) 

#define ISoftEHCIRootHubPort_put_ForcePortResume(This,fvarForcePortResume)	\
    ( (This)->lpVtbl -> put_ForcePortResume(This,fvarForcePortResume) ) 

#define ISoftEHCIRootHubPort_get_OverCurrentChange(This,pfvarOverCurrentChange)	\
    ( (This)->lpVtbl -> get_OverCurrentChange(This,pfvarOverCurrentChange) ) 

#define ISoftEHCIRootHubPort_put_OverCurrentChange(This,fvarOverCurrentChange)	\
    ( (This)->lpVtbl -> put_OverCurrentChange(This,fvarOverCurrentChange) ) 

#define ISoftEHCIRootHubPort_get_OverCurrent(This,pfvarOverCurrent)	\
    ( (This)->lpVtbl -> get_OverCurrent(This,pfvarOverCurrent) ) 

#define ISoftEHCIRootHubPort_put_OverCurrent(This,fvarOverCurrent)	\
    ( (This)->lpVtbl -> put_OverCurrent(This,fvarOverCurrent) ) 

#define ISoftEHCIRootHubPort_get_EnableChange(This,pfvarEnableChange)	\
    ( (This)->lpVtbl -> get_EnableChange(This,pfvarEnableChange) ) 

#define ISoftEHCIRootHubPort_put_EnableChange(This,fvarEnableChange)	\
    ( (This)->lpVtbl -> put_EnableChange(This,fvarEnableChange) ) 

#define ISoftEHCIRootHubPort_get_Enabled(This,pfvarEnabled)	\
    ( (This)->lpVtbl -> get_Enabled(This,pfvarEnabled) ) 

#define ISoftEHCIRootHubPort_put_Enabled(This,fvarEnabled)	\
    ( (This)->lpVtbl -> put_Enabled(This,fvarEnabled) ) 

#define ISoftEHCIRootHubPort_get_ConnectStatusChange(This,pfvarConnectStatusChange)	\
    ( (This)->lpVtbl -> get_ConnectStatusChange(This,pfvarConnectStatusChange) ) 

#define ISoftEHCIRootHubPort_put_ConnectStatusChange(This,fvarConnectStatusChange)	\
    ( (This)->lpVtbl -> put_ConnectStatusChange(This,fvarConnectStatusChange) ) 

#define ISoftEHCIRootHubPort_get_CurrentConnectStatus(This,pfvarStatus)	\
    ( (This)->lpVtbl -> get_CurrentConnectStatus(This,pfvarStatus) ) 

#define ISoftEHCIRootHubPort_get_Status(This,plStatus)	\
    ( (This)->lpVtbl -> get_Status(This,plStatus) ) 

#define ISoftEHCIRootHubPort_get_Device(This,ppDSFDevice)	\
    ( (This)->lpVtbl -> get_Device(This,ppDSFDevice) ) 

#define ISoftEHCIRootHubPort_HotPlug(This,pDSFDevice)	\
    ( (This)->lpVtbl -> HotPlug(This,pDSFDevice) ) 

#define ISoftEHCIRootHubPort_Unplug(This)	\
    ( (This)->lpVtbl -> Unplug(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftEHCIRootHubPort_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_SoftEHCIRootHubPort;

#ifdef __cplusplus

class DECLSPEC_UUID("6FD5902A-DC5A-4AB4-B9B0-B44D70930C9B")
SoftEHCIRootHubPort;
#endif
#endif /* __SoftEHCI_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif



