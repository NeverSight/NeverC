

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 7.00.0555 */
/* Compiler settings for softusbif.idl:
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


#ifndef __softusbif_h__
#define __softusbif_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __ISoftUSBConfigurations_FWD_DEFINED__
#define __ISoftUSBConfigurations_FWD_DEFINED__
typedef interface ISoftUSBConfigurations ISoftUSBConfigurations;
#endif 	/* __ISoftUSBConfigurations_FWD_DEFINED__ */


#ifndef __ISoftUSBConfiguration_FWD_DEFINED__
#define __ISoftUSBConfiguration_FWD_DEFINED__
typedef interface ISoftUSBConfiguration ISoftUSBConfiguration;
#endif 	/* __ISoftUSBConfiguration_FWD_DEFINED__ */


#ifndef __ISoftUSBInterface_FWD_DEFINED__
#define __ISoftUSBInterface_FWD_DEFINED__
typedef interface ISoftUSBInterface ISoftUSBInterface;
#endif 	/* __ISoftUSBInterface_FWD_DEFINED__ */


#ifndef __ISoftUSBEndpoint_FWD_DEFINED__
#define __ISoftUSBEndpoint_FWD_DEFINED__
typedef interface ISoftUSBEndpoint ISoftUSBEndpoint;
#endif 	/* __ISoftUSBEndpoint_FWD_DEFINED__ */


#ifndef __ISoftUSBDeviceQualifier_FWD_DEFINED__
#define __ISoftUSBDeviceQualifier_FWD_DEFINED__
typedef interface ISoftUSBDeviceQualifier ISoftUSBDeviceQualifier;
#endif 	/* __ISoftUSBDeviceQualifier_FWD_DEFINED__ */


#ifndef __ISoftUSBEndpointEvents_FWD_DEFINED__
#define __ISoftUSBEndpointEvents_FWD_DEFINED__
typedef interface ISoftUSBEndpointEvents ISoftUSBEndpointEvents;
#endif 	/* __ISoftUSBEndpointEvents_FWD_DEFINED__ */


#ifndef __ISoftUSBStrings_FWD_DEFINED__
#define __ISoftUSBStrings_FWD_DEFINED__
typedef interface ISoftUSBStrings ISoftUSBStrings;
#endif 	/* __ISoftUSBStrings_FWD_DEFINED__ */


#ifndef __ISoftUSBInterfaces_FWD_DEFINED__
#define __ISoftUSBInterfaces_FWD_DEFINED__
typedef interface ISoftUSBInterfaces ISoftUSBInterfaces;
#endif 	/* __ISoftUSBInterfaces_FWD_DEFINED__ */


#ifndef __ISoftUSBEndpoints_FWD_DEFINED__
#define __ISoftUSBEndpoints_FWD_DEFINED__
typedef interface ISoftUSBEndpoints ISoftUSBEndpoints;
#endif 	/* __ISoftUSBEndpoints_FWD_DEFINED__ */


#ifndef __ISoftUSBHub_FWD_DEFINED__
#define __ISoftUSBHub_FWD_DEFINED__
typedef interface ISoftUSBHub ISoftUSBHub;
#endif 	/* __ISoftUSBHub_FWD_DEFINED__ */


#ifndef __ISoftUSBHubPorts_FWD_DEFINED__
#define __ISoftUSBHubPorts_FWD_DEFINED__
typedef interface ISoftUSBHubPorts ISoftUSBHubPorts;
#endif 	/* __ISoftUSBHubPorts_FWD_DEFINED__ */


#ifndef __ISoftUSBHubPort_FWD_DEFINED__
#define __ISoftUSBHubPort_FWD_DEFINED__
typedef interface ISoftUSBHubPort ISoftUSBHubPort;
#endif 	/* __ISoftUSBHubPort_FWD_DEFINED__ */


#ifndef __ISoftUSBDevice_FWD_DEFINED__
#define __ISoftUSBDevice_FWD_DEFINED__
typedef interface ISoftUSBDevice ISoftUSBDevice;
#endif 	/* __ISoftUSBDevice_FWD_DEFINED__ */


#ifndef __SoftUSBDevice_FWD_DEFINED__
#define __SoftUSBDevice_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBDevice SoftUSBDevice;
#else
typedef struct SoftUSBDevice SoftUSBDevice;
#endif /* __cplusplus */

#endif 	/* __SoftUSBDevice_FWD_DEFINED__ */


#ifndef __SoftUSBDeviceQualifier_FWD_DEFINED__
#define __SoftUSBDeviceQualifier_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBDeviceQualifier SoftUSBDeviceQualifier;
#else
typedef struct SoftUSBDeviceQualifier SoftUSBDeviceQualifier;
#endif /* __cplusplus */

#endif 	/* __SoftUSBDeviceQualifier_FWD_DEFINED__ */


#ifndef __SoftUSBConfiguration_FWD_DEFINED__
#define __SoftUSBConfiguration_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBConfiguration SoftUSBConfiguration;
#else
typedef struct SoftUSBConfiguration SoftUSBConfiguration;
#endif /* __cplusplus */

#endif 	/* __SoftUSBConfiguration_FWD_DEFINED__ */


#ifndef __SoftUSBInterface_FWD_DEFINED__
#define __SoftUSBInterface_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBInterface SoftUSBInterface;
#else
typedef struct SoftUSBInterface SoftUSBInterface;
#endif /* __cplusplus */

#endif 	/* __SoftUSBInterface_FWD_DEFINED__ */


#ifndef __SoftUSBEndpoint_FWD_DEFINED__
#define __SoftUSBEndpoint_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBEndpoint SoftUSBEndpoint;
#else
typedef struct SoftUSBEndpoint SoftUSBEndpoint;
#endif /* __cplusplus */

#endif 	/* __SoftUSBEndpoint_FWD_DEFINED__ */


#ifndef __SoftUSBConfigurations_FWD_DEFINED__
#define __SoftUSBConfigurations_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBConfigurations SoftUSBConfigurations;
#else
typedef struct SoftUSBConfigurations SoftUSBConfigurations;
#endif /* __cplusplus */

#endif 	/* __SoftUSBConfigurations_FWD_DEFINED__ */


#ifndef __SoftUSBInterfaces_FWD_DEFINED__
#define __SoftUSBInterfaces_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBInterfaces SoftUSBInterfaces;
#else
typedef struct SoftUSBInterfaces SoftUSBInterfaces;
#endif /* __cplusplus */

#endif 	/* __SoftUSBInterfaces_FWD_DEFINED__ */


#ifndef __SoftUSBEndpoints_FWD_DEFINED__
#define __SoftUSBEndpoints_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBEndpoints SoftUSBEndpoints;
#else
typedef struct SoftUSBEndpoints SoftUSBEndpoints;
#endif /* __cplusplus */

#endif 	/* __SoftUSBEndpoints_FWD_DEFINED__ */


#ifndef __SoftUSBStrings_FWD_DEFINED__
#define __SoftUSBStrings_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBStrings SoftUSBStrings;
#else
typedef struct SoftUSBStrings SoftUSBStrings;
#endif /* __cplusplus */

#endif 	/* __SoftUSBStrings_FWD_DEFINED__ */


#ifndef __ISoftUSBString_FWD_DEFINED__
#define __ISoftUSBString_FWD_DEFINED__
typedef interface ISoftUSBString ISoftUSBString;
#endif 	/* __ISoftUSBString_FWD_DEFINED__ */


#ifndef __SoftUSBString_FWD_DEFINED__
#define __SoftUSBString_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBString SoftUSBString;
#else
typedef struct SoftUSBString SoftUSBString;
#endif /* __cplusplus */

#endif 	/* __SoftUSBString_FWD_DEFINED__ */


#ifndef __SoftUSBHub_FWD_DEFINED__
#define __SoftUSBHub_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBHub SoftUSBHub;
#else
typedef struct SoftUSBHub SoftUSBHub;
#endif /* __cplusplus */

#endif 	/* __SoftUSBHub_FWD_DEFINED__ */


#ifndef __SoftUSBHubPorts_FWD_DEFINED__
#define __SoftUSBHubPorts_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBHubPorts SoftUSBHubPorts;
#else
typedef struct SoftUSBHubPorts SoftUSBHubPorts;
#endif /* __cplusplus */

#endif 	/* __SoftUSBHubPorts_FWD_DEFINED__ */


#ifndef __SoftUSBHubPort_FWD_DEFINED__
#define __SoftUSBHubPort_FWD_DEFINED__

#ifdef __cplusplus
typedef class SoftUSBHubPort SoftUSBHubPort;
#else
typedef struct SoftUSBHubPort SoftUSBHubPort;
#endif /* __cplusplus */

#endif 	/* __SoftUSBHubPort_FWD_DEFINED__ */


