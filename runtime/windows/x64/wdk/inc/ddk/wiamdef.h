/******************************************************************************
*
*  (C) COPYRIGHT MICROSOFT CORP., 1998-1999
*
*  TITLE:       wiamdef.h
*
*  VERSION:     2.0
*
*  DATE:        28 July, 1999
*
*  DESCRIPTION:
*   Header file used to define WIA constants and globals.
*   Note: This header was introduced first in Windows XP
*
******************************************************************************/

#ifndef _WIAMDEF_H_
#define _WIAMDEF_H_

#if (NTDDI_VERSION >= NTDDI_WINXP)
#pragma once

#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>

//
//  The following array of PROPIDs identifies properties that are ALWAYS
//  present in a WIA_PROPERTY_CONTEXT.  Drivers can specify additional
//  properties when creating a property context with wiasCreatePropContext.
//

#ifdef STD_PROPS_IN_CONTEXT

#define NUM_STD_PROPS_IN_CONTEXT 13
PROPID  WIA_StdPropsInContext[NUM_STD_PROPS_IN_CONTEXT] = {
    WIA_IPA_DATATYPE,
    WIA_IPA_DEPTH,
    WIA_IPS_XRES,
    WIA_IPS_XPOS,
    WIA_IPS_XEXTENT,
    WIA_IPA_PIXELS_PER_LINE,
    WIA_IPS_YRES,
    WIA_IPS_YPOS,
    WIA_IPS_YEXTENT,
    WIA_IPA_NUMBER_OF_LINES,
    WIA_IPS_CUR_INTENT,
    WIA_IPA_TYMED,
    WIA_IPA_FORMAT,
    };
#endif

#if (NTDDI_VERSION >= NTDDI_VISTA)
//
//  drvAcquireItemData flags
//
#define WIA_MINIDRV_TRANSFER_ACQUIRE_CHILDREN  0x00000001
#define WIA_MINIDRV_TRANSFER_DOWNLOAD          0x00000002
#define WIA_MINIDRV_TRANSFER_UPLOAD            0x00000004
#endif //#if (NTDDI_VERSION >= NTDDI_VISTA)

//**************************************************************************
//
//  WIA Service prototypes
//
//
// History:
//
//    4/27/1999 - Initial Version
//
//**************************************************************************

// Flag used by wiasGetImageInformation.

#define WIAS_INIT_CONTEXT 1

// Flag used by wiasDownSampleBuffer

#define WIAS_GET_DOWNSAMPLED_SIZE_ONLY 0x1

//
// IWiaMiniDrvService methods
//

#ifdef __cplusplus
extern "C" {
#endif

HRESULT _stdcall wiasCreateDrvItem(LONG lObjectFlags, BSTR bstrItemName, BSTR bstrFullItemName,
   __inout IWiaMiniDrv *pIMiniDrv, LONG cbDevSpecContext,
   __deref_out_bcount_opt(cbDevSpecContext) BYTE **ppDevSpecContext, __out IWiaDrvItem **ppIWiaDrvItem);

HRESULT _stdcall wiasReadMultiple(__in BYTE *pWiasContext, ULONG ulCount,
   __in_ecount(ulCount) const PROPSPEC *ps, __out_ecount(ulCount) PROPVARIANT *pv,
   __out_ecount_opt(ulCount) PROPVARIANT *pvOld);

HRESULT _stdcall wiasWriteMultiple(__in BYTE *pWiasContext, ULONG ulCount,
   __in_ecount(ulCount) const PROPSPEC *ps, const PROPVARIANT *pv);

HRESULT _stdcall wiasWritePropBin(__in BYTE *pWiasContext, PROPID propid, LONG cbVal,
   __in_bcount(cbVal) BYTE *pbVal);

HRESULT _stdcall wiasGetPropertyAttributes(__in BYTE *pWiasContext, LONG cPropSpec,
   __in_ecount(cPropSpec) PROPSPEC *pPropSpec, ULONG *pulAccessFlags,
   __out_ecount(cPropSpec) PROPVARIANT *pPropVar);

HRESULT _stdcall wiasSetPropertyAttributes(__in BYTE *pWiasContext, LONG cPropSpec,
   __in_ecount(cPropSpec) PROPSPEC *pPropSpec, __in ULONG *pulAccessFlags,
   __out_ecount(cPropSpec) PROPVARIANT  *pPropVar);

HRESULT _stdcall wiasValidateItemProperties(__in BYTE *pWiasContext, ULONG nPropSpec,
    __in_ecount(nPropSpec) const PROPSPEC *pPropSpec);

HRESULT _stdcall wiasCreatePropContext(ULONG cPropSpec, __in_ecount(cPropSpec) PROPSPEC *pPropSpec,
   ULONG cProps, __in_ecount_opt(cProps) PROPID *pProps, __in WIA_PROPERTY_CONTEXT  *pContext);

HRESULT _stdcall wiasGetImageInformation(__in BYTE *pWiasContext, LONG lFlags,
    __inout PMINIDRV_TRANSFER_CONTEXT pmdtc);

HRESULT _stdcall wiasWritePageBufToFile(__in PMINIDRV_TRANSFER_CONTEXT pmdtc);
HRESULT _stdcall wiasWritePageBufToStream(__in PMINIDRV_TRANSFER_CONTEXT pmdtc, __in IStream * pstream);
HRESULT _stdcall wiasWriteBufToFile(LONG lFlags, __in PMINIDRV_TRANSFER_CONTEXT pmdtc);

HRESULT _stdcall wiasReadPropStr(__in BYTE *pWiasContext, PROPID propid,
    __out BSTR *pbstr, __out_opt BSTR *pbstrOld, BOOL bMustExist);
HRESULT _stdcall wiasReadPropLong(__in BYTE *pWiasContext, PROPID propid,
   __out LONG *plVal,  __out_opt LONG *plValOld, BOOL bMustExist);
HRESULT _stdcall wiasReadPropFloat(__in BYTE *pWiasContext, PROPID propid,
   __out FLOAT *pfVal, __out_opt FLOAT *pfValOld, BOOL bMustExist);
HRESULT _stdcall wiasReadPropGuid(__in BYTE *pWiasContext, PROPID propid,
    __out GUID *pguidVal, __out_opt GUID *pguidValOld, BOOL bMustExist);
HRESULT _stdcall wiasReadPropBin(__in BYTE *pWiasContext, PROPID propid,
    __out BYTE **ppbVal, __out_opt BYTE **ppbValOld, BOOL bMustExist);

HRESULT _stdcall wiasWritePropStr(__in BYTE *pWiasContext, PROPID propid, __in_opt BSTR bstr);
HRESULT _stdcall wiasWritePropLong(__in BYTE *pWiasContext, PROPID propid, LONG lVal);
HRESULT _stdcall wiasWritePropFloat(__in BYTE *pWiasContext, PROPID propid, float fVal);
HRESULT _stdcall wiasWritePropGuid(__in BYTE *pWiasContext, PROPID propid, GUID guidVal);

HRESULT _stdcall wiasSetItemPropNames(__in BYTE *pWiasContext, LONG cItemProps,
    __inout_ecount(cItemProps) PROPID *ppId, __inout_ecount(cItemProps) LPOLESTR *ppszNames);
HRESULT _stdcall wiasSetItemPropAttribs(__in BYTE *pWiasContext, LONG cPropSpec,
    __in_ecount(cPropSpec) PROPSPEC *pPropSpec, __in_ecount(cPropSpec) PWIA_PROPERTY_INFO pwpi);

HRESULT _stdcall wiasSendEndOfPage(__in BYTE *pWiasContext,
   LONG lPageCount, __inout PMINIDRV_TRANSFER_CONTEXT pmdtc);

HRESULT _stdcall wiasGetItemType(__in BYTE *pWiasContext, __out LONG *plType);

HRESULT _stdcall wiasGetDrvItem(__in BYTE *pWiasContext, __out IWiaDrvItem **ppIWiaDrvItem);
HRESULT _stdcall wiasGetRootItem(__in BYTE *pWiasContext, __out BYTE **ppWiasContext);

HRESULT _stdcall wiasSetValidFlag(__in BYTE* pWiasContext, PROPID propid, ULONG ulNom, ULONG ulValidBits);
HRESULT _stdcall wiasSetValidRangeLong(__in BYTE* pWiasContext, PROPID propid, LONG lMin, LONG lNom, LONG lMax, LONG lStep);
HRESULT _stdcall wiasSetValidRangeFloat(__in BYTE* pWiasContext, PROPID propid, FLOAT fMin, FLOAT fNom, FLOAT fMax, FLOAT fStep);
HRESULT _stdcall wiasSetValidListLong(__in BYTE *pWiasContext, PROPID propid, ULONG ulCount, LONG lNom, LONG *plValues);
HRESULT _stdcall wiasSetValidListFloat(__in BYTE *pWiasContext, PROPID propid, ULONG ulCount, FLOAT fNom, __in_ecount(ulCount) FLOAT *pfValues);
HRESULT _stdcall wiasSetValidListGuid(__in BYTE *pWiasContext, PROPID propid, ULONG ulCount, GUID guidNom, __in_ecount(ulCount) GUID *pguidValues);
HRESULT _stdcall wiasSetValidListStr(__in BYTE *pWiasContext, PROPID propid, ULONG ulCount, BSTR bstrNom, __in_ecount(ulCount) BSTR *bstrValues);

HRESULT _stdcall wiasFreePropContext(__inout WIA_PROPERTY_CONTEXT *pContext);
HRESULT _stdcall wiasIsPropChanged(PROPID propid, __in WIA_PROPERTY_CONTEXT *pContext, __out BOOL *pbChanged);
HRESULT _stdcall wiasSetPropChanged(PROPID propid, __in WIA_PROPERTY_CONTEXT *pContext, BOOL bChanged);
HRESULT _stdcall wiasGetChangedValueLong(__in BYTE *pWiasContext, __in WIA_PROPERTY_CONTEXT *pContext,
    BOOL bNoValidation, PROPID propID, __out WIAS_CHANGED_VALUE_INFO *pInfo);
HRESULT _stdcall wiasGetChangedValueFloat(__in BYTE *pWiasContext, __in WIA_PROPERTY_CONTEXT *pContext,
    BOOL bNoValidation, PROPID propID, __out WIAS_CHANGED_VALUE_INFO *pInfo);
HRESULT _stdcall wiasGetChangedValueGuid(__in BYTE *pWiasContext, __in WIA_PROPERTY_CONTEXT *pContext,
    BOOL bNoValidation, PROPID propID, __out WIAS_CHANGED_VALUE_INFO *pInfo);
HRESULT _stdcall wiasGetChangedValueStr(__in BYTE *pWiasContext, __in WIA_PROPERTY_CONTEXT *pContext,
    BOOL bNoValidation, PROPID propID, __out WIAS_CHANGED_VALUE_INFO *pInfo);

HRESULT _stdcall wiasGetContextFromName(__in BYTE *pWiasContext, LONG lFlags, __in BSTR bstrName, __out BYTE **ppWiasContext);

HRESULT _stdcall wiasUpdateScanRect(__in BYTE *pWiasContext, __in WIA_PROPERTY_CONTEXT *pContext, LONG lWidth, LONG lHeight);
HRESULT _stdcall wiasUpdateValidFormat(__in BYTE *pWiasContext, __in WIA_PROPERTY_CONTEXT *pContext, __in IWiaMiniDrv *pIMiniDrv);

HRESULT _stdcall wiasGetChildrenContexts(__in BYTE *pParentContext, __out ULONG *pulNumChildren,
    __out_ecount(*pulNumChildren) BYTE ***pppChildren);

HRESULT _stdcall wiasQueueEvent(__in BSTR bstrDeviceId, __in const GUID *pEventGUID, __in_opt BSTR bstrFullItemName);

VOID __cdecl wiasDebugTrace(__in HINSTANCE hInstance, __in LPCSTR pszFormat, ... );
VOID __cdecl wiasDebugError(__in HINSTANCE hInstance, __in LPCSTR pszFormat, ... );
VOID __stdcall wiasPrintDebugHResult(__in HINSTANCE hInstance, HRESULT hr );

BSTR __cdecl wiasFormatArgs(__in LPCSTR lpszFormat, ...);

HRESULT _stdcall wiasCreateChildAppItem(__in BYTE *pParentWiasContext, LONG lFlags,
    __in BSTR bstrItemName, __in BSTR bstrFullItemName, __out BYTE  **ppWiasChildContext);

HRESULT _stdcall wiasCreateLogInstance(__in BYTE *pModuleHandle, __out IWiaLogEx  **ppIWiaLogEx);
HRESULT _stdcall wiasDownSampleBuffer(LONG lFlags, __inout WIAS_DOWN_SAMPLE_INFO *pInfo);
HRESULT _stdcall wiasParseEndorserString(__in BYTE *pWiasContext, LONG lFlags,
   __out_opt WIAS_ENDORSER_INFO *pInfo, __out BSTR *pOutputString);

#ifndef WIA_MAP_OLD_DEBUG

#if defined(_DEBUG) || defined(DBG) || defined(WIA_DEBUG)

#define WIAS_TRACE(x) wiasDebugTrace x
#define WIAS_ERROR(x) wiasDebugError x
#define WIAS_HRESULT(x) wiasPrintDebugHResult x
#define WIAS_ASSERT(x, y) \
        if (!(y)) { \
            WIAS_ERROR((x, (char*) TEXT("ASSERTION FAILED: %hs(%d): %hs"), __FILE__,__LINE__,#x)); \
            DebugBreak(); \
        }

#else

#define WIAS_TRACE(x)
#define WIAS_ERROR(x)
#define WIAS_HRESULT(x)
#define WIAS_ASSERT(x, y)

#endif

#define WIAS_LTRACE(pILog,ResID,Detail,Args) \
         { if ( pILog ) \
            pILog->Log(WIALOG_TRACE, ResID, Detail, wiasFormatArgs Args);\
         };
#define WIAS_LERROR(pILog,ResID,Args) \
         {if ( pILog )\
            pILog->Log(WIALOG_ERROR, ResID, WIALOG_NO_LEVEL, wiasFormatArgs Args);\
         };
#define WIAS_LWARNING(pILog,ResID,Args) \
         {if ( pILog )\
            pILog->Log(WIALOG_WARNING, ResID, WIALOG_NO_LEVEL, wiasFormatArgs Args);\
         };
#define WIAS_LHRESULT(pILog,hr) \
         {if ( pILog )\
            pILog->hResult(hr);\
         };

//
// IWiaLog Defines
//

// Type of logging
#define WIALOG_TRACE   0x00000001
#define WIALOG_WARNING 0x00000002
#define WIALOG_ERROR   0x00000004

// level of detail for TRACE logging
#define WIALOG_LEVEL1  1 // Entry and Exit point of each function/method
#define WIALOG_LEVEL2  2 // LEVEL 1, + traces within the function/method
#define WIALOG_LEVEL3  3 // LEVEL 1, LEVEL 2, and any extra debugging information
#define WIALOG_LEVEL4  4 // USER DEFINED data + all LEVELS of tracing

#define WIALOG_NO_RESOURCE_ID   0
#define WIALOG_NO_LEVEL         0

//
// Entering / Leaving class
//

class CWiaLogProc {
private:
    CHAR   m_szMessage[MAX_PATH];
    IWiaLog *m_pIWiaLog;
    INT     m_DetailLevel;
    INT     m_ResourceID;

public:
    inline CWiaLogProc(IWiaLog *pIWiaLog, INT ResourceID, INT DetailLevel, __in __nullterminated CHAR *pszMsg) {
        ZeroMemory(m_szMessage, sizeof(m_szMessage));
        StringCchCopyA(m_szMessage, ARRAYSIZE(m_szMessage), pszMsg);
        m_pIWiaLog = pIWiaLog;
        m_DetailLevel = DetailLevel;
        m_ResourceID = ResourceID;
        WIAS_LTRACE(pIWiaLog,
                    ResourceID,
                    DetailLevel,
                    ("%s, entering",m_szMessage));
    }

    inline ~CWiaLogProc() {
        WIAS_LTRACE(m_pIWiaLog,
                    m_ResourceID,
                    m_DetailLevel,
                    ("%s, leaving",m_szMessage));
    }
};

class CWiaLogProcEx {
private:
    CHAR        m_szMessage[MAX_PATH];
    IWiaLogEx   *m_pIWiaLog;
    INT         m_DetailLevel;
    INT         m_ResourceID;

public:
    inline CWiaLogProcEx(IWiaLogEx *pIWiaLog, INT ResourceID, INT DetailLevel, __in __nullterminated CHAR *pszMsg, LONG lMethodId = 0) {
        UNREFERENCED_PARAMETER(lMethodId);

        ZeroMemory(m_szMessage, sizeof(m_szMessage));
        StringCchCopyA(m_szMessage, ARRAYSIZE(m_szMessage), pszMsg);
        m_pIWiaLog = pIWiaLog;
        m_DetailLevel = DetailLevel;
        m_ResourceID = ResourceID;
        WIAS_LTRACE(pIWiaLog,
                    ResourceID,
                    DetailLevel,
                    ("%s, entering",m_szMessage));
    }

    inline ~CWiaLogProcEx() {
        WIAS_LTRACE(m_pIWiaLog,
                    m_ResourceID,
                    m_DetailLevel,
                    ("%s, leaving",m_szMessage));
    }
};

#endif // WIA_MAP_OLD_DEBUG


#ifdef __cplusplus
}

#endif
#endif //#ifdef (NTDDI_VERSION >= NTDDI_WINXP)

#endif // _WIAMDEF_H_