#ifdef __cplusplus
extern "C"{
#endif 



#ifndef __SOFTUSB_LIBRARY_DEFINED__
#define __SOFTUSB_LIBRARY_DEFINED__

/* library SOFTUSB */
/* [helpstringcontext][helpcontext][helpstring][helpstringdll][helpfile][version][lcid][uuid] */ 
















#if defined (MIDL_PASS)
typedef struct LIST_ENTRY
    {
    struct LIST_ENTRY *Flink;
    struct LIST_ENTRY *Blink;
    } 	LIST_ENTRY;

#endif
typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("A0E64F57-5D2D-4EE2-A257-227A575ACC60") struct SOFTUSB_OUT_TRANSFER
    {
    BYTE bStatus;
    BYTE DataToggle;
    ULONG cbData;
    ULARGE_INTEGER uliQueuedTime;
    LIST_ENTRY NextTransfer;
    BYTE Data[ 1 ];
    } 	SOFTUSB_OUT_TRANSFER;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("17C3648C-CE1B-41EE-86CE-C681264A8336") 
enum RequestMatchOpts
    {	MatchExactLength	= 0,
	MatchAnyLength	= 1
    } 	RequestMatchOpts;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("550F7244-399C-4155-ADF0-2CCD83BB00F0") 
enum SoftUSBHubPowerSwitchingMode
    {	GangedPowerSwitching	= 0,
	IndividualPowerSwitching	= 1,
	NoPowerSwitching	= 2
    } 	SoftUSBHubPowerSwitchingMode;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("5BEA0626-9743-4517-8283-ECFAD924F4DB") 
enum SoftUSBHubOverCurrentProtectionMode
    {	GlobalOverCurrentProtection	= 0,
	IndividualOverCurrentProtection	= 1,
	NoOverCurrentProtection	= 2
    } 	SoftUSBHubOverCurrentProtectionMode;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("2A3C8E65-6335-4920-AFEA-DF6ED9B02760") 
enum SoftUSBHubSpeed
    {	SoftUSBHubSpeedLow	= 0,
	SoftUSBHubSpeedFull	= 1,
	SoftUSBHubSpeedHigh	= 2,
	SoftUSBHubNotConnected	= 3
    } 	SoftUSBHubSpeed;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("07B2373A-6942-491E-9BF7-CACF68A454F5") 
enum SoftUSBHubIndicator
    {	SoftUSBHubOff	= 0,
	SoftUSBHubAmber	= 1,
	SoftUSBHubGreen	= 2,
	SoftUSBHubBlinkingOffGreen	= 3,
	SoftUSBHubBlinkingOffAmber	= 4,
	SoftUSBHubBlinkingGreenAmber	= 5
    } 	SoftUSBHubIndicator;

typedef /* [helpstringcontext][helpcontext][helpstring][uuid] */  DECLSPEC_UUID("2A3A45AE-3650-4922-8502-BA5960186974") 
enum SoftUSBDeviceState
    {	SoftUSBDeviceStateDetached	= 0,
	SoftUSBDeviceStateAttached	= 1,
	SoftUSBDeviceStatePowered	= 2,
	SoftUSBDeviceStateDefault	= 3,
	SoftWUSBDeviceStateUnconnected	= 4,
	SoftWUSBDeviceStateUnauthenticated	= 5,
	SoftUSBDeviceStateAddress	= 6,
	SoftUSBDeviceStateConfigured	= 7,
	SoftUSBDeviceStateSuspended	= 8
    } 	SoftUSBDeviceState;

#define	SOFTUSB_FOREVER	( ( ULONG  )-1 )

#define	SOFTUSB_ALL	( ( ULONG  )-1 )

#define	SOFTUSBENDPOINT_OBJECTFLAGS	( 100 )

#define	SOFTUSBENDPOINT_DONOTTRACETRANSFERS	( 0 )

#define	SOFTUSBENDPOINT_TRACETRANSFERINPUT	( 0x1 )

#define	SOFTUSBENDPOINT_TRACETRANSFEROUPUT	( 0x2 )

#define	SOFTUSBENDPOINT_TRACETRANSFERINPUTANDOUPUT	( 0x3 )

#define	SOFTUSBENDPOINT_TRACEOUTLISTOVERFLOW	( 0x20 )


EXTERN_C const IID LIBID_SOFTUSB;

#ifndef __ISoftUSBConfigurations_INTERFACE_DEFINED__
#define __ISoftUSBConfigurations_INTERFACE_DEFINED__

/* interface ISoftUSBConfigurations */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBConfigurations;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("15482CE8-BA91-4CEC-9D42-62C8390C3EAC")
    ISoftUSBConfigurations : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Count( 
            /* [retval][out] */ __RPC__out long *plCount) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Item( 
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBConfiguration	**ppSOFTUSBConfig) = 0;
        
        virtual /* [hidden][propget][id] */ HRESULT __stdcall get__NewEnum( 
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Add( 
            /* [in] */ __RPC__in SoftUSBConfiguration	*ppSOFTUSBConfig,
            /* [optional][in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Remove( 
            /* [in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Clear( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBConfigurationsVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBConfigurations * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBConfigurations * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBConfigurations * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBConfigurations * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBConfigurations * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBConfigurations * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBConfigurations * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Count )( 
            __RPC__in ISoftUSBConfigurations * This,
            /* [retval][out] */ __RPC__out long *plCount);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Item )( 
            __RPC__in ISoftUSBConfigurations * This,
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBConfiguration	**ppSOFTUSBConfig);
        
        /* [hidden][propget][id] */ HRESULT ( __stdcall *get__NewEnum )( 
            __RPC__in ISoftUSBConfigurations * This,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Add )( 
            __RPC__in ISoftUSBConfigurations * This,
            /* [in] */ __RPC__in SoftUSBConfiguration	*ppSOFTUSBConfig,
            /* [optional][in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Remove )( 
            __RPC__in ISoftUSBConfigurations * This,
            /* [in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Clear )( 
            __RPC__in ISoftUSBConfigurations * This);
        
        END_INTERFACE
    } ISoftUSBConfigurationsVtbl;

    interface ISoftUSBConfigurations
    {
        CONST_VTBL struct ISoftUSBConfigurationsVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBConfigurations_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBConfigurations_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBConfigurations_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBConfigurations_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBConfigurations_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBConfigurations_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBConfigurations_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBConfigurations_get_Count(This,plCount)	\
    ( (This)->lpVtbl -> get_Count(This,plCount) ) 

#define ISoftUSBConfigurations_get_Item(This,Index,ppSOFTUSBConfig)	\
    ( (This)->lpVtbl -> get_Item(This,Index,ppSOFTUSBConfig) ) 

#define ISoftUSBConfigurations_get__NewEnum(This,ppunkEnum)	\
    ( (This)->lpVtbl -> get__NewEnum(This,ppunkEnum) ) 

#define ISoftUSBConfigurations_Add(This,ppSOFTUSBConfig,Index)	\
    ( (This)->lpVtbl -> Add(This,ppSOFTUSBConfig,Index) ) 

#define ISoftUSBConfigurations_Remove(This,Index)	\
    ( (This)->lpVtbl -> Remove(This,Index) ) 

#define ISoftUSBConfigurations_Clear(This)	\
    ( (This)->lpVtbl -> Clear(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBConfigurations_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBConfiguration_INTERFACE_DEFINED__
#define __ISoftUSBConfiguration_INTERFACE_DEFINED__

/* interface ISoftUSBConfiguration */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBConfiguration;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("2043f5a3-28de-4618-a5a6-519cefe62d96")
    ISoftUSBConfiguration : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Length( 
            /* [retval][out] */ __RPC__out BYTE *pbLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Length( 
            /* [in] */ BYTE bLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DescriptorType( 
            /* [retval][out] */ __RPC__out BYTE *pbDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DescriptorType( 
            /* [in] */ BYTE bDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_TotalLength( 
            /* [retval][out] */ __RPC__out SHORT *psTotalLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_NumInterfaces( 
            /* [retval][out] */ __RPC__out BYTE *pbNumInterfaces) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_ConfigurationValue( 
            /* [retval][out] */ __RPC__out BYTE *pbConfigValue) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_ConfigurationValue( 
            /* [in] */ BYTE bConfigValue) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Configuration( 
            /* [retval][out] */ __RPC__out BYTE *pbConfig) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Configuration( 
            /* [in] */ BYTE bConfig) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Attributes( 
            /* [retval][out] */ __RPC__out BYTE *pbAttributes) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Attributes( 
            /* [in] */ BYTE bAttributes) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_MaxPower( 
            /* [retval][out] */ __RPC__out BYTE *pbMaxPower) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_MaxPower( 
            /* [in] */ BYTE bMaxPower) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Interfaces( 
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBInterfaces **ppiInterfaces) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_AlternateForInterface( 
            /* [in] */ BYTE bInterface,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBInterface **ppiInterface) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceSpecificDescriptor( 
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaDevSpecData) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceSpecificDescriptor( 
            /* [in] */ __RPC__in SAFEARRAY * psaDevSpecData) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Context( 
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFPropertyBag **ppContext) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBConfigurationVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBConfiguration * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBConfiguration * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBConfiguration * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Length )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [retval][out] */ __RPC__out BYTE *pbLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Length )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [in] */ BYTE bLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DescriptorType )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [retval][out] */ __RPC__out BYTE *pbDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DescriptorType )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [in] */ BYTE bDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_TotalLength )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [retval][out] */ __RPC__out SHORT *psTotalLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_NumInterfaces )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [retval][out] */ __RPC__out BYTE *pbNumInterfaces);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_ConfigurationValue )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [retval][out] */ __RPC__out BYTE *pbConfigValue);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_ConfigurationValue )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [in] */ BYTE bConfigValue);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Configuration )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [retval][out] */ __RPC__out BYTE *pbConfig);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Configuration )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [in] */ BYTE bConfig);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Attributes )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [retval][out] */ __RPC__out BYTE *pbAttributes);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Attributes )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [in] */ BYTE bAttributes);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_MaxPower )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [retval][out] */ __RPC__out BYTE *pbMaxPower);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_MaxPower )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [in] */ BYTE bMaxPower);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Interfaces )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBInterfaces **ppiInterfaces);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_AlternateForInterface )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [in] */ BYTE bInterface,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBInterface **ppiInterface);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceSpecificDescriptor )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaDevSpecData);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceSpecificDescriptor )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [in] */ __RPC__in SAFEARRAY * psaDevSpecData);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Context )( 
            __RPC__in ISoftUSBConfiguration * This,
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFPropertyBag **ppContext);
        
        END_INTERFACE
    } ISoftUSBConfigurationVtbl;

    interface ISoftUSBConfiguration
    {
        CONST_VTBL struct ISoftUSBConfigurationVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBConfiguration_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBConfiguration_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBConfiguration_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBConfiguration_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBConfiguration_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBConfiguration_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBConfiguration_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBConfiguration_get_Length(This,pbLength)	\
    ( (This)->lpVtbl -> get_Length(This,pbLength) ) 

#define ISoftUSBConfiguration_put_Length(This,bLength)	\
    ( (This)->lpVtbl -> put_Length(This,bLength) ) 

#define ISoftUSBConfiguration_get_DescriptorType(This,pbDescriptorType)	\
    ( (This)->lpVtbl -> get_DescriptorType(This,pbDescriptorType) ) 

#define ISoftUSBConfiguration_put_DescriptorType(This,bDescriptorType)	\
    ( (This)->lpVtbl -> put_DescriptorType(This,bDescriptorType) ) 

#define ISoftUSBConfiguration_get_TotalLength(This,psTotalLength)	\
    ( (This)->lpVtbl -> get_TotalLength(This,psTotalLength) ) 

#define ISoftUSBConfiguration_get_NumInterfaces(This,pbNumInterfaces)	\
    ( (This)->lpVtbl -> get_NumInterfaces(This,pbNumInterfaces) ) 

#define ISoftUSBConfiguration_get_ConfigurationValue(This,pbConfigValue)	\
    ( (This)->lpVtbl -> get_ConfigurationValue(This,pbConfigValue) ) 

#define ISoftUSBConfiguration_put_ConfigurationValue(This,bConfigValue)	\
    ( (This)->lpVtbl -> put_ConfigurationValue(This,bConfigValue) ) 

#define ISoftUSBConfiguration_get_Configuration(This,pbConfig)	\
    ( (This)->lpVtbl -> get_Configuration(This,pbConfig) ) 

#define ISoftUSBConfiguration_put_Configuration(This,bConfig)	\
    ( (This)->lpVtbl -> put_Configuration(This,bConfig) ) 

#define ISoftUSBConfiguration_get_Attributes(This,pbAttributes)	\
    ( (This)->lpVtbl -> get_Attributes(This,pbAttributes) ) 

#define ISoftUSBConfiguration_put_Attributes(This,bAttributes)	\
    ( (This)->lpVtbl -> put_Attributes(This,bAttributes) ) 

#define ISoftUSBConfiguration_get_MaxPower(This,pbMaxPower)	\
    ( (This)->lpVtbl -> get_MaxPower(This,pbMaxPower) ) 

#define ISoftUSBConfiguration_put_MaxPower(This,bMaxPower)	\
    ( (This)->lpVtbl -> put_MaxPower(This,bMaxPower) ) 

#define ISoftUSBConfiguration_get_Interfaces(This,ppiInterfaces)	\
    ( (This)->lpVtbl -> get_Interfaces(This,ppiInterfaces) ) 

#define ISoftUSBConfiguration_get_AlternateForInterface(This,bInterface,ppiInterface)	\
    ( (This)->lpVtbl -> get_AlternateForInterface(This,bInterface,ppiInterface) ) 

#define ISoftUSBConfiguration_get_DeviceSpecificDescriptor(This,ppsaDevSpecData)	\
    ( (This)->lpVtbl -> get_DeviceSpecificDescriptor(This,ppsaDevSpecData) ) 

#define ISoftUSBConfiguration_put_DeviceSpecificDescriptor(This,psaDevSpecData)	\
    ( (This)->lpVtbl -> put_DeviceSpecificDescriptor(This,psaDevSpecData) ) 

#define ISoftUSBConfiguration_get_Context(This,ppContext)	\
    ( (This)->lpVtbl -> get_Context(This,ppContext) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBConfiguration_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBInterface_INTERFACE_DEFINED__
#define __ISoftUSBInterface_INTERFACE_DEFINED__

/* interface ISoftUSBInterface */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBInterface;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("62bb9c16-8608-4f32-8f8a-ace032f38628")
    ISoftUSBInterface : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Length( 
            /* [retval][out] */ __RPC__out BYTE *pbLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Length( 
            /* [in] */ BYTE bLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DescriptorType( 
            /* [retval][out] */ __RPC__out BYTE *pbDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DescriptorType( 
            /* [in] */ BYTE bDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_InterfaceNumber( 
            /* [retval][out] */ __RPC__out BYTE *pbInterfaceNumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_InterfaceNumber( 
            /* [in] */ BYTE bInterfaceNumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_AlternateSetting( 
            /* [retval][out] */ __RPC__out BYTE *pbAlternateSetting) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_AlternateSetting( 
            /* [in] */ BYTE bAlternateSetting) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_NumEndpoints( 
            /* [retval][out] */ __RPC__out BYTE *pbNumEndpoints) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_InterfaceClass( 
            /* [retval][out] */ __RPC__out BYTE *pbClass) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_InterfaceClass( 
            /* [in] */ BYTE bClass) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_InterfaceSubClass( 
            /* [retval][out] */ __RPC__out BYTE *pbSubClass) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_InterfaceSubClass( 
            /* [in] */ BYTE bSubClass) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_InterfaceProtocol( 
            /* [retval][out] */ __RPC__out BYTE *pbProtocol) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_InterfaceProtocol( 
            /* [in] */ BYTE bProtocol) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Interface( 
            /* [retval][out] */ __RPC__out BYTE *pbInterface) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Interface( 
            /* [in] */ BYTE bInterface) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Endpoints( 
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBEndpoints **ppiEndPoints) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceSpecificDescriptor( 
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaDevSpecData) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceSpecificDescriptor( 
            /* [in] */ __RPC__in SAFEARRAY * psaDevSpecData) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Context( 
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFPropertyBag **ppContext) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBInterfaceVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBInterface * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBInterface * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBInterface * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBInterface * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBInterface * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBInterface * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBInterface * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Length )( 
            __RPC__in ISoftUSBInterface * This,
            /* [retval][out] */ __RPC__out BYTE *pbLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Length )( 
            __RPC__in ISoftUSBInterface * This,
            /* [in] */ BYTE bLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DescriptorType )( 
            __RPC__in ISoftUSBInterface * This,
            /* [retval][out] */ __RPC__out BYTE *pbDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DescriptorType )( 
            __RPC__in ISoftUSBInterface * This,
            /* [in] */ BYTE bDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_InterfaceNumber )( 
            __RPC__in ISoftUSBInterface * This,
            /* [retval][out] */ __RPC__out BYTE *pbInterfaceNumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_InterfaceNumber )( 
            __RPC__in ISoftUSBInterface * This,
            /* [in] */ BYTE bInterfaceNumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_AlternateSetting )( 
            __RPC__in ISoftUSBInterface * This,
            /* [retval][out] */ __RPC__out BYTE *pbAlternateSetting);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_AlternateSetting )( 
            __RPC__in ISoftUSBInterface * This,
            /* [in] */ BYTE bAlternateSetting);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_NumEndpoints )( 
            __RPC__in ISoftUSBInterface * This,
            /* [retval][out] */ __RPC__out BYTE *pbNumEndpoints);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_InterfaceClass )( 
            __RPC__in ISoftUSBInterface * This,
            /* [retval][out] */ __RPC__out BYTE *pbClass);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_InterfaceClass )( 
            __RPC__in ISoftUSBInterface * This,
            /* [in] */ BYTE bClass);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_InterfaceSubClass )( 
            __RPC__in ISoftUSBInterface * This,
            /* [retval][out] */ __RPC__out BYTE *pbSubClass);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_InterfaceSubClass )( 
            __RPC__in ISoftUSBInterface * This,
            /* [in] */ BYTE bSubClass);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_InterfaceProtocol )( 
            __RPC__in ISoftUSBInterface * This,
            /* [retval][out] */ __RPC__out BYTE *pbProtocol);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_InterfaceProtocol )( 
            __RPC__in ISoftUSBInterface * This,
            /* [in] */ BYTE bProtocol);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Interface )( 
            __RPC__in ISoftUSBInterface * This,
            /* [retval][out] */ __RPC__out BYTE *pbInterface);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Interface )( 
            __RPC__in ISoftUSBInterface * This,
            /* [in] */ BYTE bInterface);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Endpoints )( 
            __RPC__in ISoftUSBInterface * This,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBEndpoints **ppiEndPoints);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceSpecificDescriptor )( 
            __RPC__in ISoftUSBInterface * This,
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaDevSpecData);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceSpecificDescriptor )( 
            __RPC__in ISoftUSBInterface * This,
            /* [in] */ __RPC__in SAFEARRAY * psaDevSpecData);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Context )( 
            __RPC__in ISoftUSBInterface * This,
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFPropertyBag **ppContext);
        
        END_INTERFACE
    } ISoftUSBInterfaceVtbl;

    interface ISoftUSBInterface
    {
        CONST_VTBL struct ISoftUSBInterfaceVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBInterface_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBInterface_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBInterface_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBInterface_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBInterface_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBInterface_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBInterface_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBInterface_get_Length(This,pbLength)	\
    ( (This)->lpVtbl -> get_Length(This,pbLength) ) 

#define ISoftUSBInterface_put_Length(This,bLength)	\
    ( (This)->lpVtbl -> put_Length(This,bLength) ) 

#define ISoftUSBInterface_get_DescriptorType(This,pbDescriptorType)	\
    ( (This)->lpVtbl -> get_DescriptorType(This,pbDescriptorType) ) 

#define ISoftUSBInterface_put_DescriptorType(This,bDescriptorType)	\
    ( (This)->lpVtbl -> put_DescriptorType(This,bDescriptorType) ) 

#define ISoftUSBInterface_get_InterfaceNumber(This,pbInterfaceNumber)	\
    ( (This)->lpVtbl -> get_InterfaceNumber(This,pbInterfaceNumber) ) 

#define ISoftUSBInterface_put_InterfaceNumber(This,bInterfaceNumber)	\
    ( (This)->lpVtbl -> put_InterfaceNumber(This,bInterfaceNumber) ) 

#define ISoftUSBInterface_get_AlternateSetting(This,pbAlternateSetting)	\
    ( (This)->lpVtbl -> get_AlternateSetting(This,pbAlternateSetting) ) 

#define ISoftUSBInterface_put_AlternateSetting(This,bAlternateSetting)	\
    ( (This)->lpVtbl -> put_AlternateSetting(This,bAlternateSetting) ) 

#define ISoftUSBInterface_get_NumEndpoints(This,pbNumEndpoints)	\
    ( (This)->lpVtbl -> get_NumEndpoints(This,pbNumEndpoints) ) 

#define ISoftUSBInterface_get_InterfaceClass(This,pbClass)	\
    ( (This)->lpVtbl -> get_InterfaceClass(This,pbClass) ) 

#define ISoftUSBInterface_put_InterfaceClass(This,bClass)	\
    ( (This)->lpVtbl -> put_InterfaceClass(This,bClass) ) 

#define ISoftUSBInterface_get_InterfaceSubClass(This,pbSubClass)	\
    ( (This)->lpVtbl -> get_InterfaceSubClass(This,pbSubClass) ) 

#define ISoftUSBInterface_put_InterfaceSubClass(This,bSubClass)	\
    ( (This)->lpVtbl -> put_InterfaceSubClass(This,bSubClass) ) 

#define ISoftUSBInterface_get_InterfaceProtocol(This,pbProtocol)	\
    ( (This)->lpVtbl -> get_InterfaceProtocol(This,pbProtocol) ) 

#define ISoftUSBInterface_put_InterfaceProtocol(This,bProtocol)	\
    ( (This)->lpVtbl -> put_InterfaceProtocol(This,bProtocol) ) 

#define ISoftUSBInterface_get_Interface(This,pbInterface)	\
    ( (This)->lpVtbl -> get_Interface(This,pbInterface) ) 

#define ISoftUSBInterface_put_Interface(This,bInterface)	\
    ( (This)->lpVtbl -> put_Interface(This,bInterface) ) 

#define ISoftUSBInterface_get_Endpoints(This,ppiEndPoints)	\
    ( (This)->lpVtbl -> get_Endpoints(This,ppiEndPoints) ) 

#define ISoftUSBInterface_get_DeviceSpecificDescriptor(This,ppsaDevSpecData)	\
    ( (This)->lpVtbl -> get_DeviceSpecificDescriptor(This,ppsaDevSpecData) ) 

#define ISoftUSBInterface_put_DeviceSpecificDescriptor(This,psaDevSpecData)	\
    ( (This)->lpVtbl -> put_DeviceSpecificDescriptor(This,psaDevSpecData) ) 

#define ISoftUSBInterface_get_Context(This,ppContext)	\
    ( (This)->lpVtbl -> get_Context(This,ppContext) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBInterface_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBEndpoint_INTERFACE_DEFINED__
#define __ISoftUSBEndpoint_INTERFACE_DEFINED__

/* interface ISoftUSBEndpoint */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][dual][nonextensible][uuid] */ 


EXTERN_C const IID IID_ISoftUSBEndpoint;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("fd6ea833-81cb-4065-a219-8528bdcdc2e6")
    ISoftUSBEndpoint : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Length( 
            /* [retval][out] */ __RPC__out BYTE *pbLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Length( 
            /* [in] */ BYTE bLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DescriptorType( 
            /* [retval][out] */ __RPC__out BYTE *pbDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DescriptorType( 
            /* [in] */ BYTE bDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_EndpointAddress( 
            /* [retval][out] */ __RPC__out BYTE *pbAddress) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_EndpointAddress( 
            /* [in] */ BYTE bAddress) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Attributes( 
            /* [retval][out] */ __RPC__out BYTE *pbAttributes) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Attributes( 
            /* [in] */ BYTE bAttributes) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_MaxPacketSize( 
            /* [retval][out] */ __RPC__out SHORT *psMaxPacket) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_MaxPacketSize( 
            /* [in] */ SHORT sMaxPacketSize) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Interval( 
            /* [retval][out] */ __RPC__out BYTE *pbInterval) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Interval( 
            /* [in] */ BYTE bInterval) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Halted( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pHalted) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Halted( 
            /* [in] */ VARIANT_BOOL Halted) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_MarshalEvents( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pMarshalEvents) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_MarshalEvents( 
            /* [in] */ VARIANT_BOOL MarshalEvents) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_OutQueueSize( 
            /* [retval][out] */ __RPC__out long *plOutQueueSize) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_OutQueueSize( 
            /* [in] */ long lOutQueueSize) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_LoopbackEndpoint( 
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBEndpoint **ppiSoftUSBEndpoint) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_LoopbackEndpoint( 
            /* [in] */ __RPC__in_opt ISoftUSBEndpoint *piSoftUSBEndpoint) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT __stdcall putref_LoopbackEndpoint( 
            /* [in] */ __RPC__in_opt ISoftUSBEndpoint *piSoftUSBEndpoint) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_HandleStdDeviceRequests( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pHandleStdRequests) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_HandleStdDeviceRequests( 
            /* [in] */ VARIANT_BOOL HandleStdRequests) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceSpecificDescriptor( 
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaDevSpecData) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceSpecificDescriptor( 
            /* [in] */ __RPC__in SAFEARRAY * psaDevSpecData) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Context( 
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFPropertyBag **ppContext) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring] */ HRESULT __stdcall QueueINData( 
            /* [in] */ __RPC__in BYTE *pbDataBuffer,
            /* [in] */ ULONG cbDataBuffer,
            /* [in] */ BYTE bStatus,
            /* [in] */ ULONG ulTimeToLive) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall DrainOUTQueue( 
            /* [in] */ ULONG cMaxTransfers,
            /* [out] */ __RPC__out ULONG *pcTransfersRemaining,
            /* [out] */ __RPC__deref_out_opt SOFTUSB_OUT_TRANSFER **ppTransfers) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ void __stdcall FreeOUTQueue( 
            /* [in] */ __RPC__in void *pTransfers) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall QueueDeviceRequestResponse( 
            /* [in] */ __RPC__in USBSETUPREQUEST *pRequest,
            /* [in] */ RequestMatchOpts MatchOpt,
            /* [in] */ __RPC__in BYTE *pbResponseData,
            /* [in] */ ULONG cbResponseData,
            /* [in] */ BYTE FinalRequestStatus,
            /* [in] */ ULONG ulTimeToLive) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall PurgeINQueue( void) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall PurgeOUTQueue( void) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall PurgeOUTQueueOlderThan( 
            /* [in] */ ULONG ulOlderThan) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_InQueueLength( 
            /* [retval][out] */ __RPC__out long *plInQueueLength) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBEndpointVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBEndpoint * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBEndpoint * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBEndpoint * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Length )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__out BYTE *pbLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Length )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ BYTE bLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DescriptorType )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__out BYTE *pbDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DescriptorType )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ BYTE bDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_EndpointAddress )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__out BYTE *pbAddress);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_EndpointAddress )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ BYTE bAddress);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Attributes )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__out BYTE *pbAttributes);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Attributes )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ BYTE bAttributes);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_MaxPacketSize )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__out SHORT *psMaxPacket);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_MaxPacketSize )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ SHORT sMaxPacketSize);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Interval )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__out BYTE *pbInterval);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Interval )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ BYTE bInterval);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Halted )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pHalted);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Halted )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ VARIANT_BOOL Halted);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_MarshalEvents )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pMarshalEvents);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_MarshalEvents )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ VARIANT_BOOL MarshalEvents);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_OutQueueSize )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__out long *plOutQueueSize);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_OutQueueSize )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ long lOutQueueSize);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_LoopbackEndpoint )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBEndpoint **ppiSoftUSBEndpoint);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_LoopbackEndpoint )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ __RPC__in_opt ISoftUSBEndpoint *piSoftUSBEndpoint);
        
        /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT ( __stdcall *putref_LoopbackEndpoint )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ __RPC__in_opt ISoftUSBEndpoint *piSoftUSBEndpoint);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_HandleStdDeviceRequests )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pHandleStdRequests);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_HandleStdDeviceRequests )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ VARIANT_BOOL HandleStdRequests);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceSpecificDescriptor )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__deref_out_opt SAFEARRAY * *ppsaDevSpecData);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceSpecificDescriptor )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ __RPC__in SAFEARRAY * psaDevSpecData);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Context )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFPropertyBag **ppContext);
        
        /* [helpstringcontext][helpcontext][helpstring] */ HRESULT ( __stdcall *QueueINData )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ __RPC__in BYTE *pbDataBuffer,
            /* [in] */ ULONG cbDataBuffer,
            /* [in] */ BYTE bStatus,
            /* [in] */ ULONG ulTimeToLive);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *DrainOUTQueue )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ ULONG cMaxTransfers,
            /* [out] */ __RPC__out ULONG *pcTransfersRemaining,
            /* [out] */ __RPC__deref_out_opt SOFTUSB_OUT_TRANSFER **ppTransfers);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ void ( __stdcall *FreeOUTQueue )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ __RPC__in void *pTransfers);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *QueueDeviceRequestResponse )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ __RPC__in USBSETUPREQUEST *pRequest,
            /* [in] */ RequestMatchOpts MatchOpt,
            /* [in] */ __RPC__in BYTE *pbResponseData,
            /* [in] */ ULONG cbResponseData,
            /* [in] */ BYTE FinalRequestStatus,
            /* [in] */ ULONG ulTimeToLive);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *PurgeINQueue )( 
            __RPC__in ISoftUSBEndpoint * This);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *PurgeOUTQueue )( 
            __RPC__in ISoftUSBEndpoint * This);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *PurgeOUTQueueOlderThan )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [in] */ ULONG ulOlderThan);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_InQueueLength )( 
            __RPC__in ISoftUSBEndpoint * This,
            /* [retval][out] */ __RPC__out long *plInQueueLength);
        
        END_INTERFACE
    } ISoftUSBEndpointVtbl;

    interface ISoftUSBEndpoint
    {
        CONST_VTBL struct ISoftUSBEndpointVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBEndpoint_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBEndpoint_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBEndpoint_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBEndpoint_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBEndpoint_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBEndpoint_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBEndpoint_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBEndpoint_get_Length(This,pbLength)	\
    ( (This)->lpVtbl -> get_Length(This,pbLength) ) 

#define ISoftUSBEndpoint_put_Length(This,bLength)	\
    ( (This)->lpVtbl -> put_Length(This,bLength) ) 

#define ISoftUSBEndpoint_get_DescriptorType(This,pbDescriptorType)	\
    ( (This)->lpVtbl -> get_DescriptorType(This,pbDescriptorType) ) 

#define ISoftUSBEndpoint_put_DescriptorType(This,bDescriptorType)	\
    ( (This)->lpVtbl -> put_DescriptorType(This,bDescriptorType) ) 

#define ISoftUSBEndpoint_get_EndpointAddress(This,pbAddress)	\
    ( (This)->lpVtbl -> get_EndpointAddress(This,pbAddress) ) 

#define ISoftUSBEndpoint_put_EndpointAddress(This,bAddress)	\
    ( (This)->lpVtbl -> put_EndpointAddress(This,bAddress) ) 

#define ISoftUSBEndpoint_get_Attributes(This,pbAttributes)	\
    ( (This)->lpVtbl -> get_Attributes(This,pbAttributes) ) 

#define ISoftUSBEndpoint_put_Attributes(This,bAttributes)	\
    ( (This)->lpVtbl -> put_Attributes(This,bAttributes) ) 

#define ISoftUSBEndpoint_get_MaxPacketSize(This,psMaxPacket)	\
    ( (This)->lpVtbl -> get_MaxPacketSize(This,psMaxPacket) ) 

#define ISoftUSBEndpoint_put_MaxPacketSize(This,sMaxPacketSize)	\
    ( (This)->lpVtbl -> put_MaxPacketSize(This,sMaxPacketSize) ) 

#define ISoftUSBEndpoint_get_Interval(This,pbInterval)	\
    ( (This)->lpVtbl -> get_Interval(This,pbInterval) ) 

#define ISoftUSBEndpoint_put_Interval(This,bInterval)	\
    ( (This)->lpVtbl -> put_Interval(This,bInterval) ) 

#define ISoftUSBEndpoint_get_Halted(This,pHalted)	\
    ( (This)->lpVtbl -> get_Halted(This,pHalted) ) 

#define ISoftUSBEndpoint_put_Halted(This,Halted)	\
    ( (This)->lpVtbl -> put_Halted(This,Halted) ) 

#define ISoftUSBEndpoint_get_MarshalEvents(This,pMarshalEvents)	\
    ( (This)->lpVtbl -> get_MarshalEvents(This,pMarshalEvents) ) 

#define ISoftUSBEndpoint_put_MarshalEvents(This,MarshalEvents)	\
    ( (This)->lpVtbl -> put_MarshalEvents(This,MarshalEvents) ) 

#define ISoftUSBEndpoint_get_OutQueueSize(This,plOutQueueSize)	\
    ( (This)->lpVtbl -> get_OutQueueSize(This,plOutQueueSize) ) 

#define ISoftUSBEndpoint_put_OutQueueSize(This,lOutQueueSize)	\
    ( (This)->lpVtbl -> put_OutQueueSize(This,lOutQueueSize) ) 

#define ISoftUSBEndpoint_get_LoopbackEndpoint(This,ppiSoftUSBEndpoint)	\
    ( (This)->lpVtbl -> get_LoopbackEndpoint(This,ppiSoftUSBEndpoint) ) 

#define ISoftUSBEndpoint_put_LoopbackEndpoint(This,piSoftUSBEndpoint)	\
    ( (This)->lpVtbl -> put_LoopbackEndpoint(This,piSoftUSBEndpoint) ) 

#define ISoftUSBEndpoint_putref_LoopbackEndpoint(This,piSoftUSBEndpoint)	\
    ( (This)->lpVtbl -> putref_LoopbackEndpoint(This,piSoftUSBEndpoint) ) 

#define ISoftUSBEndpoint_get_HandleStdDeviceRequests(This,pHandleStdRequests)	\
    ( (This)->lpVtbl -> get_HandleStdDeviceRequests(This,pHandleStdRequests) ) 

#define ISoftUSBEndpoint_put_HandleStdDeviceRequests(This,HandleStdRequests)	\
    ( (This)->lpVtbl -> put_HandleStdDeviceRequests(This,HandleStdRequests) ) 

#define ISoftUSBEndpoint_get_DeviceSpecificDescriptor(This,ppsaDevSpecData)	\
    ( (This)->lpVtbl -> get_DeviceSpecificDescriptor(This,ppsaDevSpecData) ) 

#define ISoftUSBEndpoint_put_DeviceSpecificDescriptor(This,psaDevSpecData)	\
    ( (This)->lpVtbl -> put_DeviceSpecificDescriptor(This,psaDevSpecData) ) 

#define ISoftUSBEndpoint_get_Context(This,ppContext)	\
    ( (This)->lpVtbl -> get_Context(This,ppContext) ) 

#define ISoftUSBEndpoint_QueueINData(This,pbDataBuffer,cbDataBuffer,bStatus,ulTimeToLive)	\
    ( (This)->lpVtbl -> QueueINData(This,pbDataBuffer,cbDataBuffer,bStatus,ulTimeToLive) ) 

#define ISoftUSBEndpoint_DrainOUTQueue(This,cMaxTransfers,pcTransfersRemaining,ppTransfers)	\
    ( (This)->lpVtbl -> DrainOUTQueue(This,cMaxTransfers,pcTransfersRemaining,ppTransfers) ) 

#define ISoftUSBEndpoint_FreeOUTQueue(This,pTransfers)	\
    ( (This)->lpVtbl -> FreeOUTQueue(This,pTransfers) ) 

#define ISoftUSBEndpoint_QueueDeviceRequestResponse(This,pRequest,MatchOpt,pbResponseData,cbResponseData,FinalRequestStatus,ulTimeToLive)	\
    ( (This)->lpVtbl -> QueueDeviceRequestResponse(This,pRequest,MatchOpt,pbResponseData,cbResponseData,FinalRequestStatus,ulTimeToLive) ) 

#define ISoftUSBEndpoint_PurgeINQueue(This)	\
    ( (This)->lpVtbl -> PurgeINQueue(This) ) 

#define ISoftUSBEndpoint_PurgeOUTQueue(This)	\
    ( (This)->lpVtbl -> PurgeOUTQueue(This) ) 

#define ISoftUSBEndpoint_PurgeOUTQueueOlderThan(This,ulOlderThan)	\
    ( (This)->lpVtbl -> PurgeOUTQueueOlderThan(This,ulOlderThan) ) 

#define ISoftUSBEndpoint_get_InQueueLength(This,plInQueueLength)	\
    ( (This)->lpVtbl -> get_InQueueLength(This,plInQueueLength) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBEndpoint_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBDeviceQualifier_INTERFACE_DEFINED__
#define __ISoftUSBDeviceQualifier_INTERFACE_DEFINED__

/* interface ISoftUSBDeviceQualifier */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBDeviceQualifier;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("a445e2cb-401f-4ac7-850f-579185d65140")
    ISoftUSBDeviceQualifier : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Length( 
            /* [retval][out] */ __RPC__out BYTE *pbLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Length( 
            /* [in] */ BYTE bLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DescriptorType( 
            /* [retval][out] */ __RPC__out BYTE *pbDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DescriptorType( 
            /* [in] */ BYTE bDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_USB( 
            /* [retval][out] */ __RPC__out SHORT *psUSB) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_USB( 
            /* [in] */ SHORT sUSB) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceClass( 
            /* [retval][out] */ __RPC__out BYTE *pbDeviceClass) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceClass( 
            /* [in] */ BYTE bDeviceClass) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceSubClass( 
            /* [retval][out] */ __RPC__out BYTE *pbDeviceSubClass) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceSubClass( 
            /* [in] */ BYTE bDeviceSubClass) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceProtocol( 
            /* [retval][out] */ __RPC__out BYTE *pbProtocol) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceProtocol( 
            /* [in] */ BYTE bProtocol) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_MaxPacketSize0( 
            /* [retval][out] */ __RPC__out BYTE *pbMaxPacket) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_MaxPacketSize0( 
            /* [in] */ BYTE bMaxPacket) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_NumConfigurations( 
            /* [retval][out] */ __RPC__out BYTE *pbNumConfigs) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_NumConfigurations( 
            /* [in] */ BYTE bNumConfigs) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Reserved( 
            /* [retval][out] */ __RPC__out BYTE *pbReserved) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Reserved( 
            /* [in] */ BYTE bReserved) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBDeviceQualifierVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBDeviceQualifier * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBDeviceQualifier * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBDeviceQualifier * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Length )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [retval][out] */ __RPC__out BYTE *pbLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Length )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [in] */ BYTE bLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DescriptorType )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [retval][out] */ __RPC__out BYTE *pbDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DescriptorType )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [in] */ BYTE bDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_USB )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [retval][out] */ __RPC__out SHORT *psUSB);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_USB )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [in] */ SHORT sUSB);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceClass )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [retval][out] */ __RPC__out BYTE *pbDeviceClass);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceClass )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [in] */ BYTE bDeviceClass);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceSubClass )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [retval][out] */ __RPC__out BYTE *pbDeviceSubClass);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceSubClass )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [in] */ BYTE bDeviceSubClass);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceProtocol )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [retval][out] */ __RPC__out BYTE *pbProtocol);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceProtocol )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [in] */ BYTE bProtocol);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_MaxPacketSize0 )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [retval][out] */ __RPC__out BYTE *pbMaxPacket);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_MaxPacketSize0 )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [in] */ BYTE bMaxPacket);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_NumConfigurations )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [retval][out] */ __RPC__out BYTE *pbNumConfigs);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_NumConfigurations )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [in] */ BYTE bNumConfigs);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Reserved )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [retval][out] */ __RPC__out BYTE *pbReserved);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Reserved )( 
            __RPC__in ISoftUSBDeviceQualifier * This,
            /* [in] */ BYTE bReserved);
        
        END_INTERFACE
    } ISoftUSBDeviceQualifierVtbl;

    interface ISoftUSBDeviceQualifier
    {
        CONST_VTBL struct ISoftUSBDeviceQualifierVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBDeviceQualifier_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBDeviceQualifier_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBDeviceQualifier_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBDeviceQualifier_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBDeviceQualifier_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBDeviceQualifier_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBDeviceQualifier_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBDeviceQualifier_get_Length(This,pbLength)	\
    ( (This)->lpVtbl -> get_Length(This,pbLength) ) 

#define ISoftUSBDeviceQualifier_put_Length(This,bLength)	\
    ( (This)->lpVtbl -> put_Length(This,bLength) ) 

#define ISoftUSBDeviceQualifier_get_DescriptorType(This,pbDescriptorType)	\
    ( (This)->lpVtbl -> get_DescriptorType(This,pbDescriptorType) ) 

#define ISoftUSBDeviceQualifier_put_DescriptorType(This,bDescriptorType)	\
    ( (This)->lpVtbl -> put_DescriptorType(This,bDescriptorType) ) 

#define ISoftUSBDeviceQualifier_get_USB(This,psUSB)	\
    ( (This)->lpVtbl -> get_USB(This,psUSB) ) 

#define ISoftUSBDeviceQualifier_put_USB(This,sUSB)	\
    ( (This)->lpVtbl -> put_USB(This,sUSB) ) 

#define ISoftUSBDeviceQualifier_get_DeviceClass(This,pbDeviceClass)	\
    ( (This)->lpVtbl -> get_DeviceClass(This,pbDeviceClass) ) 

#define ISoftUSBDeviceQualifier_put_DeviceClass(This,bDeviceClass)	\
    ( (This)->lpVtbl -> put_DeviceClass(This,bDeviceClass) ) 

#define ISoftUSBDeviceQualifier_get_DeviceSubClass(This,pbDeviceSubClass)	\
    ( (This)->lpVtbl -> get_DeviceSubClass(This,pbDeviceSubClass) ) 

#define ISoftUSBDeviceQualifier_put_DeviceSubClass(This,bDeviceSubClass)	\
    ( (This)->lpVtbl -> put_DeviceSubClass(This,bDeviceSubClass) ) 

#define ISoftUSBDeviceQualifier_get_DeviceProtocol(This,pbProtocol)	\
    ( (This)->lpVtbl -> get_DeviceProtocol(This,pbProtocol) ) 

#define ISoftUSBDeviceQualifier_put_DeviceProtocol(This,bProtocol)	\
    ( (This)->lpVtbl -> put_DeviceProtocol(This,bProtocol) ) 

#define ISoftUSBDeviceQualifier_get_MaxPacketSize0(This,pbMaxPacket)	\
    ( (This)->lpVtbl -> get_MaxPacketSize0(This,pbMaxPacket) ) 

#define ISoftUSBDeviceQualifier_put_MaxPacketSize0(This,bMaxPacket)	\
    ( (This)->lpVtbl -> put_MaxPacketSize0(This,bMaxPacket) ) 

#define ISoftUSBDeviceQualifier_get_NumConfigurations(This,pbNumConfigs)	\
    ( (This)->lpVtbl -> get_NumConfigurations(This,pbNumConfigs) ) 

#define ISoftUSBDeviceQualifier_put_NumConfigurations(This,bNumConfigs)	\
    ( (This)->lpVtbl -> put_NumConfigurations(This,bNumConfigs) ) 

#define ISoftUSBDeviceQualifier_get_Reserved(This,pbReserved)	\
    ( (This)->lpVtbl -> get_Reserved(This,pbReserved) ) 

#define ISoftUSBDeviceQualifier_put_Reserved(This,bReserved)	\
    ( (This)->lpVtbl -> put_Reserved(This,bReserved) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBDeviceQualifier_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBEndpointEvents_INTERFACE_DEFINED__
#define __ISoftUSBEndpointEvents_INTERFACE_DEFINED__

/* interface ISoftUSBEndpointEvents */
/* [helpstringcontext][helpcontext][helpstring][object][nonextensible][uuid] */ 


EXTERN_C const IID IID_ISoftUSBEndpointEvents;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E6000897-6A14-4C06-A950-D069B8667091")
    ISoftUSBEndpointEvents : public IUnknown
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall OnSetupTransfer( 
            /* [in] */ BYTE DataToggle,
            /* [in] */ __RPC__in BYTE *pbDataBuffer,
            /* [in] */ ULONG cbDataBuffer,
            /* [out] */ __RPC__out BYTE *pbStatus) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall OnWriteTransfer( 
            /* [in] */ BYTE DataToggle,
            /* [in] */ __RPC__in BYTE *pbDataBuffer,
            /* [in] */ ULONG cbDataBuffer,
            /* [out] */ __RPC__out BYTE *pbStatus) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall OnReadTransfer( 
            /* [in] */ BYTE DataToggle,
            /* [out] */ __RPC__out BYTE *pbDataBuffer,
            /* [in] */ ULONG cbDataBuffer,
            /* [out] */ __RPC__out ULONG *cbDataWritten,
            /* [out] */ __RPC__out BYTE *pbStatus) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall OnDeviceRequest( 
            /* [in] */ __RPC__in USBSETUPREQUEST *pSetupRequest,
            /* [out] */ __RPC__out ULONG *RequestHandle,
            /* [in] */ __RPC__in BYTE *pbRequestData,
            /* [in] */ ULONG cbRequestData,
            /* [out] */ __RPC__deref_out_opt BYTE **ppbResponseData,
            /* [out] */ __RPC__out ULONG *pcbResponseData,
            /* [out] */ __RPC__out BYTE *pbSetupStatus) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall OnDeviceRequestComplete( 
            /* [in] */ ULONG RequestHandle,
            /* [out] */ __RPC__out BYTE *pbFinalRequestStatus) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBEndpointEventsVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBEndpointEvents * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBEndpointEvents * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBEndpointEvents * This);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *OnSetupTransfer )( 
            __RPC__in ISoftUSBEndpointEvents * This,
            /* [in] */ BYTE DataToggle,
            /* [in] */ __RPC__in BYTE *pbDataBuffer,
            /* [in] */ ULONG cbDataBuffer,
            /* [out] */ __RPC__out BYTE *pbStatus);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *OnWriteTransfer )( 
            __RPC__in ISoftUSBEndpointEvents * This,
            /* [in] */ BYTE DataToggle,
            /* [in] */ __RPC__in BYTE *pbDataBuffer,
            /* [in] */ ULONG cbDataBuffer,
            /* [out] */ __RPC__out BYTE *pbStatus);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *OnReadTransfer )( 
            __RPC__in ISoftUSBEndpointEvents * This,
            /* [in] */ BYTE DataToggle,
            /* [out] */ __RPC__out BYTE *pbDataBuffer,
            /* [in] */ ULONG cbDataBuffer,
            /* [out] */ __RPC__out ULONG *cbDataWritten,
            /* [out] */ __RPC__out BYTE *pbStatus);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *OnDeviceRequest )( 
            __RPC__in ISoftUSBEndpointEvents * This,
            /* [in] */ __RPC__in USBSETUPREQUEST *pSetupRequest,
            /* [out] */ __RPC__out ULONG *RequestHandle,
            /* [in] */ __RPC__in BYTE *pbRequestData,
            /* [in] */ ULONG cbRequestData,
            /* [out] */ __RPC__deref_out_opt BYTE **ppbResponseData,
            /* [out] */ __RPC__out ULONG *pcbResponseData,
            /* [out] */ __RPC__out BYTE *pbSetupStatus);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *OnDeviceRequestComplete )( 
            __RPC__in ISoftUSBEndpointEvents * This,
            /* [in] */ ULONG RequestHandle,
            /* [out] */ __RPC__out BYTE *pbFinalRequestStatus);
        
        END_INTERFACE
    } ISoftUSBEndpointEventsVtbl;

    interface ISoftUSBEndpointEvents
    {
        CONST_VTBL struct ISoftUSBEndpointEventsVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBEndpointEvents_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBEndpointEvents_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBEndpointEvents_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBEndpointEvents_OnSetupTransfer(This,DataToggle,pbDataBuffer,cbDataBuffer,pbStatus)	\
    ( (This)->lpVtbl -> OnSetupTransfer(This,DataToggle,pbDataBuffer,cbDataBuffer,pbStatus) ) 

#define ISoftUSBEndpointEvents_OnWriteTransfer(This,DataToggle,pbDataBuffer,cbDataBuffer,pbStatus)	\
    ( (This)->lpVtbl -> OnWriteTransfer(This,DataToggle,pbDataBuffer,cbDataBuffer,pbStatus) ) 

#define ISoftUSBEndpointEvents_OnReadTransfer(This,DataToggle,pbDataBuffer,cbDataBuffer,cbDataWritten,pbStatus)	\
    ( (This)->lpVtbl -> OnReadTransfer(This,DataToggle,pbDataBuffer,cbDataBuffer,cbDataWritten,pbStatus) ) 

#define ISoftUSBEndpointEvents_OnDeviceRequest(This,pSetupRequest,RequestHandle,pbRequestData,cbRequestData,ppbResponseData,pcbResponseData,pbSetupStatus)	\
    ( (This)->lpVtbl -> OnDeviceRequest(This,pSetupRequest,RequestHandle,pbRequestData,cbRequestData,ppbResponseData,pcbResponseData,pbSetupStatus) ) 

#define ISoftUSBEndpointEvents_OnDeviceRequestComplete(This,RequestHandle,pbFinalRequestStatus)	\
    ( (This)->lpVtbl -> OnDeviceRequestComplete(This,RequestHandle,pbFinalRequestStatus) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBEndpointEvents_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBStrings_INTERFACE_DEFINED__
#define __ISoftUSBStrings_INTERFACE_DEFINED__

/* interface ISoftUSBStrings */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBStrings;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("B8714568-8ED3-4FE9-ADED-82EAA467956A")
    ISoftUSBStrings : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Count( 
            /* [retval][out] */ __RPC__out long *plCount) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Item( 
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBString	**ppSOFTUSBString) = 0;
        
        virtual /* [hidden][propget][id] */ HRESULT __stdcall get__NewEnum( 
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Add( 
            /* [in] */ __RPC__in SoftUSBString	*pSOFTUSBString,
            /* [optional][in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Remove( 
            /* [in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Clear( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBStringsVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBStrings * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBStrings * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBStrings * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBStrings * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBStrings * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBStrings * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBStrings * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Count )( 
            __RPC__in ISoftUSBStrings * This,
            /* [retval][out] */ __RPC__out long *plCount);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Item )( 
            __RPC__in ISoftUSBStrings * This,
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBString	**ppSOFTUSBString);
        
        /* [hidden][propget][id] */ HRESULT ( __stdcall *get__NewEnum )( 
            __RPC__in ISoftUSBStrings * This,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Add )( 
            __RPC__in ISoftUSBStrings * This,
            /* [in] */ __RPC__in SoftUSBString	*pSOFTUSBString,
            /* [optional][in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Remove )( 
            __RPC__in ISoftUSBStrings * This,
            /* [in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Clear )( 
            __RPC__in ISoftUSBStrings * This);
        
        END_INTERFACE
    } ISoftUSBStringsVtbl;

    interface ISoftUSBStrings
    {
        CONST_VTBL struct ISoftUSBStringsVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBStrings_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBStrings_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBStrings_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBStrings_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBStrings_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBStrings_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBStrings_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBStrings_get_Count(This,plCount)	\
    ( (This)->lpVtbl -> get_Count(This,plCount) ) 

#define ISoftUSBStrings_get_Item(This,Index,ppSOFTUSBString)	\
    ( (This)->lpVtbl -> get_Item(This,Index,ppSOFTUSBString) ) 

#define ISoftUSBStrings_get__NewEnum(This,ppunkEnum)	\
    ( (This)->lpVtbl -> get__NewEnum(This,ppunkEnum) ) 

#define ISoftUSBStrings_Add(This,pSOFTUSBString,Index)	\
    ( (This)->lpVtbl -> Add(This,pSOFTUSBString,Index) ) 

#define ISoftUSBStrings_Remove(This,Index)	\
    ( (This)->lpVtbl -> Remove(This,Index) ) 

#define ISoftUSBStrings_Clear(This)	\
    ( (This)->lpVtbl -> Clear(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBStrings_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBInterfaces_INTERFACE_DEFINED__
#define __ISoftUSBInterfaces_INTERFACE_DEFINED__

/* interface ISoftUSBInterfaces */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBInterfaces;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("450274FA-92A8-4FB1-A447-2CD3A45465A8")
    ISoftUSBInterfaces : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Count( 
            /* [retval][out] */ __RPC__out long *plCount) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Item( 
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBInterface	**ppSOFTUSBInterface) = 0;
        
        virtual /* [hidden][propget][id] */ HRESULT __stdcall get__NewEnum( 
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Add( 
            /* [in] */ __RPC__in SoftUSBInterface	*ppSOFTUSBInterface,
            /* [optional][in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Remove( 
            /* [in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Clear( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBInterfacesVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBInterfaces * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBInterfaces * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBInterfaces * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBInterfaces * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBInterfaces * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBInterfaces * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBInterfaces * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Count )( 
            __RPC__in ISoftUSBInterfaces * This,
            /* [retval][out] */ __RPC__out long *plCount);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Item )( 
            __RPC__in ISoftUSBInterfaces * This,
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBInterface	**ppSOFTUSBInterface);
        
        /* [hidden][propget][id] */ HRESULT ( __stdcall *get__NewEnum )( 
            __RPC__in ISoftUSBInterfaces * This,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Add )( 
            __RPC__in ISoftUSBInterfaces * This,
            /* [in] */ __RPC__in SoftUSBInterface	*ppSOFTUSBInterface,
            /* [optional][in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Remove )( 
            __RPC__in ISoftUSBInterfaces * This,
            /* [in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Clear )( 
            __RPC__in ISoftUSBInterfaces * This);
        
        END_INTERFACE
    } ISoftUSBInterfacesVtbl;

    interface ISoftUSBInterfaces
    {
        CONST_VTBL struct ISoftUSBInterfacesVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBInterfaces_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBInterfaces_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBInterfaces_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBInterfaces_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBInterfaces_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBInterfaces_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBInterfaces_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBInterfaces_get_Count(This,plCount)	\
    ( (This)->lpVtbl -> get_Count(This,plCount) ) 

#define ISoftUSBInterfaces_get_Item(This,Index,ppSOFTUSBInterface)	\
    ( (This)->lpVtbl -> get_Item(This,Index,ppSOFTUSBInterface) ) 

#define ISoftUSBInterfaces_get__NewEnum(This,ppunkEnum)	\
    ( (This)->lpVtbl -> get__NewEnum(This,ppunkEnum) ) 

#define ISoftUSBInterfaces_Add(This,ppSOFTUSBInterface,Index)	\
    ( (This)->lpVtbl -> Add(This,ppSOFTUSBInterface,Index) ) 

#define ISoftUSBInterfaces_Remove(This,Index)	\
    ( (This)->lpVtbl -> Remove(This,Index) ) 

#define ISoftUSBInterfaces_Clear(This)	\
    ( (This)->lpVtbl -> Clear(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBInterfaces_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBEndpoints_INTERFACE_DEFINED__
#define __ISoftUSBEndpoints_INTERFACE_DEFINED__

/* interface ISoftUSBEndpoints */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBEndpoints;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("797C784C-C45E-4DDD-9F21-5CBE273FA778")
    ISoftUSBEndpoints : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Count( 
            /* [retval][out] */ __RPC__out long *plCount) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Item( 
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBEndpoint	**ppSOFTUSBEndpoint) = 0;
        
        virtual /* [hidden][propget][id] */ HRESULT __stdcall get__NewEnum( 
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Add( 
            /* [in] */ __RPC__in SoftUSBEndpoint	*ppSOFTUSBEndpoint,
            /* [optional][in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Remove( 
            /* [in] */ VARIANT Index) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Clear( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBEndpointsVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBEndpoints * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBEndpoints * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBEndpoints * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBEndpoints * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBEndpoints * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBEndpoints * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBEndpoints * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Count )( 
            __RPC__in ISoftUSBEndpoints * This,
            /* [retval][out] */ __RPC__out long *plCount);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Item )( 
            __RPC__in ISoftUSBEndpoints * This,
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBEndpoint	**ppSOFTUSBEndpoint);
        
        /* [hidden][propget][id] */ HRESULT ( __stdcall *get__NewEnum )( 
            __RPC__in ISoftUSBEndpoints * This,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Add )( 
            __RPC__in ISoftUSBEndpoints * This,
            /* [in] */ __RPC__in SoftUSBEndpoint	*ppSOFTUSBEndpoint,
            /* [optional][in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Remove )( 
            __RPC__in ISoftUSBEndpoints * This,
            /* [in] */ VARIANT Index);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Clear )( 
            __RPC__in ISoftUSBEndpoints * This);
        
        END_INTERFACE
    } ISoftUSBEndpointsVtbl;

    interface ISoftUSBEndpoints
    {
        CONST_VTBL struct ISoftUSBEndpointsVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBEndpoints_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBEndpoints_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBEndpoints_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBEndpoints_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBEndpoints_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBEndpoints_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBEndpoints_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBEndpoints_get_Count(This,plCount)	\
    ( (This)->lpVtbl -> get_Count(This,plCount) ) 

#define ISoftUSBEndpoints_get_Item(This,Index,ppSOFTUSBEndpoint)	\
    ( (This)->lpVtbl -> get_Item(This,Index,ppSOFTUSBEndpoint) ) 

#define ISoftUSBEndpoints_get__NewEnum(This,ppunkEnum)	\
    ( (This)->lpVtbl -> get__NewEnum(This,ppunkEnum) ) 

#define ISoftUSBEndpoints_Add(This,ppSOFTUSBEndpoint,Index)	\
    ( (This)->lpVtbl -> Add(This,ppSOFTUSBEndpoint,Index) ) 

#define ISoftUSBEndpoints_Remove(This,Index)	\
    ( (This)->lpVtbl -> Remove(This,Index) ) 

#define ISoftUSBEndpoints_Clear(This)	\
    ( (This)->lpVtbl -> Clear(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBEndpoints_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBHub_INTERFACE_DEFINED__
#define __ISoftUSBHub_INTERFACE_DEFINED__

/* interface ISoftUSBHub */
/* [object][helpstringcontext][helpcontext][helpstring][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBHub;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("0442b742-2bd3-4b07-99d8-65b6395bcdb0")
    ISoftUSBHub : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_NbrPorts( 
            /* [retval][out] */ __RPC__out BYTE *pbPorts) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_NbrPorts( 
            /* [in] */ BYTE bPorts) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_LogicalPowerSwitchingMode( 
            /* [retval][out] */ __RPC__out SoftUSBHubPowerSwitchingMode *pMode) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_LogicalPowerSwitchingMode( 
            /* [in] */ SoftUSBHubPowerSwitchingMode Mode) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_CompoundDevice( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarCompound) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_CompoundDevice( 
            /* [in] */ VARIANT_BOOL fvarCompound) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_OverCurrentProtectMode( 
            /* [retval][out] */ __RPC__out SoftUSBHubOverCurrentProtectionMode *pMode) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_OverCurrentProtectMode( 
            /* [in] */ SoftUSBHubOverCurrentProtectionMode Mode) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_TTThinkTime( 
            /* [retval][out] */ __RPC__out BYTE *pbThinkTime) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_TTThinkTime( 
            /* [in] */ BYTE bThinkTime) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_PortIndicatorsSupported( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarSupported) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_PortIndicatorsSupported( 
            /* [in] */ VARIANT_BOOL fvarSupported) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_PwrOn2PwrGood( 
            /* [retval][out] */ __RPC__out BYTE *pbTime2PwrGood) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_PwrOn2PwrGood( 
            /* [in] */ BYTE bTime2PwrGood) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_ContrCurrent( 
            /* [retval][out] */ __RPC__out BYTE *pbContrCurrent) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_ContrCurrent( 
            /* [in] */ BYTE bContrCurrent) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_LocalPowerSourceChange( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_LocalPowerSourceChange( 
            /* [in] */ VARIANT_BOOL fvarChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_OverCurrent( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOverCurrent) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_OverCurrent( 
            /* [in] */ VARIANT_BOOL fvarOverCurrent) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_OverCurrentChange( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOverCurrentChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_OverCurrentChange( 
            /* [in] */ VARIANT_BOOL fvarOverCurrentChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Status( 
            /* [retval][out] */ __RPC__out long *plStatus) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_RootHubPortNumber( 
            /* [retval][out] */ __RPC__out BYTE *pbPort) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_SoftUSBDevice( 
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBDevice	**ppSoftUSBDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Ports( 
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBHubPorts	**ppPorts) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Destroy( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBHubVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBHub * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBHub * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBHub * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBHub * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_NbrPorts )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__out BYTE *pbPorts);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_NbrPorts )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ BYTE bPorts);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_LogicalPowerSwitchingMode )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__out SoftUSBHubPowerSwitchingMode *pMode);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_LogicalPowerSwitchingMode )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ SoftUSBHubPowerSwitchingMode Mode);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_CompoundDevice )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarCompound);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_CompoundDevice )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ VARIANT_BOOL fvarCompound);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_OverCurrentProtectMode )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__out SoftUSBHubOverCurrentProtectionMode *pMode);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_OverCurrentProtectMode )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ SoftUSBHubOverCurrentProtectionMode Mode);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_TTThinkTime )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__out BYTE *pbThinkTime);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_TTThinkTime )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ BYTE bThinkTime);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_PortIndicatorsSupported )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarSupported);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_PortIndicatorsSupported )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ VARIANT_BOOL fvarSupported);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_PwrOn2PwrGood )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__out BYTE *pbTime2PwrGood);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_PwrOn2PwrGood )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ BYTE bTime2PwrGood);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_ContrCurrent )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__out BYTE *pbContrCurrent);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_ContrCurrent )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ BYTE bContrCurrent);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_LocalPowerSourceChange )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_LocalPowerSourceChange )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ VARIANT_BOOL fvarChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_OverCurrent )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOverCurrent);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_OverCurrent )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ VARIANT_BOOL fvarOverCurrent);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_OverCurrentChange )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOverCurrentChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_OverCurrentChange )( 
            __RPC__in ISoftUSBHub * This,
            /* [in] */ VARIANT_BOOL fvarOverCurrentChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Status )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__out long *plStatus);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_RootHubPortNumber )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__out BYTE *pbPort);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_SoftUSBDevice )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBDevice	**ppSoftUSBDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Ports )( 
            __RPC__in ISoftUSBHub * This,
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBHubPorts	**ppPorts);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Destroy )( 
            __RPC__in ISoftUSBHub * This);
        
        END_INTERFACE
    } ISoftUSBHubVtbl;

    interface ISoftUSBHub
    {
        CONST_VTBL struct ISoftUSBHubVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBHub_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBHub_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBHub_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBHub_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBHub_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBHub_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBHub_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBHub_get_NbrPorts(This,pbPorts)	\
    ( (This)->lpVtbl -> get_NbrPorts(This,pbPorts) ) 

#define ISoftUSBHub_put_NbrPorts(This,bPorts)	\
    ( (This)->lpVtbl -> put_NbrPorts(This,bPorts) ) 

#define ISoftUSBHub_get_LogicalPowerSwitchingMode(This,pMode)	\
    ( (This)->lpVtbl -> get_LogicalPowerSwitchingMode(This,pMode) ) 

#define ISoftUSBHub_put_LogicalPowerSwitchingMode(This,Mode)	\
    ( (This)->lpVtbl -> put_LogicalPowerSwitchingMode(This,Mode) ) 

#define ISoftUSBHub_get_CompoundDevice(This,pfvarCompound)	\
    ( (This)->lpVtbl -> get_CompoundDevice(This,pfvarCompound) ) 

#define ISoftUSBHub_put_CompoundDevice(This,fvarCompound)	\
    ( (This)->lpVtbl -> put_CompoundDevice(This,fvarCompound) ) 

#define ISoftUSBHub_get_OverCurrentProtectMode(This,pMode)	\
    ( (This)->lpVtbl -> get_OverCurrentProtectMode(This,pMode) ) 

#define ISoftUSBHub_put_OverCurrentProtectMode(This,Mode)	\
    ( (This)->lpVtbl -> put_OverCurrentProtectMode(This,Mode) ) 

#define ISoftUSBHub_get_TTThinkTime(This,pbThinkTime)	\
    ( (This)->lpVtbl -> get_TTThinkTime(This,pbThinkTime) ) 

#define ISoftUSBHub_put_TTThinkTime(This,bThinkTime)	\
    ( (This)->lpVtbl -> put_TTThinkTime(This,bThinkTime) ) 

#define ISoftUSBHub_get_PortIndicatorsSupported(This,pfvarSupported)	\
    ( (This)->lpVtbl -> get_PortIndicatorsSupported(This,pfvarSupported) ) 

#define ISoftUSBHub_put_PortIndicatorsSupported(This,fvarSupported)	\
    ( (This)->lpVtbl -> put_PortIndicatorsSupported(This,fvarSupported) ) 

#define ISoftUSBHub_get_PwrOn2PwrGood(This,pbTime2PwrGood)	\
    ( (This)->lpVtbl -> get_PwrOn2PwrGood(This,pbTime2PwrGood) ) 

#define ISoftUSBHub_put_PwrOn2PwrGood(This,bTime2PwrGood)	\
    ( (This)->lpVtbl -> put_PwrOn2PwrGood(This,bTime2PwrGood) ) 

#define ISoftUSBHub_get_ContrCurrent(This,pbContrCurrent)	\
    ( (This)->lpVtbl -> get_ContrCurrent(This,pbContrCurrent) ) 

#define ISoftUSBHub_put_ContrCurrent(This,bContrCurrent)	\
    ( (This)->lpVtbl -> put_ContrCurrent(This,bContrCurrent) ) 

#define ISoftUSBHub_get_LocalPowerSourceChange(This,pfvarChange)	\
    ( (This)->lpVtbl -> get_LocalPowerSourceChange(This,pfvarChange) ) 

#define ISoftUSBHub_put_LocalPowerSourceChange(This,fvarChange)	\
    ( (This)->lpVtbl -> put_LocalPowerSourceChange(This,fvarChange) ) 

#define ISoftUSBHub_get_OverCurrent(This,pfvarOverCurrent)	\
    ( (This)->lpVtbl -> get_OverCurrent(This,pfvarOverCurrent) ) 

#define ISoftUSBHub_put_OverCurrent(This,fvarOverCurrent)	\
    ( (This)->lpVtbl -> put_OverCurrent(This,fvarOverCurrent) ) 

#define ISoftUSBHub_get_OverCurrentChange(This,pfvarOverCurrentChange)	\
    ( (This)->lpVtbl -> get_OverCurrentChange(This,pfvarOverCurrentChange) ) 

#define ISoftUSBHub_put_OverCurrentChange(This,fvarOverCurrentChange)	\
    ( (This)->lpVtbl -> put_OverCurrentChange(This,fvarOverCurrentChange) ) 

#define ISoftUSBHub_get_Status(This,plStatus)	\
    ( (This)->lpVtbl -> get_Status(This,plStatus) ) 

#define ISoftUSBHub_get_RootHubPortNumber(This,pbPort)	\
    ( (This)->lpVtbl -> get_RootHubPortNumber(This,pbPort) ) 

#define ISoftUSBHub_get_SoftUSBDevice(This,ppSoftUSBDevice)	\
    ( (This)->lpVtbl -> get_SoftUSBDevice(This,ppSoftUSBDevice) ) 

#define ISoftUSBHub_get_Ports(This,ppPorts)	\
    ( (This)->lpVtbl -> get_Ports(This,ppPorts) ) 

#define ISoftUSBHub_Destroy(This)	\
    ( (This)->lpVtbl -> Destroy(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBHub_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBHubPorts_INTERFACE_DEFINED__
#define __ISoftUSBHubPorts_INTERFACE_DEFINED__

/* interface ISoftUSBHubPorts */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBHubPorts;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("DBC9054B-5F42-459C-96EE-182073F8BD3E")
    ISoftUSBHubPorts : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Count( 
            /* [retval][out] */ __RPC__out long *plCount) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Item( 
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBHubPort	**ppSoftUSBHubPort) = 0;
        
        virtual /* [hidden][propget][id] */ HRESULT __stdcall get__NewEnum( 
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBHubPortsVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBHubPorts * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBHubPorts * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBHubPorts * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBHubPorts * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBHubPorts * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBHubPorts * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBHubPorts * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Count )( 
            __RPC__in ISoftUSBHubPorts * This,
            /* [retval][out] */ __RPC__out long *plCount);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Item )( 
            __RPC__in ISoftUSBHubPorts * This,
            /* [in] */ VARIANT Index,
            /* [retval][out] */ __RPC__deref_out_opt SoftUSBHubPort	**ppSoftUSBHubPort);
        
        /* [hidden][propget][id] */ HRESULT ( __stdcall *get__NewEnum )( 
            __RPC__in ISoftUSBHubPorts * This,
            /* [retval][out] */ __RPC__deref_out_opt IUnknown **ppunkEnum);
        
        END_INTERFACE
    } ISoftUSBHubPortsVtbl;

    interface ISoftUSBHubPorts
    {
        CONST_VTBL struct ISoftUSBHubPortsVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBHubPorts_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBHubPorts_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBHubPorts_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBHubPorts_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBHubPorts_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBHubPorts_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBHubPorts_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBHubPorts_get_Count(This,plCount)	\
    ( (This)->lpVtbl -> get_Count(This,plCount) ) 

#define ISoftUSBHubPorts_get_Item(This,Index,ppSoftUSBHubPort)	\
    ( (This)->lpVtbl -> get_Item(This,Index,ppSoftUSBHubPort) ) 

#define ISoftUSBHubPorts_get__NewEnum(This,ppunkEnum)	\
    ( (This)->lpVtbl -> get__NewEnum(This,ppunkEnum) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBHubPorts_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBHubPort_INTERFACE_DEFINED__
#define __ISoftUSBHubPort_INTERFACE_DEFINED__

/* interface ISoftUSBHubPort */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBHubPort;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("11AE2FF8-4F47-4A14-85CA-0D13AC9F4A5A")
    ISoftUSBHubPort : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceRemovable( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarRemovable) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceRemovable( 
            /* [in] */ VARIANT_BOOL fvarRemovable) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_PwrCtrl( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarPwrCtrl) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_PwrCtrl( 
            /* [in] */ VARIANT_BOOL fvarPwrCtrl) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_CurrentConnectStatus( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarStatus) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Enabled( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEnabled) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Enabled( 
            /* [in] */ VARIANT_BOOL fvarEnabled) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Suspend( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarSuspend) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Suspend( 
            /* [in] */ VARIANT_BOOL fvarSuspend) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_OverCurrent( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOverCurrent) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_OverCurrent( 
            /* [in] */ VARIANT_BOOL fvarOverCurrent) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Reset( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarReset) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Power( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarPower) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_AttachedDeviceSpeed( 
            /* [retval][out] */ __RPC__out SoftUSBHubSpeed *pSpeed) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_TestMode( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarTestMode) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_IndicatorControl( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarIndicatorControl) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Indicator( 
            /* [retval][out] */ __RPC__out SoftUSBHubIndicator *pIndicator) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_ConnectStatusChange( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarConnectStatusChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_ConnectStatusChange( 
            /* [in] */ VARIANT_BOOL fvarConnectStatusChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_EnableChange( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEnableChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_EnableChange( 
            /* [in] */ VARIANT_BOOL fvarEnableChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_SuspendChange( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarSuspendChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_SuspendChange( 
            /* [in] */ VARIANT_BOOL fvarSuspendChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_OverCurrentChange( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOverCurrentChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_OverCurrentChange( 
            /* [in] */ VARIANT_BOOL fvarOverCurrentChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_ResetChange( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarResetChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_ResetChange( 
            /* [in] */ VARIANT_BOOL fvarResetChange) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Status( 
            /* [retval][out] */ __RPC__out long *plStatus) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Device( 
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFDevice **ppDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall HotPlug( 
            /* [in] */ __RPC__in /* external definition not present */ DSFDevice *pDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Unplug( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBHubPortVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBHubPort * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBHubPort * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBHubPort * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceRemovable )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarRemovable);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceRemovable )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ VARIANT_BOOL fvarRemovable);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_PwrCtrl )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarPwrCtrl);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_PwrCtrl )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ VARIANT_BOOL fvarPwrCtrl);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_CurrentConnectStatus )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarStatus);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Enabled )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEnabled);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Enabled )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ VARIANT_BOOL fvarEnabled);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Suspend )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarSuspend);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Suspend )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ VARIANT_BOOL fvarSuspend);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_OverCurrent )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOverCurrent);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_OverCurrent )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ VARIANT_BOOL fvarOverCurrent);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Reset )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarReset);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Power )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarPower);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_AttachedDeviceSpeed )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out SoftUSBHubSpeed *pSpeed);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_TestMode )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarTestMode);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_IndicatorControl )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarIndicatorControl);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Indicator )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out SoftUSBHubIndicator *pIndicator);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_ConnectStatusChange )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarConnectStatusChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_ConnectStatusChange )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ VARIANT_BOOL fvarConnectStatusChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_EnableChange )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarEnableChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_EnableChange )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ VARIANT_BOOL fvarEnableChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_SuspendChange )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarSuspendChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_SuspendChange )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ VARIANT_BOOL fvarSuspendChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_OverCurrentChange )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarOverCurrentChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_OverCurrentChange )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ VARIANT_BOOL fvarOverCurrentChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_ResetChange )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarResetChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_ResetChange )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ VARIANT_BOOL fvarResetChange);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Status )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__out long *plStatus);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Device )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFDevice **ppDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *HotPlug )( 
            __RPC__in ISoftUSBHubPort * This,
            /* [in] */ __RPC__in /* external definition not present */ DSFDevice *pDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Unplug )( 
            __RPC__in ISoftUSBHubPort * This);
        
        END_INTERFACE
    } ISoftUSBHubPortVtbl;

    interface ISoftUSBHubPort
    {
        CONST_VTBL struct ISoftUSBHubPortVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBHubPort_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBHubPort_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBHubPort_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBHubPort_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBHubPort_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBHubPort_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBHubPort_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBHubPort_get_DeviceRemovable(This,pfvarRemovable)	\
    ( (This)->lpVtbl -> get_DeviceRemovable(This,pfvarRemovable) ) 

#define ISoftUSBHubPort_put_DeviceRemovable(This,fvarRemovable)	\
    ( (This)->lpVtbl -> put_DeviceRemovable(This,fvarRemovable) ) 

#define ISoftUSBHubPort_get_PwrCtrl(This,pfvarPwrCtrl)	\
    ( (This)->lpVtbl -> get_PwrCtrl(This,pfvarPwrCtrl) ) 

#define ISoftUSBHubPort_put_PwrCtrl(This,fvarPwrCtrl)	\
    ( (This)->lpVtbl -> put_PwrCtrl(This,fvarPwrCtrl) ) 

#define ISoftUSBHubPort_get_CurrentConnectStatus(This,pfvarStatus)	\
    ( (This)->lpVtbl -> get_CurrentConnectStatus(This,pfvarStatus) ) 

#define ISoftUSBHubPort_get_Enabled(This,pfvarEnabled)	\
    ( (This)->lpVtbl -> get_Enabled(This,pfvarEnabled) ) 

#define ISoftUSBHubPort_put_Enabled(This,fvarEnabled)	\
    ( (This)->lpVtbl -> put_Enabled(This,fvarEnabled) ) 

#define ISoftUSBHubPort_get_Suspend(This,pfvarSuspend)	\
    ( (This)->lpVtbl -> get_Suspend(This,pfvarSuspend) ) 

#define ISoftUSBHubPort_put_Suspend(This,fvarSuspend)	\
    ( (This)->lpVtbl -> put_Suspend(This,fvarSuspend) ) 

#define ISoftUSBHubPort_get_OverCurrent(This,pfvarOverCurrent)	\
    ( (This)->lpVtbl -> get_OverCurrent(This,pfvarOverCurrent) ) 

#define ISoftUSBHubPort_put_OverCurrent(This,fvarOverCurrent)	\
    ( (This)->lpVtbl -> put_OverCurrent(This,fvarOverCurrent) ) 

#define ISoftUSBHubPort_get_Reset(This,pfvarReset)	\
    ( (This)->lpVtbl -> get_Reset(This,pfvarReset) ) 

#define ISoftUSBHubPort_get_Power(This,pfvarPower)	\
    ( (This)->lpVtbl -> get_Power(This,pfvarPower) ) 

#define ISoftUSBHubPort_get_AttachedDeviceSpeed(This,pSpeed)	\
    ( (This)->lpVtbl -> get_AttachedDeviceSpeed(This,pSpeed) ) 

#define ISoftUSBHubPort_get_TestMode(This,pfvarTestMode)	\
    ( (This)->lpVtbl -> get_TestMode(This,pfvarTestMode) ) 

#define ISoftUSBHubPort_get_IndicatorControl(This,pfvarIndicatorControl)	\
    ( (This)->lpVtbl -> get_IndicatorControl(This,pfvarIndicatorControl) ) 

#define ISoftUSBHubPort_get_Indicator(This,pIndicator)	\
    ( (This)->lpVtbl -> get_Indicator(This,pIndicator) ) 

#define ISoftUSBHubPort_get_ConnectStatusChange(This,pfvarConnectStatusChange)	\
    ( (This)->lpVtbl -> get_ConnectStatusChange(This,pfvarConnectStatusChange) ) 

#define ISoftUSBHubPort_put_ConnectStatusChange(This,fvarConnectStatusChange)	\
    ( (This)->lpVtbl -> put_ConnectStatusChange(This,fvarConnectStatusChange) ) 

#define ISoftUSBHubPort_get_EnableChange(This,pfvarEnableChange)	\
    ( (This)->lpVtbl -> get_EnableChange(This,pfvarEnableChange) ) 

#define ISoftUSBHubPort_put_EnableChange(This,fvarEnableChange)	\
    ( (This)->lpVtbl -> put_EnableChange(This,fvarEnableChange) ) 

#define ISoftUSBHubPort_get_SuspendChange(This,pfvarSuspendChange)	\
    ( (This)->lpVtbl -> get_SuspendChange(This,pfvarSuspendChange) ) 

#define ISoftUSBHubPort_put_SuspendChange(This,fvarSuspendChange)	\
    ( (This)->lpVtbl -> put_SuspendChange(This,fvarSuspendChange) ) 

#define ISoftUSBHubPort_get_OverCurrentChange(This,pfvarOverCurrentChange)	\
    ( (This)->lpVtbl -> get_OverCurrentChange(This,pfvarOverCurrentChange) ) 

#define ISoftUSBHubPort_put_OverCurrentChange(This,fvarOverCurrentChange)	\
    ( (This)->lpVtbl -> put_OverCurrentChange(This,fvarOverCurrentChange) ) 

#define ISoftUSBHubPort_get_ResetChange(This,pfvarResetChange)	\
    ( (This)->lpVtbl -> get_ResetChange(This,pfvarResetChange) ) 

#define ISoftUSBHubPort_put_ResetChange(This,fvarResetChange)	\
    ( (This)->lpVtbl -> put_ResetChange(This,fvarResetChange) ) 

#define ISoftUSBHubPort_get_Status(This,plStatus)	\
    ( (This)->lpVtbl -> get_Status(This,plStatus) ) 

#define ISoftUSBHubPort_get_Device(This,ppDSFDevice)	\
    ( (This)->lpVtbl -> get_Device(This,ppDSFDevice) ) 

#define ISoftUSBHubPort_HotPlug(This,pDSFDevice)	\
    ( (This)->lpVtbl -> HotPlug(This,pDSFDevice) ) 

#define ISoftUSBHubPort_Unplug(This)	\
    ( (This)->lpVtbl -> Unplug(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBHubPort_INTERFACE_DEFINED__ */


#ifndef __ISoftUSBDevice_INTERFACE_DEFINED__
#define __ISoftUSBDevice_INTERFACE_DEFINED__

/* interface ISoftUSBDevice */
/* [helpstringcontext][helpcontext][helpstring][object][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBDevice;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("9ac61697-81ae-459a-8629-bf5d5a838519")
    ISoftUSBDevice : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Endpoint0( 
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBEndpoint **ppiEndpoint0) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Length( 
            /* [retval][out] */ __RPC__out BYTE *pbLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Length( 
            /* [in] */ BYTE bLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DescriptorType( 
            /* [retval][out] */ __RPC__out BYTE *pbDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DescriptorType( 
            /* [in] */ BYTE bDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_USB( 
            /* [retval][out] */ __RPC__out SHORT *psUSB) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_USB( 
            /* [in] */ SHORT sUSB) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceClass( 
            /* [retval][out] */ __RPC__out BYTE *pbDeviceClass) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceClass( 
            /* [in] */ BYTE bDeviceClass) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceSubClass( 
            /* [retval][out] */ __RPC__out BYTE *pbDeviceSubClass) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceSubClass( 
            /* [in] */ BYTE bDeviceSubClass) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceProtocol( 
            /* [retval][out] */ __RPC__out BYTE *pbProtocol) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceProtocol( 
            /* [in] */ BYTE bProtocol) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_MaxPacketSize0( 
            /* [retval][out] */ __RPC__out BYTE *pbMaxPacket) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_MaxPacketSize0( 
            /* [in] */ BYTE bMaxPacket) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Vendor( 
            /* [retval][out] */ __RPC__out SHORT *psVendor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Vendor( 
            /* [in] */ SHORT sVendor) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Product( 
            /* [retval][out] */ __RPC__out SHORT *psProduct) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Product( 
            /* [in] */ SHORT sProduct) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Device( 
            /* [retval][out] */ __RPC__out SHORT *psDeviceNumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Device( 
            /* [in] */ SHORT sDeviceNumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Manufacturer( 
            /* [retval][out] */ __RPC__out BYTE *pbManufacturer) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Manufacturer( 
            /* [in] */ BYTE bManufacturer) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_ProductDesc( 
            /* [retval][out] */ __RPC__out BYTE *pbProductDesc) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_ProductDesc( 
            /* [in] */ BYTE bProductDesc) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_SerialNumber( 
            /* [retval][out] */ __RPC__out BYTE *pbSerialNumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_SerialNumber( 
            /* [in] */ BYTE bSerialNumber) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_NumConfigurations( 
            /* [retval][out] */ __RPC__out BYTE *pbNumConfigs) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DeviceQualifier( 
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBDeviceQualifier **ppiDeviceQualifier) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DeviceQualifier( 
            /* [in] */ __RPC__in_opt ISoftUSBDeviceQualifier *piDeviceQualifier) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT __stdcall putref_DeviceQualifier( 
            /* [in] */ __RPC__in_opt ISoftUSBDeviceQualifier *piDeviceQualifier) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Configurations( 
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBConfigurations **ppiConfigurations) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_USB1xConfigurations( 
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBConfigurations **ppiUSB1xConfigurations) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_RemoteWakeup( 
            /* [in] */ VARIANT_BOOL RemoteWakeup) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_RemoteWakeup( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pRemoteWakeup) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Address( 
            /* [retval][out] */ __RPC__out BYTE *pbAddress) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_HasExternalPower( 
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarPowered) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_HasExternalPower( 
            /* [in] */ VARIANT_BOOL fvarPowered) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_OperatingSpeed( 
            /* [retval][out] */ __RPC__out SoftUSBHubSpeed *pSpeed) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Configuration( 
            /* [retval][out] */ __RPC__out BYTE *pbConfig) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Strings( 
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBStrings **ppiStrings) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DSFDevice( 
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFDevice **ppDSFDevice) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Context( 
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFPropertyBag **ppContext) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_State( 
            /* [retval][out] */ __RPC__out SoftUSBDeviceState *pState) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Resume( void) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT __stdcall Destroy( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBDeviceVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBDevice * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBDevice * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBDevice * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBDevice * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Endpoint0 )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBEndpoint **ppiEndpoint0);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Length )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out BYTE *pbLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Length )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ BYTE bLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DescriptorType )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out BYTE *pbDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DescriptorType )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ BYTE bDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_USB )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out SHORT *psUSB);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_USB )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ SHORT sUSB);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceClass )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out BYTE *pbDeviceClass);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceClass )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ BYTE bDeviceClass);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceSubClass )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out BYTE *pbDeviceSubClass);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceSubClass )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ BYTE bDeviceSubClass);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceProtocol )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out BYTE *pbProtocol);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceProtocol )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ BYTE bProtocol);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_MaxPacketSize0 )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out BYTE *pbMaxPacket);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_MaxPacketSize0 )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ BYTE bMaxPacket);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Vendor )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out SHORT *psVendor);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Vendor )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ SHORT sVendor);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Product )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out SHORT *psProduct);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Product )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ SHORT sProduct);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Device )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out SHORT *psDeviceNumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Device )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ SHORT sDeviceNumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Manufacturer )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out BYTE *pbManufacturer);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Manufacturer )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ BYTE bManufacturer);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_ProductDesc )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out BYTE *pbProductDesc);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_ProductDesc )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ BYTE bProductDesc);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_SerialNumber )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out BYTE *pbSerialNumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_SerialNumber )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ BYTE bSerialNumber);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_NumConfigurations )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out BYTE *pbNumConfigs);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DeviceQualifier )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBDeviceQualifier **ppiDeviceQualifier);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DeviceQualifier )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ __RPC__in_opt ISoftUSBDeviceQualifier *piDeviceQualifier);
        
        /* [helpstringcontext][helpcontext][helpstring][propputref][id] */ HRESULT ( __stdcall *putref_DeviceQualifier )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ __RPC__in_opt ISoftUSBDeviceQualifier *piDeviceQualifier);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Configurations )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBConfigurations **ppiConfigurations);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_USB1xConfigurations )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBConfigurations **ppiUSB1xConfigurations);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_RemoteWakeup )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ VARIANT_BOOL RemoteWakeup);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_RemoteWakeup )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pRemoteWakeup);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Address )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out BYTE *pbAddress);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_HasExternalPower )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out VARIANT_BOOL *pfvarPowered);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_HasExternalPower )( 
            __RPC__in ISoftUSBDevice * This,
            /* [in] */ VARIANT_BOOL fvarPowered);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_OperatingSpeed )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out SoftUSBHubSpeed *pSpeed);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Configuration )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out BYTE *pbConfig);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Strings )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt ISoftUSBStrings **ppiStrings);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DSFDevice )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFDevice **ppDSFDevice);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Context )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__deref_out_opt /* external definition not present */ DSFPropertyBag **ppContext);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_State )( 
            __RPC__in ISoftUSBDevice * This,
            /* [retval][out] */ __RPC__out SoftUSBDeviceState *pState);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Resume )( 
            __RPC__in ISoftUSBDevice * This);
        
        /* [helpstringcontext][helpcontext][helpstring][id] */ HRESULT ( __stdcall *Destroy )( 
            __RPC__in ISoftUSBDevice * This);
        
        END_INTERFACE
    } ISoftUSBDeviceVtbl;

    interface ISoftUSBDevice
    {
        CONST_VTBL struct ISoftUSBDeviceVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBDevice_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBDevice_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBDevice_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBDevice_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBDevice_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBDevice_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBDevice_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBDevice_get_Endpoint0(This,ppiEndpoint0)	\
    ( (This)->lpVtbl -> get_Endpoint0(This,ppiEndpoint0) ) 

#define ISoftUSBDevice_get_Length(This,pbLength)	\
    ( (This)->lpVtbl -> get_Length(This,pbLength) ) 

#define ISoftUSBDevice_put_Length(This,bLength)	\
    ( (This)->lpVtbl -> put_Length(This,bLength) ) 

#define ISoftUSBDevice_get_DescriptorType(This,pbDescriptorType)	\
    ( (This)->lpVtbl -> get_DescriptorType(This,pbDescriptorType) ) 

#define ISoftUSBDevice_put_DescriptorType(This,bDescriptorType)	\
    ( (This)->lpVtbl -> put_DescriptorType(This,bDescriptorType) ) 

#define ISoftUSBDevice_get_USB(This,psUSB)	\
    ( (This)->lpVtbl -> get_USB(This,psUSB) ) 

#define ISoftUSBDevice_put_USB(This,sUSB)	\
    ( (This)->lpVtbl -> put_USB(This,sUSB) ) 

#define ISoftUSBDevice_get_DeviceClass(This,pbDeviceClass)	\
    ( (This)->lpVtbl -> get_DeviceClass(This,pbDeviceClass) ) 

#define ISoftUSBDevice_put_DeviceClass(This,bDeviceClass)	\
    ( (This)->lpVtbl -> put_DeviceClass(This,bDeviceClass) ) 

#define ISoftUSBDevice_get_DeviceSubClass(This,pbDeviceSubClass)	\
    ( (This)->lpVtbl -> get_DeviceSubClass(This,pbDeviceSubClass) ) 

#define ISoftUSBDevice_put_DeviceSubClass(This,bDeviceSubClass)	\
    ( (This)->lpVtbl -> put_DeviceSubClass(This,bDeviceSubClass) ) 

#define ISoftUSBDevice_get_DeviceProtocol(This,pbProtocol)	\
    ( (This)->lpVtbl -> get_DeviceProtocol(This,pbProtocol) ) 

#define ISoftUSBDevice_put_DeviceProtocol(This,bProtocol)	\
    ( (This)->lpVtbl -> put_DeviceProtocol(This,bProtocol) ) 

#define ISoftUSBDevice_get_MaxPacketSize0(This,pbMaxPacket)	\
    ( (This)->lpVtbl -> get_MaxPacketSize0(This,pbMaxPacket) ) 

#define ISoftUSBDevice_put_MaxPacketSize0(This,bMaxPacket)	\
    ( (This)->lpVtbl -> put_MaxPacketSize0(This,bMaxPacket) ) 

#define ISoftUSBDevice_get_Vendor(This,psVendor)	\
    ( (This)->lpVtbl -> get_Vendor(This,psVendor) ) 

#define ISoftUSBDevice_put_Vendor(This,sVendor)	\
    ( (This)->lpVtbl -> put_Vendor(This,sVendor) ) 

#define ISoftUSBDevice_get_Product(This,psProduct)	\
    ( (This)->lpVtbl -> get_Product(This,psProduct) ) 

#define ISoftUSBDevice_put_Product(This,sProduct)	\
    ( (This)->lpVtbl -> put_Product(This,sProduct) ) 

#define ISoftUSBDevice_get_Device(This,psDeviceNumber)	\
    ( (This)->lpVtbl -> get_Device(This,psDeviceNumber) ) 

#define ISoftUSBDevice_put_Device(This,sDeviceNumber)	\
    ( (This)->lpVtbl -> put_Device(This,sDeviceNumber) ) 

#define ISoftUSBDevice_get_Manufacturer(This,pbManufacturer)	\
    ( (This)->lpVtbl -> get_Manufacturer(This,pbManufacturer) ) 

#define ISoftUSBDevice_put_Manufacturer(This,bManufacturer)	\
    ( (This)->lpVtbl -> put_Manufacturer(This,bManufacturer) ) 

#define ISoftUSBDevice_get_ProductDesc(This,pbProductDesc)	\
    ( (This)->lpVtbl -> get_ProductDesc(This,pbProductDesc) ) 

#define ISoftUSBDevice_put_ProductDesc(This,bProductDesc)	\
    ( (This)->lpVtbl -> put_ProductDesc(This,bProductDesc) ) 

#define ISoftUSBDevice_get_SerialNumber(This,pbSerialNumber)	\
    ( (This)->lpVtbl -> get_SerialNumber(This,pbSerialNumber) ) 

#define ISoftUSBDevice_put_SerialNumber(This,bSerialNumber)	\
    ( (This)->lpVtbl -> put_SerialNumber(This,bSerialNumber) ) 

#define ISoftUSBDevice_get_NumConfigurations(This,pbNumConfigs)	\
    ( (This)->lpVtbl -> get_NumConfigurations(This,pbNumConfigs) ) 

#define ISoftUSBDevice_get_DeviceQualifier(This,ppiDeviceQualifier)	\
    ( (This)->lpVtbl -> get_DeviceQualifier(This,ppiDeviceQualifier) ) 

#define ISoftUSBDevice_put_DeviceQualifier(This,piDeviceQualifier)	\
    ( (This)->lpVtbl -> put_DeviceQualifier(This,piDeviceQualifier) ) 

#define ISoftUSBDevice_putref_DeviceQualifier(This,piDeviceQualifier)	\
    ( (This)->lpVtbl -> putref_DeviceQualifier(This,piDeviceQualifier) ) 

#define ISoftUSBDevice_get_Configurations(This,ppiConfigurations)	\
    ( (This)->lpVtbl -> get_Configurations(This,ppiConfigurations) ) 

#define ISoftUSBDevice_get_USB1xConfigurations(This,ppiUSB1xConfigurations)	\
    ( (This)->lpVtbl -> get_USB1xConfigurations(This,ppiUSB1xConfigurations) ) 

#define ISoftUSBDevice_put_RemoteWakeup(This,RemoteWakeup)	\
    ( (This)->lpVtbl -> put_RemoteWakeup(This,RemoteWakeup) ) 

#define ISoftUSBDevice_get_RemoteWakeup(This,pRemoteWakeup)	\
    ( (This)->lpVtbl -> get_RemoteWakeup(This,pRemoteWakeup) ) 

#define ISoftUSBDevice_get_Address(This,pbAddress)	\
    ( (This)->lpVtbl -> get_Address(This,pbAddress) ) 

#define ISoftUSBDevice_get_HasExternalPower(This,pfvarPowered)	\
    ( (This)->lpVtbl -> get_HasExternalPower(This,pfvarPowered) ) 

#define ISoftUSBDevice_put_HasExternalPower(This,fvarPowered)	\
    ( (This)->lpVtbl -> put_HasExternalPower(This,fvarPowered) ) 

#define ISoftUSBDevice_get_OperatingSpeed(This,pSpeed)	\
    ( (This)->lpVtbl -> get_OperatingSpeed(This,pSpeed) ) 

#define ISoftUSBDevice_get_Configuration(This,pbConfig)	\
    ( (This)->lpVtbl -> get_Configuration(This,pbConfig) ) 

#define ISoftUSBDevice_get_Strings(This,ppiStrings)	\
    ( (This)->lpVtbl -> get_Strings(This,ppiStrings) ) 

#define ISoftUSBDevice_get_DSFDevice(This,ppDSFDevice)	\
    ( (This)->lpVtbl -> get_DSFDevice(This,ppDSFDevice) ) 

#define ISoftUSBDevice_get_Context(This,ppContext)	\
    ( (This)->lpVtbl -> get_Context(This,ppContext) ) 

#define ISoftUSBDevice_get_State(This,pState)	\
    ( (This)->lpVtbl -> get_State(This,pState) ) 

#define ISoftUSBDevice_Resume(This)	\
    ( (This)->lpVtbl -> Resume(This) ) 

#define ISoftUSBDevice_Destroy(This)	\
    ( (This)->lpVtbl -> Destroy(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBDevice_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_SoftUSBDevice;

#ifdef __cplusplus

class DECLSPEC_UUID("23f4a589-a546-4ed4-b18b-fa427cde2ac5")
SoftUSBDevice;
#endif

EXTERN_C const CLSID CLSID_SoftUSBDeviceQualifier;

#ifdef __cplusplus

class DECLSPEC_UUID("97353e3a-cb00-4242-aaa7-b5efae55aab0")
SoftUSBDeviceQualifier;
#endif

EXTERN_C const CLSID CLSID_SoftUSBConfiguration;

#ifdef __cplusplus

class DECLSPEC_UUID("92cf5e08-ac7b-4100-8ef8-4f2487fc8b90")
SoftUSBConfiguration;
#endif

EXTERN_C const CLSID CLSID_SoftUSBInterface;

#ifdef __cplusplus

class DECLSPEC_UUID("e9b15f26-b117-4f4c-9489-26cf8041bf4d")
SoftUSBInterface;
#endif

EXTERN_C const CLSID CLSID_SoftUSBEndpoint;

#ifdef __cplusplus

class DECLSPEC_UUID("56D58287-C835-48A4-86A9-A0CBF8A8384A")
SoftUSBEndpoint;
#endif

EXTERN_C const CLSID CLSID_SoftUSBConfigurations;

#ifdef __cplusplus

class DECLSPEC_UUID("89D25542-A41B-49E1-9B80-2D3D28656541")
SoftUSBConfigurations;
#endif

EXTERN_C const CLSID CLSID_SoftUSBInterfaces;

#ifdef __cplusplus

class DECLSPEC_UUID("8BACAC01-DD39-4D96-88DC-25411C409E86")
SoftUSBInterfaces;
#endif

EXTERN_C const CLSID CLSID_SoftUSBEndpoints;

#ifdef __cplusplus

class DECLSPEC_UUID("5B538872-325E-48E6-8F83-EA5F1273A38E")
SoftUSBEndpoints;
#endif

EXTERN_C const CLSID CLSID_SoftUSBStrings;

#ifdef __cplusplus

class DECLSPEC_UUID("BA8EDA13-3019-4F40-8AEB-83051C28F313")
SoftUSBStrings;
#endif

#ifndef __ISoftUSBString_INTERFACE_DEFINED__
#define __ISoftUSBString_INTERFACE_DEFINED__

/* interface ISoftUSBString */
/* [object][helpstringcontext][helpcontext][helpstring][hidden][nonextensible][oleautomation][dual][uuid] */ 


EXTERN_C const IID IID_ISoftUSBString;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("3D6CC2C8-7EF0-40DD-9999-3071A7D3E6E0")
    ISoftUSBString : public IDispatch
    {
    public:
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Value( 
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrString) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Value( 
            /* [in] */ __RPC__in BSTR bstrString) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_Length( 
            /* [retval][out] */ __RPC__out BYTE *pbLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_Length( 
            /* [in] */ BYTE bLength) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT __stdcall get_DescriptorType( 
            /* [retval][out] */ __RPC__out BYTE *pbDescriptorType) = 0;
        
        virtual /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT __stdcall put_DescriptorType( 
            /* [in] */ BYTE bDescriptorType) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct ISoftUSBStringVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            __RPC__in ISoftUSBString * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [annotation][iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            __RPC__in ISoftUSBString * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            __RPC__in ISoftUSBString * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            __RPC__in ISoftUSBString * This,
            /* [out] */ __RPC__out UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            __RPC__in ISoftUSBString * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ __RPC__deref_out_opt ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            __RPC__in ISoftUSBString * This,
            /* [in] */ __RPC__in REFIID riid,
            /* [size_is][in] */ __RPC__in_ecount_full(cNames) LPOLESTR *rgszNames,
            /* [range][in] */ __RPC__in_range(0,16384) UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ __RPC__out_ecount_full(cNames) DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            ISoftUSBString * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Value )( 
            __RPC__in ISoftUSBString * This,
            /* [retval][out] */ __RPC__deref_out_opt BSTR *pbstrString);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Value )( 
            __RPC__in ISoftUSBString * This,
            /* [in] */ __RPC__in BSTR bstrString);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_Length )( 
            __RPC__in ISoftUSBString * This,
            /* [retval][out] */ __RPC__out BYTE *pbLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_Length )( 
            __RPC__in ISoftUSBString * This,
            /* [in] */ BYTE bLength);
        
        /* [helpstringcontext][helpcontext][helpstring][propget][id] */ HRESULT ( __stdcall *get_DescriptorType )( 
            __RPC__in ISoftUSBString * This,
            /* [retval][out] */ __RPC__out BYTE *pbDescriptorType);
        
        /* [helpstringcontext][helpcontext][helpstring][propput][id] */ HRESULT ( __stdcall *put_DescriptorType )( 
            __RPC__in ISoftUSBString * This,
            /* [in] */ BYTE bDescriptorType);
        
        END_INTERFACE
    } ISoftUSBStringVtbl;

    interface ISoftUSBString
    {
        CONST_VTBL struct ISoftUSBStringVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISoftUSBString_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISoftUSBString_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISoftUSBString_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISoftUSBString_GetTypeInfoCount(This,pctinfo)	\
    ( (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo) ) 

#define ISoftUSBString_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    ( (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo) ) 

#define ISoftUSBString_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    ( (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId) ) 

#define ISoftUSBString_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    ( (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr) ) 


#define ISoftUSBString_get_Value(This,pbstrString)	\
    ( (This)->lpVtbl -> get_Value(This,pbstrString) ) 

#define ISoftUSBString_put_Value(This,bstrString)	\
    ( (This)->lpVtbl -> put_Value(This,bstrString) ) 

#define ISoftUSBString_get_Length(This,pbLength)	\
    ( (This)->lpVtbl -> get_Length(This,pbLength) ) 

#define ISoftUSBString_put_Length(This,bLength)	\
    ( (This)->lpVtbl -> put_Length(This,bLength) ) 

#define ISoftUSBString_get_DescriptorType(This,pbDescriptorType)	\
    ( (This)->lpVtbl -> get_DescriptorType(This,pbDescriptorType) ) 

#define ISoftUSBString_put_DescriptorType(This,bDescriptorType)	\
    ( (This)->lpVtbl -> put_DescriptorType(This,bDescriptorType) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISoftUSBString_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_SoftUSBString;

#ifdef __cplusplus

class DECLSPEC_UUID("4D45AEA4-6FBE-4D81-B600-7511648520C7")
SoftUSBString;
#endif

EXTERN_C const CLSID CLSID_SoftUSBHub;

#ifdef __cplusplus

class DECLSPEC_UUID("4195454B-4ACE-44CD-B4B9-30CEE8D8951B")
SoftUSBHub;
#endif

EXTERN_C const CLSID CLSID_SoftUSBHubPorts;

#ifdef __cplusplus

class DECLSPEC_UUID("364C8DB9-665B-428A-840B-1D0CE777F05F")
SoftUSBHubPorts;
#endif

EXTERN_C const CLSID CLSID_SoftUSBHubPort;

#ifdef __cplusplus

class DECLSPEC_UUID("736CD631-7F9D-4625-B693-F278E119FAD8")
SoftUSBHubPort;
#endif
#endif /* __SOFTUSB_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif



